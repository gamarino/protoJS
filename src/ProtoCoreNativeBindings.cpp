#include "ProtoCoreNativeBindings.h"
#include "ProtoDeferred.h"
#include "ArrayElementsStorage.h"
#include "JSContext.h"
#include "JSSymbols.h"
#include "EventLoop.h"
#include "CPUThreadPool.h"
#include "ThreadPoolExecutor.h"
#include <atomic>
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

const proto::ProtoObject* cpuChunkWorker(
    proto::ProtoContext* context,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    if (!args || args->getSize(context) < 2) return PROTO_NONE;
    const proto::ProtoObject* holder = args->getAt(context, 0);
    const proto::ProtoObject* iterObj = args->getAt(context, 1);
    long long n = iterObj->asLong(context);
    uint32_t state = 1;
    uint64_t sum = 0;
    for (long long i = 0; i < n; i++) {
        state = static_cast<uint32_t>(static_cast<uint64_t>(state) * 1103515245ULL + 12345ULL);
        sum += state;
    }
    const proto::ProtoString* key = JSSymbols::value(context);
    holder->setAttribute(context, key,
        context->fromLong(static_cast<long long>(sum)));
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
    // worker expects [holder, n] where holder is a mutable ProtoObject
    // that the worker writes its result to.  We construct holder here
    // and prepend it.
    const proto::ProtoObject* holder = ctx->newObject(/*mutable=*/true);
    if (!holder) return PROTO_NONE;

    const proto::ProtoList* workerArgs = ctx->newList()->appendLast(ctx, holder);
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

    // Spawn the thread.  On failure return a rejected Deferred so the
    // user gets a .catch'able error rather than silent undefined.
    JSContextWrapper* wrapper = JSContextWrapper::current();
    proto::ProtoSpace* space = wrapper ? wrapper->getProtoSpace() : ctx->space;
    if (!space) return PROTO_NONE;

    const proto::ProtoString* threadName = ctx->fromUTF8String(
        ("runInThread-" + workerName).c_str())->asString(ctx);
    const proto::ProtoThread* thread =
        space->newThread(ctx, threadName, worker, workerArgs, nullptr);

    const proto::ProtoObject* deferred = ProtoDeferred::createPending(ctx);
    if (!deferred) return PROTO_NONE;
    if (!thread) {
        ProtoDeferred::rejectFromAsync(ctx, deferred,
            ctx->fromUTF8String("runInThread: failed to create thread"),
            wrapper);
        return deferred;
    }

    // Schedule a join + resolve via the CPU thread pool: the join blocks
    // until the worker thread finishes, then we hop onto the event loop
    // (which runs on the JS thread) to read the holder and resolve the
    // Deferred.  This pattern matches the existing QuickJS-side runInThread.
    CPUThreadPool::getInstance().getExecutor().submit(
        [thread, holder, deferred, wrapper, space]() {
            proto::ProtoContext joinCtx(space, nullptr, nullptr,
                                         nullptr, nullptr, nullptr);
            const_cast<proto::ProtoThread*>(thread)->join(&joinCtx);
            // Marshal back to the event loop for the resolution.
            EventLoop::getInstance().enqueueCallback([holder, deferred, wrapper]() {
                if (!wrapper) return;
                JSContextWrapper::CurrentScope wscope(wrapper);
                proto::ProtoContext* c = wrapper->getProtoContext();
                if (!c) return;
                const proto::ProtoString* k = JSSymbols::value(c);
                const proto::ProtoObject* result =
                    holder->getAttribute(c, k, false);
                ProtoDeferred::resolveFromAsync(c, deferred,
                    result ? result : PROTO_NONE, wrapper);
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
