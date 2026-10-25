#include "../../ProtoCoreTypes.h"
#include "../../runtime/PropertyDictionary.h"
#include "../../runtime/PinnedBuiltin.h"
#include "WorkerThreadsModule.h"
#include <vector>
#include <mutex>
#include <algorithm>
#include "../../ProtoDeferred.h"
#include "../../ProtoNativeModule.h"
#include "../../ArrayElementsStorage.h"
#include "../../ArrayPrototype.h"
#include "../../FunctionPrototype.h"
#include "../../JSSymbols.h"
#include "../../JSContext.h"
#include "../../EventLoop.h"
#include "../../GcOrphanQueue.h"
#include "../../ThreadProtoContext.h"
#include "../../runtime/ProtoInterpreter.h"
#include "../../runtime/ProtoBytecodeModule.h"
#include "../events/EventsModule.h"
#include "../../runtime/ThreadIdentity.h"
#include "../../console.h"
#include <fstream>
#include <sstream>
#include <atomic>
#include <thread>
#include <memory>
#include <string>
#include <cstdio>
#include <cstring>

namespace protojs {

namespace {

// ---- Attribute keys ----------------------------------------------------
//
// Re-interned per call: createSymbol is a hash-bucket lookup, very cheap.
// (Workers once had spaces of their own, with symbol tables of their own,
// and a cached key of one space mismatched in another; a worker now runs in
// the main thread's space.)

const proto::ProtoString* keyEvents(proto::ProtoContext* ctx) {
    return proto::ProtoString::createSymbol(ctx, "__events__");
}
const proto::ProtoString* keyWorkerState(proto::ProtoContext* ctx) {
    return proto::ProtoString::createSymbol(ctx, "__worker_state__");
}
const proto::ProtoString* keyWorkerRef(proto::ProtoContext* ctx) {
    return proto::ProtoString::createSymbol(ctx, "__worker_ref__");
}

// Counter — main.cpp's drain loop already checks getActiveWorkerCount.
std::atomic<int> g_activeWorkers{0};

// ---- Messages: a structured clone inside the one space -----------------
//
// A worker runs in the main thread's space (docs/API_REFERENCE.md, "worker_threads"), so a message
// is not serialized: the sender makes a structured clone of the value, in its
// own thread, at the time of the call, and the receiver gets that clone, as
// HTML's structured clone algorithm and Node's postMessage specify.  What
// cannot change is shared instead of copied, because a copy would be
// indistinguishable from it: numbers, strings, booleans, BigInts, undefined,
// null and protoCore's own immutable values.  Arrays and objects are copied,
// with their own enumerable data properties cloned in turn; a value reached
// twice is cloned once, so cycles and shared substructure survive.  A
// function or a Symbol cannot be cloned: postMessage throws DataCloneError.
// Other built-in objects (Map, Set, Date, typed arrays) are copied as plain
// objects of their own data properties, as the JSON encoding before did.

[[noreturn]] void throwDataCloneError(proto::ProtoContext* ctx, const char* what) {
    std::string msg = std::string(what) + " could not be cloned.";
    const proto::ProtoObject* err = makeNativeError(ctx, "Error", msg.c_str());
    if (err && err != PROTO_NONE) {
        if (const proto::ProtoString* nk = JSSymbols::name(ctx))
            err = err->setAttribute(ctx, nk, ctx->fromUTF8String("DataCloneError"));
    }
    signalNativeException(err);
}

struct MessageCloner {
    proto::ProtoContext* ctx;
    // Originals already cloned, and their clones (cycles, shared parts).
    std::vector<std::pair<const proto::ProtoObject*, const proto::ProtoObject*>> seen;
    // Every clone made, pinned by the caller's young generation: `ctx` is a
    // context of the sending thread that lives until the clone is handed on.

    const proto::ProtoObject* cloned(const proto::ProtoObject* v) const {
        for (const auto& e : seen) if (e.first == v) return e.second;
        return nullptr;
    }

    // A value that is not a mutable JavaScript object: shared, not copied.
    bool isShared(const proto::ProtoObject* v) const {
        if (!v || v == PROTO_NONE) return true;
        if (proto::isSmallInt(v)) return true;
        if ((reinterpret_cast<uintptr_t>(v) & 0x3F) != 0) return true;  // tagged values
        if (v == getUndefinedSentinel() || v == getNullSentinel()) return true;
        // A BigInt is an object cell carrying its own value slot.
        const proto::ProtoString* bigK = JSSymbols::bigIntValue(ctx);
        if (bigK && v->hasOwnAttribute(ctx, bigK) == PROTO_TRUE) return true;
        return false;
    }

