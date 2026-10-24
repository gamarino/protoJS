#include "Deferred.h"
#include "TypeBridge.h"
#include "JSContext.h"
#include "CPUThreadPool.h"
#include "DeferredPool.h"
#include "ProtoDeferred.h"
#include "runtime/ThreadIdentity.h"
#include "runtime/ProtoBytecodeModule.h"
#include "EventLoop.h"
#include "runtime/ProtoInterpreter.h"
#include "protoCore.h"
#include <iostream>
#include <memory>
#include <cstring>
#include <cstdlib>
#include <string>

namespace protojs {

static JSClassID protojs_deferred_class_id;

// Store JSContextWrapper in JSContext opaque data
static const char* JS_CONTEXT_WRAPPER_KEY = "protojs_wrapper";

void Deferred::init(JSContext* ctx, JSContextWrapper* wrapper) {
    // Store wrapper in JSContext opaque
    JS_SetContextOpaque(ctx, wrapper);
    
    JS_NewClassID(&protojs_deferred_class_id);
    JSClassDef class_def = {
        "Deferred",
        finalizer
    };
    JS_NewClass(JS_GetRuntime(ctx), protojs_deferred_class_id, &class_def);

    JSValue proto = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, proto, "then", JS_NewCFunction(ctx, then, "then", 1));
    JS_SetPropertyStr(ctx, proto, "catch", JS_NewCFunction(ctx, catch_, "catch", 1));
    JS_SetClassProto(ctx, protojs_deferred_class_id, proto);

    JSValue ctor = JS_NewCFunction2(ctx, constructor, "Deferred", 1, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, ctor, proto);
    
    JSValue global_obj = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global_obj, "Deferred", ctor);
    JS_FreeValue(ctx, global_obj);
}

JSContextWrapper* Deferred::getWrapperFromContext(JSContext* ctx) {
    return static_cast<JSContextWrapper*>(JS_GetContextOpaque(ctx));
}

JSValue Deferred::constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv) {
    if (argc < 1 || !JS_IsFunction(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "Deferred expects a function");
    }

    JSValue obj = JS_NewObjectClass(ctx, protojs_deferred_class_id);
    if (JS_IsException(obj)) return obj;

    JSContextWrapper* wrapper = getWrapperFromContext(ctx);
    if (!wrapper) {
        JS_FreeValue(ctx, obj);
        return JS_ThrowTypeError(ctx, "Deferred: JSContextWrapper not found");
    }

    proto::ProtoSpace* space = wrapper->getProtoSpace();
    JSRuntime* rt = wrapper->getJSRuntime();

    // Get function source first (fast, no cross-runtime serialization). Worker prefers this.
    std::string functionSource;
    JSValue toStringVal = JS_GetPropertyStr(ctx, argv[0], "toString");
    if (JS_IsFunction(ctx, toStringVal)) {
        JSValue sourceVal = JS_Call(ctx, toStringVal, argv[0], 0, nullptr);
        JS_FreeValue(ctx, toStringVal);
        if (!JS_IsException(sourceVal)) {
            const char* src = JS_ToCString(ctx, sourceVal);
            if (src) {
                functionSource.assign(src);
                JS_FreeCString(ctx, src);
            }
            JS_FreeValue(ctx, sourceVal);
        }
    }

    // Resolve/reject callbacks for event-loop completion
    JSValue resolve = JS_NewCFunction(ctx, [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
        return JS_UNDEFINED;
    }, "resolve", 1);
    JSValue reject = JS_NewCFunction(ctx, [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
        return JS_UNDEFINED;
    }, "reject", 1);

    if (functionSource.empty()) {
        JS_FreeValue(ctx, obj);
        JS_FreeValue(ctx, resolve);
        JS_FreeValue(ctx, reject);
        return JS_ThrowTypeError(ctx, "Deferred: the function has no source text to run.");
    }

    auto task = std::make_shared<DeferredTask>(
        ctx,
        JS_DupValue(ctx, argv[0]),
        resolve,
        reject,
        rt,
        space,
        wrapper,
        JS_DupValue(ctx, obj),      // Keep Deferred alive until completion (keeps then/catch callbacks valid)
        std::move(functionSource)
    );
    
    // Store same shared_ptr in opaque so then/catch see the same task the worker completes
    struct OpaqueHolder { std::shared_ptr<DeferredTask> task; };
    OpaqueHolder* holder = new OpaqueHolder{ task };
    JS_SetOpaque(obj, holder);

    executeOnPool(task);

    return obj;
}

