#include "WorkerThreadsModule.h"
#include "../events/EventsModule.h"
#include "../../EventLoop.h"
#include "../../CPUThreadPool.h"
#include "../../JSContext.h"
#include "quickjs.h"
#include "headers/protoCore.h"
#include <fstream>
#include <sstream>
#include <thread>
#include <mutex>
#include <queue>
#include <atomic>
#include <memory>
#include <unordered_map>
#include <string>

namespace protojs {

static JSClassID worker_thread_class_id;
static thread_local bool is_worker_thread = false;
static thread_local JSValue worker_data_value = JS_UNDEFINED;
static thread_local JSValue parent_port_value = JS_UNDEFINED;
static std::atomic<int> active_worker_count{0};

struct WorkerThreadData;
/** Map workerId -> WorkerThreadData* for ProtoThread entry lookup. */
static std::unordered_map<std::string, WorkerThreadData*> s_workerDataMap;
static std::mutex s_workerDataMutex;
static std::atomic<uint64_t> s_workerIdCounter{0};

struct WorkerThreadData {
    /** When using ProtoThread: join this instead of workerThread. */
    const proto::ProtoThread* protoThread{nullptr};
    std::thread workerThread;
    /** Worker runs on protoCore path via this wrapper (compile+load+run). Owned here; destroyed after join. */
    std::unique_ptr<JSContextWrapper> workerWrapper;
    JSRuntime* workerRuntime;
    JSContext* workerContext;
    JSContext* mainContext;
    JSValue workerObj;
    /** EventEmitter instance for worker (main context); used by workerOn/workerEmit to avoid GetPropertyStr on Worker. */
    JSValue workerEventsObj;
    std::string filename;
    /** JSON-serialized workerData (main context); parsed in worker context. */
    std::string workerDataJson;
    std::string workerId;
    std::mutex messageMutex;
    std::queue<JSValue> messageQueue;
    std::atomic<bool> terminated{false};
    std::atomic<bool> running{true};

    WorkerThreadData(JSContext* mainCtx, JSValue worker, JSValue eventsObj, const std::string& file, std::string dataJson)
        : mainContext(mainCtx), workerObj(worker), workerEventsObj(eventsObj), filename(file), workerDataJson(std::move(dataJson)),
          workerRuntime(nullptr), workerContext(nullptr) {}