    const proto::ProtoObject* clone(const proto::ProtoObject* v) {
        if (isShared(v)) return v;
        if (const proto::ProtoObject* c = cloned(v)) return c;
        const proto::ProtoString* symK = JSSymbols::isSymbol(ctx);
        if (symK && v->getAttribute(ctx, symK, false) == PROTO_TRUE)
            throwDataCloneError(ctx, "Symbol()");
        if (isCallableValue(ctx, v)) throwDataCloneError(ctx, "function");

        const proto::ProtoString* arrKey = JSSymbols::isArray(ctx);
        const proto::ProtoObject* flag = arrKey ? v->getAttribute(ctx, arrKey, false) : nullptr;
        if (flag && flag != PROTO_NONE) {
            const proto::ProtoObject* arr = createNewArray(ctx, nullptr);
            seen.emplace_back(v, arr);
            const proto::ProtoList* src = getArrayElements(ctx, v);
            const long long n = src ? static_cast<long long>(src->getSize(ctx)) : 0;
            const proto::ProtoList* els = ctx->newList();
            for (long long i = 0; i < n; ++i)
                els = els->appendLast(ctx, clone(src->getAt(ctx, static_cast<int>(i))));
            setArrayElements(ctx, arr, els);
            if (const proto::ProtoString* lk = JSSymbols::length(ctx))
                arr->setAttribute(ctx, lk, ctx->fromInteger(n));
            return arr;
        }

        const proto::ProtoObject* obj = ctx->newObject(/*mutable=*/true);
        seen.emplace_back(v, obj);
        // Own attributes, without protoJS's internal markers.
        const proto::ProtoSparseList* own = v->getOwnAttributes(ctx);
        if (own) {
            const proto::ProtoSparseListIterator* it = own->getIterator(ctx);
            while (it && it->hasNext(ctx)) {
                proto::proto_ulong rawKey = it->nextKey(ctx);
                const proto::ProtoObject* pv = it->nextValue(ctx);
                it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
                const proto::ProtoString* k = reinterpret_cast<const proto::ProtoString*>(rawKey);
                if (!k || !pv) continue;
                std::string ks;
                k->toUTF8String(ctx, ks);
                if (ks.size() >= 2 && ks[0] == '_' && ks[1] == '_') continue;
                obj = dict::createDataProperty(ctx, obj, k, clone(pv));
            }
        }
        // The keys of the property dictionary (runtime/PropertyDictionary.h).
        std::vector<std::pair<const proto::ProtoString*, const proto::ProtoObject*>> de;
        dict::entries(ctx, v, de);
        for (const auto& e : de) obj = dict::createDataProperty(ctx, obj, e.first, clone(e.second));
        // createDataProperty may return a new object for an immutable one;
        // the clone is mutable, so the identity recorded above stands.
        return obj;
    }
};

// The structured clone of `value`, made through `ctx` (the sender's).
const proto::ProtoObject* cloneMessage(proto::ProtoContext* ctx, const proto::ProtoObject* value) {
    MessageCloner cloner{ctx, {}};
    return cloner.clone(value);
}

// ---- EventEmitter helper ----------------------------------------------
//
// Build a fresh EventEmitter instance.  We bypass the JS-level
// constructor (which is a no-op) and just clone the prototype chain
// directly: events.EventEmitter.prototype → newChild.

const proto::ProtoObject* makeEventEmitterInstance(
        proto::ProtoContext* ctx,
        const proto::ProtoObject* nativeGlobal) {
    if (!ctx || !nativeGlobal) return ctx ? ctx->newObject(true) : nullptr;
    const proto::ProtoString* eventsKey =
        ctx->fromUTF8String("events")->asString(ctx);
    if (!eventsKey) return ctx->newObject(true);
    const proto::ProtoObject* eventsMod =
        nativeGlobal->getAttribute(ctx, eventsKey, false);
    if (!eventsMod || eventsMod == PROTO_NONE) return ctx->newObject(true);
    const proto::ProtoString* eeKey =
        ctx->fromUTF8String("EventEmitter")->asString(ctx);
    const proto::ProtoObject* eeCtor =
        eeKey ? eventsMod->getAttribute(ctx, eeKey, false) : nullptr;
    if (!eeCtor || eeCtor == PROTO_NONE) return ctx->newObject(true);
    const proto::ProtoString* protoKey = JSSymbols::prototype(ctx);
    const proto::ProtoObject* eeProto =
        protoKey ? eeCtor->getAttribute(ctx, protoKey, false) : nullptr;
    if (!eeProto || eeProto == PROTO_NONE) return ctx->newObject(true);
    return eeProto->newChild(ctx, /*mutable=*/true);
}

struct WorkerState;

// ---- WorkerState (carried via ExternalPointer) -------------------------

struct WorkerState : GcOrphanQueue::Orphan {
    JSContextWrapper* mainWrapper{nullptr};
    proto::ProtoRootSet::Handle workerPin{
        proto::ProtoRootSet::kNullHandle};
    // The worker's wrapper: it borrows the main wrapper's space
    // (docs/API_REFERENCE.md, "worker_threads"), built and used on the worker thread.
    std::unique_ptr<JSContextWrapper> workerWrapper;
    // The worker's protoCore thread, in the main space; it gets the stack
    // main.cpp configures for every JavaScript thread
    // (ProtoSpace::setThreadStackBytes).  Pinned in the main wrapper's root
    // set until it has been joined.
    const proto::ProtoThread* thread{nullptr};
    proto::ProtoRootSet::Handle threadPin{proto::ProtoRootSet::kNullHandle};
    std::mutex joinMutex;
    bool joined{false};
    std::string filename;
    // The clone of options.workerData, and the main thread's global object
    // (the parent of the worker's), pinned in the main wrapper's root set
    // until the worker thread has read them.
    proto::ProtoRootSet::Handle workerData{proto::ProtoRootSet::kNullHandle};
    proto::ProtoRootSet::Handle ownerGlobal{proto::ProtoRootSet::kNullHandle};
    // The main thread's identity objects (undefined, null, ...), adopted by
    // the worker thread: values pass between the two without a copy.
    ThreadIdentity identity;
    std::atomic<bool> terminated{false};
    std::atomic<bool> running{false};