void Deferred::finalizer(JSRuntime* rt, JSValue val) {
    struct OpaqueHolder { std::shared_ptr<DeferredTask> task; };
    OpaqueHolder* holder = static_cast<OpaqueHolder*>(JS_GetOpaque(val, protojs_deferred_class_id));
    if (holder) {
        std::shared_ptr<DeferredTask> task = holder->task;
        delete holder;
        JS_SetOpaque(val, nullptr);
        if (task) {
            JS_FreeValueRT(rt, task->func);
            JS_FreeValueRT(rt, task->resolve);
            JS_FreeValueRT(rt, task->reject);
            JS_FreeValueRT(rt, task->result);
            JS_FreeValueRT(rt, task->error);
            JS_FreeValueRT(rt, task->thenCallback);
            JS_FreeValueRT(rt, task->catchCallback);
            JS_FreeValueRT(rt, task->deferredObj);
        }
    }
}

JSValue Deferred::then(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1 || !JS_IsFunction(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "then expects a function");
    }
    
    struct OpaqueHolder { std::shared_ptr<DeferredTask> task; };
    OpaqueHolder* holder = static_cast<OpaqueHolder*>(JS_GetOpaque(this_val, protojs_deferred_class_id));
    if (!holder || !holder->task) {
        return JS_ThrowTypeError(ctx, "Invalid Deferred object");
    }
    DeferredTask* task = holder->task.get();
    task->thenCallback = JS_DupValue(ctx, argv[0]);

    if (task->isResolved && !JS_IsUndefined(task->result)) {
        JSValue thenArgs[] = { task->result };
        JSValue thenResult = JS_Call(ctx, task->thenCallback, JS_UNDEFINED, 1, thenArgs);
        JS_FreeValue(ctx, thenResult);
    }
    
    return JS_DupValue(ctx, this_val); // Return this for chaining
}

JSValue Deferred::catch_(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1 || !JS_IsFunction(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "catch expects a function");
    }
    
    struct OpaqueHolder { std::shared_ptr<DeferredTask> task; };
    OpaqueHolder* holder = static_cast<OpaqueHolder*>(JS_GetOpaque(this_val, protojs_deferred_class_id));
    if (!holder || !holder->task) {
        return JS_ThrowTypeError(ctx, "Invalid Deferred object");
    }
    DeferredTask* task = holder->task.get();
    task->catchCallback = JS_DupValue(ctx, argv[0]);

    if (task->isRejected && !JS_IsUndefined(task->error)) {
        JSValue catchArgs[] = { task->error };
        JSValue catchResult = JS_Call(ctx, task->catchCallback, JS_UNDEFINED, 1, catchArgs);
        JS_FreeValue(ctx, catchResult);
    }
    
    return JS_DupValue(ctx, this_val); // Return this for chaining
}

static std::atomic<int> active_deferred_count{0};

Deferred::DeferredTask::DeferredTask(JSContext* ctx, JSValue f,
                                     JSValue res, JSValue rej, JSRuntime* runtime, proto::ProtoSpace* s, JSContextWrapper* w,
                                     JSValue deferred_obj, std::string fn_source)
    : func(f), resolve(res), reject(rej), mainJSContext(ctx), rt(runtime), space(s), wrapper(w),
      deferredObj(deferred_obj), functionSource(std::move(fn_source)) {}

int Deferred::getActiveDeferredCount() {
    return active_deferred_count.load();
}

std::pair<JSValue, Deferred::TaskHandle> Deferred::createPending(JSContext* ctx, JSContextWrapper* wrapper) {
    proto::ProtoSpace* space = wrapper->getProtoSpace();
    JSRuntime* rt = wrapper->getJSRuntime();
    JSValue resolve = JS_NewCFunction(ctx, [](JSContext*, JSValueConst, int, JSValueConst*) { return JS_UNDEFINED; }, "resolve", 1);
    JSValue reject = JS_NewCFunction(ctx, [](JSContext*, JSValueConst, int, JSValueConst*) { return JS_UNDEFINED; }, "reject", 1);
    JSValue obj = JS_NewObjectClass(ctx, protojs_deferred_class_id);
    if (JS_IsException(obj)) return { obj, nullptr };
    auto task = std::make_shared<DeferredTask>(
        ctx, JS_UNDEFINED, resolve, reject, rt, space, wrapper,
        JS_DupValue(ctx, obj), std::string());
    struct OpaqueHolder { std::shared_ptr<DeferredTask> task; };
    OpaqueHolder* holder = new OpaqueHolder{ task };
    JS_SetOpaque(obj, holder);
    return std::make_pair(obj, task);
}

