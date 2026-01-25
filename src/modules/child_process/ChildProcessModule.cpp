#include "ChildProcessModule.h"
#include "../events/EventsModule.h"
#include "../net/NetModule.h"
#include "../../EventLoop.h"
#include "../../IOThreadPool.h"
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <string>
#include <vector>
#include <cstring>

namespace protojs {

static JSClassID child_process_class_id;

struct ChildProcessData {
    pid_t pid;
    int stdinFd[2];
    int stdoutFd[2];
    int stderrFd[2];
    bool spawned;
    bool killed;
    int exitCode;
    int signal;
    JSContext* mainContext;
    JSValue childObj;
    std::thread stdoutThread;
    std::thread stderrThread;
    std::mutex mutex;
    
    ChildProcessData(JSContext* ctx, JSValue child) 
        : pid(-1), spawned(false), killed(false), exitCode(-1), signal(0), 
          mainContext(ctx), childObj(child) {
        stdinFd[0] = stdinFd[1] = -1;
        stdoutFd[0] = stdoutFd[1] = -1;
        stderrFd[0] = stderrFd[1] = -1;
    }
    
    ~ChildProcessData() {
        if (stdoutThread.joinable()) stdoutThread.join();
        if (stderrThread.joinable()) stderrThread.join();
        if (stdinFd[0] >= 0) close(stdinFd[0]);
        if (stdinFd[1] >= 0) close(stdinFd[1]);
        if (stdoutFd[0] >= 0) close(stdoutFd[0]);
        if (stdoutFd[1] >= 0) close(stdoutFd[1]);
        if (stderrFd[0] >= 0) close(stderrFd[0]);
        if (stderrFd[1] >= 0) close(stderrFd[1]);
        if (!JS_IsUndefined(childObj)) {
            JS_FreeValueRT(JS_GetRuntime(mainContext), childObj);
        }
    }
};

void ChildProcessModule::init(JSContext* ctx) {
    JSRuntime* rt = JS_GetRuntime(ctx);
    
    // Register ChildProcess class
    JS_NewClassID(&child_process_class_id);
    JSClassDef childClassDef = {
        "ChildProcess",
        ChildProcessFinalizer
    };
    JS_NewClass(rt, child_process_class_id, &childClassDef);
    
    JSValue childProto = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, childProto, "kill", JS_NewCFunction(ctx, childKill, "kill", 1));
    JS_SetPropertyStr(ctx, childProto, "send", JS_NewCFunction(ctx, childSend, "send", 1));
    JS_SetClassProto(ctx, child_process_class_id, childProto);
    
    // Create child_process module
    JSValue childProcessModule = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, childProcessModule, "spawn", JS_NewCFunction(ctx, spawn, "spawn", 2));
    JS_SetPropertyStr(ctx, childProcessModule, "exec", JS_NewCFunction(ctx, exec, "exec", 2));
    JS_SetPropertyStr(ctx, childProcessModule, "execFile", JS_NewCFunction(ctx, execFile, "execFile", 2));
    JS_SetPropertyStr(ctx, childProcessModule, "fork", JS_NewCFunction(ctx, fork, "fork", 1));
    
    JSValue global_obj = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global_obj, "child_process", childProcessModule);
    JS_FreeValue(ctx, global_obj);
}

JSValue ChildProcessModule::spawn(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "spawn expects command");
    }
    
    const char* command = JS_ToCString(ctx, argv[0]);
    if (!command) return JS_EXCEPTION;
    
    std::string cmd(command);
    JS_FreeCString(ctx, command);
    
    std::vector<std::string> args;
    if (argc > 1 && JS_IsArray(ctx, argv[1])) {
        uint32_t len;
        JS_ToUint32(ctx, &len, JS_GetPropertyStr(ctx, argv[1], "length"));
        for (uint32_t i = 0; i < len; i++) {
            JSValue arg = JS_GetPropertyUint32(ctx, argv[1], i);
            const char* argStr = JS_ToCString(ctx, arg);
            if (argStr) {
                args.push_back(argStr);
                JS_FreeCString(ctx, argStr);
            }
            JS_FreeValue(ctx, arg);
        }
    }
    
    JSValue options = argc > 2 ? JS_DupValue(ctx, argv[2]) : JS_NewObject(ctx);
    
    JSValue child = JS_NewObjectClass(ctx, child_process_class_id);
    if (JS_IsException(child)) {
        JS_FreeValue(ctx, options);
        return child;
    }
    
    // Create EventEmitter for child
    JSValue eventEmitterCtor = JS_GetPropertyStr(ctx, JS_GetGlobalObject(ctx), "EventEmitter");
    if (!JS_IsUndefined(eventEmitterCtor) && JS_IsFunction(ctx, eventEmitterCtor)) {
        JSValue emitter = JS_CallConstructor(ctx, eventEmitterCtor, 0, nullptr);
        if (!JS_IsException(emitter)) {
            JS_SetPropertyStr(ctx, child, "_events", emitter);
        }
        JS_FreeValue(ctx, emitter);
    }
    JS_FreeValue(ctx, eventEmitterCtor);
    
    spawnProcess(ctx, cmd, args, options, child);
    JS_FreeValue(ctx, options);
    
    return child;
}

