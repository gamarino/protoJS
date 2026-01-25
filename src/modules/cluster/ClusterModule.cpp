#include "ClusterModule.h"
#include "../events/EventsModule.h"
#include "../net/NetModule.h"
#include "../../EventLoop.h"
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <string>
#include <vector>
#include <map>
#include <atomic>
#include <cstring>

namespace protojs {

static JSClassID cluster_worker_class_id;
static std::atomic<bool> is_master_process{true};
static std::atomic<int> worker_id_counter{0};
static std::map<pid_t, JSValue> worker_processes;

struct ClusterWorkerData {
    pid_t pid;
    int ipcFd[2];  // [0] = read, [1] = write
    JSContext* mainContext;
    JSValue workerObj;
    int workerId;
    std::atomic<bool> disconnected{false};
    std::atomic<bool> killed{false};
    
    ClusterWorkerData(JSContext* ctx, JSValue worker, int id) 
        : pid(-1), mainContext(ctx), workerObj(worker), workerId(id) {
        ipcFd[0] = -1;
        ipcFd[1] = -1;
    }
    
    ~ClusterWorkerData() {
        if (ipcFd[0] >= 0) close(ipcFd[0]);
        if (ipcFd[1] >= 0) close(ipcFd[1]);
        if (!JS_IsUndefined(workerObj)) {
            JS_FreeValueRT(JS_GetRuntime(mainContext), workerObj);
        }
    }
};

void ClusterModule::init(JSContext* ctx) {
    JSRuntime* rt = JS_GetRuntime(ctx);
    
    // Register Worker class
    JS_NewClassID(&cluster_worker_class_id);
    JSClassDef workerClassDef = {
        "ClusterWorker",
        WorkerFinalizer
    };
    JS_NewClass(rt, cluster_worker_class_id, &workerClassDef);
    
    JSValue workerProto = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, workerProto, "send", JS_NewCFunction(ctx, workerSend, "send", 1));
    JS_SetPropertyStr(ctx, workerProto, "disconnect", JS_NewCFunction(ctx, workerDisconnect, "disconnect", 0));
    JS_SetPropertyStr(ctx, workerProto, "kill", JS_NewCFunction(ctx, workerKill, "kill", 1));
    JS_SetClassProto(ctx, cluster_worker_class_id, workerProto);
    
    // Create cluster module
    JSValue clusterModule = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, clusterModule, "setupMaster", JS_NewCFunction(ctx, setupMaster, "setupMaster", 1));
    JS_SetPropertyStr(ctx, clusterModule, "fork", JS_NewCFunction(ctx, fork, "fork", 1));
    
    // isMaster and isWorker getters
    JS_SetPropertyStr(ctx, clusterModule, "isMaster", JS_NewCFunction(ctx, isMasterGetter, "isMaster", 0));
    JS_SetPropertyStr(ctx, clusterModule, "isWorker", JS_NewCFunction(ctx, isWorkerGetter, "isWorker", 0));
    
    JSValue global_obj = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global_obj, "cluster", clusterModule);
    JS_FreeValue(ctx, global_obj);
}

JSValue ClusterModule::setupMaster(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    // Setup master process configuration
    // For now, just return undefined (options can be parsed if needed)
    return JS_UNDEFINED;
}

