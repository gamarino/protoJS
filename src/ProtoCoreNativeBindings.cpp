#include "ProtoCoreNativeBindings.h"
#include "ProtoDeferred.h"
#include "ArrayElementsStorage.h"
#include "JSContext.h"
#include "JSSymbols.h"
#include "EventLoop.h"
#include "CPUThreadPool.h"
#include "ThreadPoolExecutor.h"
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <string>
#include <cstdint>

namespace protojs {

namespace {

// ---- Native worker registry ------------------------------------------
// Workers are C++ ProtoMethod functions selectable from JS code by name.
// The QuickJS-side ProtoCoreModule maintains its own copy populated at
// its init time; we duplicate the registration here so this module is
// self-contained (no ordering dependency between the two init paths).

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

}  // namespace

const proto::ProtoObject* ProtoCoreNativeBindings::init(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* globalObj) {
    if (!ctx || !globalObj) return globalObj;
    // Build the protoCore module object.  Just runInThread for now;
    // future commits will add Set/Multiset/SparseList/Tuple/etc.
    const proto::ProtoObject* mod = ctx->newObject(/*mutable=*/true);
    if (!mod) return globalObj;
    const proto::ProtoString* name = ctx->fromUTF8String("runInThread")
        ? ctx->fromUTF8String("runInThread")->asString(ctx) : nullptr;
    if (name) {
        const proto::ProtoObject* fn =
            ctx->fromMethod(nullptr, runInThreadNative);
        if (fn) mod->setAttribute(ctx, name, fn);
    }
    const proto::ProtoString* modName = ctx->fromUTF8String("protoCore")
        ? ctx->fromUTF8String("protoCore")->asString(ctx) : nullptr;
    if (!modName) return globalObj;
    return globalObj->setAttribute(ctx, modName, mod);
}

}  // namespace protojs