    ~WorkerThreadData() {
        if (protoThread) {
            JSContextWrapper* wrapper = static_cast<JSContextWrapper*>(JS_GetContextOpaque(mainContext));
            if (wrapper) {
                const_cast<proto::ProtoThread*>(protoThread)->join(wrapper->getProtoContext());
            }
        }
        if (workerThread.joinable()) {
            terminated = true;
            running = false;
            workerThread.join();
        }
        {
            std::lock_guard<std::mutex> lock(s_workerDataMutex);
            s_workerDataMap.erase(workerId);
        }
        workerWrapper.reset();
        if (!JS_IsUndefined(workerObj)) {
            JS_FreeValueRT(JS_GetRuntime(mainContext), workerObj);
        }
        if (!JS_IsUndefined(workerEventsObj)) {
            JS_FreeValueRT(JS_GetRuntime(mainContext), workerEventsObj);
        }
    }
};

/** ProtoMethod run in the worker OS thread (ProtoThread). Looks up data by workerId and runs worker script. */
static const proto::ProtoObject* workerProtoThreadEntry(
    proto::ProtoContext* context,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* args,
    const proto::ProtoSparseList* /*kwargs*/)
{
    if (!context || !args || args->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* arg0 = args->getAt(context, 0);
    if (!arg0 || !arg0->isString(context)) return PROTO_NONE;
    const proto::ProtoString* idStr = arg0->asString(context);
    std::string workerId;
    idStr->toUTF8String(context, workerId);

    WorkerThreadData* data = nullptr;
    {
        std::lock_guard<std::mutex> lock(s_workerDataMutex);
        auto it = s_workerDataMap.find(workerId);
        if (it != s_workerDataMap.end()) {
            data = it->second;
        }
    }
    if (data) {
        WorkerThreadsModule::workerThreadExecution(data->mainContext, data->filename, data->workerDataJson, data->workerObj);
        active_worker_count--;
        std::lock_guard<std::mutex> lock(s_workerDataMutex);
        s_workerDataMap.erase(workerId);
    }
    return PROTO_NONE;
}

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
    JS_SetPropertyStr(ctx, workerProto, "on", JS_NewCFunction(ctx, workerOn, "on", 2));
    JS_SetPropertyStr(ctx, workerProto, "emit", JS_NewCFunction(ctx, workerEmit, "emit", 2));
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
    
    // Parse options (second argument) and serialize workerData so it can be used in the worker context
    std::string workerDataJson;
    if (argc > 1 && JS_IsObject(argv[1])) {
        JSValue dataVal = JS_GetPropertyStr(ctx, argv[1], "workerData");
        if (!JS_IsUndefined(dataVal)) {
            JSValue jsonVal = JS_JSONStringify(ctx, dataVal, JS_UNDEFINED, JS_UNDEFINED);
            JS_FreeValue(ctx, dataVal);
            if (!JS_IsException(jsonVal)) {
                size_t len = 0;
                const char* cstr = JS_ToCStringLen(ctx, &len, jsonVal);
                if (cstr) {
                    workerDataJson.assign(cstr, len);
                    JS_FreeCString(ctx, cstr);
                }
                JS_FreeValue(ctx, jsonVal);
            }
        }
    }

    JSValue worker = JS_NewObjectClass(ctx, worker_thread_class_id);
    if (JS_IsException(worker)) {
        return worker;
    }

    // Create EventEmitter for worker (EventEmitter lives under global.events, not global)
    JSValue global_obj = JS_GetGlobalObject(ctx);
    JSValue eventsMod = JS_GetPropertyStr(ctx, global_obj, "events");
    JS_FreeValue(ctx, global_obj);
    JSValue eventEmitterCtor = JS_IsUndefined(eventsMod) ? JS_UNDEFINED : JS_GetPropertyStr(ctx, eventsMod, "EventEmitter");
    JS_FreeValue(ctx, eventsMod);
    JSValue emitter = JS_UNDEFINED;
    if (!JS_IsUndefined(eventEmitterCtor) && JS_IsFunction(ctx, eventEmitterCtor)) {
        emitter = JS_CallConstructor(ctx, eventEmitterCtor, 0, nullptr);
        if (!JS_IsException(emitter)) {
            JS_SetPropertyStr(ctx, worker, "_events", JS_DupValue(ctx, emitter));
        }
    }
    JS_FreeValue(ctx, eventEmitterCtor);

    WorkerThreadData* data = new WorkerThreadData(ctx, JS_DupValue(ctx, worker),
        JS_IsException(emitter) ? JS_UNDEFINED : JS_DupValue(ctx, emitter),
        filePath, std::move(workerDataJson));
    if (!JS_IsUndefined(emitter) && !JS_IsException(emitter)) {
        JS_FreeValue(ctx, emitter);
    }
    JS_SetOpaque(worker, data);

    data->workerId = "w" + std::to_string(s_workerIdCounter++);
    {
        std::lock_guard<std::mutex> lock(s_workerDataMutex);
        s_workerDataMap[data->workerId] = data;
    }

    JSContextWrapper* wrapper = static_cast<JSContextWrapper*>(JS_GetContextOpaque(ctx));
    if (wrapper) {
        proto::ProtoSpace* space = wrapper->getProtoSpace();
        proto::ProtoContext* pctx = wrapper->getProtoContext();
        const proto::ProtoList* argsList = pctx->newList();
        argsList = argsList->appendLast(pctx, pctx->fromUTF8String(data->workerId.c_str()));
        const proto::ProtoString* threadName = pctx->fromUTF8String(("worker-" + data->workerId).c_str())->asString(pctx);
        const proto::ProtoThread* thread = space->newThread(pctx, threadName, workerProtoThreadEntry, argsList, nullptr);
        if (thread) {
            data->protoThread = thread;
            active_worker_count++;
        } else {
            std::lock_guard<std::mutex> lock(s_workerDataMutex);
            s_workerDataMap.erase(data->workerId);
            data->workerThread = std::thread([data]() {
                workerThreadExecution(data->mainContext, data->filename, data->workerDataJson, data->workerObj);
                active_worker_count--;
            });
            active_worker_count++;
        }
    } else {
        active_worker_count++;
        data->workerThread = std::thread([data]() {
            workerThreadExecution(data->mainContext, data->filename, data->workerDataJson, data->workerObj);
            active_worker_count--;
        });
    }

    return worker;
}

int WorkerThreadsModule::getActiveWorkerCount() {
    return active_worker_count.load();
}

void WorkerThreadsModule::workerThreadExecution(JSContext* mainCtx, const std::string& filename, const std::string& workerDataJson, JSValue workerObj) {
    is_worker_thread = true;
    worker_data_value = JS_UNDEFINED;

    WorkerThreadData* data = static_cast<WorkerThreadData*>(JS_GetOpaque(workerObj, worker_thread_class_id));
    if (!data) {
        return;
    }

    // Create worker context via JSContextWrapper (protoCore path: compile+load+run)
    auto wrapper = std::make_unique<JSContextWrapper>(0, 0, 3.0);
    wrapper->setUseProtoEval(true);
    data->workerWrapper = std::move(wrapper);
    data->workerContext = data->workerWrapper->getJSContext();
    data->workerRuntime = data->workerWrapper->getJSRuntime();

    JSContext* workerCtx = data->workerContext;

    // EventsModule required for parentPort (EventEmitter).
    // EventsModule was migrated to register on the protoCore-native
    // global; the JS_GetPropertyStr lookup below still hits a
    // QuickJS-side global, so for now we install on BOTH globals to
    // keep this worker thread initialiser working until the worker's
    // setup is itself migrated (tracked separately as part of the
    // WorkerThreadsModule migration in MIGRATION_QUICKJS_TO_PROTOCORE.md).
    {
        const proto::ProtoObject* nativeGlobal =
            data->workerWrapper->getNativeGlobal();
        nativeGlobal = EventsModule::init(
            data->workerWrapper->getProtoContext(), nativeGlobal);
        data->workerWrapper->updateNativeGlobal(nativeGlobal);
    }

    // Create parentPort object (EventEmitter-like)
    JSValue parentPort = JS_NewObject(workerCtx);
    JSValue globalObj = JS_GetGlobalObject(workerCtx);
    JSValue eventsMod = JS_GetPropertyStr(workerCtx, globalObj, "events");
    JSValue eventEmitterCtor = JS_IsUndefined(eventsMod) ? JS_UNDEFINED : JS_GetPropertyStr(workerCtx, eventsMod, "EventEmitter");
    JS_FreeValue(workerCtx, eventsMod);
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

    // Create postMessage function (serializes message to JSON so it can be parsed in main context)
    JS_SetPropertyStr(workerCtx, parentPort, "postMessage", JS_NewCFunction(workerCtx, workerParentPortPostMessage, "postMessage", 1));

    parent_port_value = parentPort;

    // Set parentPort in global scope
    JS_SetPropertyStr(workerCtx, globalObj, "parentPort", JS_DupValue(workerCtx, parentPort));
    JS_FreeValue(workerCtx, globalObj);

    // Set workerData in global scope (parsed from JSON so it belongs to this context)
    if (!workerDataJson.empty()) {
        JSValue parsed = JS_ParseJSON(workerCtx, workerDataJson.c_str(), workerDataJson.size(), "<workerData>");
        if (!JS_IsException(parsed)) {
            worker_data_value = JS_DupValue(workerCtx, parsed);
            JSValue globalW = JS_GetGlobalObject(workerCtx);
            JS_SetPropertyStr(workerCtx, globalW, "workerData", JS_DupValue(workerCtx, parsed));
            JS_FreeValue(workerCtx, globalW);
            JS_FreeValue(workerCtx, parsed);
        }
    }

    // Load worker script from file
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
        return;
    }

    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string code = buffer.str();
    file.close();

