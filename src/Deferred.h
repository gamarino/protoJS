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
        JSValue func;                    // Function from main context (for reference, not used in worker)
        std::string funcCode;            // Serialized function code for worker thread
        JSValue resolve;
        JSValue reject;
        JSContext* mainJSContext;  // Main thread context (for callbacks)
        JSRuntime* rt;              // Shared runtime
        proto::ProtoSpace* space;
        JSContextWrapper* wrapper;
        
        DeferredTask(JSContext* ctx, JSValue f, const std::string& code, JSValue res, JSValue rej, 
                     JSRuntime* runtime, proto::ProtoSpace* s, JSContextWrapper* w)
            : func(f), funcCode(code), resolve(res), reject(rej), mainJSContext(ctx), 
              rt(runtime), space(s), wrapper(w) {}
    };
    
    static JSValue constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv);
    static void finalizer(JSRuntime* rt, JSValue val);
    
    /**
     * @brief Execute a Deferred task in main thread (Opción B).
     * 
     * Función JS se ejecuta en main thread, trabajo pesado se delega a protoCore.
     */
    static void executeTaskInMainThread(std::shared_ptr<DeferredTask> task);
    
    /**
     * @brief Execute heavy work in protoCore on worker thread.
     * 
     * Crea ProtoContext aislado y ejecuta trabajo en CPUThreadPool.
     */
    static void executeWorkInWorkerThread(std::shared_ptr<DeferredTask> task, 
                                         const proto::ProtoObject* workFunction);
    
    /**
     * @brief Helper to get JSContextWrapper from JSContext opaque data.
     */
    static JSContextWrapper* getWrapperFromContext(JSContext* ctx);
};

} // namespace protojs

#endif // PROTOJS_DEFERRED_H