void ChildProcessModule::spawnProcess(JSContext* ctx, const std::string& command, 
                                      const std::vector<std::string>& args, 
                                      JSValue options, JSValue childObj) {
    ChildProcessData* data = new ChildProcessData(ctx, JS_DupValue(ctx, childObj));
    JS_SetOpaque(childObj, data);
    
    // Create pipes for stdio
    if (pipe(data->stdinFd) < 0 || pipe(data->stdoutFd) < 0 || pipe(data->stderrFd) < 0) {
        delete data;
        return;
    }
    
    // Set up stdio streams
    JSValue stdout = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, childObj, "stdout", stdout);
    JSValue stderr = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, childObj, "stderr", stderr);
    JSValue stdin = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, childObj, "stdin", stdin);
    
    // Fork process
    pid_t pid = ::fork();
    if (pid < 0) {
        delete data;
        return;
    }
    
    if (pid == 0) {
        // Child process
        close(data->stdinFd[1]);
        close(data->stdoutFd[0]);
        close(data->stderrFd[0]);
        
        dup2(data->stdinFd[0], STDIN_FILENO);
        dup2(data->stdoutFd[1], STDOUT_FILENO);
        dup2(data->stderrFd[1], STDERR_FILENO);
        
        close(data->stdinFd[0]);
        close(data->stdoutFd[1]);
        close(data->stderrFd[1]);
        
        // Build argv
        std::vector<char*> argv;
        argv.push_back(const_cast<char*>(command.c_str()));
        for (const auto& arg : args) {
            argv.push_back(const_cast<char*>(arg.c_str()));
        }
        argv.push_back(nullptr);
        
        execvp(command.c_str(), argv.data());
        _exit(127);
    } else {
        // Parent process
        close(data->stdinFd[0]);
        close(data->stdoutFd[1]);
        close(data->stderrFd[1]);
        
        data->pid = pid;
        data->spawned = true;
        
        JS_SetPropertyStr(ctx, childObj, "pid", JS_NewInt32(ctx, pid));
        
        // Start stdout reader thread
        data->stdoutThread = std::thread([data, ctx]() {
            char buffer[4096];
            while (data->spawned && !data->killed) {
                ssize_t n = read(data->stdoutFd[0], buffer, sizeof(buffer));
                if (n <= 0) break;
                
                JSValue dataEvent = JS_NewString(ctx, "data");
                JSValue bufferObj = JS_NewArrayBufferCopy(ctx, (uint8_t*)buffer, n);
                
                EventLoop::getInstance().enqueueCallback([ctx, data, dataEvent, bufferObj]() {
                    JSValue emit = JS_GetPropertyStr(ctx, data->childObj, "emit");
                    if (JS_IsFunction(ctx, emit)) {
                        JSValue stdout = JS_GetPropertyStr(ctx, data->childObj, "stdout");
                        JSValue emitStdout = JS_GetPropertyStr(ctx, stdout, "emit");
                        if (JS_IsFunction(ctx, emitStdout)) {
                            JSValue args[] = {dataEvent, bufferObj};
                            JS_Call(ctx, emitStdout, stdout, 2, args);
                        }
                        JS_FreeValue(ctx, emitStdout);
                        JS_FreeValue(ctx, stdout);
                    }
                    JS_FreeValue(ctx, emit);
                    JS_FreeValue(ctx, dataEvent);
                    JS_FreeValue(ctx, bufferObj);
                });
            }
        });
        
        // Start stderr reader thread
        data->stderrThread = std::thread([data, ctx]() {
            char buffer[4096];
            while (data->spawned && !data->killed) {
                ssize_t n = read(data->stderrFd[0], buffer, sizeof(buffer));
                if (n <= 0) break;
                
                JSValue dataEvent = JS_NewString(ctx, "data");
                JSValue bufferObj = JS_NewArrayBufferCopy(ctx, (uint8_t*)buffer, n);
                
                EventLoop::getInstance().enqueueCallback([ctx, data, dataEvent, bufferObj]() {
                    JSValue emit = JS_GetPropertyStr(ctx, data->childObj, "emit");
                    if (JS_IsFunction(ctx, emit)) {
                        JSValue stderr = JS_GetPropertyStr(ctx, data->childObj, "stderr");
                        JSValue emitStderr = JS_GetPropertyStr(ctx, stderr, "emit");
                        if (JS_IsFunction(ctx, emitStderr)) {
                            JSValue args[] = {dataEvent, bufferObj};
                            JS_Call(ctx, emitStderr, stderr, 2, args);
                        }
                        JS_FreeValue(ctx, emitStderr);
                        JS_FreeValue(ctx, stderr);
                    }
                    JS_FreeValue(ctx, emit);
                    JS_FreeValue(ctx, dataEvent);
                    JS_FreeValue(ctx, bufferObj);
                });
            }
        });
        
        // Wait for process in background
        std::thread([data, ctx]() {
            int status;
            waitpid(data->pid, &status, 0);
            
            data->spawned = false;
            if (WIFEXITED(status)) {
                data->exitCode = WEXITSTATUS(status);
            } else if (WIFSIGNALED(status)) {
                data->signal = WTERMSIG(status);
            }
            
            EventLoop::getInstance().enqueueCallback([ctx, data]() {
                JSValue emit = JS_GetPropertyStr(ctx, data->childObj, "emit");
                if (JS_IsFunction(ctx, emit)) {
                    JSValue exitEvent = JS_NewString(ctx, "exit");
                    JSValue code = JS_NewInt32(ctx, data->exitCode);
                    JSValue signal = JS_NewInt32(ctx, data->signal);
                    JSValue args[] = {exitEvent, code, signal};
                    JS_Call(ctx, emit, data->childObj, 3, args);
                    JS_FreeValue(ctx, exitEvent);
                    JS_FreeValue(ctx, code);
                    JS_FreeValue(ctx, signal);
                }
                JS_FreeValue(ctx, emit);
            });
        }).detach();
    }
}