    // Execute worker script via protoCore path (compile+load+run)
    JSValue result = data->workerWrapper->eval(code, filename);
    if (JS_IsException(result)) {
        JSValue exception = JS_GetException(workerCtx);
        const char* error = JS_ToCString(workerCtx, exception);
        if (error) {
            std::string errorCopy(error);
            JS_FreeCString(workerCtx, error);
            EventLoop::getInstance().enqueueCallback([mainCtx, workerObj, errorCopy]() {
                JSValue emit = JS_GetPropertyStr(mainCtx, workerObj, "emit");
                if (JS_IsFunction(mainCtx, emit)) {
                    JSValue errorEvent = JS_NewString(mainCtx, "error");
                    JSValue errorVal = JS_NewString(mainCtx, errorCopy.c_str());
                    JSValue args[] = {errorEvent, errorVal};
                    JS_Call(mainCtx, emit, workerObj, 2, args);
                    JS_FreeValue(mainCtx, errorEvent);
                    JS_FreeValue(mainCtx, errorVal);
                }
                JS_FreeValue(mainCtx, emit);
            });
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

void WorkerThreadsModule::sendMessageToMainJson(JSContext* mainCtx, JSValue workerObj, const std::string& jsonMessage) {
    EventLoop::getInstance().enqueueCallback([mainCtx, workerObj, jsonMessage]() {
        JSValue message = JS_ParseJSON(mainCtx, jsonMessage.c_str(), jsonMessage.size(), "<worker message>");
        if (JS_IsException(message)) {
            message = JS_NULL;
        }
        JSValue emitFn = JS_GetPropertyStr(mainCtx, workerObj, "emit");
        if (JS_IsFunction(mainCtx, emitFn)) {
            JSValue messageEvent = JS_NewString(mainCtx, "message");
            JSValue args[] = { messageEvent, message };
            JS_Call(mainCtx, emitFn, workerObj, 2, args);
            JS_FreeValue(mainCtx, messageEvent);
        }
        JS_FreeValue(mainCtx, emitFn);
        JS_FreeValue(mainCtx, message);
    });
}

JSValue WorkerThreadsModule::workerParentPortPostMessage(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    WorkerThreadData* wdata = static_cast<WorkerThreadData*>(JS_GetOpaque(this_val, 0));
    if (!wdata || argc < 1) {
        return JS_UNDEFINED;
    }
    JSValue jsonVal = JS_JSONStringify(ctx, argv[0], JS_UNDEFINED, JS_UNDEFINED);
    if (JS_IsException(jsonVal)) {
        return jsonVal;
    }
    size_t len = 0;
    const char* cstr = JS_ToCStringLen(ctx, &len, jsonVal);
    std::string copy(cstr ? cstr : "null", cstr ? len : 4);
    if (cstr) {
        JS_FreeCString(ctx, cstr);
    }
    JS_FreeValue(ctx, jsonVal);
    sendMessageToMainJson(wdata->mainContext, wdata->workerObj, copy);
    return JS_UNDEFINED;
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

JSValue WorkerThreadsModule::workerEmit(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "emit expects at least event name");
    }
    WorkerThreadData* data = static_cast<WorkerThreadData*>(JS_GetOpaque(this_val, worker_thread_class_id));
    if (!data || JS_IsUndefined(data->workerEventsObj)) {
        return JS_UNDEFINED;
    }
    JSValue emitMethod = JS_GetPropertyStr(ctx, data->workerEventsObj, "emit");
    if (!JS_IsFunction(ctx, emitMethod)) {
        JS_FreeValue(ctx, emitMethod);
        return JS_UNDEFINED;
    }
    const int maxEmitArgs = 8;
    JSValue args[maxEmitArgs];
    int n = argc > maxEmitArgs ? maxEmitArgs : argc;
    for (int i = 0; i < n; i++) {
        args[i] = JS_DupValue(ctx, argv[i]);
    }
    JSValue result = JS_Call(ctx, emitMethod, data->workerEventsObj, n, args);
    for (int i = 0; i < n; i++) {
        JS_FreeValue(ctx, args[i]);
    }
    JS_FreeValue(ctx, emitMethod);
    return result;
}

JSValue WorkerThreadsModule::workerOn(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 2 || !JS_IsFunction(ctx, argv[1])) {
        return JS_ThrowTypeError(ctx, "on expects (eventName, callback)");
    }
    WorkerThreadData* data = static_cast<WorkerThreadData*>(JS_GetOpaque(this_val, worker_thread_class_id));
    if (!data || JS_IsUndefined(data->workerEventsObj)) {
        return JS_ThrowTypeError(ctx, "Worker has no _events (EventEmitter not available)");
    }
    JSValue onMethod = JS_GetPropertyStr(ctx, data->workerEventsObj, "on");
    if (!JS_IsFunction(ctx, onMethod)) {
        JS_FreeValue(ctx, onMethod);
        return JS_ThrowTypeError(ctx, "Worker._events.on is not a function");
    }
    JSValue args[2] = { JS_DupValue(ctx, argv[0]), JS_DupValue(ctx, argv[1]) };
    JSValue result = JS_Call(ctx, onMethod, data->workerEventsObj, 2, args);
    JS_FreeValue(ctx, args[0]);
    JS_FreeValue(ctx, args[1]);
    JS_FreeValue(ctx, onMethod);
    return result;
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
