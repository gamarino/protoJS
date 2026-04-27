#include "ProtoDeferred.h"
#include "EventLoop.h"
#include "JSContext.h"
#include "JSSymbols.h"
#include "runtime/ProtoInterpreter.h"
#include "runtime/ProtoBytecodeModule.h"
#include <atomic>

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

// Active-count for the event-loop drain.  Atomic because resolveFromAsync
// runs on the main thread but the worker thread may have already
// decremented when scheduling its own resolve callback.
std::atomic<int> g_activeCount{0};

// ---- GC-rooted async invocation registry ---------------------------------
//
// When drainQueue enqueues an EventLoop callback, it captures `cb` and
// `val` (raw ProtoObject*) into a C++ lambda.  Those captures are
// invisible to protoCore's tracing GC: a GC cycle that runs between the
// enqueue and the lambda's execution will reclaim cb and/or val if
// they're no longer reachable through the JS object graph.  In a
// staggered setImmediate scenario (each deferred is built, .then-ed,
// then immediately dropped from JS scope), this happens often enough
// to corrupt observable behaviour — e.g. `completed` reading back as a
// stale 14-digit pointer-shaped integer.
//
// The fix: pin (cb, val) as a 2-tuple under a per-invocation id in a
// ProtoSparseList that lives on the JS global object.  The global is
// always GC-rooted, so the registry — and through it the cb and val
// — stay alive until the lambda fires and explicitly removes the
// entry.
constexpr const char* kPendingRegistryAttr = "__protojs_pending_async__";
std::atomic<unsigned long> g_pendingId{0};

const proto::ProtoString* pendingRegistryKey(proto::ProtoContext* ctx) {
    static thread_local const proto::ProtoString* k = nullptr;
    if (!k) k = proto::ProtoString::createSymbol(ctx, kPendingRegistryAttr);
    return k;
}

unsigned long pinPendingInvocation(proto::ProtoContext* ctx,
                                    JSContextWrapper* wrapper,
                                    const proto::ProtoObject* cb,
                                    const proto::ProtoObject* val) {
    if (!wrapper || !ctx) return 0;
    const proto::ProtoObject* global = wrapper->getNativeGlobal();
    if (!global) return 0;
    const proto::ProtoString* key = pendingRegistryKey(ctx);
    if (!key) return 0;
    const proto::ProtoObject* attr = global->getAttribute(ctx, key, false);
    const proto::ProtoSparseList* reg =
        (attr && attr != PROTO_NONE) ? attr->asSparseList(ctx)
                                     : ctx->newSparseList();
    unsigned long id = g_pendingId.fetch_add(1, std::memory_order_acq_rel) + 1;
    const proto::ProtoList* tuple = ctx->newList()
        ->appendLast(ctx, cb ? cb : PROTO_NONE)
        ->appendLast(ctx, val ? val : PROTO_NONE);
    reg = reg->setAt(ctx, id, tuple->asObject(ctx));
    global->setAttribute(ctx, key, reg->asObject(ctx));
    return id;
}

