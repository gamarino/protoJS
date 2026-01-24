#ifndef PROTOJS_DEFERRED_H
#define PROTOJS_DEFERRED_H

#include "quickjs.h"
#include "headers/protoCore.h"
#include "CPUThreadPool.h"
#include "EventLoop.h"
#include <functional>
#include <memory>

namespace protojs {

class JSContextWrapper;

/**
 * @brief Lightweight task that executes in CPU thread pool.
 * 
 * Similar to Java virtual threads - multiple Deferred tasks can run
 * on the same OS thread from the CPU pool.
 */
class Deferred {
public:
    static void init(JSContext* ctx, JSContextWrapper* wrapper);

private:
    // Lightweight task structure (not a full ProtoThread)
    struct DeferredTask {
        JSValue func;                    // Function from main context (for reference)
        uint8_t* serializedFunc;        // Serialized function bytecode buffer
        size_t serializedFuncSize;      // Size of serialized buffer
        JSValue resolve;
        JSValue reject;
        JSContext* mainJSContext;  // Main thread context (for callbacks)
        JSRuntime* rt;              // Shared runtime
        proto::ProtoSpace* space;
        JSContextWrapper* wrapper;
        
        // Promise state
        JSValue result = JS_UNDEFINED;
        JSValue error = JS_UNDEFINED;
        bool isResolved = false;
        bool isRejected = false;
        JSValue thenCallback = JS_UNDEFINED;
        JSValue catchCallback = JS_UNDEFINED;
        
        // Result from worker thread
        uint8_t* serializedResult = nullptr;  // Serialized result buffer
        size_t serializedResultSize = 0;     // Size of result buffer
        bool hasError = false;                // Whether execution resulted in error
        
        DeferredTask(JSContext* ctx, JSValue f, uint8_t* serialized, size_t serializedSize,
                     JSValue res, JSValue rej, JSRuntime* runtime, proto::ProtoSpace* s, JSContextWrapper* w)
            : func(f), serializedFunc(serialized), serializedFuncSize(serializedSize),
              resolve(res), reject(rej), mainJSContext(ctx), 
              rt(runtime), space(s), wrapper(w) {}
        
        ~DeferredTask() {
            // Free serialized buffers (allocated with js_malloc_rt)
            if (serializedFunc) {
                js_free_rt(rt, serializedFunc);
            }
            if (serializedResult) {
                js_free_rt(rt, serializedResult);
            }
        }
    };
    
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
