#include "Profiler.h"
#include "../JSContext.h"
#include <iostream>
#include <sstream>
#include <iomanip>

namespace protojs {

std::vector<Profiler::ProfileEntry> Profiler::profileEntries;
bool Profiler::profiling = false;
std::chrono::high_resolution_clock::time_point Profiler::profileStart;

void Profiler::init(JSContext* ctx) {
    JSValue profilerModule = JS_NewObject(ctx);
    
    JS_SetPropertyStr(ctx, profilerModule, "startProfiling", JS_NewCFunction(ctx, startProfiling, "startProfiling", 0));
    JS_SetPropertyStr(ctx, profilerModule, "stopProfiling", JS_NewCFunction(ctx, stopProfiling, "stopProfiling", 0));
    JS_SetPropertyStr(ctx, profilerModule, "getProfile", JS_NewCFunction(ctx, getProfile, "getProfile", 0));
    JS_SetPropertyStr(ctx, profilerModule, "startMemoryProfiling", JS_NewCFunction(ctx, startMemoryProfiling, "startMemoryProfiling", 0));
    JS_SetPropertyStr(ctx, profilerModule, "stopMemoryProfiling", JS_NewCFunction(ctx, stopMemoryProfiling, "stopMemoryProfiling", 0));
    JS_SetPropertyStr(ctx, profilerModule, "getMemoryProfile", JS_NewCFunction(ctx, getMemoryProfile, "getMemoryProfile", 0));
    
    JSValue global_obj = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global_obj, "profiler", profilerModule);
    JS_FreeValue(ctx, global_obj);
}

JSValue Profiler::startProfiling(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (profiling) {
        return JS_NewBool(ctx, false);
    }
    
    profiling = true;
    profileStart = std::chrono::high_resolution_clock::now();
    profileEntries.clear();
    
    return JS_NewBool(ctx, true);
}

JSValue Profiler::stopProfiling(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (!profiling) {
        return JS_NewBool(ctx, false);
    }
    
    profiling = false;
    auto endTime = std::chrono::high_resolution_clock::now();
    auto duration = std::chrono::duration_cast<std::chrono::microseconds>(endTime - profileStart).count();
    
    return JS_NewFloat64(ctx, duration / 1000.0); // Return duration in milliseconds
}

JSValue Profiler::getProfile(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    JSValue profile = JS_NewObject(ctx);
    
    JSValue entries = JS_NewArray(ctx);
    for (size_t i = 0; i < profileEntries.size(); i++) {
        JSValue entry = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, entry, "name", JS_NewString(ctx, profileEntries[i].name.c_str()));
        JS_SetPropertyStr(ctx, entry, "duration", JS_NewFloat64(ctx, profileEntries[i].duration));
        JS_SetPropertyStr(ctx, entry, "memoryDelta", JS_NewInt64(ctx, 
            static_cast<int64_t>(profileEntries[i].memoryAfter) - static_cast<int64_t>(profileEntries[i].memoryBefore)));
        JS_SetPropertyUint32(ctx, entries, i, entry);
    }
    JS_SetPropertyStr(ctx, profile, "entries", entries);
    JS_SetPropertyStr(ctx, profile, "profiling", JS_NewBool(ctx, profiling));
    
    return profile;
}

JSValue Profiler::startMemoryProfiling(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return startProfiling(ctx, this_val, argc, argv);
}

JSValue Profiler::stopMemoryProfiling(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    return stopProfiling(ctx, this_val, argc, argv);
}

JSValue Profiler::getMemoryProfile(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    JSValue profile = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, profile, "current", JS_NewInt64(ctx, getMemoryUsage()));
    
    if (!profileEntries.empty()) {
        uint64_t minMem = profileEntries[0].memoryBefore;
        uint64_t maxMem = profileEntries[0].memoryBefore;
        for (const auto& entry : profileEntries) {
            if (entry.memoryBefore < minMem) minMem = entry.memoryBefore;
            if (entry.memoryAfter > maxMem) maxMem = entry.memoryAfter;
        }
        JS_SetPropertyStr(ctx, profile, "min", JS_NewInt64(ctx, minMem));
        JS_SetPropertyStr(ctx, profile, "max", JS_NewInt64(ctx, maxMem));
    }
    
    return profile;
}

uint64_t Profiler::getMemoryUsage() {
    // Basic memory usage estimation
    // In a real implementation, would query system memory or QuickJS memory stats
    return 0; // Placeholder
}

} // namespace protojs