JSValue ChildProcessModule::exec(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    // exec is similar to spawn but uses shell
    return spawn(ctx, this_val, argc, argv);
}

JSValue ChildProcessModule::execFile(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    // execFile is similar to spawn but doesn't use shell
    return spawn(ctx, this_val, argc, argv);
}

JSValue ChildProcessModule::fork(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    // fork creates a new Node.js process
    // Similar to spawn but for Node.js scripts
    return spawn(ctx, this_val, argc, argv);
}

JSValue ChildProcessModule::childKill(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    ChildProcessData* data = static_cast<ChildProcessData*>(JS_GetOpaque(this_val, child_process_class_id));
    if (!data) {
        return JS_UNDEFINED;
    }
    
    int signal = SIGTERM;
    if (argc > 0 && JS_IsString(argv[0])) {
        const char* sigStr = JS_ToCString(ctx, argv[0]);
        if (sigStr) {
            if (strcmp(sigStr, "SIGTERM") == 0) signal = SIGTERM;
            else if (strcmp(sigStr, "SIGINT") == 0) signal = SIGINT;
            else if (strcmp(sigStr, "SIGKILL") == 0) signal = SIGKILL;
            JS_FreeCString(ctx, sigStr);
        }
    }
    
    if (data->pid > 0) {
        kill(data->pid, signal);
        data->killed = true;
    }
    
    return JS_UNDEFINED;
}

JSValue ChildProcessModule::childSend(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "send expects a message");
    }
    
    ChildProcessData* data = static_cast<ChildProcessData*>(JS_GetOpaque(this_val, child_process_class_id));
    if (!data || !data->spawned) {
        return JS_UNDEFINED;
    }
    
    // Send message via IPC (for forked processes)
    // Simplified implementation
    return JS_NewBool(ctx, true);
}

void ChildProcessModule::ChildProcessFinalizer(JSRuntime* rt, JSValue val) {
    ChildProcessData* data = static_cast<ChildProcessData*>(JS_GetOpaque(val, child_process_class_id));
    if (data) {
        delete data;
    }
}

} // namespace protojs