void Deferred::incrementActiveCount() {
    active_deferred_count++;
}

void Deferred::resolveTaskFromNative(TaskHandle task, JSValue resultValue) {
    active_deferred_count--;
    JSContext* ctx = task->mainJSContext;
    task->isResolved = true;
    task->result = JS_DupValue(ctx, resultValue);
    if (!JS_IsUndefined(task->thenCallback)) {
        JSValue thenArgs[] = { task->result };
        JSValue r = JS_Call(ctx, task->thenCallback, JS_UNDEFINED, 1, thenArgs);
        JS_FreeValue(ctx, r);
    }
    if (!JS_IsUndefined(task->deferredObj)) {
        JS_FreeValue(ctx, task->deferredObj);
        task->deferredObj = JS_UNDEFINED;
    }
}

void Deferred::rejectWithMessage(std::shared_ptr<DeferredTask> task, const std::string& message) {
    EventLoop::getInstance().enqueueCallback([task, message]() {
        active_deferred_count--;
        JSContext* ctx = task->mainJSContext;
        task->isRejected = true;
        task->error = JS_NewString(ctx, message.c_str());
        if (!JS_IsUndefined(task->catchCallback)) {
            JSValue catchArgs[] = { task->error };
            JSValue r = JS_Call(ctx, task->catchCallback, JS_UNDEFINED, 1, catchArgs);
            JS_FreeValue(ctx, r);
        }
        if (!JS_IsUndefined(task->deferredObj)) { JS_FreeValue(ctx, task->deferredObj); task->deferredObj = JS_UNDEFINED; }
    });
}

void Deferred::executeOnPool(std::shared_ptr<DeferredTask> task) {
    active_deferred_count++;
    JSContextWrapper* wrapper = task->wrapper;
    proto::ProtoContext* pctx = wrapper ? wrapper->getProtoContext() : nullptr;
    proto::ProtoRootSet* rs = wrapper ? wrapper->getRootSet() : nullptr;
    if (!wrapper || !pctx || !rs) {
        rejectWithMessage(task, "Deferred: no ProtoSpace (threading only via protoCore)");
        return;
    }

    // The function, as a protoCore function of this wrapper's realm, compiled
    // here on the owner thread. It runs on a thread of the pool, in this
    // wrapper's space: nothing is serialized, and no other space exists.
    const proto::ProtoObject* fn = nullptr;
    try {
        fn = wrapper->evalIsolatedToProto("(" + task->functionSource + ")", "(deferred)");
    } catch (const proto::ProtoException&) {
        fn = nullptr;
    }
    if (!fn || fn == PROTO_NONE || !isCallableValue(pctx, fn)) {
        rejectWithMessage(task, "Deferred: the function could not be compiled for the pool");
        return;
    }

    DeferredPool::Task poolTask;
    poolTask.fn = rs->add(fn);
    poolTask.global = *wrapper->getNativeGlobalRootPtr();
    poolTask.module = static_cast<const ProtoBytecodeModule*>(wrapper->getRootModule());
    poolTask.identity = captureThreadIdentity(pctx);
    // Settled on the owner thread: the outcome enters the QuickJS heap once,
    // there, and the QuickJS callbacks run.
    poolTask.onSettled = [task](proto::ProtoContext* c, const proto::ProtoObject* value, bool threw) {
        active_deferred_count--;
        JSContext* ctx = task->mainJSContext;
        JSValue v = TypeBridge::toJS(ctx, value, c);
        if (threw) {
            task->isRejected = true;
            task->error = v;
            if (!JS_IsUndefined(task->catchCallback)) {
                JSValue catchArgs[] = { task->error };
                JSValue r = JS_Call(ctx, task->catchCallback, JS_UNDEFINED, 1, catchArgs);
                JS_FreeValue(ctx, r);
            }
        } else {
            task->isResolved = true;
            task->result = v;
            if (!JS_IsUndefined(task->thenCallback)) {
                JSValue thenArgs[] = { task->result };
                JSValue r = JS_Call(ctx, task->thenCallback, JS_UNDEFINED, 1, thenArgs);
                JS_FreeValue(ctx, r);
            }
        }
        if (!JS_IsUndefined(task->deferredObj)) { JS_FreeValue(ctx, task->deferredObj); task->deferredObj = JS_UNDEFINED; }
    };
    if (!wrapper->deferredPool().submit(pctx, poolTask)) {
        rs->remove(poolTask.fn);
        rejectWithMessage(task, "Deferred: no pool thread could be started");
    }
}

} // namespace protojs
