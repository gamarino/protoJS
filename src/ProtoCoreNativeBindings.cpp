#include "ProtoCoreNativeBindings.h"
#include "ProtoDeferred.h"
#include "ArrayElementsStorage.h"
#include "ArrayPrototype.h"
#include "FunctionPrototype.h"
#include "ProtoNativeModule.h"
#include "JSContext.h"
#include "JSSymbols.h"
#include "EventLoop.h"
#include "CPUThreadPool.h"
#include "ThreadPoolExecutor.h"
#include "runtime/ProtoInterpreter.h"
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <string>
#include <cstdint>

namespace protojs {

namespace {

// ---- Native worker registry ------------------------------------------
// Workers are C++ ProtoMethod functions selectable from JS code by name.
// This is the only registry; the QuickJS-side ProtoCoreModule that used
// to keep a second copy is no longer built.

// Worker-side result slot: a heap-allocated struct shared between the
// JS thread (which reads it after join) and the worker thread (which
// writes it on exit).  The JS side wraps a pointer to one of these
// in a ProtoExternalPointer so the GC sees a stable address — the
// raw void* never moves regardless of GC compaction state.
struct WorkerResult {
    std::atomic<long long> value{0};
    WorkerResult() = default;
};

void freeWorkerResult(void* p) {
    delete static_cast<WorkerResult*>(p);
}

const proto::ProtoObject* cpuChunkWorker(
    proto::ProtoContext* context,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    if (!args || args->getSize(context) < 2) return PROTO_NONE;
    const proto::ProtoObject* resultPtrObj = args->getAt(context, 0);
    const proto::ProtoObject* iterObj = args->getAt(context, 1);
    if (!resultPtrObj || resultPtrObj == PROTO_NONE) return PROTO_NONE;
    const proto::ProtoExternalPointer* extPtr =
        resultPtrObj->asExternalPointer(context);
    if (!extPtr) return PROTO_NONE;
    WorkerResult* result =
        static_cast<WorkerResult*>(extPtr->getPointer(context));
    if (!result) return PROTO_NONE;

    long long n = iterObj->asLong(context);
    uint32_t state = 1;
    uint64_t sum = 0;
    for (long long i = 0; i < n; i++) {
        state = static_cast<uint32_t>(static_cast<uint64_t>(state) * 1103515245ULL + 12345ULL);
        sum += state;
    }
    result->value.store(static_cast<long long>(sum), std::memory_order_release);
    return PROTO_NONE;
}

// Thread entry: create a fresh ProtoContext for this thread (caller=nullptr,
// so no context is shared across threads), then run the worker.
const proto::ProtoObject* cpuChunkThreadEntry(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* pl,
    const proto::ProtoList* args,
    const proto::ProtoSparseList* kwargs) {
    if (!context || !context->space) return PROTO_NONE;
    proto::ProtoContext threadCtx(context->space, nullptr, nullptr,
                                   nullptr, nullptr, nullptr);
    return cpuChunkWorker(&threadCtx, self, pl, args, kwargs);
}

const std::unordered_map<std::string, proto::ProtoMethod>& nativeWorkers() {
    static const std::unordered_map<std::string, proto::ProtoMethod> w = {
        {"cpuChunk", cpuChunkThreadEntry},
    };
    return w;
}

// ---- runInThread implementation --------------------------------------

const proto::ProtoObject* runInThreadNative(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    if (!ctx || !args || args->getSize(ctx) < 1) return PROTO_NONE;

    // Arg 0 — worker name (ProtoString).
    const proto::ProtoObject* nameObj = args->getAt(ctx, 0);
    if (!nameObj || !nameObj->isString(ctx)) return PROTO_NONE;
    std::string workerName;
    nameObj->asString(ctx)->toUTF8String(ctx, workerName);
    auto it = nativeWorkers().find(workerName);
    if (it == nativeWorkers().end()) return PROTO_NONE;
    proto::ProtoMethod worker = it->second;

    // Arg 1 (optional) — array-like of worker args.  Our cpuChunk
    // worker expects [resultPtr, n] where resultPtr is an ExternalPointer
    // wrapping a heap-allocated WorkerResult struct.  Using an external
    // pointer (raw void* immune to GC compaction) avoids the race where
    // a regular ProtoObject holder gets moved by GC between newThread
    // and the worker's setAttribute call — a race that becomes very
    // visible once allocation pressure rises (e.g. closure cells make
    // every closure-creating function allocate, raising GC churn).
    // Heap-allocated result struct — lifetime managed manually by the
    // resolve callback below, NOT by an ExternalPointer finalizer.  If
    // we used a finalizer, the wrapper could be GC'd between the
    // worker reading args[0] and the resolve callback running, freeing
    // `result` underneath the worker.
    auto* result = new WorkerResult();
    const proto::ProtoObject* resultPtrObj =
        ctx->fromExternalPointer(result, /*finalizer=*/nullptr);
    if (!resultPtrObj) { delete result; return PROTO_NONE; }

    const proto::ProtoList* workerArgs = ctx->newList()->appendLast(ctx, resultPtrObj);
    if (args->getSize(ctx) >= 2) {
        const proto::ProtoObject* userArgs = args->getAt(ctx, 1);
        // userArgs may be either an Array (with __elements__) or a plain
        // array-like with .length.  Iterate by length.
        if (userArgs && userArgs != PROTO_NONE) {
            // Prefer the native ProtoList storage when present (Array
            // with __elements__).  Fall back to string-keyed reads for
            // legacy array-likes.
            if (const proto::ProtoList* els = getArrayElements(ctx, userArgs)) {
                long long len = static_cast<long long>(els->getSize(ctx));
                for (long long i = 0; i < len; i++) {
                    const proto::ProtoObject* item = els->getAt(ctx, static_cast<int>(i));
                    workerArgs = workerArgs->appendLast(ctx, item ? item : PROTO_NONE);
                }
            } else {
                const proto::ProtoString* lenKey = JSSymbols::length(ctx);
                const proto::ProtoObject* lenVal = lenKey
                    ? userArgs->getAttribute(ctx, lenKey, false) : nullptr;
                long long len = (lenVal && lenVal->isInteger(ctx))
                    ? lenVal->asLong(ctx) : 0;
                for (long long i = 0; i < len; i++) {
                    const proto::ProtoString* idxKey =
                        JSSymbols::indexKey(ctx, static_cast<uint32_t>(i));
                    const proto::ProtoObject* item = idxKey
                        ? userArgs->getAttribute(ctx, idxKey, false) : PROTO_NONE;
                    workerArgs = workerArgs->appendLast(ctx, item ? item : PROTO_NONE);
                }
            }
        }
    }

    JSContextWrapper* wrapper = JSContextWrapper::current();
    proto::ProtoSpace* space = wrapper ? wrapper->getProtoSpace() : ctx->space;
    if (!space) return PROTO_NONE;

    // Reuse a pre-interned thread-name symbol per worker to avoid
    // re-allocating a string each call (which would add GC pressure
    // around the spawn).
    static thread_local const proto::ProtoString* tlNameRunInThread = nullptr;
    if (!tlNameRunInThread)
        tlNameRunInThread = proto::ProtoString::createSymbol(ctx, "runInThread");
    const proto::ProtoThread* thread =
        space->newThread(ctx, tlNameRunInThread, worker, workerArgs, nullptr);

    const proto::ProtoObject* deferred = ProtoDeferred::createPending(ctx);
    if (!deferred) return PROTO_NONE;
    if (!thread) {
        ProtoDeferred::rejectFromAsync(ctx, deferred,
            ctx->fromUTF8String("runInThread: failed to create thread"),
            wrapper);
        return deferred;
    }
    // Pin the deferred and the workerArgs in the wrapper's protoCore
    // root set so the GC cannot reclaim them between thread spawn and
    // the resolve callback running on the JS thread.  The submit
    // lambda below captures only the opaque handles; resolution looks
    // them up via the root set, which is iterated as roots during STW.
    proto::ProtoRootSet* rs = wrapper->getRootSet();
    proto::ProtoRootSet::Handle deferredHandle =
        rs ? rs->add(deferred) : proto::ProtoRootSet::kNullHandle;
    proto::ProtoRootSet::Handle argsHandle =
        rs ? rs->add(workerArgs->asObject(ctx)) : proto::ProtoRootSet::kNullHandle;

    CPUThreadPool::getInstance().getExecutor().submit(
        [thread, result, wrapper, space, deferredHandle, argsHandle]() {
            proto::ProtoContext joinCtx(space, nullptr, nullptr,
                                         nullptr, nullptr, nullptr);
            const_cast<proto::ProtoThread*>(thread)->join(&joinCtx);
            EventLoop::getInstance().enqueueCallback(
                [result, wrapper, deferredHandle, argsHandle]() {
                long long v = result->value.load(std::memory_order_acquire);
                delete result;
                if (!wrapper) return;
                JSContextWrapper::CurrentScope wscope(wrapper);
                proto::ProtoContext* c = wrapper->getProtoContext();
                if (!c) return;
                proto::ProtoRootSet* rs = wrapper->getRootSet();
                if (!rs) return;
                const proto::ProtoObject* deferred = rs->resolve(deferredHandle);
                rs->remove(deferredHandle);
                rs->remove(argsHandle);  // workerArgs no longer needed
                if (!deferred) return;
                ProtoDeferred::resolveFromAsync(c, deferred,
                    c->fromLong(v), wrapper);
            });
        });

    return deferred;
}

// ---- Collections ------------------------------------------------------
//
// Each instance keeps its persistent protoCore collection in one private
// attribute.  A mutator derives the new collection and republishes it with
// setAttributeIfEqual, retrying when another thread got there first, so two
// threads adding to the same instance cannot lose an update — the reason the
// value is held in an attribute rather than in a C++ side table.

// Interned private keys, cached per thread (symbols compare by pointer).
struct CollKeys {
    const proto::ProtoString* set        = nullptr;
    const proto::ProtoString* multiset   = nullptr;
    const proto::ProtoString* sparselist = nullptr;
};

CollKeys& collKeys(proto::ProtoContext* ctx) {
    static thread_local CollKeys k;
    if (!k.set) {
        k.set        = proto::ProtoString::createSymbol(ctx, "__pc_set__");
        k.multiset   = proto::ProtoString::createSymbol(ctx, "__pc_multiset__");
        k.sparselist = proto::ProtoString::createSymbol(ctx, "__pc_sparselist__");
    }
    return k;
}

const proto::ProtoObject* throwTypeError(proto::ProtoContext* ctx, const char* msg) {
    signalNativeException(makeNativeError(ctx, "TypeError", msg));
    return PROTO_NONE;
}

// The three prototype objects, one set per thread.  Their identity is what
// distinguishes `new protoCore.Set()` — whose receiver is a fresh child of
// the prototype — from a plain `protoCore.Set()` call, whose receiver is the
// `protoCore` module object.  This works whichever slot the interpreter
// dispatches a constructor through (`__construct__` or `__native_fn__`).
const proto::ProtoObject*& setProtoSlot() {
    static thread_local const proto::ProtoObject* p = nullptr; return p;
}
const proto::ProtoObject*& multisetProtoSlot() {
    static thread_local const proto::ProtoObject* p = nullptr; return p;
}
const proto::ProtoObject*& sparseListProtoSlot() {
    static thread_local const proto::ProtoObject* p = nullptr; return p;
}

bool calledWithNew(proto::ProtoContext* ctx,
                    const proto::ProtoObject* self,
                    const proto::ProtoObject* protoObj) {
    if (!self || self == PROTO_NONE || !protoObj) return false;
    return self->getPrototype(ctx) == protoObj;
}

// Bound so that a receiver which can never be published (an immutable
// instance, say) fails loudly instead of spinning forever.
constexpr int kCasAttempts = 1000;

// Read one element of an Array argument list, or nullptr when the argument
// is not an Array.
const proto::ProtoList* arrayArgElements(proto::ProtoContext* ctx,
                                          const proto::ProtoList* args) {
    if (!args || args->getSize(ctx) == 0) return nullptr;
    const proto::ProtoObject* a = args->getAt(ctx, 0);
    if (!a || a == PROTO_NONE || a == getUndefinedSentinel()) return nullptr;
    return getArrayElements(ctx, a);
}

bool readIndex(proto::ProtoContext* ctx, const proto::ProtoObject* v,
                unsigned long& out) {
    if (!v || v == PROTO_NONE) return false;
    if (v->isInteger(ctx)) {
        long long n = v->asLong(ctx);
        if (n < 0) return false;
        out = static_cast<unsigned long>(n);
        return true;
    }
    if (v->isDouble(ctx) || v->isFloat(ctx)) {
        double d = v->asDouble(ctx);
        if (d < 0 || d != static_cast<double>(static_cast<long long>(d))) return false;
        out = static_cast<unsigned long>(static_cast<long long>(d));
        return true;
    }
    return false;
}

// ---- Set ---------------------------------------------------------------

const proto::ProtoObject* setConstruct(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    if (!ctx) return PROTO_NONE;
    if (!calledWithNew(ctx, self, setProtoSlot()))
        return throwTypeError(ctx, "Constructor Set requires 'new'");
    const proto::ProtoSet* s = ctx->newSet();
    if (!s) return throwTypeError(ctx, "Set: could not create the collection");
    if (args && args->getSize(ctx) > 0) {
        const proto::ProtoObject* init = args->getAt(ctx, 0);
        if (init && init != PROTO_NONE && init != getUndefinedSentinel()) {
            const proto::ProtoList* els = arrayArgElements(ctx, args);
            if (!els) return throwTypeError(ctx, "Set expects an array");
            unsigned long n = els->getSize(ctx);
            for (unsigned long i = 0; i < n; i++)
                s = s->add(ctx, els->getAt(ctx, static_cast<int>(i)));
        }
    }
    self->setAttribute(ctx, collKeys(ctx).set, s->asObject(ctx));
    // A non-object result makes the interpreter keep the receiver it built.
    return PROTO_NONE;
}

const proto::ProtoSet* readSet(proto::ProtoContext* ctx,
                                const proto::ProtoObject* self) {
    if (!self || self == PROTO_NONE) return nullptr;
    const proto::ProtoObject* a =
        self->getOwnAttributeDirect(ctx, collKeys(ctx).set);
    if (!a || a == PROTO_NONE) return nullptr;
    return a->asSet(ctx);
}

const proto::ProtoObject* setAdd(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    if (!args || args->getSize(ctx) < 1)
        return throwTypeError(ctx, "Set.add expects a value");
    const proto::ProtoString* key = collKeys(ctx).set;
    const proto::ProtoObject* value = args->getAt(ctx, 0);
    for (int attempt = 0; attempt < kCasAttempts; attempt++) {
        const proto::ProtoObject* cur = (self && self != PROTO_NONE)
            ? self->getOwnAttributeDirect(ctx, key) : nullptr;
        const proto::ProtoSet* s = (cur && cur != PROTO_NONE) ? cur->asSet(ctx) : nullptr;
        if (!s) return throwTypeError(ctx, "Invalid Set object");
        const proto::ProtoSet* neu = s->add(ctx, value);
        if (!neu) return throwTypeError(ctx, "Invalid Set object");
        if (neu == s) return self;
        if (self->setAttributeIfEqual(ctx, key, cur, neu->asObject(ctx)))
            return self;
    }
    return throwTypeError(ctx, "Set.add: could not update the receiver");
}

const proto::ProtoObject* setHas(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    if (!args || args->getSize(ctx) < 1)
        return throwTypeError(ctx, "Set.has expects a value");
    const proto::ProtoSet* s = readSet(ctx, self);
    if (!s) return throwTypeError(ctx, "Invalid Set object");
    const proto::ProtoObject* r = s->has(ctx, args->getAt(ctx, 0));
    return (r == PROTO_TRUE) ? PROTO_TRUE : PROTO_FALSE;
}

const proto::ProtoObject* setRemove(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    if (!args || args->getSize(ctx) < 1)
        return throwTypeError(ctx, "Set.remove expects a value");
    const proto::ProtoString* key = collKeys(ctx).set;
    const proto::ProtoObject* value = args->getAt(ctx, 0);
    for (int attempt = 0; attempt < kCasAttempts; attempt++) {
        const proto::ProtoObject* cur = (self && self != PROTO_NONE)
            ? self->getOwnAttributeDirect(ctx, key) : nullptr;
        const proto::ProtoSet* s = (cur && cur != PROTO_NONE) ? cur->asSet(ctx) : nullptr;
        if (!s) return throwTypeError(ctx, "Invalid Set object");
        const proto::ProtoSet* neu = s->remove(ctx, value);
        if (!neu || neu == s) return self;
        if (self->setAttributeIfEqual(ctx, key, cur, neu->asObject(ctx)))
            return self;
    }
    return throwTypeError(ctx, "Set.remove: could not update the receiver");
}

const proto::ProtoObject* setSize(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*,
    const proto::ProtoSparseList*) {
    const proto::ProtoSet* s = readSet(ctx, self);
    if (!s) return throwTypeError(ctx, "Invalid Set object");
    return ctx->fromInteger(static_cast<long long>(s->getSize(ctx)));
}

// ---- Multiset ----------------------------------------------------------

const proto::ProtoObject* multisetConstruct(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    if (!ctx) return PROTO_NONE;
    if (!calledWithNew(ctx, self, multisetProtoSlot()))
        return throwTypeError(ctx, "Constructor Multiset requires 'new'");
    const proto::ProtoMultiset* m = ctx->newMultiset();
    if (!m) return throwTypeError(ctx, "Multiset: could not create the collection");
    if (args && args->getSize(ctx) > 0) {
        const proto::ProtoObject* init = args->getAt(ctx, 0);
        if (init && init != PROTO_NONE && init != getUndefinedSentinel()) {
            const proto::ProtoList* els = arrayArgElements(ctx, args);
            if (!els) return throwTypeError(ctx, "Multiset expects an array");
            unsigned long n = els->getSize(ctx);
            for (unsigned long i = 0; i < n; i++)
                m = m->add(ctx, els->getAt(ctx, static_cast<int>(i)));
        }
    }
    self->setAttribute(ctx, collKeys(ctx).multiset, m->asObject(ctx));
    return PROTO_NONE;
}

const proto::ProtoMultiset* readMultiset(proto::ProtoContext* ctx,
                                          const proto::ProtoObject* self) {
    if (!self || self == PROTO_NONE) return nullptr;
    const proto::ProtoObject* a =
        self->getOwnAttributeDirect(ctx, collKeys(ctx).multiset);
    if (!a || a == PROTO_NONE) return nullptr;
    return a->asMultiset(ctx);
}

const proto::ProtoObject* multisetAdd(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    if (!args || args->getSize(ctx) < 1)
        return throwTypeError(ctx, "Multiset.add expects a value");
    const proto::ProtoString* key = collKeys(ctx).multiset;
    const proto::ProtoObject* value = args->getAt(ctx, 0);
    for (int attempt = 0; attempt < kCasAttempts; attempt++) {
        const proto::ProtoObject* cur = (self && self != PROTO_NONE)
            ? self->getOwnAttributeDirect(ctx, key) : nullptr;
        const proto::ProtoMultiset* m =
            (cur && cur != PROTO_NONE) ? cur->asMultiset(ctx) : nullptr;
        if (!m) return throwTypeError(ctx, "Invalid Multiset object");
        const proto::ProtoMultiset* neu = m->add(ctx, value);
        if (!neu) return throwTypeError(ctx, "Invalid Multiset object");
        if (self->setAttributeIfEqual(ctx, key, cur, neu->asObject(ctx)))
            return self;
    }
    return throwTypeError(ctx, "Multiset.add: could not update the receiver");
}

const proto::ProtoObject* multisetCount(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    if (!args || args->getSize(ctx) < 1)
        return throwTypeError(ctx, "Multiset.count expects a value");
    const proto::ProtoMultiset* m = readMultiset(ctx, self);
    if (!m) return throwTypeError(ctx, "Invalid Multiset object");
    const proto::ProtoObject* c = m->count(ctx, args->getAt(ctx, 0));
    return (c && c != PROTO_NONE) ? c : ctx->fromInteger(0);
}

const proto::ProtoObject* multisetRemove(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    if (!args || args->getSize(ctx) < 1)
        return throwTypeError(ctx, "Multiset.remove expects a value");
    const proto::ProtoString* key = collKeys(ctx).multiset;
    const proto::ProtoObject* value = args->getAt(ctx, 0);
    for (int attempt = 0; attempt < kCasAttempts; attempt++) {
        const proto::ProtoObject* cur = (self && self != PROTO_NONE)
            ? self->getOwnAttributeDirect(ctx, key) : nullptr;
        const proto::ProtoMultiset* m =
            (cur && cur != PROTO_NONE) ? cur->asMultiset(ctx) : nullptr;
        if (!m) return throwTypeError(ctx, "Invalid Multiset object");
        const proto::ProtoMultiset* neu = m->remove(ctx, value);
        if (!neu || neu == m) return self;
        if (self->setAttributeIfEqual(ctx, key, cur, neu->asObject(ctx)))
            return self;
    }
    return throwTypeError(ctx, "Multiset.remove: could not update the receiver");
}

const proto::ProtoObject* multisetSize(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*,
    const proto::ProtoSparseList*) {
    const proto::ProtoMultiset* m = readMultiset(ctx, self);
    if (!m) return throwTypeError(ctx, "Invalid Multiset object");
    return ctx->fromInteger(static_cast<long long>(m->getSize(ctx)));
}

// ---- SparseList --------------------------------------------------------

const proto::ProtoObject* sparseListConstruct(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*,
    const proto::ProtoSparseList*) {
    if (!ctx) return PROTO_NONE;
    if (!calledWithNew(ctx, self, sparseListProtoSlot()))
        return throwTypeError(ctx, "Constructor SparseList requires 'new'");
    const proto::ProtoSparseList* sl = ctx->newSparseList();
    if (!sl) return throwTypeError(ctx, "SparseList: could not create the collection");
    self->setAttribute(ctx, collKeys(ctx).sparselist, sl->asObject(ctx));
    return PROTO_NONE;
}

const proto::ProtoSparseList* readSparseList(proto::ProtoContext* ctx,
                                              const proto::ProtoObject* self) {
    if (!self || self == PROTO_NONE) return nullptr;
    const proto::ProtoObject* a =
        self->getOwnAttributeDirect(ctx, collKeys(ctx).sparselist);
    if (!a || a == PROTO_NONE) return nullptr;
    return a->asSparseList(ctx);
}

const proto::ProtoObject* sparseListSet(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    if (!args || args->getSize(ctx) < 2)
        return throwTypeError(ctx, "SparseList.set expects index and value");
    unsigned long index = 0;
    if (!readIndex(ctx, args->getAt(ctx, 0), index))
        return throwTypeError(ctx, "SparseList.set expects a non-negative integer index");
    const proto::ProtoObject* value = args->getAt(ctx, 1);
    const proto::ProtoString* key = collKeys(ctx).sparselist;
    for (int attempt = 0; attempt < kCasAttempts; attempt++) {
        const proto::ProtoObject* cur = (self && self != PROTO_NONE)
            ? self->getOwnAttributeDirect(ctx, key) : nullptr;
        const proto::ProtoSparseList* sl =
            (cur && cur != PROTO_NONE) ? cur->asSparseList(ctx) : nullptr;
        if (!sl) return throwTypeError(ctx, "Invalid SparseList object");
        const proto::ProtoSparseList* neu = sl->setAt(ctx, index, value);
        if (!neu) return throwTypeError(ctx, "Invalid SparseList object");
        if (self->setAttributeIfEqual(ctx, key, cur, neu->asObject(ctx)))
            return self;
    }
    return throwTypeError(ctx, "SparseList.set: could not update the receiver");
}

const proto::ProtoObject* sparseListGet(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    if (!args || args->getSize(ctx) < 1)
        return throwTypeError(ctx, "SparseList.get expects an index");
    const proto::ProtoSparseList* sl = readSparseList(ctx, self);
    if (!sl) return throwTypeError(ctx, "Invalid SparseList object");
    unsigned long index = 0;
    if (!readIndex(ctx, args->getAt(ctx, 0), index)) {
        const proto::ProtoObject* u = getUndefinedSentinel();
        return u ? u : PROTO_NONE;
    }
    // An absent index reads as undefined, never as PROTO_NONE: PROTO_NONE is
    // also a legitimate stored value, so returning it would be ambiguous.
    if (!sl->has(ctx, index)) {
        const proto::ProtoObject* u = getUndefinedSentinel();
        return u ? u : PROTO_NONE;
    }
    const proto::ProtoObject* v = sl->getAt(ctx, index);
    return v ? v : PROTO_NONE;
}

const proto::ProtoObject* sparseListHas(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    if (!args || args->getSize(ctx) < 1)
        return throwTypeError(ctx, "SparseList.has expects an index");
    const proto::ProtoSparseList* sl = readSparseList(ctx, self);
    if (!sl) return throwTypeError(ctx, "Invalid SparseList object");
    unsigned long index = 0;
    if (!readIndex(ctx, args->getAt(ctx, 0), index)) return PROTO_FALSE;
    return sl->has(ctx, index) ? PROTO_TRUE : PROTO_FALSE;
}

const proto::ProtoObject* sparseListSize(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*,
    const proto::ProtoSparseList*) {
    const proto::ProtoSparseList* sl = readSparseList(ctx, self);
    if (!sl) return throwTypeError(ctx, "Invalid SparseList object");
    return ctx->fromInteger(static_cast<long long>(sl->getSize(ctx)));
}

// ---- Tuple and mutability helpers --------------------------------------

// Tuple(array) — the elements are carried in protoCore list storage and
// published as a regular JavaScript Array.  The QuickJS version also
// returned a plain array and never enforced immutability; the interpreter
// has no handling for a bare ProtoTuple value.
const proto::ProtoObject* tupleFn(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    const proto::ProtoList* els = arrayArgElements(ctx, args);
    if (!els) return throwTypeError(ctx, "Tuple expects an array");

    const proto::ProtoObject** groot = getCurrentGlobalRoot();
    const proto::ProtoObject* arrayProto = nullptr;
    if (groot && *groot) {
        const proto::ProtoString* apK = JSSymbols::arrayProto(ctx);
        if (apK) arrayProto = (*groot)->getAttribute(ctx, apK, false);
        if (arrayProto == PROTO_NONE) arrayProto = nullptr;
    }
    const proto::ProtoObject* arr = createNewArray(ctx, arrayProto);
    if (!arr) return throwTypeError(ctx, "Tuple: could not create the result array");
    setArrayElements(ctx, arr, els);
    return arr;
}

// True for JavaScript primitives.  protoCore's public API exposes no
// mutability query, so object cells always report false — the same
// placeholder the QuickJS version carried, and documented as such.
bool isPrimitiveValue(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    if (!v || v == PROTO_NONE) return true;
    if (v == getUndefinedSentinel() || v == getNullSentinel()) return true;
    if (v == PROTO_TRUE || v == PROTO_FALSE) return true;
    // Only the predicates the interpreter itself links against: several
    // others are declared in protoCore.h but not defined in the library.
    return v->isBoolean(ctx) || v->isInteger(ctx) || v->isDouble(ctx)
        || v->isFloat(ctx) || v->isString(ctx);
}

const proto::ProtoObject* cloneArg(proto::ProtoContext* ctx,
                                    const proto::ProtoList* args,
                                    bool mutableCopy,
                                    const char* message) {
    if (!args || args->getSize(ctx) < 1)
        return throwTypeError(ctx, message);
    const proto::ProtoObject* v = args->getAt(ctx, 0);
    if (isPrimitiveValue(ctx, v)) return throwTypeError(ctx, message);
    const proto::ProtoObject* copy = v->clone(ctx, mutableCopy);
    return copy ? copy : PROTO_NONE;
}

const proto::ProtoObject* immutableObjectFn(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    return cloneArg(ctx, args, false, "ImmutableObject expects an object");
}

const proto::ProtoObject* mutableObjectFn(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    return cloneArg(ctx, args, true, "MutableObject expects an object");
}

const proto::ProtoObject* makeImmutableFn(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    return cloneArg(ctx, args, false, "makeImmutable expects an object");
}

const proto::ProtoObject* makeMutableFn(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    return cloneArg(ctx, args, true, "makeMutable expects an object");
}

const proto::ProtoObject* isImmutableFn(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    if (!args || args->getSize(ctx) < 1)
        return throwTypeError(ctx, "isImmutable expects a value");
    return isPrimitiveValue(ctx, args->getAt(ctx, 0)) ? PROTO_TRUE : PROTO_FALSE;
}

// ---- Constructor assembly ----------------------------------------------

// Build a constructor object of the shape L_OP_call_constructor expects: a
// wrapNativeFunction wrapper carrying `prototype` and `__construct__`.  A
// bare ProtoMethod would be rejected with "function is not a constructor".
const proto::ProtoObject* buildConstructor(proto::ProtoContext* ctx,
                                            const char* name,
                                            long long length,
                                            proto::ProtoMethod construct,
                                            const NativeEntry* methods,
                                            size_t methodCount,
                                            const proto::ProtoObject*& protoSlot) {
    const proto::ProtoObject* prototypeObj =
        ProtoNativeModule::buildModule(ctx, methods, methodCount);
    if (!prototypeObj) return nullptr;
    protoSlot = prototypeObj;

    const proto::ProtoObject* ctor =
        wrapNativeFunction(ctx, construct, name, length, /*globalRoot=*/nullptr);
    if (!ctor) return nullptr;

    const proto::ProtoString* protoKey = JSSymbols::prototype(ctx);
    if (protoKey) ctor = ctor->setAttribute(ctx, protoKey, prototypeObj);
    const proto::ProtoString* constructKey = JSSymbols::construct(ctx);
    if (constructKey) {
        const proto::ProtoObject* cm = ctx->fromMethod(nullptr, construct);
        if (cm) ctor = ctor->setAttribute(ctx, constructKey, cm);
    }
    return ctor;
}

}  // namespace

const proto::ProtoObject* ProtoCoreNativeBindings::init(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* globalObj) {
    if (!ctx || !globalObj) return globalObj;

    const proto::ProtoObject* mod = ctx->newObject(/*mutable=*/true);
    if (!mod) return globalObj;

    auto put = [&](const char* name, const proto::ProtoObject* value) {
        if (!value) return;
        const proto::ProtoObject* k = ctx->fromUTF8String(name);
        const proto::ProtoString* key = k ? k->asString(ctx) : nullptr;
        if (key) mod->setAttribute(ctx, key, value);
    };

    static const NativeEntry setMethods[] = {
        {"add",    setAdd},
        {"has",    setHas},
        {"remove", setRemove},
        {"size",   setSize},
        NATIVE_MODULE_END
    };
    static const NativeEntry multisetMethods[] = {
        {"add",    multisetAdd},
        {"count",  multisetCount},
        {"remove", multisetRemove},
        {"size",   multisetSize},
        NATIVE_MODULE_END
    };
    static const NativeEntry sparseListMethods[] = {
        {"set",  sparseListSet},
        {"get",  sparseListGet},
        {"has",  sparseListHas},
        {"size", sparseListSize},
        NATIVE_MODULE_END
    };

    put("Set", buildConstructor(ctx, "Set", 1, setConstruct,
                                 setMethods, 4, setProtoSlot()));
    put("Multiset", buildConstructor(ctx, "Multiset", 1, multisetConstruct,
                                      multisetMethods, 4, multisetProtoSlot()));
    put("SparseList", buildConstructor(ctx, "SparseList", 0, sparseListConstruct,
                                        sparseListMethods, 4, sparseListProtoSlot()));

    put("Tuple", wrapNativeFunction(ctx, tupleFn, "Tuple", 1, nullptr));
    put("ImmutableObject",
        wrapNativeFunction(ctx, immutableObjectFn, "ImmutableObject", 1, nullptr));
    put("MutableObject",
        wrapNativeFunction(ctx, mutableObjectFn, "MutableObject", 1, nullptr));
    put("makeImmutable",
        wrapNativeFunction(ctx, makeImmutableFn, "makeImmutable", 1, nullptr));
    put("makeMutable",
        wrapNativeFunction(ctx, makeMutableFn, "makeMutable", 1, nullptr));
    put("isImmutable",
        wrapNativeFunction(ctx, isImmutableFn, "isImmutable", 1, nullptr));
    put("runInThread", ctx->fromMethod(nullptr, runInThreadNative));

    const proto::ProtoString* modName = ctx->fromUTF8String("protoCore")
        ? ctx->fromUTF8String("protoCore")->asString(ctx) : nullptr;
    if (!modName) return globalObj;
    return globalObj->setAttribute(ctx, modName, mod);
}

}  // namespace protojs
