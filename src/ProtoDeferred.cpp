#include "ProtoDeferred.h"
#include "EventLoop.h"
#include "FunctionPrototype.h"
#include "JSContext.h"
#include "JSSymbols.h"
#include "runtime/ProtoInterpreter.h"
#include "runtime/ProtoBytecodeModule.h"
#include <atomic>
#include <iostream>
#include <string>

namespace protojs {

namespace {

// State codes stored as SmallInteger in __df_state__.
constexpr long long kStatePending   = 0;
constexpr long long kStateFulfilled = 1;
constexpr long long kStateRejected  = 2;

// Cached interned-symbol keys for the Deferred internal attributes.
// Symbols compare by pointer; thread_local so the cache never crosses
// threads.  Lazy-init on first use.
struct DeferredKeys {
    const proto::ProtoString* state    = nullptr;
    const proto::ProtoString* value    = nullptr;
    const proto::ProtoString* thenList = nullptr;
    const proto::ProtoString* catchList= nullptr;
    const proto::ProtoString* prototype= nullptr;
};

DeferredKeys& keys(proto::ProtoContext* ctx) {
    static thread_local DeferredKeys k;
    if (!k.state) {
        k.state     = proto::ProtoString::createSymbol(ctx, "__df_state__");
        k.value     = proto::ProtoString::createSymbol(ctx, "__df_value__");
        k.thenList  = proto::ProtoString::createSymbol(ctx, "__df_then__");
        k.catchList = proto::ProtoString::createSymbol(ctx, "__df_catch__");
        k.prototype = proto::ProtoString::createSymbol(ctx, "__df_proto__");
    }
    return k;
}

// A value is callable when it is a raw ProtoMethod, a wrapped native
// function, a bytecode closure or a bound function.  Same test the
// interpreter applies to Proxy traps (ProtoInterpreter.cpp).
bool isCallableValue(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    if (!ctx || !v || v == PROTO_NONE) return false;
    if (v->isMethod(ctx)) return true;
    const proto::ProtoString* bcK = JSSymbols::bytecodeId(ctx);
    if (bcK && v->hasAttribute(ctx, bcK) == PROTO_TRUE) return true;
    const proto::ProtoString* nfK = JSSymbols::nativeFn(ctx);
    if (nfK && v->hasAttribute(ctx, nfK) == PROTO_TRUE) return true;
    const proto::ProtoString* bfK = JSSymbols::boundFn(ctx);
    if (bfK && v->hasAttribute(ctx, bfK) == PROTO_TRUE) return true;
    return false;
}

// Consume and report an exception left behind by a user callback.
//
// callJSFunction reports a throw by setting a thread-local flag and returning
// PROTO_NONE.  Nothing in the event loop consumes that flag, so leaving it set
// made the next native call on this thread believe that IT had thrown.  Every
// site that invokes a user callback must drain it.
void drainCallbackException(proto::ProtoContext* ctx, const char* where) {
    if (!hasCallException()) return;
    const proto::ProtoObject* exc = consumeCallException();
    std::string errStr;
    if (ctx && exc && exc != PROTO_NONE) {
        const proto::ProtoString* nameKey = JSSymbols::name(ctx);
        if (nameKey) {
            const proto::ProtoObject* nv = exc->getAttribute(ctx, nameKey, true);
            if (nv && nv != PROTO_NONE && nv->isString(ctx))
                nv->asString(ctx)->toUTF8String(ctx, errStr);
        }
        const proto::ProtoString* msgKey = JSSymbols::message(ctx);
        if (msgKey) {
            const proto::ProtoObject* mv = exc->getAttribute(ctx, msgKey, true);
            if (mv && mv != PROTO_NONE && mv->isString(ctx)) {
                std::string tmp;
                mv->asString(ctx)->toUTF8String(ctx, tmp);
                if (!tmp.empty()) {
                    if (!errStr.empty()) errStr += ": ";
                    errStr += tmp;
                }
            }
        }
        if (errStr.empty() && exc->isString(ctx))
            exc->asString(ctx)->toUTF8String(ctx, errStr);
    }
    if (errStr.empty()) errStr = "Error";
    std::cerr << "Uncaught exception in " << where << ": " << errStr << std::endl;
}

// Active-count for the event-loop drain.  Atomic because resolveFromAsync
// runs on the main thread but the worker thread may have already
// decremented when scheduling its own resolve callback.
std::atomic<int> g_activeCount{0};

// ---- GC-rooted async invocation pinning ----------------------------------
//
// When drainQueue enqueues an EventLoop callback, it captures `cb` and
// `val` (raw ProtoObject*) into a C++ lambda.  Those captures are
// invisible to protoCore's tracing GC: a GC cycle that runs between the
// enqueue and the lambda's execution would reclaim cb and/or val once
// the JS object graph stops referencing them.
//
// The fix: pin both objects in the wrapper's protoCore root set
// (proto::ProtoRootSet) before enqueueing.  The lambda captures the
// two opaque handles, resolves+removes them on entry, and dispatches.
// No setAttribute on the global, no contention with other writers,
// no name-collision risk between embedders.
struct PinnedInvocation {
    proto::ProtoRootSet::Handle cb;
    proto::ProtoRootSet::Handle val;
};

PinnedInvocation pinInvocation(JSContextWrapper* wrapper,
                                const proto::ProtoObject* cb,
                                const proto::ProtoObject* val) {
    PinnedInvocation p{0, 0};
    if (!wrapper) return p;
    auto* rs = wrapper->getRootSet();
    if (!rs) return p;
    p.cb  = rs->add(cb ? cb : PROTO_NONE);
    p.val = rs->add(val ? val : PROTO_NONE);
    return p;
}

void takeInvocation(JSContextWrapper* wrapper,
                     const PinnedInvocation& p,
                     const proto::ProtoObject*& outCb,
                     const proto::ProtoObject*& outVal) {
    outCb = nullptr;
    outVal = nullptr;
    if (!wrapper) return;
    auto* rs = wrapper->getRootSet();
    if (!rs) return;
    outCb  = rs->resolve(p.cb);
    outVal = rs->resolve(p.val);
    rs->remove(p.cb);
    rs->remove(p.val);
}

// Forward decl.
const proto::ProtoObject* deferredPrototypeObject(
    proto::ProtoContext* ctx);

// Read the integer state field.  Defaults to pending if absent.
long long readState(proto::ProtoContext* ctx, const proto::ProtoObject* d) {
    auto& k = keys(ctx);
    const proto::ProtoObject* s = d->getAttribute(ctx, k.state, false);
    if (s && s->isInteger(ctx)) return s->asLong(ctx);
    return kStatePending;
}

void writeState(proto::ProtoContext* ctx, const proto::ProtoObject* d,
                 long long state) {
    auto& k = keys(ctx);
    d->setAttribute(ctx, k.state, ctx->fromInteger(state));
}

void writeValue(proto::ProtoContext* ctx, const proto::ProtoObject* d,
                 const proto::ProtoObject* value) {
    auto& k = keys(ctx);
    d->setAttribute(ctx, k.value, value ? value : PROTO_NONE);
}

// Read / write the callback queues.  Lists are empty by default.
const proto::ProtoList* readQueue(proto::ProtoContext* ctx,
                                    const proto::ProtoObject* d,
                                    const proto::ProtoString* key) {
    const proto::ProtoObject* attr = d->getAttribute(ctx, key, false);
    if (!attr || attr == PROTO_NONE) return nullptr;
    return attr->asList(ctx);
}

void writeQueue(proto::ProtoContext* ctx,
                 const proto::ProtoObject* d,
                 const proto::ProtoString* key,
                 const proto::ProtoList* list) {
    if (!list) return;
    d->setAttribute(ctx, key, list->asObject(ctx));
}

// Drain queue: schedule each callback on the event loop with `value` as
// its single argument.  After the queue is drained, decrements the
// active count once.
void drainQueue(proto::ProtoContext* ctx,
                 const proto::ProtoObject* d,
                 const proto::ProtoString* qKey,
                 const proto::ProtoObject* value,
                 JSContextWrapper* wrapper) {
    if (!ctx || !d) return;
    const proto::ProtoList* queue = readQueue(ctx, d, qKey);
    int callCount = queue ? static_cast<int>(queue->getSize(ctx)) : 0;
    if (queue) {
        for (int i = 0; i < callCount; ++i) {
            const proto::ProtoObject* cb = queue->getAt(ctx, i);
            if (!cb || cb == PROTO_NONE) continue;
            const proto::ProtoObject* val = value ? value : PROTO_NONE;
            // Pin (cb, val) in the wrapper's root set — see the
            // pinInvocation helper for the reachability bug this fixes.
            PinnedInvocation pin = pinInvocation(wrapper, cb, val);
            EventLoop::getInstance().enqueueCallback([wrapper, pin]() {
                if (!wrapper) return;
                JSContextWrapper::CurrentScope wscope(wrapper);
                proto::ProtoContext* c = wrapper->getProtoContext();
                if (!c) return;
                const proto::ProtoObject* cbR = nullptr;
                const proto::ProtoObject* valR = nullptr;
                takeInvocation(wrapper, pin, cbR, valR);
                if (!cbR || cbR == PROTO_NONE) return;
                const ProtoBytecodeModule* mod =
                    static_cast<const ProtoBytecodeModule*>(wrapper->getRootModule());
                const proto::ProtoList* args = c->newList()
                    ->appendLast(c, valR ? valR : PROTO_NONE);
                callJSFunctionFromAsync(c, cbR, PROTO_NONE, args, mod,
                                        wrapper->getNativeGlobalRootPtr());
                drainCallbackException(c, "Deferred callback");
            });
        }
        // Clear queue so subsequent state transitions don't double-fire.
        writeQueue(ctx, d, qKey, ctx->newList());
    }
    // One async-completion event per Deferred regardless of how many
    // callbacks were chained.
    g_activeCount.fetch_sub(1, std::memory_order_acq_rel);
}

// Append a callback to one of the pending queues.
void enqueueCallbackFor(proto::ProtoContext* ctx,
                         const proto::ProtoObject* d,
                         const proto::ProtoString* qKey,
                         const proto::ProtoObject* cb) {
    const proto::ProtoList* queue = readQueue(ctx, d, qKey);
    if (!queue) queue = ctx->newList();
    queue = queue->appendLast(ctx, cb);
    writeQueue(ctx, d, qKey, queue);
}

// Schedule `cb` with the already-settled value on a later event-loop turn.
// Shared by then() and catch() for a Deferred that has already settled.
void scheduleSettledCallback(proto::ProtoContext* ctx,
                              const proto::ProtoObject* d,
                              const proto::ProtoObject* cb) {
    auto& k = keys(ctx);
    const proto::ProtoObject* value = d->getAttribute(ctx, k.value, false);
    if (!value) value = PROTO_NONE;
    JSContextWrapper* wrapper = JSContextWrapper::current();
    g_activeCount.fetch_add(1, std::memory_order_acq_rel);
    PinnedInvocation pin = pinInvocation(wrapper, cb, value);
    EventLoop::getInstance().enqueueCallback([wrapper, pin]() {
        if (!wrapper) {
            g_activeCount.fetch_sub(1, std::memory_order_acq_rel);
            return;
        }
        JSContextWrapper::CurrentScope wscope(wrapper);
        proto::ProtoContext* c = wrapper->getProtoContext();
        if (!c) {
            g_activeCount.fetch_sub(1, std::memory_order_acq_rel);
            return;
        }
        const proto::ProtoObject* cbR = nullptr;
        const proto::ProtoObject* valR = nullptr;
        takeInvocation(wrapper, pin, cbR, valR);
        if (cbR && cbR != PROTO_NONE) {
            const ProtoBytecodeModule* mod =
                static_cast<const ProtoBytecodeModule*>(wrapper->getRootModule());
            const proto::ProtoList* args = c->newList()
                ->appendLast(c, valR ? valR : PROTO_NONE);
            callJSFunctionFromAsync(c, cbR, PROTO_NONE, args, mod,
                                    wrapper->getNativeGlobalRootPtr());
            drainCallbackException(c, "Deferred callback");
        }
        g_activeCount.fetch_sub(1, std::memory_order_acq_rel);
    });
}

// ---- Constructor -----------------------------------------------------
// new Deferred(workerFn) — workerFn is a callable invoked on the event
// loop.  Its return value fulfils; an exception rejects.

const proto::ProtoObject* deferredConstruct(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    if (!ctx) return PROTO_NONE;
    const proto::ProtoObject* workerFn =
        (args && args->getSize(ctx) > 0) ? args->getAt(ctx, 0) : PROTO_NONE;

    // Reject a missing or non-callable worker synchronously.  Before this
    // check, `Deferred()` built a pending Deferred that nothing could ever
    // settle, so the process sat in the event-loop drain until its 180 s
    // timeout before exiting.
    if (!isCallableValue(ctx, workerFn)) {
        signalNativeException(makeNativeError(
            ctx, "TypeError", "Deferred requires a function argument"));
        return PROTO_NONE;
    }

    const proto::ProtoObject* inst = ProtoDeferred::createPending(ctx);
    if (!inst) return PROTO_NONE;

    {
        JSContextWrapper* wrapper = JSContextWrapper::current();
        // createPending already incremented the active counter; the
        // worker invocation runs as part of that pending resolution
        // and resolveFromAsync's drainQueue does the matching
        // decrement.  Pin (workerFn, inst) in the wrapper's root set
        // so they survive any GC cycle that runs before the lambda
        // fires.
        PinnedInvocation pin = pinInvocation(wrapper, workerFn, inst);
        EventLoop::getInstance().enqueueCallback([wrapper, pin]() {
            if (!wrapper) return;
            JSContextWrapper::CurrentScope wscope(wrapper);
            proto::ProtoContext* c = wrapper->getProtoContext();
            if (!c) return;
            const proto::ProtoObject* workerFnR = nullptr;
            const proto::ProtoObject* instR = nullptr;
            takeInvocation(wrapper, pin, workerFnR, instR);
            if (!workerFnR || workerFnR == PROTO_NONE) return;
            const ProtoBytecodeModule* mod =
                static_cast<const ProtoBytecodeModule*>(wrapper->getRootModule());
            const proto::ProtoObject* result =
                callJSFunctionFromAsync(c, workerFnR, PROTO_NONE,
                                         c->newList(), mod,
                                         wrapper->getNativeGlobalRootPtr());
            // An exception thrown by the worker must reject.  It used to
            // fulfil the Deferred with undefined: callJSFunction reports a
            // throw through the thread-local flag and returns PROTO_NONE,
            // and the result was passed to resolveFromAsync unconditionally.
            if (hasCallException()) {
                const proto::ProtoObject* reason = consumeCallException();
                ProtoDeferred::rejectFromAsync(c, instR,
                    reason ? reason : PROTO_NONE, wrapper);
            } else {
                ProtoDeferred::resolveFromAsync(c, instR, result, wrapper);
            }
        });
    }

    return inst;
}

// ---- then / catch ----------------------------------------------------

const proto::ProtoObject* deferredThen(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    if (!ctx || !self || self == PROTO_NONE) return self;
    // Promises/A+ 2.2: then(onFulfilled, onRejected).  The second argument
    // used to be ignored entirely, so a rejection registered this way was
    // never delivered.
    const proto::ProtoObject* onFulfilled =
        (args && args->getSize(ctx) > 0) ? args->getAt(ctx, 0) : PROTO_NONE;
    const proto::ProtoObject* onRejected =
        (args && args->getSize(ctx) > 1) ? args->getAt(ctx, 1) : PROTO_NONE;
    const bool haveFulfil = isCallableValue(ctx, onFulfilled);
    const bool haveReject = isCallableValue(ctx, onRejected);
    // Note: a missing onFulfilled no longer returns early, because
    // then(undefined, onRejected) must still register the rejection handler.
    if (!haveFulfil && !haveReject) return self;
    auto& k = keys(ctx);

    long long state = readState(ctx, self);
    if (state == kStateFulfilled) {
        if (haveFulfil) scheduleSettledCallback(ctx, self, onFulfilled);
    } else if (state == kStateRejected) {
        if (haveReject) scheduleSettledCallback(ctx, self, onRejected);
    } else {
        if (haveFulfil) enqueueCallbackFor(ctx, self, k.thenList, onFulfilled);
        if (haveReject) enqueueCallbackFor(ctx, self, k.catchList, onRejected);
    }
    return self;
}

const proto::ProtoObject* deferredCatch(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    if (!ctx || !self || self == PROTO_NONE) return self;
    const proto::ProtoObject* cb =
        (args && args->getSize(ctx) > 0) ? args->getAt(ctx, 0) : PROTO_NONE;
    if (!isCallableValue(ctx, cb)) return self;
    auto& k = keys(ctx);

    long long state = readState(ctx, self);
    if (state == kStateRejected) {
        scheduleSettledCallback(ctx, self, cb);
    } else if (state == kStatePending) {
        enqueueCallbackFor(ctx, self, k.catchList, cb);
    }
    return self;
}

// Build the prototype object that all Deferred instances inherit from.
// Cached per-thread because we only need one canonical prototype.
const proto::ProtoObject* deferredPrototypeObject(proto::ProtoContext* ctx) {
    static thread_local const proto::ProtoObject* proto = nullptr;
    if (proto) return proto;
    const proto::ProtoObject* p = ctx->newObject(/*mutable=*/true);
    if (!p) return nullptr;
    auto installMethod = [&](const char* name, proto::ProtoMethod fn) {
        const proto::ProtoString* k = ctx->fromUTF8String(name)
            ? ctx->fromUTF8String(name)->asString(ctx) : nullptr;
        if (!k) return;
        const proto::ProtoObject* m = ctx->fromMethod(nullptr, fn);
        if (!m) return;
        p->setAttribute(ctx, k, m);
    };
    installMethod("then",  deferredThen);
    installMethod("catch", deferredCatch);
    proto = p;
    return p;
}

}  // namespace

const proto::ProtoObject* ProtoDeferred::createPending(proto::ProtoContext* ctx) {
    if (!ctx) return nullptr;
    const proto::ProtoObject* p = deferredPrototypeObject(ctx);
    if (!p) return nullptr;
    const proto::ProtoObject* inst = p->newChild(ctx, /*mutable=*/true);
    if (!inst) return nullptr;
    auto& k = keys(ctx);
    inst->setAttribute(ctx, k.state,    ctx->fromInteger(kStatePending));
    inst->setAttribute(ctx, k.value,    PROTO_NONE);
    inst->setAttribute(ctx, k.thenList, ctx->newList()->asObject(ctx));
    inst->setAttribute(ctx, k.catchList,ctx->newList()->asObject(ctx));
    // Account for the pending resolution.  Decremented once when the
    // Deferred is settled (resolveFromAsync / rejectFromAsync drain).
    g_activeCount.fetch_add(1, std::memory_order_acq_rel);
    return inst;
}

void ProtoDeferred::resolveFromAsync(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* deferred,
    const proto::ProtoObject* value,
    JSContextWrapper* wrapper) {
    if (!ctx || !deferred) return;
    long long s = readState(ctx, deferred);
    if (s != kStatePending) return;  // already settled
    auto& k = keys(ctx);
    writeState(ctx, deferred, kStateFulfilled);
    writeValue(ctx, deferred, value);
    drainQueue(ctx, deferred, k.thenList, value, wrapper);
}

void ProtoDeferred::rejectFromAsync(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* deferred,
    const proto::ProtoObject* reason,
    JSContextWrapper* wrapper) {
    if (!ctx || !deferred) return;
    long long s = readState(ctx, deferred);
    if (s != kStatePending) return;
    auto& k = keys(ctx);
    writeState(ctx, deferred, kStateRejected);
    writeValue(ctx, deferred, reason);
    drainQueue(ctx, deferred, k.catchList, reason, wrapper);
}

int ProtoDeferred::getActiveCount() {
    return g_activeCount.load(std::memory_order_acquire);
}

const proto::ProtoObject* ProtoDeferred::init(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* globalObj) {
    if (!ctx || !globalObj) return globalObj;
    // Eager-build the prototype on this thread so the cache fills.
    const proto::ProtoObject* dproto = deferredPrototypeObject(ctx);
    if (!dproto) return globalObj;

    // `new Deferred(fn)` needs a constructor object, not a bare ProtoMethod:
    // L_OP_call_constructor rejects a raw method with "function is not a
    // constructor", so `new Deferred(...)` failed outright even though the
    // documentation and the tests use it.  Same shape as EventsModule's
    // EventEmitter: a wrapNativeFunction wrapper carrying `prototype` and
    // `__construct__`.  Plain `Deferred(fn)` still works through
    // `__native_fn__`.
    const proto::ProtoObject* ctor =
        wrapNativeFunction(ctx, deferredConstruct, "Deferred",
                            /*length=*/1, /*globalRoot=*/nullptr);
    if (!ctor) return globalObj;

    // `prototype` makes instances of `new Deferred(...)` inherit then/catch
    // and makes `instanceof Deferred` hold: createPending parents every
    // instance on this same object.
    const proto::ProtoString* protoKey = JSSymbols::prototype(ctx);
    if (protoKey) ctor = ctor->setAttribute(ctx, protoKey, dproto);
    const proto::ProtoString* constructKey = JSSymbols::construct(ctx);
    if (constructKey) {
        const proto::ProtoObject* cm = ctx->fromMethod(nullptr, deferredConstruct);
        if (cm) ctor = ctor->setAttribute(ctx, constructKey, cm);
    }

    const proto::ProtoString* name = ctx->fromUTF8String("Deferred")
        ? ctx->fromUTF8String("Deferred")->asString(ctx) : nullptr;
    if (!name) return globalObj;
    return globalObj->setAttribute(ctx, name, ctor);
}

}  // namespace protojs
