#include "Profiler.h"
#include "../ProtoNativeModule.h"
#include "../ArrayElementsStorage.h"
#include "../ArrayPrototype.h"
#include <iostream>
#include <sstream>
#include <iomanip>

namespace protojs {

std::vector<Profiler::ProfileEntry> Profiler::profileEntries;
bool Profiler::profiling = false;
std::chrono::high_resolution_clock::time_point Profiler::profileStart;

uint64_t Profiler::getMemoryUsage() {
    // Placeholder — original module returned 0 here too; a real
    // implementation would query the protoCore heapSize counter or
    // /proc/self/status.  Kept signature-stable.
    return 0;
}

namespace {

const proto::ProtoObject* startProfilingImpl(
    proto::ProtoContext* /*ctx*/,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* /*args*/,
    const proto::ProtoSparseList*) {
    if (Profiler::profiling) return PROTO_FALSE;
    Profiler::profiling = true;
    Profiler::profileStart = std::chrono::high_resolution_clock::now();
    Profiler::profileEntries.clear();
    return PROTO_TRUE;
}

const proto::ProtoObject* stopProfilingImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* /*args*/,
    const proto::ProtoSparseList*) {
    if (!Profiler::profiling) return PROTO_FALSE;
    Profiler::profiling = false;
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(
        endTime - Profiler::profileStart).count();
    return ctx ? ctx->fromDouble(static_cast<double>(duration) / 1000.0)
               : PROTO_NONE;
}

const proto::ProtoObject* getProfileImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* /*args*/,
    const proto::ProtoSparseList*) {
    if (!ctx) return PROTO_NONE;
    const proto::ProtoObject* profile = ctx->newObject(/*mutable=*/true);
    const proto::ProtoObject* arr = createNewArray(ctx, nullptr);
    const proto::ProtoList* els = ctx->newList();
    for (const auto& e : Profiler::profileEntries) {
        const proto::ProtoObject* entry = ctx->newObject(/*mutable=*/true);
        const proto::ProtoString* nk = ctx->fromUTF8String("name")->asString(ctx);
        const proto::ProtoString* dk = ctx->fromUTF8String("duration")->asString(ctx);
        const proto::ProtoString* mk = ctx->fromUTF8String("memoryDelta")->asString(ctx);
        if (nk) entry->setAttribute(ctx, nk, ctx->fromUTF8String(e.name.c_str()));
        if (dk) entry->setAttribute(ctx, dk, ctx->fromDouble(e.duration));
        if (mk) entry->setAttribute(ctx, mk, ctx->fromInteger(
            static_cast<long long>(e.memoryAfter) - static_cast<long long>(e.memoryBefore)));
        els = els->appendLast(ctx, entry);
    }
    setArrayElements(ctx, arr, els);
    const proto::ProtoString* ek = ctx->fromUTF8String("entries")->asString(ctx);
    const proto::ProtoString* pk = ctx->fromUTF8String("profiling")->asString(ctx);
    if (ek) profile->setAttribute(ctx, ek, arr);
    if (pk) profile->setAttribute(ctx, pk,
        Profiler::profiling ? PROTO_TRUE : PROTO_FALSE);
    return profile;
}

const proto::ProtoObject* getMemoryProfileImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* /*args*/,
    const proto::ProtoSparseList*) {
    if (!ctx) return PROTO_NONE;
    const proto::ProtoObject* profile = ctx->newObject(/*mutable=*/true);
    const proto::ProtoString* ck = ctx->fromUTF8String("current")->asString(ctx);
    if (ck) profile->setAttribute(ctx, ck,
        ctx->fromInteger(static_cast<long long>(Profiler::getMemoryUsage())));
    if (!Profiler::profileEntries.empty()) {
        uint64_t minMem = Profiler::profileEntries[0].memoryBefore;
        uint64_t maxMem = Profiler::profileEntries[0].memoryBefore;
        for (const auto& e : Profiler::profileEntries) {
            if (e.memoryBefore < minMem) minMem = e.memoryBefore;
            if (e.memoryAfter > maxMem)  maxMem = e.memoryAfter;
        }
        const proto::ProtoString* nk = ctx->fromUTF8String("min")->asString(ctx);
        const proto::ProtoString* xk = ctx->fromUTF8String("max")->asString(ctx);
        if (nk) profile->setAttribute(ctx, nk,
            ctx->fromInteger(static_cast<long long>(minMem)));
        if (xk) profile->setAttribute(ctx, xk,
            ctx->fromInteger(static_cast<long long>(maxMem)));
    }
    return profile;
}

}  // namespace

const proto::ProtoObject* Profiler::init(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* globalObj) {
    if (!ctx || !globalObj) return globalObj;
    static const NativeEntry entries[] = {
        {"startProfiling",       startProfilingImpl},
        {"stopProfiling",        stopProfilingImpl},
        {"getProfile",           getProfileImpl},
        {"startMemoryProfiling", startProfilingImpl},   // alias
        {"stopMemoryProfiling",  stopProfilingImpl},    // alias
        {"getMemoryProfile",     getMemoryProfileImpl},
        NATIVE_MODULE_END
    };
    const proto::ProtoObject* mod =
        ProtoNativeModule::buildModule(ctx, entries, 6);
    if (!mod) return globalObj;
    return ProtoNativeModule::registerOnGlobal(ctx, globalObj, "profiler", mod);
}

} // namespace protojs