JSValue ClusterModule::fork(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (!is_master_process) {
        return JS_ThrowTypeError(ctx, "fork() can only be called from master process");
    }
    
    // Parse environment variables if provided
    std::map<std::string, std::string> env;
    if (argc > 0 && JS_IsObject(argv[0])) {
        JSValue envVal = JS_GetPropertyStr(ctx, argv[0], "env");
        if (JS_IsObject(envVal)) {
            // Iterate over env object properties
            JSPropertyEnum* props = nullptr;
            uint32_t propCount = 0;
            if (JS_GetOwnPropertyNames(ctx, &props, &propCount, envVal, JS_GPN_STRING_MASK) >= 0) {
                for (uint32_t i = 0; i < propCount; i++) {
                    JSValue key = JS_AtomToValue(ctx, props[i].atom);
                    JSValue val = JS_GetProperty(ctx, envVal, props[i].atom);
                    const char* keyStr = JS_ToCString(ctx, key);
                    const char* valStr = JS_ToCString(ctx, val);
                    if (keyStr && valStr) {
                        env[keyStr] = valStr;
                    }
                    if (keyStr) JS_FreeCString(ctx, keyStr);
                    if (valStr) JS_FreeCString(ctx, valStr);
                    JS_FreeValue(ctx, key);
                    JS_FreeValue(ctx, val);
                }
                js_free(ctx, props);
            }
        }
        JS_FreeValue(ctx, envVal);
    }
    
    int workerId = ++worker_id_counter;
    
    // Create pipe for IPC
    int pipeFd[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, pipeFd) < 0) {
        return JS_ThrowTypeError(ctx, "Failed to create IPC pipe");
    }
    
    JSValue worker = JS_NewObjectClass(ctx, cluster_worker_class_id);
    if (JS_IsException(worker)) {
        close(pipeFd[0]);
        close(pipeFd[1]);
        return worker;
    }
    
    // Create EventEmitter for worker
    JSValue eventEmitterCtor = JS_GetPropertyStr(ctx, JS_GetGlobalObject(ctx), "EventEmitter");
    if (!JS_IsUndefined(eventEmitterCtor) && JS_IsFunction(ctx, eventEmitterCtor)) {
        JSValue emitter = JS_CallConstructor(ctx, eventEmitterCtor, 0, nullptr);
        if (!JS_IsException(emitter)) {
            JS_SetPropertyStr(ctx, worker, "_events", emitter);
        }
        JS_FreeValue(ctx, emitter);
    }
    JS_FreeValue(ctx, eventEmitterCtor);
    
    ClusterWorkerData* data = new ClusterWorkerData(ctx, JS_DupValue(ctx, worker), workerId);
    data->ipcFd[0] = pipeFd[0];
    data->ipcFd[1] = pipeFd[1];
    JS_SetOpaque(worker, data);
    
    // Set worker properties
    JS_SetPropertyStr(ctx, worker, "id", JS_NewInt32(ctx, workerId));
    JS_SetPropertyStr(ctx, worker, "process", JS_NewObject(ctx)); // Placeholder
    
    // Fork process
    pid_t pid = ::fork();
    if (pid < 0) {
        delete data;
        close(pipeFd[0]);
        close(pipeFd[1]);
        return JS_ThrowTypeError(ctx, "fork() failed");
    }
    
    if (pid == 0) {
        // Child process (worker)
        is_master_process = false;
        close(pipeFd[1]); // Close write end in child
        
        // In child process, we need to reinitialize context
        // For now, just exit - full implementation would execute worker code
        _exit(0);
    } else {
        // Parent process (master)
        close(pipeFd[0]); // Close read end in parent
        data->pid = pid;
        worker_processes[pid] = JS_DupValue(ctx, worker);
        
        // Set process property
        JSValue process = JS_GetPropertyStr(ctx, worker, "process");
        JS_SetPropertyStr(ctx, process, "pid", JS_NewInt32(ctx, pid));
        JS_FreeValue(ctx, process);
        
        // Emit 'online' event
        EventLoop::getInstance().enqueueCallback([ctx, worker]() {
            JSValue emit = JS_GetPropertyStr(ctx, worker, "emit");
            if (JS_IsFunction(ctx, emit)) {
                JSValue onlineEvent = JS_NewString(ctx, "online");
                JSValue args[] = {onlineEvent};
                JS_Call(ctx, emit, worker, 1, args);
                JS_FreeValue(ctx, onlineEvent);
            }
            JS_FreeValue(ctx, emit);
        });
    }
    
    return worker;
}

void ClusterModule::workerProcessExecution(JSContext* mainCtx, JSValue workerObj, int workerId, int ipcFd) {
    // In worker process, set up IPC listener
    // For now, this is a placeholder - full implementation would:
    // 1. Set up message listener on ipcFd
    // 2. Execute worker code
    // 3. Handle messages from master
}

JSValue ClusterModule::workerSend(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "send expects a message");
    }
    
    ClusterWorkerData* data = static_cast<ClusterWorkerData*>(JS_GetOpaque(this_val, cluster_worker_class_id));
    if (!data || data->disconnected || data->killed) {
        return JS_UNDEFINED;
    }
    
    // Serialize message and send via IPC
    // For now, simplified - would serialize JSValue to JSON or binary
    JSValue message = JS_DupValue(ctx, argv[0]);
    sendMessageToWorker(ctx, this_val, message);
    
    return JS_UNDEFINED;
}

void ClusterModule::sendMessageToWorker(JSContext* mainCtx, JSValue workerObj, JSValue message) {
    ClusterWorkerData* data = static_cast<ClusterWorkerData*>(JS_GetOpaque(workerObj, cluster_worker_class_id));
    if (!data || data->ipcFd[1] < 0) {
        JS_FreeValue(mainCtx, message);
        return;
    }
    
    // Serialize message to string (simplified)
    const char* msgStr = JS_ToCString(mainCtx, message);
    if (msgStr) {
        std::string msg(msgStr);
        (void)write(data->ipcFd[1], msg.c_str(), msg.length());
        JS_FreeCString(mainCtx, msgStr);
    }
    JS_FreeValue(mainCtx, message);
}

JSValue ClusterModule::workerDisconnect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    ClusterWorkerData* data = static_cast<ClusterWorkerData*>(JS_GetOpaque(this_val, cluster_worker_class_id));
    if (data) {
        data->disconnected = true;
        if (data->ipcFd[1] >= 0) {
            close(data->ipcFd[1]);
            data->ipcFd[1] = -1;
        }
    }
    return JS_UNDEFINED;
}

JSValue ClusterModule::workerKill(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    ClusterWorkerData* data = static_cast<ClusterWorkerData*>(JS_GetOpaque(this_val, cluster_worker_class_id));
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

void ClusterModule::WorkerFinalizer(JSRuntime* rt, JSValue val) {
    ClusterWorkerData* data = static_cast<ClusterWorkerData*>(JS_GetOpaque(val, cluster_worker_class_id));
    if (data) {
        if (data->pid > 0) {
            worker_processes.erase(data->pid);
        }
        delete data;
    }
}

JSValue ClusterModule::isMasterGetter(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return JS_NewBool(ctx, is_master_process);
}

JSValue ClusterModule::isWorkerGetter(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return JS_NewBool(ctx, !is_master_process);
}

} // namespace protojs
