#ifndef PROTOJS_PROFILER_H
#define PROTOJS_PROFILER_H

#include "quickjs.h"
#include <string>
#include <map>
#include <chrono>
#include <vector>

namespace protojs {

/**
 * @brief Performance profiler for protoJS
 * 
 * Provides CPU and memory profiling capabilities.
 */
class Profiler {
public:
    static void init(JSContext* ctx);
    
    // Profiling control
    static JSValue startProfiling(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue stopProfiling(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue getProfile(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    
    // Memory profiling
    static JSValue startMemoryProfiling(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue stopMemoryProfiling(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    static JSValue getMemoryProfile(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

private:
    struct ProfileEntry {
        std::string name;
        double startTime;
        double endTime;
        double duration;
        uint64_t memoryBefore;
        uint64_t memoryAfter;
    };
    
    static std::vector<ProfileEntry> profileEntries;
    static bool profiling;
    static std::chrono::high_resolution_clock::time_point profileStart;
    
    static uint64_t getMemoryUsage();
};

} // namespace protojs

#endif // PROTOJS_PROFILER_H