    void releaseOnMutator() override;
};

// Every WorkerState not yet deleted, so ~JSContextWrapper can release the
// workers its wrapper owns (WorkerThreadsModule::releaseWorkersOf).
std::mutex g_workerStatesMutex;
std::vector<WorkerState*> g_workerStates;

void registerWorkerState(WorkerState* s) {
    std::lock_guard<std::mutex> lock(g_workerStatesMutex);
    g_workerStates.push_back(s);
}

void unregisterWorkerState(WorkerState* s) {
    std::lock_guard<std::mutex> lock(g_workerStatesMutex);
    g_workerStates.erase(std::remove(g_workerStates.begin(), g_workerStates.end(), s),
                         g_workerStates.end());
}

// Ask the worker to stop. Two atomic stores; blocks on nothing, calls no protoCore
// API. This is all a finalizer is allowed to do here.
void requestWorkerTeardown(WorkerState* s) {
    if (!s) return;
    s->terminated.store(true);
    s->running.store(false);
}

// The blocking half.  `ctx` may be null, in which case the calling thread's own
// registered context is used.  The worker is a protoCore thread of this space, and
// ProtoThread::join leaves the running set while it waits, so the space can still
// collect while the worker finishes.  One caller joins; the others find it joined.
void awaitWorkerTeardown(WorkerState* s, proto::ProtoContext* ctx) {
    if (!s) return;
    if (!ctx) ctx = threadProtoContext();
    std::lock_guard<std::mutex> lock(s->joinMutex);
    if (s->joined || !s->thread || !ctx) return;
    const_cast<proto::ProtoThread*>(s->thread)->join(ctx);
    s->joined = true;
    if (s->mainWrapper && s->threadPin != proto::ProtoRootSet::kNullHandle) {
        if (proto::ProtoRootSet* rs = s->mainWrapper->getRootSet()) rs->remove(s->threadPin);
        s->threadPin = proto::ProtoRootSet::kNullHandle;
    }
}

// The workerData clone and the owner's global, if the worker thread never read them.
void releaseStartupPins(WorkerState* s) {
    proto::ProtoRootSet* rs = s->mainWrapper ? s->mainWrapper->getRootSet() : nullptr;
    if (!rs) return;
    for (proto::ProtoRootSet::Handle* h : {&s->workerData, &s->ownerGlobal}) {
        if (*h != proto::ProtoRootSet::kNullHandle) rs->remove(*h);
        *h = proto::ProtoRootSet::kNullHandle;
    }
}

// Everything the old finalizer did after the two stores, now on a mutator thread.
//
// This is protoCore's own worked example, verbatim in shape.
// docs/GarbageCollector.md S7: "Releasing an exiting ProtoThread needs to give back
// an allocation batch, destroy a ProtoContext and join a std::thread. Every one of
// those is forbidden here: returning the batch takes ProtoSpace::globalMutex
// (blocks), destroying the context submits a young generation (publishes to a
// shared structure), and joining blocks outright." Releasing a protoJS Worker needs
// all three and more: the join is on a thread running an entire JS program;
// `workerPin` release takes the root-set mutex the collector itself holds during
// root collection; and `workerWrapper.reset()` runs ~JSContextWrapper, which
// destroys a ProtoRootSet, runs GCBridge::cleanup -- which iterates a
// ProtoSparseList and dereferences ProtoObject*s, two more things S7 forbids
// outright. From the GC thread, mid-sweep.
//
// It no longer shuts the process-wide thread pools down, and that was a real bug
// rather than a cosmetic one: CPUThreadPool and IOThreadPool are singletons, so a
// worker's wrapper used to replace the main thread's pools when it was built and
// destroy them when it was released, leaving the main thread with a pool that had
// silently gone away.  Ownership is now collective -- JSContextWrapper::poolOwners_
// -- so only the last live wrapper shuts them down.  Pinned by
// tests/unit/test_context_pool_ownership.cpp.
void WorkerState::releaseOnMutator() {
    awaitWorkerTeardown(this, nullptr);
    if (mainWrapper && workerPin != proto::ProtoRootSet::kNullHandle) {
        proto::ProtoRootSet* rs = mainWrapper->getRootSet();
        if (rs) rs->remove(workerPin);
        workerPin = proto::ProtoRootSet::kNullHandle;
    }
    // Safe only here. Note that ~JSContextWrapper reaches
    // ThreadPoolExecutor::shutdown, which waits on a condition variable and then
    // joins; that call brackets itself with a ThreadUnmanagedScope, and because the slot
    // it reads is THIS thread's rather than the worker wrapper's context, it parks
    // the right thread.
    releaseStartupPins(this);
    workerWrapper.reset();
    unregisterWorkerState(this);
    delete this;
}

// The finalizer. Only the request half, then record the orphan.
void freeWorkerState(void* p) {
    auto* s = static_cast<WorkerState*>(p);
    if (!s) return;
    requestWorkerTeardown(s);
    GcOrphanQueue::post(s);
}

WorkerState* getWorkerState(proto::ProtoContext* ctx,
                              const proto::ProtoObject* self) {
    if (!ctx || !self) return nullptr;
    const proto::ProtoObject* attr =
        self->getAttribute(ctx, keyWorkerState(ctx), false);
    if (!attr || attr == PROTO_NONE) return nullptr;
    const proto::ProtoExternalPointer* ext = attr->asExternalPointer(ctx);
    return ext ? static_cast<WorkerState*>(ext->getPointer(ctx)) : nullptr;
}

// ---- Worker prototype methods (main side) ------------------------------

const proto::ProtoObject* invokeEEMethod(
        proto::ProtoContext* ctx,
        const proto::ProtoObject* self,
        const char* methodName,
        const proto::ProtoArguments* args) {
    if (!self) return PROTO_NONE;
    const proto::ProtoObject* ee =
        self->getAttribute(ctx, keyEvents(ctx), false);
    if (!ee || ee == PROTO_NONE) return PROTO_NONE;
    const proto::ProtoString* mk =
        ctx->fromUTF8String(methodName)->asString(ctx);
    if (!mk) return PROTO_NONE;
    const proto::ProtoObject* fn = ee->getAttribute(ctx, mk, true);
    if (!fn || fn == PROTO_NONE) return PROTO_NONE;
    const proto::ProtoArguments noArgs(nullptr, 0);
    return callJSFunction(ctx, fn, ee, args ? args : &noArgs);
}

// Async variant used from EventLoop callbacks: those do not run inside
// a runBytecode frame, so callJSFunction would lack thread-local
// module / global-root context.  callJSFunctionFromAsync restores
// both from the wrapper before delegating.
void invokeEEMethodFromAsync(
        JSContextWrapper* wrapper,
        const proto::ProtoObject* self,
        const char* methodName,
        const proto::ProtoList* args) {
    if (!wrapper || !self) return;
    proto::ProtoContext* ctx = wrapper->getProtoContext();
    if (!ctx) return;
    const proto::ProtoObject* ee =
        self->getAttribute(ctx, keyEvents(ctx), false);
    if (!ee || ee == PROTO_NONE) return;
    const proto::ProtoString* mk =
        ctx->fromUTF8String(methodName)->asString(ctx);
    if (!mk) return;
    const proto::ProtoObject* fn = ee->getAttribute(ctx, mk, true);
    if (!fn || fn == PROTO_NONE) return;
    const ProtoBytecodeModule* mod =
        static_cast<const ProtoBytecodeModule*>(wrapper->getRootModule());
    callJSFunctionFromAsync(ctx, fn, ee,
                             args ? args : ctx->newList(),
                             mod, wrapper->getNativeGlobalRootPtr());
}

const proto::ProtoObject* workerOn(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoArguments* args,
    const proto::ProtoKeywordArguments*) {
    invokeEEMethod(ctx, self, "on", args);
    return self ? self : PROTO_NONE;
}

const proto::ProtoObject* workerEmit(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoArguments* args,
    const proto::ProtoKeywordArguments*) {
    return invokeEEMethod(ctx, self, "emit", args);
}

const proto::ProtoObject* workerPostMessage(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoArguments* args,
    const proto::ProtoKeywordArguments*) {
    if (!ctx || !self) return PROTO_NONE;
    if (!args || args->getSize(ctx) == 0) return PROTO_NONE;
    WorkerState* state = getWorkerState(ctx, self);
    if (!state || !state->workerWrapper || state->terminated.load())
        return PROTO_NONE;

    // The clone is made now, on this thread (a later change to the value is
    // not seen), and pinned until it is delivered.
    proto::ProtoRootSet* rs = state->mainWrapper ? state->mainWrapper->getRootSet() : nullptr;
    if (!rs) return PROTO_NONE;
    // Cloned in the caller's context (a native creates none): the partial
    // clones are young objects of that context, alive until it ends, and the
    // finished clone is pinned before this returns.
    const proto::ProtoRootSet::Handle message = rs->add(cloneMessage(ctx, args->getAt(ctx, 0)));

    JSContextWrapper* wrapperRaw = state->workerWrapper.get();
    EventLoop::getInstance().enqueueCallback(
        [wrapperRaw, rs, message]() {
            const proto::ProtoObject* msgVal = rs->resolve(message);
            rs->remove(message);
            if (!wrapperRaw) return;
            JSContextWrapper::CurrentScope ws(wrapperRaw);
            proto::ProtoContext* wctx = wrapperRaw->getProtoContext();
            if (!wctx) return;
            const proto::ProtoObject* wg = wrapperRaw->getNativeGlobal();
            if (!wg) return;
            const proto::ProtoString* ppKey =
                wctx->fromUTF8String("parentPort")->asString(wctx);
            const proto::ProtoObject* pp =
                ppKey ? wg->getAttribute(wctx, ppKey, false) : nullptr;
            if (!pp || pp == PROTO_NONE) return;
            const proto::ProtoList* a = wctx->newList()
                ->appendLast(wctx, wctx->fromUTF8String("message"))
                ->appendLast(wctx, msgVal ? msgVal : PROTO_NONE);
            invokeEEMethodFromAsync(wrapperRaw, pp, "emit", a);
        });
    return PROTO_NONE;
}

const proto::ProtoObject* workerTerminate(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoArguments* /*args*/,
    const proto::ProtoKeywordArguments*) {
    WorkerState* state = getWorkerState(ctx, self);
    if (state) {
        // The owner relinquish point: the join, the pin release and the
        // destruction of the worker's wrapper happen on a mutator thread, never
        // in the collector (protoCore's finalizer contract).  Not here, though:
        // a native runs inside a GC critical section, and the worker is a
        // thread of this space, so a join here would hold the stop-the-world
        // quorum that the worker may need to finish (EMBEDDER-CONFORMANCE rule
        // 12).  They run as the next event-loop callback, outside any critical
        // section -- as Node's terminate(), which is asynchronous too.
        requestWorkerTeardown(state);
        EventLoop::getInstance().enqueueCallback([state]() {
            JSContextWrapper* mainWrapper = state->mainWrapper;
            if (!mainWrapper) return;  // released by releaseWorkersOf already
            awaitWorkerTeardown(state, mainWrapper->getProtoContext());
            if (state->workerPin != proto::ProtoRootSet::kNullHandle) {
                proto::ProtoRootSet* rs = mainWrapper->getRootSet();
                if (rs) rs->remove(state->workerPin);
                state->workerPin = proto::ProtoRootSet::kNullHandle;
            }
            releaseStartupPins(state);
            state->workerWrapper.reset();
        });
    }
    return PROTO_NONE;
}

const proto::ProtoObject* getWorkerProto(proto::ProtoContext* ctx) {
    // Made on first use; pinned for the wrapper's life (runtime/PinnedBuiltin.h).
    static PinnedBuiltin protoCache;
    if (const proto::ProtoObject* cached = protoCache.get(ctx)) return cached;
    static const NativeEntry entries[] = {
        {"on",           workerOn},
        {"emit",         workerEmit},
        {"postMessage",  workerPostMessage},
        {"terminate",    workerTerminate},
        NATIVE_MODULE_END
    };
    return protoCache.keep(ctx, ProtoNativeModule::buildModule(ctx, entries, 4));
}

// ---- Worker side: parentPort.postMessage -------------------------------
//
// Runs on the worker thread.  Clones the value (see cloneMessage), pins the
// clone, and dispatches back into the main wrapper to emit('message') on the
// Worker instance.

const proto::ProtoObject* parentPortPostMessage(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoArguments* args,
    const proto::ProtoKeywordArguments*) {
    if (!ctx || !self || !args || args->getSize(ctx) == 0) return PROTO_NONE;
    const proto::ProtoObject* ref =
        self->getAttribute(ctx, keyWorkerRef(ctx), false);
    if (!ref) return PROTO_NONE;
    const proto::ProtoExternalPointer* refExt = ref->asExternalPointer(ctx);
    if (!refExt) return PROTO_NONE;
    auto* state = static_cast<WorkerState*>(refExt->getPointer(ctx));
    if (!state || !state->mainWrapper) return PROTO_NONE;

    JSContextWrapper* mainWrapper = state->mainWrapper;
    proto::ProtoRootSet* rs = mainWrapper->getRootSet();
    if (!rs) return PROTO_NONE;
    // Cloned in the caller's context (a native creates none): the partial
    // clones are young objects of that context, alive until it ends, and the
    // finished clone is pinned before this returns.
    const proto::ProtoRootSet::Handle message = rs->add(cloneMessage(ctx, args->getAt(ctx, 0)));

    proto::ProtoRootSet::Handle pin = state->workerPin;
    EventLoop::getInstance().enqueueCallback(
        [mainWrapper, rs, pin, message]() {
            const proto::ProtoObject* msgVal = rs->resolve(message);
            rs->remove(message);
            JSContextWrapper::CurrentScope ws(mainWrapper);
            proto::ProtoContext* mctx = mainWrapper->getProtoContext();
            if (!mctx) return;
            const proto::ProtoObject* worker = rs->resolve(pin);
            if (!worker || worker == PROTO_NONE) return;
            const proto::ProtoList* a = mctx->newList()
                ->appendLast(mctx, mctx->fromUTF8String("message"))
                ->appendLast(mctx, msgVal ? msgVal : PROTO_NONE);
            invokeEEMethodFromAsync(mainWrapper, worker, "emit", a);
        });
    return PROTO_NONE;
}

const proto::ProtoObject* getParentPortProto(proto::ProtoContext* ctx) {
    // Made on first use; pinned for the wrapper's life (runtime/PinnedBuiltin.h).
    static PinnedBuiltin protoCache;
    if (const proto::ProtoObject* cached = protoCache.get(ctx)) return cached;
    static const NativeEntry entries[] = {
        {"postMessage", parentPortPostMessage},
        NATIVE_MODULE_END
    };
    return protoCache.keep(ctx, ProtoNativeModule::buildModule(ctx, entries, 1));
}

// ---- Worker thread body ------------------------------------------------

// Emits `eventName` on the Worker instance, on the main thread, with `arg` as
// a string argument (none when empty).
void emitOnWorker(JSContextWrapper* mainWrapper,
                   proto::ProtoRootSet::Handle pin,
                   const std::string& eventName,
                   const std::string& arg) {
    EventLoop::getInstance().enqueueCallback(
        [mainWrapper, pin, eventName, arg]() {
            if (!mainWrapper) return;
            JSContextWrapper::CurrentScope ws(mainWrapper);
            proto::ProtoContext* mctx = mainWrapper->getProtoContext();
            if (!mctx) return;
            proto::ProtoRootSet* rs = mainWrapper->getRootSet();
            const proto::ProtoObject* worker =
                rs ? rs->resolve(pin) : nullptr;
            if (!worker || worker == PROTO_NONE) return;
            const proto::ProtoList* a = mctx->newList()
                ->appendLast(mctx, mctx->fromUTF8String(eventName.c_str()));
            if (!arg.empty()) a = a->appendLast(mctx, mctx->fromUTF8String(arg.c_str()));
            invokeEEMethodFromAsync(mainWrapper, worker, "emit", a);
        });
}

// The worker's body, on its protoCore thread in the main space.
void runWorker(WorkerState* state, proto::ProtoContext* ctx) {
    // Values pass between the threads without a copy: this thread must use the
    // main thread's `undefined`, `null` and the other identity objects.
    adoptThreadIdentity(state->identity);

    proto::ProtoRootSet* mainRs = state->mainWrapper->getRootSet();
    const proto::ProtoObject* ownerGlobal =
        (mainRs && state->ownerGlobal != proto::ProtoRootSet::kNullHandle)
            ? mainRs->resolve(state->ownerGlobal) : nullptr;
    // The worker's wrapper borrows the main wrapper's space; its global is a
    // child of the main global object (JSContextWrapper's borrowing
    // constructor).  setUseProtoEval keeps eval on the protoCore path.
    state->workerWrapper =
        std::make_unique<JSContextWrapper>(*state->mainWrapper, ctx, ownerGlobal);
    state->workerWrapper->setUseProtoEval(true);

    {
        JSContextWrapper::CurrentScope ws(state->workerWrapper.get());
        proto::ProtoContext* wctx = ctx;
        // The worker's global inherits console, the modules and the realm's
        // constructors from the main global: nothing to install but its own
        // parentPort and workerData.
        const proto::ProtoObject* wg = state->workerWrapper->getNativeGlobal();
        if (!wg) {
            emitOnWorker(state->mainWrapper, state->workerPin, "error",
                         "Worker: could not create the worker's global object");
            return;
        }

        // Build parentPort instance from a prototype + an EventEmitter
        // mixin so users can call parentPort.on('message', cb).
        const proto::ProtoObject* ppProto = getParentPortProto(wctx);
        const proto::ProtoObject* parentPort = ppProto
            ? ppProto->newChild(wctx, /*mutable=*/true)
            : wctx->newObject(/*mutable=*/true);
        // Mix in an EventEmitter as a delegate stored under __events__,
        // and forward parentPort.on/emit to it via the same trick used
        // by Worker.  For simplicity we just install on/emit directly
        // on parentPort by pulling them off the EventEmitter prototype.
        const proto::ProtoObject* ee =
            makeEventEmitterInstance(wctx, wg);
        parentPort->setAttribute(wctx, keyEvents(wctx), ee);
        // Forward on/emit by referencing the same listeners object.
        // The simplest approach: copy on/emit/once/removeListener
        // from the EventEmitter prototype straight onto parentPort,
        // using __events__ as the receiver target — but our EE
        // methods use `self` as the EE itself, not via a delegate.
        // So we add tiny shim methods that forward via invokeEEMethod.
        struct Shim {
            static const proto::ProtoObject* on_(
                proto::ProtoContext* c,
                const proto::ProtoObject* s,
                const proto::ParentLink*,
                const proto::ProtoArguments* a,
                const proto::ProtoKeywordArguments*) {
                invokeEEMethod(c, s, "on", a);
                return s;
            }
            static const proto::ProtoObject* emit_(
                proto::ProtoContext* c,
                const proto::ProtoObject* s,
                const proto::ParentLink*,
                const proto::ProtoArguments* a,
                const proto::ProtoKeywordArguments*) {
                return invokeEEMethod(c, s, "emit", a);
            }
        };
        // Shims built once per thread and pinned for the wrapper's life
        // (runtime/PinnedBuiltin.h): a static is not a root.
        static PinnedBuiltin ppShimsProtoCache;
        const proto::ProtoObject* ppShimsProto = ppShimsProtoCache.get(wctx);
        if (!ppShimsProto) {
            static const NativeEntry e[] = {
                {"on",   Shim::on_},
                {"emit", Shim::emit_},
                NATIVE_MODULE_END
            };
            ppShimsProto = ppShimsProtoCache.keep(wctx, ProtoNativeModule::buildModule(wctx, e, 2));
        }
        if (ppShimsProto) {
            // Copy the on/emit methods onto parentPort directly.
            const proto::ProtoString* onK =
                wctx->fromUTF8String("on")->asString(wctx);
            const proto::ProtoString* emK =
                wctx->fromUTF8String("emit")->asString(wctx);
            if (onK)
                parentPort->setAttribute(wctx, onK,
                    ppShimsProto->getAttribute(wctx, onK, true));
            if (emK)
                parentPort->setAttribute(wctx, emK,
                    ppShimsProto->getAttribute(wctx, emK, true));
        }
        // Pin a back-reference so parentPort.postMessage can resolve
        // the WorkerState (and from there the main wrapper + pin).
        const proto::ProtoObject* refExt =
            wctx->fromExternalPointer(state, /*finalizer=*/nullptr);
        if (refExt) parentPort->setAttribute(wctx, keyWorkerRef(wctx), refExt);

        // Install parentPort on the worker global.
        const proto::ProtoString* ppKey =
            wctx->fromUTF8String("parentPort")->asString(wctx);
        if (ppKey) {
            wg = wg->setAttribute(wctx, ppKey, parentPort);
            state->workerWrapper->updateNativeGlobal(wg);
        }

        // workerData: the clone the main thread made when it built the Worker.
        if (mainRs && state->workerData != proto::ProtoRootSet::kNullHandle) {
            const proto::ProtoObject* wd = mainRs->resolve(state->workerData);
            const proto::ProtoString* wdKey =
                wctx->fromUTF8String("workerData")->asString(wctx);
            if (wdKey && wd) {
                wg = wg->setAttribute(wctx, wdKey, wd);
                state->workerWrapper->updateNativeGlobal(wg);
            }
        }
        // Both now hang off the worker's global.
        releaseStartupPins(state);
    }

    std::ifstream file(state->filename, std::ios::binary);
    if (!file.is_open()) {
        emitOnWorker(state->mainWrapper, state->workerPin, "error",
                     "Cannot open file: " + state->filename);
        return;
    }
    std::stringstream buf;
    buf << file.rdbuf();
    std::string code = buf.str();

    // eval reports a JavaScript exception itself; one raised around it (a
    // ProtoException) or a foreign C++ exception becomes the worker's "error"
    // event (workerThreadMain catches what escapes this function).
    JSValue rv = JS_UNDEFINED;
    std::string escaped;
    try {
        rv = state->workerWrapper->eval(code, state->filename);
    } catch (const proto::ProtoException&) {
        escaped = "Uncaught exception in worker";
    } catch (const std::exception& e) {
        escaped = std::string("Uncaught C++ exception in worker: ") + e.what();
    } catch (...) {
        escaped = "Uncaught C++ exception of an unknown type in worker";
    }
    if (!escaped.empty()) {
        emitOnWorker(state->mainWrapper, state->workerPin, "error", escaped);
    } else if (JS_IsException(rv)) {
        JSContext* wqctx = state->workerWrapper->getJSContext();
        JSValue exc = JS_GetException(wqctx);
        const char* err = JS_ToCString(wqctx, exc);
        std::string msg = err ? err : "worker exception";
        if (err) JS_FreeCString(wqctx, err);
        JS_FreeValue(wqctx, exc);
        emitOnWorker(state->mainWrapper, state->workerPin, "error", msg);
    }
    JS_FreeValue(state->workerWrapper->getJSContext(), rv);
}

// The entry of the worker's protoCore thread: args[0] is the WorkerState.
// No C++ exception may leave it.
const proto::ProtoObject* workerThreadMain(proto::ProtoContext* ctx,
                                           const proto::ProtoObject* /*self*/,
                                           const proto::ParentLink*,
                                           const proto::ProtoArguments* args,
                                           const proto::ProtoKeywordArguments*) {
    if (!ctx || !args || args->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* p = args->getAt(ctx, 0);
    const proto::ProtoExternalPointer* ep = p ? p->asExternalPointer(ctx) : nullptr;
    auto* state = ep ? static_cast<WorkerState*>(ep->getPointer(ctx)) : nullptr;
    if (!state) return PROTO_NONE;
    state->running.store(true);
    try {
        runWorker(state, ctx);
    } catch (const proto::ProtoException&) {
        emitOnWorker(state->mainWrapper, state->workerPin, "error", "Uncaught exception in worker");
    } catch (const std::exception& e) {
        emitOnWorker(state->mainWrapper, state->workerPin, "error",
                     std::string("Uncaught C++ exception in worker: ") + e.what());
    } catch (...) {
        emitOnWorker(state->mainWrapper, state->workerPin, "error",
                     "Uncaught C++ exception of an unknown type in worker");
    }
    // This thread's context ends with this function; the wrapper lives on
    // (its listeners run on the main event loop) and is destroyed there.
    if (state->workerWrapper) state->workerWrapper->releaseThreadContext();
    emitOnWorker(state->mainWrapper, state->workerPin, "exit", "");
    state->running.store(false);
    g_activeWorkers.fetch_sub(1);
    return PROTO_NONE;
}

// ---- Worker constructor (main side) ------------------------------------

const proto::ProtoObject* workerConstructor(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoArguments* args,
    const proto::ProtoKeywordArguments*) {
    if (!ctx || !args || args->getSize(ctx) == 0) return PROTO_NONE;
    if (refuseOnDeferredThread(ctx, "new Worker()")) return PROTO_NONE;
    const proto::ProtoObject* fileArg = args->getAt(ctx, 0);
    if (!fileArg || !fileArg->isString(ctx)) return PROTO_NONE;
    std::string filename;
    fileArg->asString(ctx)->toUTF8String(ctx, filename);

    JSContextWrapper* mainWrapper = JSContextWrapper::current();
    if (!mainWrapper) return PROTO_NONE;
    proto::ProtoRootSet* rs = mainWrapper->getRootSet();
    if (!rs) return PROTO_NONE;

    // workerData: the structured clone of options.workerData, made now
    // (a DataCloneError is thrown here, before any thread exists).
    proto::ProtoRootSet::Handle workerData = proto::ProtoRootSet::kNullHandle;
    if (args->getSize(ctx) > 1) {
        const proto::ProtoObject* opts = args->getAt(ctx, 1);
        if (opts && opts != PROTO_NONE) {
            const proto::ProtoString* wdKey =
                ctx->fromUTF8String("workerData")->asString(ctx);
            const proto::ProtoObject* wdv =
                wdKey ? opts->getAttribute(ctx, wdKey, false) : nullptr;
            // Cloned in the caller's context, as in workerPostMessage.
            if (wdv && wdv != PROTO_NONE) workerData = rs->add(cloneMessage(ctx, wdv));
        }
    }

    // Build the Worker instance.
    const proto::ProtoObject* proto = getWorkerProto(ctx);
    const proto::ProtoObject* worker = proto
        ? proto->newChild(ctx, /*mutable=*/true)
        : ctx->newObject(/*mutable=*/true);

    // Attach an EventEmitter (so on/emit forwarding has a target).
    const proto::ProtoObject* ee =
        makeEventEmitterInstance(ctx, mainWrapper->getNativeGlobal());
    worker->setAttribute(ctx, keyEvents(ctx), ee);

    auto* state = new WorkerState{};
    state->mainWrapper = mainWrapper;
    state->filename    = filename;
    state->workerData  = workerData;
    // The parent of the worker's global object, and the identity objects
    // the worker thread adopts: both read here, on the main thread.
    if (const proto::ProtoObject* g = mainWrapper->getGlobalObject(ctx))
        state->ownerGlobal = rs->add(g);
    state->identity = captureThreadIdentity(ctx);

    // Pin the Worker instance so the worker thread can resolve it
    // when emitting message/error/exit events.
    state->workerPin = rs->add(worker);

    const proto::ProtoObject* extPtr =
        ctx->fromExternalPointer(state, freeWorkerState);
    if (!extPtr) {
        rs->remove(state->workerPin);
        releaseStartupPins(state);
        delete state;
        return PROTO_NONE;
    }
    registerWorkerState(state);
    worker->setAttribute(ctx, keyWorkerState(ctx), extPtr);

    // The worker is a protoCore thread of this space (docs/API_REFERENCE.md, "worker_threads").
    g_activeWorkers.fetch_add(1);
    const proto::ProtoObject* self = ctx->fromExternalPointer(state, /*finalizer=*/nullptr);
    const proto::ProtoList* threadArgs = self ? ctx->newList()->appendLast(ctx, self) : nullptr;
    const proto::ProtoString* threadName = proto::ProtoString::createSymbol(ctx, "worker");
    const proto::ProtoThread* thread = threadArgs
        ? ctx->space->newThread(ctx, threadName, &workerThreadMain, threadArgs, nullptr)
        : nullptr;
    if (!thread) {
        g_activeWorkers.fetch_sub(1);
        signalNativeException(makeNativeError(ctx, "Error",
            "Worker: could not create the worker thread"));
        return PROTO_NONE;
    }
    {
        // A ProtoThread is a cell: pinned until it has been joined.
        std::lock_guard<std::mutex> lock(state->joinMutex);
        state->thread = thread;
        state->threadPin = rs->add(reinterpret_cast<const proto::ProtoObject*>(thread));
    }
    return worker;
}

// ---- Module-level functions: isMainThread / parentPort / workerData ---
//
// In a Worker, these are populated on the worker's own global (see
// runWorker).  At module-init time on the main side the
// wrapper is the main thread's, so `isMainThread` is true and
// `parentPort` / `workerData` are absent / null.

const proto::ProtoObject* isMainThreadImpl(
    proto::ProtoContext* /*ctx*/,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoArguments* /*args*/,
    const proto::ProtoKeywordArguments*) {
    JSContextWrapper* w = JSContextWrapper::current();
    // The main wrapper is the first one created; subsequent wrappers
    // are workers.  We don't have a flag for this distinction yet, so
    // approximate: a wrapper is a worker if its global has parentPort.
    if (!w) return PROTO_TRUE;
    proto::ProtoContext* c = w->getProtoContext();
    const proto::ProtoObject* g = w->getNativeGlobal();
    if (!c || !g) return PROTO_TRUE;
    const proto::ProtoString* k =
        c->fromUTF8String("parentPort")->asString(c);
    const proto::ProtoObject* pp = k ? g->getAttribute(c, k, false) : nullptr;
    return (pp && pp != PROTO_NONE) ? PROTO_FALSE : PROTO_TRUE;
}

const proto::ProtoObject* parentPortImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoArguments* /*args*/,
    const proto::ProtoKeywordArguments*) {
    JSContextWrapper* w = JSContextWrapper::current();
    if (!w || !ctx) return PROTO_NONE;
    const proto::ProtoString* k =
        ctx->fromUTF8String("parentPort")->asString(ctx);
    const proto::ProtoObject* g = w->getNativeGlobal();
    if (!k || !g) return PROTO_NONE;
    const proto::ProtoObject* pp = g->getAttribute(ctx, k, false);
    return (pp && pp != PROTO_NONE) ? pp : PROTO_NONE;
}

const proto::ProtoObject* workerDataImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoArguments* /*args*/,
    const proto::ProtoKeywordArguments*) {
    JSContextWrapper* w = JSContextWrapper::current();
    if (!w || !ctx) return PROTO_NONE;
    const proto::ProtoString* k =
        ctx->fromUTF8String("workerData")->asString(ctx);
    const proto::ProtoObject* g = w->getNativeGlobal();
    if (!k || !g) return PROTO_NONE;
    const proto::ProtoObject* wd = g->getAttribute(ctx, k, false);
    return (wd && wd != PROTO_NONE) ? wd : PROTO_NONE;
}

}  // namespace

