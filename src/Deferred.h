#ifndef PROTOJS_DEFERRED_H
#define PROTOJS_DEFERRED_H

#include "quickjs.h"
#include "headers/protoCore.h"
#include "CPUThreadPool.h"
#include "EventLoop.h"
#include <atomic>
#include <functional>
#include <memory>
#include <string>
#include <utility>

namespace protojs {

class JSContextWrapper;

/**
 * @brief Lightweight task that executes in a ProtoThread (protoCore multithreading only).
 *
 * Deferred tasks are scheduled via ProtoSpace::newThread (public API); each task runs in its own
 * ProtoThread. Roots are tracked by protoCore's GC. No native threads are used for execution.
 * If wrapper/space is unavailable or newThread fails, the Deferred is rejected (no CPUThreadPool fallback).
 */
class Deferred {
public:
    /** Lightweight task structure (public so TaskHandle can be used across modules). */
    struct DeferredTask {
        JSValue func = JS_UNDEFINED;
        uint8_t* serializedFunc = nullptr;
        size_t serializedFuncSize = 0;
        JSValue resolve;
        JSValue reject;
        JSContext* mainJSContext = nullptr;
        JSRuntime* rt = nullptr;
        proto::ProtoSpace* space = nullptr;
        JSContextWrapper* wrapper = nullptr;
        JSValue result = JS_UNDEFINED;
        JSValue error = JS_UNDEFINED;
        bool isResolved = false;
        bool isRejected = false;
        JSValue thenCallback = JS_UNDEFINED;
        JSValue catchCallback = JS_UNDEFINED;
        JSValue deferredObj = JS_UNDEFINED;
        std::string functionSource;
        uint8_t* serializedResult = nullptr;
        size_t serializedResultSize = 0;
        bool hasError = false;

        DeferredTask(JSContext* ctx, JSValue f, uint8_t* serialized, size_t serializedSize,
                     JSValue res, JSValue rej, JSRuntime* runtime, proto::ProtoSpace* s, JSContextWrapper* w,
                     JSValue deferred_obj, std::string fn_source);
        ~DeferredTask();
    };

    static void init(JSContext* ctx, JSContextWrapper* wrapper);
    static int getActiveDeferredCount();
    using TaskHandle = std::shared_ptr<DeferredTask>;
    static std::pair<JSValue, TaskHandle> createPending(JSContext* ctx, JSContextWrapper* wrapper);
    static void resolveTaskFromNative(TaskHandle task, JSValue resultValue);
    static void incrementActiveCount();
    /** Runs the task in the current thread using JSContextWrapper (protoCore path). Used by ProtoThread entry. */
    static void runDeferredTaskInProtoThread(std::shared_ptr<DeferredTask> task);

private:
    static JSValue constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv);
    static void finalizer(JSRuntime* rt, JSValue val);
    static JSValue then(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue catch_(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    
    /**
     * @brief Execute a Deferred task in worker thread using bytecode transfer.
     * 
     * Serializes the function, executes it in a worker thread, and handles result round-trip.
     */
    static void executeTaskInWorkerThread(std::shared_ptr<DeferredTask> task);
    
    /**
     * @brief Worker thread execution function.
     * 
     * Deserializes function, executes it, and serializes result.
     */
    static void workerThreadExecution(std::shared_ptr<DeferredTask> task);
    
    /**
     * @brief Helper to get JSContextWrapper from JSContext opaque data.
     */
    static JSContextWrapper* getWrapperFromContext(JSContext* ctx);
};

} // namespace protojs

#endif // PROTOJS_DEFERRED_H
