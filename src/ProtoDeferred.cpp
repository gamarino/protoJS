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
            // Capture by value into the lambda; cb is reachable through
            // the global → instance → queue chain until we drain.
            const proto::ProtoObject* val = value ? value : PROTO_NONE;
            EventLoop::getInstance().enqueueCallback([wrapper, cb, val]() {
                if (!wrapper) return;
                JSContextWrapper::CurrentScope wscope(wrapper);
                proto::ProtoContext* c = wrapper->getProtoContext();
                if (!c) return;
                const ProtoBytecodeModule* mod =
                    static_cast<const ProtoBytecodeModule*>(wrapper->getRootModule());
                const proto::ProtoList* args = c->newList()->appendLast(c, val);
                callJSFunctionFromAsync(c, cb, PROTO_NONE, args, mod,
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
        EventLoop::getInstance().enqueueCallback([wrapper, inst, workerFn]() {
            if (!wrapper) return;
            JSContextWrapper::CurrentScope wscope(wrapper);
            proto::ProtoContext* c = wrapper->getProtoContext();
            if (!c) return;
            const ProtoBytecodeModule* mod =
                static_cast<const ProtoBytecodeModule*>(wrapper->getRootModule());
            const proto::ProtoObject* result =
                callJSFunctionFromAsync(c, workerFn, PROTO_NONE,
                                         c->newList(), mod,
                                         wrapper->getNativeGlobalRootPtr());
            ProtoDeferred::resolveFromAsync(c, inst, result, wrapper);
        });
        // Account for the pending event-loop completion so main.cpp's
        // drain loop waits for us.
        g_activeCount.fetch_add(1, std::memory_order_acq_rel);
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
        const proto::ProtoObject* val = value;
        g_activeCount.fetch_add(1, std::memory_order_acq_rel);
        EventLoop::getInstance().enqueueCallback([wrapper, cb, val]() {
            if (!wrapper) return;
            JSContextWrapper::CurrentScope wscope(wrapper);
            proto::ProtoContext* c = wrapper->getProtoContext();
            if (!c) return;
            const ProtoBytecodeModule* mod =
                static_cast<const ProtoBytecodeModule*>(wrapper->getRootModule());
            const proto::ProtoList* args = c->newList()->appendLast(c, val);
            callJSFunctionFromAsync(c, cb, PROTO_NONE, args, mod,
                                    wrapper->getNativeGlobalRootPtr());
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
        const proto::ProtoObject* val = reason;
        g_activeCount.fetch_add(1, std::memory_order_acq_rel);
        EventLoop::getInstance().enqueueCallback([wrapper, cb, val]() {
            if (!wrapper) return;
            JSContextWrapper::CurrentScope wscope(wrapper);
            proto::ProtoContext* c = wrapper->getProtoContext();
            if (!c) return;
            const ProtoBytecodeModule* mod =
                static_cast<const ProtoBytecodeModule*>(wrapper->getRootModule());
            const proto::ProtoList* args = c->newList()->appendLast(c, val);
            callJSFunctionFromAsync(c, cb, PROTO_NONE, args, mod,
                                    wrapper->getNativeGlobalRootPtr());
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
