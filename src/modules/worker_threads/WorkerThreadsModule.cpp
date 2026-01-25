#include "WorkerThreadsModule.h"
#include "../events/EventsModule.h"
#include "../../EventLoop.h"
#include "../../CPUThreadPool.h"
#include "../../JSContext.h"
#include <fstream>
#include <sstream>
#include <thread>
#include <mutex>
#include <queue>
#include <atomic>

namespace protojs {

static JSClassID worker_thread_class_id;
static thread_local bool is_worker_thread = false;
static thread_local JSValue worker_data_value = JS_UNDEFINED;
static thread_local JSValue parent_port_value = JS_UNDEFINED;

struct WorkerThreadData {
    std::thread workerThread;
    JSRuntime* workerRuntime;
    JSContext* workerContext;
    JSContext* mainContext;
    JSValue workerObj;
    std::string filename;
    JSValue workerData;
    std::mutex messageMutex;
    std::queue<JSValue> messageQueue;
    std::atomic<bool> terminated{false};
    std::atomic<bool> running{true};
    
    WorkerThreadData(JSContext* mainCtx, JSValue worker, const std::string& file, JSValue data)
        : mainContext(mainCtx), workerObj(worker), filename(file), workerData(data),
          workerRuntime(nullptr), workerContext(nullptr) {}
    
    ~WorkerThreadData() {
        if (workerThread.joinable()) {
            terminated = true;
            running = false;
            workerThread.join();
        }
        if (workerContext) {
            JS_FreeContext(workerContext);
        }
        if (workerRuntime) {
            JS_FreeRuntime(workerRuntime);
        }
        if (!JS_IsUndefined(workerObj)) {
            JS_FreeValueRT(JS_GetRuntime(mainContext), workerObj);
        }
        if (!JS_IsUndefined(workerData)) {
            JS_FreeValueRT(JS_GetRuntime(mainContext), workerData);
        }
    }
};

void WorkerThreadsModule::init(JSContext* ctx) {
    JSRuntime* rt = JS_GetRuntime(ctx);
    
    // Register Worker class
    JS_NewClassID(&worker_thread_class_id);
    JSClassDef workerClassDef = {
        "Worker",
        WorkerFinalizer
    };
    JS_NewClass(rt, worker_thread_class_id, &workerClassDef);
    
    JSValue workerProto = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, workerProto, "postMessage", JS_NewCFunction(ctx, workerPostMessage, "postMessage", 1));
    JS_SetPropertyStr(ctx, workerProto, "terminate", JS_NewCFunction(ctx, workerTerminate, "terminate", 0));
    JS_SetClassProto(ctx, worker_thread_class_id, workerProto);
    
    // Create worker_threads module
    JSValue workerThreadsModule = JS_NewObject(ctx);
    
    // Worker constructor
    JSValue workerCtor = JS_NewCFunction2(ctx, WorkerConstructor, "Worker", 1, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, workerCtor, workerProto);
    JS_SetPropertyStr(ctx, workerThreadsModule, "Worker", workerCtor);
    
    // isMainThread
    JS_SetPropertyStr(ctx, workerThreadsModule, "isMainThread", JS_NewCFunction(ctx, isMainThread, "isMainThread", 0));
    
    // parentPort (getter that returns appropriate value)
    JS_SetPropertyStr(ctx, workerThreadsModule, "parentPort", JS_NewCFunction(ctx, parentPortGetter, "parentPort", 0));
    
    // workerData (getter)
    JS_SetPropertyStr(ctx, workerThreadsModule, "workerData", JS_NewCFunction(ctx, workerDataGetter, "workerData", 0));
    
    JSValue global_obj = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global_obj, "worker_threads", workerThreadsModule);
    JS_FreeValue(ctx, global_obj);
}

JSValue WorkerThreadsModule::WorkerConstructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "Worker constructor expects filename");
    }
    
    const char* filename = JS_ToCString(ctx, argv[0]);
    if (!filename) return JS_EXCEPTION;
    
    std::string filePath(filename);
    JS_FreeCString(ctx, filename);
    
    // Parse options (second argument)
    JSValue workerData = JS_UNDEFINED;
    if (argc > 1 && JS_IsObject(argv[1])) {
        JSValue dataVal = JS_GetPropertyStr(ctx, argv[1], "workerData");
        if (!JS_IsUndefined(dataVal)) {
            workerData = JS_DupValue(ctx, dataVal);
        }
        JS_FreeValue(ctx, dataVal);
    }
    
    JSValue worker = JS_NewObjectClass(ctx, worker_thread_class_id);
    if (JS_IsException(worker)) {
        if (!JS_IsUndefined(workerData)) {
            JS_FreeValue(ctx, workerData);
        }
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
    
    WorkerThreadData* data = new WorkerThreadData(ctx, JS_DupValue(ctx, worker), filePath, workerData);
    JS_SetOpaque(worker, data);
    
    // Start worker thread
    data->workerThread = std::thread([data]() {
        workerThreadExecution(data->mainContext, data->filename, data->workerData, data->workerObj);
    });
    
    return worker;
}