// ---- Public ------------------------------------------------------------

int WorkerThreadsModule::getActiveWorkerCount() {
    return g_activeWorkers.load();
}

void WorkerThreadsModule::releaseWorkersOf(JSContextWrapper* owner) {
    if (!owner) return;
    std::vector<WorkerState*> owned;
    {
        std::lock_guard<std::mutex> lock(g_workerStatesMutex);
        for (WorkerState* s : g_workerStates)
            if (s->mainWrapper == owner) owned.push_back(s);
        // The states leave the registry: the owner is going away, and the
        // finalizer that may still run at its space's teardown only posts an
        // orphan, whose release is then a no-op on these fields.
        for (WorkerState* s : owned)
            g_workerStates.erase(std::remove(g_workerStates.begin(), g_workerStates.end(), s),
                                 g_workerStates.end());
    }
    proto::ProtoContext* ctx = owner->getProtoContext();
    for (WorkerState* s : owned) {
        // The process drain loop has already waited for running workers; a
        // worker still running here (the loop's timeout, an embedder) is
        // asked to stop, as Node stops workers at exit.
        requestWorkerTeardown(s);
        awaitWorkerTeardown(s, ctx);
        if (s->workerPin != proto::ProtoRootSet::kNullHandle) {
            if (proto::ProtoRootSet* rs = owner->getRootSet()) rs->remove(s->workerPin);
            s->workerPin = proto::ProtoRootSet::kNullHandle;
        }
        releaseStartupPins(s);
        s->workerWrapper.reset();
        s->mainWrapper = nullptr;
    }
}