void takePendingInvocation(proto::ProtoContext* ctx,
                            JSContextWrapper* wrapper,
                            unsigned long id,
                            const proto::ProtoObject*& outCb,
                            const proto::ProtoObject*& outVal) {
    outCb = nullptr;
    outVal = nullptr;
    if (!wrapper || !ctx || id == 0) return;
    const proto::ProtoObject* global = wrapper->getNativeGlobal();
    if (!global) return;
    const proto::ProtoString* key = pendingRegistryKey(ctx);
    if (!key) return;
    const proto::ProtoObject* attr = global->getAttribute(ctx, key, false);
    if (!attr || attr == PROTO_NONE) return;
    const proto::ProtoSparseList* reg = attr->asSparseList(ctx);
    if (!reg) return;
    const proto::ProtoObject* tupleObj = reg->getAt(ctx, id);
    if (!tupleObj || tupleObj == PROTO_NONE) return;
    const proto::ProtoList* tuple = tupleObj->asList(ctx);
    if (!tuple || tuple->getSize(ctx) < 2) return;
    outCb = tuple->getAt(ctx, 0);
    outVal = tuple->getAt(ctx, 1);
    reg = reg->removeAt(ctx, id);
    global->setAttribute(ctx, key, reg->asObject(ctx));
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
            // Pin (cb, val) in the GC-rooted registry — see comment on
            // pinPendingInvocation for the reachability bug this fixes.
            unsigned long id = pinPendingInvocation(ctx, wrapper, cb, val);
            EventLoop::getInstance().enqueueCallback([wrapper, id]() {
                if (!wrapper) return;
                JSContextWrapper::CurrentScope wscope(wrapper);
                proto::ProtoContext* c = wrapper->getProtoContext();
                if (!c) return;
                const proto::ProtoObject* cbR = nullptr;
                const proto::ProtoObject* valR = nullptr;
                takePendingInvocation(c, wrapper, id, cbR, valR);
                if (!cbR || cbR == PROTO_NONE) return;
                const ProtoBytecodeModule* mod =
                    static_cast<const ProtoBytecodeModule*>(wrapper->getRootModule());
                const proto::ProtoList* args = c->newList()
                    ->appendLast(c, valR ? valR : PROTO_NONE);
                callJSFunctionFromAsync(c, cbR, PROTO_NONE, args, mod,
                                        wrapper->getNativeGlobalRootPtr());
            });
        }
        // Clear queue so subsequent state transitions don't double-fire.
        writeQueue(ctx, d, qKey, ctx->newList());
    }
    // One async-completion event per Deferred regardless of how many
    // callbacks were chained.
    g_activeCount.fetch_sub(1, std::memory_order_acq_rel);
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

    const proto::ProtoObject* inst = ProtoDeferred::createPending(ctx);
    if (!inst) return PROTO_NONE;

    if (workerFn && workerFn != PROTO_NONE) {
        JSContextWrapper* wrapper = JSContextWrapper::current();
        // createPending already incremented the active counter; the
        // worker invocation runs as part of that pending resolution
        // and resolveFromAsync's drainQueue does the matching
        // decrement.  Pin (workerFn, inst) in the GC-rooted registry
        // so they survive any GC cycle that runs before the lambda
        // fires — see pinPendingInvocation comment.
        unsigned long id = pinPendingInvocation(ctx, wrapper, workerFn, inst);
        EventLoop::getInstance().enqueueCallback([wrapper, id]() {
            if (!wrapper) return;
            JSContextWrapper::CurrentScope wscope(wrapper);
            proto::ProtoContext* c = wrapper->getProtoContext();
            if (!c) return;
            const proto::ProtoObject* workerFnR = nullptr;
            const proto::ProtoObject* instR = nullptr;
            takePendingInvocation(c, wrapper, id, workerFnR, instR);
            if (!workerFnR || workerFnR == PROTO_NONE) return;
            const ProtoBytecodeModule* mod =
                static_cast<const ProtoBytecodeModule*>(wrapper->getRootModule());
            const proto::ProtoObject* result =
                callJSFunctionFromAsync(c, workerFnR, PROTO_NONE,
                                         c->newList(), mod,
                                         wrapper->getNativeGlobalRootPtr());
            ProtoDeferred::resolveFromAsync(c, instR, result, wrapper);
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
    const proto::ProtoObject* cb =
        (args && args->getSize(ctx) > 0) ? args->getAt(ctx, 0) : PROTO_NONE;
    if (!cb || cb == PROTO_NONE) return self;
    auto& k = keys(ctx);

    long long state = readState(ctx, self);
    if (state == kStateFulfilled) {
        // Already fulfilled — fire callback on next turn.
        const proto::ProtoObject* value =
            self->getAttribute(ctx, k.value, false);
        if (!value) value = PROTO_NONE;
        JSContextWrapper* wrapper = JSContextWrapper::current();
        g_activeCount.fetch_add(1, std::memory_order_acq_rel);
        unsigned long id = pinPendingInvocation(ctx, wrapper, cb, value);
        EventLoop::getInstance().enqueueCallback([wrapper, id]() {
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
            takePendingInvocation(c, wrapper, id, cbR, valR);
            if (cbR && cbR != PROTO_NONE) {
                const ProtoBytecodeModule* mod =
                    static_cast<const ProtoBytecodeModule*>(wrapper->getRootModule());
                const proto::ProtoList* args = c->newList()
                    ->appendLast(c, valR ? valR : PROTO_NONE);
                callJSFunctionFromAsync(c, cbR, PROTO_NONE, args, mod,
                                        wrapper->getNativeGlobalRootPtr());
            }
            g_activeCount.fetch_sub(1, std::memory_order_acq_rel);
        });
    } else if (state == kStatePending) {
        // Queue for later dispatch when the Deferred resolves.
        const proto::ProtoList* queue = readQueue(ctx, self, k.thenList);
        if (!queue) queue = ctx->newList();
        queue = queue->appendLast(ctx, cb);
        writeQueue(ctx, self, k.thenList, queue);
    }
    // state == rejected: ignore .then per Promise semantics.
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
    if (!cb || cb == PROTO_NONE) return self;
    auto& k = keys(ctx);

    long long state = readState(ctx, self);
    if (state == kStateRejected) {
        const proto::ProtoObject* reason =
            self->getAttribute(ctx, k.value, false);
        if (!reason) reason = PROTO_NONE;
        JSContextWrapper* wrapper = JSContextWrapper::current();
        g_activeCount.fetch_add(1, std::memory_order_acq_rel);
        unsigned long id = pinPendingInvocation(ctx, wrapper, cb, reason);
        EventLoop::getInstance().enqueueCallback([wrapper, id]() {
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
            takePendingInvocation(c, wrapper, id, cbR, valR);
            if (cbR && cbR != PROTO_NONE) {
                const ProtoBytecodeModule* mod =
                    static_cast<const ProtoBytecodeModule*>(wrapper->getRootModule());
                const proto::ProtoList* args = c->newList()
                    ->appendLast(c, valR ? valR : PROTO_NONE);
                callJSFunctionFromAsync(c, cbR, PROTO_NONE, args, mod,
                                        wrapper->getNativeGlobalRootPtr());
            }
            g_activeCount.fetch_sub(1, std::memory_order_acq_rel);
        });
    } else if (state == kStatePending) {
        const proto::ProtoList* queue = readQueue(ctx, self, k.catchList);
        if (!queue) queue = ctx->newList();
        queue = queue->appendLast(ctx, cb);
        writeQueue(ctx, self, k.catchList, queue);
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
    if (!deferredPrototypeObject(ctx)) return globalObj;
    // Install constructor on global.  ProtoMethod IS callable and
    // protoCore's call dispatch uses asMethod() — using the same
    // signature for the constructor is fine; `new Deferred(fn)`
    // and `Deferred(fn)` produce identical results in this design
    // (no this-binding subtleties).
    const proto::ProtoObject* ctor = ctx->fromMethod(nullptr, deferredConstruct);
    if (!ctor) return globalObj;
    const proto::ProtoString* name = ctx->fromUTF8String("Deferred")
        ? ctx->fromUTF8String("Deferred")->asString(ctx) : nullptr;
    if (!name) return globalObj;
    return globalObj->setAttribute(ctx, name, ctor);
}

}  // namespace protojs
