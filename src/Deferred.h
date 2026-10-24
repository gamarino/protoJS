#ifndef PROTOJS_DEFERRED_H
#define PROTOJS_DEFERRED_H

#include "quickjs.h"
#include "protoCore.h"
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
 * @brief The `Deferred` of a QuickJS-run ES module (`--input-type=module`).
 *
 * A script runs on protoCore's interpreter and gets ProtoDeferred
 * (src/ProtoDeferred.h). An ES module still runs on QuickJS, whose heap is not
 * protoCore's; its `Deferred` compiles the function's source into a protoCore
 * function on the calling thread and runs it on the wrapper's DeferredPool, in
 * the wrapper's own space, like any other Deferred (one ProtoSpace per
 * process). The outcome is converted into the QuickJS heap once, on the owner
 * thread, where the `then` / `catch` callbacks run.
 */
class Deferred {
public:
    /** Lightweight task structure (public so TaskHandle can be used across modules). */
    struct DeferredTask {
        JSValue func = JS_UNDEFINED;
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

        DeferredTask(JSContext* ctx, JSValue f,
                     JSValue res, JSValue rej, JSRuntime* runtime, proto::ProtoSpace* s, JSContextWrapper* w,
                     JSValue deferred_obj, std::string fn_source);
    };

    static void init(JSContext* ctx, JSContextWrapper* wrapper);
    static int getActiveDeferredCount();
    using TaskHandle = std::shared_ptr<DeferredTask>;
    static std::pair<JSValue, TaskHandle> createPending(JSContext* ctx, JSContextWrapper* wrapper);
    static void resolveTaskFromNative(TaskHandle task, JSValue resultValue);
    static void incrementActiveCount();

private:
    static JSValue constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv);
    static void finalizer(JSRuntime* rt, JSValue val);
    static JSValue then(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue catch_(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    
    /**
     * @brief Run the task's function on the wrapper's DeferredPool, in the
     * wrapper's space, and settle the task on the owner thread.
     */
    static void executeOnPool(std::shared_ptr<DeferredTask> task);

    /** @brief Settle `task` on the owner thread: reject with `message`. */
    static void rejectWithMessage(std::shared_ptr<DeferredTask> task, const std::string& message);
    
    /**
     * @brief Helper to get JSContextWrapper from JSContext opaque data.
     */
    static JSContextWrapper* getWrapperFromContext(JSContext* ctx);
};

} // namespace protojs

#endif // PROTOJS_DEFERRED_H