const proto::ProtoObject* WorkerThreadsModule::init(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* globalObj) {
    if (!ctx || !globalObj) return globalObj;

    const proto::ProtoObject* workerProto = getWorkerProto(ctx);

    // Build the Worker constructor (wrapNativeFunction so the runtime
    // dispatches via __native_fn__ + __construct__ for `new Worker(...)`).
    const proto::ProtoObject* workerCtor =
        wrapNativeFunction(ctx, workerConstructor, "Worker",
                            /*length=*/1, /*globalRoot=*/nullptr);
    if (!workerCtor) return globalObj;
    const proto::ProtoString* protoKey = JSSymbols::prototype(ctx);
    if (protoKey)
        workerCtor = workerCtor->setAttribute(ctx, protoKey, workerProto);
    {
        const proto::ProtoString* ck =
            ctx->fromUTF8String("__construct__")->asString(ctx);
        if (ck) workerCtor = workerCtor->setAttribute(ctx, ck,
            ctx->fromMethod(nullptr, workerConstructor));
    }

    // Build the worker_threads module object exposing Worker +
    // isMainThread / parentPort / workerData.
    const proto::ProtoObject* mod = ctx->newObject(/*mutable=*/true);
    if (!mod) return globalObj;
    {
        const proto::ProtoString* k =
            ctx->fromUTF8String("Worker")->asString(ctx);
        if (k) mod->setAttribute(ctx, k, workerCtor);
    }
    auto installFn = [&](const char* name,
                           proto::ProtoMethod fn) {
        const proto::ProtoString* k =
            ctx->fromUTF8String(name)->asString(ctx);
        if (!k) return;
        mod->setAttribute(ctx, k, ctx->fromMethod(nullptr, fn));
    };
    installFn("isMainThread", isMainThreadImpl);
    installFn("parentPort",   parentPortImpl);
    installFn("workerData",   workerDataImpl);

    return ProtoNativeModule::registerOnGlobal(
        ctx, globalObj, "worker_threads", mod);
}

} // namespace protojs