void WorkerThreadsModule::workerThreadExecution(JSContext* mainCtx, const std::string& filename, JSValue workerData, JSValue workerObj) {
    is_worker_thread = true;
    worker_data_value = workerData;
    
    // Create runtime and context for worker thread
    JSRuntime* workerRt = JS_NewRuntime();
    if (!workerRt) {
        EventLoop::getInstance().enqueueCallback([mainCtx, workerObj]() {
            JSValue emit = JS_GetPropertyStr(mainCtx, workerObj, "emit");
            if (JS_IsFunction(mainCtx, emit)) {
                JSValue errorEvent = JS_NewString(mainCtx, "error");
                JSValue error = JS_NewString(mainCtx, "Failed to create worker runtime");
                JSValue args[] = {errorEvent, error};
                JS_Call(mainCtx, emit, workerObj, 2, args);
                JS_FreeValue(mainCtx, errorEvent);
                JS_FreeValue(mainCtx, error);
            }
            JS_FreeValue(mainCtx, emit);
        });
        return;
    }
    
    JSContext* workerCtx = JS_NewContext(workerRt);
    if (!workerCtx) {
        JS_FreeRuntime(workerRt);
        EventLoop::getInstance().enqueueCallback([mainCtx, workerObj]() {
            JSValue emit = JS_GetPropertyStr(mainCtx, workerObj, "emit");
            if (JS_IsFunction(mainCtx, emit)) {
                JSValue errorEvent = JS_NewString(mainCtx, "error");
                JSValue error = JS_NewString(mainCtx, "Failed to create worker context");
                JSValue args[] = {errorEvent, error};
                JS_Call(mainCtx, emit, workerObj, 2, args);
                JS_FreeValue(mainCtx, errorEvent);
                JS_FreeValue(mainCtx, error);
            }
            JS_FreeValue(mainCtx, emit);
        });
        return;
    }
    
    WorkerThreadData* data = static_cast<WorkerThreadData*>(JS_GetOpaque(workerObj, worker_thread_class_id));
    if (data) {
        data->workerRuntime = workerRt;
        data->workerContext = workerCtx;
    }
    
    // Create parentPort object (EventEmitter-like)
    JSValue parentPort = JS_NewObject(workerCtx);
    JSValue eventEmitterCtor = JS_GetPropertyStr(workerCtx, JS_GetGlobalObject(workerCtx), "EventEmitter");
    if (!JS_IsUndefined(eventEmitterCtor) && JS_IsFunction(workerCtx, eventEmitterCtor)) {
        JSValue emitter = JS_CallConstructor(workerCtx, eventEmitterCtor, 0, nullptr);
        if (!JS_IsException(emitter)) {
            JS_SetPropertyStr(workerCtx, parentPort, "_events", emitter);
        }
        JS_FreeValue(workerCtx, emitter);
    }
    JS_FreeValue(workerCtx, eventEmitterCtor);
    
    // Store data pointer in parentPort opaque for postMessage access
    JS_SetOpaque(parentPort, data);
    
    // Create postMessage function
    JS_SetPropertyStr(workerCtx, parentPort, "postMessage", JS_NewCFunction(workerCtx, 
        [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
            WorkerThreadData* wdata = static_cast<WorkerThreadData*>(JS_GetOpaque(this_val, 0));
            if (wdata && argc > 0) {
                JSValue message = JS_DupValue(ctx, argv[0]);
                sendMessageToMain(wdata->mainContext, wdata->workerObj, message);
            }
            return JS_UNDEFINED;
        }, "postMessage", 1));
    
    parent_port_value = parentPort;
    
    // Set parentPort in global scope
    JSValue global = JS_GetGlobalObject(workerCtx);
    JS_SetPropertyStr(workerCtx, global, "parentPort", JS_DupValue(workerCtx, parentPort));
    JS_FreeValue(workerCtx, global);
    
    // Set workerData in global scope
    if (!JS_IsUndefined(workerData)) {
        JSValue global = JS_GetGlobalObject(workerCtx);
        JS_SetPropertyStr(workerCtx, global, "workerData", JS_DupValue(workerCtx, workerData));
        JS_FreeValue(workerCtx, global);
    }
    
    // Load and execute worker script
    std::ifstream file(filename);
    if (!file.is_open()) {
        EventLoop::getInstance().enqueueCallback([mainCtx, workerObj, filename]() {
            JSValue emit = JS_GetPropertyStr(mainCtx, workerObj, "emit");
            if (JS_IsFunction(mainCtx, emit)) {
                JSValue errorEvent = JS_NewString(mainCtx, "error");
                std::string errorMsg = "Cannot open file: " + filename;
                JSValue error = JS_NewString(mainCtx, errorMsg.c_str());
                JSValue args[] = {errorEvent, error};
                JS_Call(mainCtx, emit, workerObj, 2, args);
                JS_FreeValue(mainCtx, errorEvent);
                JS_FreeValue(mainCtx, error);
            }
            JS_FreeValue(mainCtx, emit);
        });
        JS_FreeContext(workerCtx);
        JS_FreeRuntime(workerRt);
        return;
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string code = buffer.str();
    file.close();
    
    // Execute worker script
    JSValue result = JS_Eval(workerCtx, code.c_str(), code.length(), filename.c_str(), JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(result)) {
        JSValue exception = JS_GetException(workerCtx);
        const char* error = JS_ToCString(workerCtx, exception);
        if (error) {
            EventLoop::getInstance().enqueueCallback([mainCtx, workerObj, error]() {
                JSValue emit = JS_GetPropertyStr(mainCtx, workerObj, "emit");
                if (JS_IsFunction(mainCtx, emit)) {
                    JSValue errorEvent = JS_NewString(mainCtx, "error");
                    JSValue errorVal = JS_NewString(mainCtx, error);
                    JSValue args[] = {errorEvent, errorVal};
                    JS_Call(mainCtx, emit, workerObj, 2, args);
                    JS_FreeValue(mainCtx, errorEvent);
                    JS_FreeValue(mainCtx, errorVal);
                }
                JS_FreeValue(mainCtx, emit);
            });
            JS_FreeCString(workerCtx, error);
        }
        JS_FreeValue(workerCtx, exception);
    }
    JS_FreeValue(workerCtx, result);
    
    // Emit exit event
    EventLoop::getInstance().enqueueCallback([mainCtx, workerObj]() {
        JSValue emit = JS_GetPropertyStr(mainCtx, workerObj, "emit");
        if (JS_IsFunction(mainCtx, emit)) {
            JSValue exitEvent = JS_NewString(mainCtx, "exit");
            JSValue args[] = {exitEvent};
            JS_Call(mainCtx, emit, workerObj, 1, args);
            JS_FreeValue(mainCtx, exitEvent);
        }
        JS_FreeValue(mainCtx, emit);
    });
}

void WorkerThreadsModule::sendMessageToMain(JSContext* mainCtx, JSValue workerObj, JSValue message) {
    EventLoop::getInstance().enqueueCallback([mainCtx, workerObj, message]() {
        JSValue emit = JS_GetPropertyStr(mainCtx, workerObj, "emit");
        if (JS_IsFunction(mainCtx, emit)) {
            JSValue messageEvent = JS_NewString(mainCtx, "message");
            JSValue args[] = {messageEvent, message};
            JS_Call(mainCtx, emit, workerObj, 2, args);
            JS_FreeValue(mainCtx, messageEvent);
        }
        JS_FreeValue(mainCtx, emit);
        JS_FreeValue(mainCtx, message);
    });
}

JSValue WorkerThreadsModule::workerPostMessage(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "postMessage expects a message");
    }
    
    WorkerThreadData* data = static_cast<WorkerThreadData*>(JS_GetOpaque(this_val, worker_thread_class_id));
    if (!data || !data->running) {
        return JS_UNDEFINED;
    }
    
    JSValue message = JS_DupValue(ctx, argv[0]);
    
    // Send message to worker thread
    if (data->workerContext) {
        EventLoop::getInstance().enqueueCallback([data, message]() {
            JSValue parentPort = JS_GetPropertyStr(data->workerContext, JS_GetGlobalObject(data->workerContext), "parentPort");
            if (!JS_IsUndefined(parentPort)) {
                JSValue emit = JS_GetPropertyStr(data->workerContext, parentPort, "emit");
                if (JS_IsFunction(data->workerContext, emit)) {
                    JSValue messageEvent = JS_NewString(data->workerContext, "message");
                    JSValue args[] = {messageEvent, message};
                    JS_Call(data->workerContext, emit, parentPort, 2, args);
                    JS_FreeValue(data->workerContext, messageEvent);
                }
                JS_FreeValue(data->workerContext, emit);
            }
            JS_FreeValue(data->workerContext, parentPort);
            JS_FreeValue(data->workerContext, message);
        });
    }
    
    return JS_UNDEFINED;
}

JSValue WorkerThreadsModule::workerTerminate(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    WorkerThreadData* data = static_cast<WorkerThreadData*>(JS_GetOpaque(this_val, worker_thread_class_id));
    if (data) {
        data->terminated = true;
        data->running = false;
    }
    return JS_UNDEFINED;
}

void WorkerThreadsModule::WorkerFinalizer(JSRuntime* rt, JSValue val) {
    WorkerThreadData* data = static_cast<WorkerThreadData*>(JS_GetOpaque(val, worker_thread_class_id));
    if (data) {
        delete data;
    }
}

JSValue WorkerThreadsModule::isMainThread(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return JS_NewBool(ctx, !is_worker_thread);
}

JSValue WorkerThreadsModule::parentPortGetter(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (is_worker_thread && !JS_IsUndefined(parent_port_value)) {
        return JS_DupValue(ctx, parent_port_value);
    }
    return JS_NULL;
}

JSValue WorkerThreadsModule::workerDataGetter(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (is_worker_thread && !JS_IsUndefined(worker_data_value)) {
        return JS_DupValue(ctx, worker_data_value);
    }
    return JS_UNDEFINED;
}

} // namespace protojs
