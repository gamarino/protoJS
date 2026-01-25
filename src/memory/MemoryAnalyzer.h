#ifndef PROTOJS_MEMORYANALYZER_H
#define PROTOJS_MEMORYANALYZER_H

#include "quickjs.h"
#include <string>
#include <map>
#include <vector>
#include <ctime>

namespace protojs {

/**
 * @brief Memory Analyzer for heap snapshots, leak detection, and allocation tracking
 * 
 * Provides memory analysis capabilities similar to Chrome DevTools memory profiler.
 */
class MemoryAnalyzer {
public:
    /**
     * @brief Heap snapshot structure
     */
    struct HeapSnapshot {
        time_t timestamp;
        size_t totalSize;
        std::map<std::string, size_t> objectCounts;
        std::map<std::string, size_t> memoryUsage;
        JSMemoryUsage jsMemoryUsage;
    };
    
    /**
     * @brief Leak detection result
     */
    struct LeakReport {
        std::vector<std::string> leakedTypes;
        std::map<std::string, size_t> leakCounts;
        std::map<std::string, size_t> leakSizes;
        size_t totalLeakedSize;
    };
    
    /**
     * @brief Initialize the memory analyzer module
     */
    static void init(JSContext* ctx);
    
    /**
     * @brief Take a heap snapshot
     */
    static JSValue takeHeapSnapshot(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    
    /**
     * @brief Detect memory leaks by comparing two snapshots
     */
    static JSValue detectLeaks(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    
    /**
     * @brief Export snapshot to Chrome DevTools format
     */
    static JSValue exportSnapshot(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    
    /**
     * @brief Get current memory usage statistics
     */
    static JSValue getMemoryUsage(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    
    /**
     * @brief Start allocation tracking
     */
    static JSValue startAllocationTracking(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
    
    /**
     * @brief Stop allocation tracking and get report
     */
    static JSValue stopAllocationTracking(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

private:
    /**
     * @brief Capture a heap snapshot from the current context
     */
    static HeapSnapshot captureSnapshot(JSContext* ctx);
    
    /**
     * @brief Compare two snapshots and detect leaks
     */
    static LeakReport compareSnapshots(const HeapSnapshot& before, const HeapSnapshot& after);
    
    /**
     * @brief Generate Chrome DevTools heap snapshot JSON
     */
    static std::string generateChromeDevToolsFormat(const HeapSnapshot& snapshot);
    
    /**
     * @brief Convert snapshot to JS object
     */
    static JSValue snapshotToJSObject(JSContext* ctx, const HeapSnapshot& snapshot);
    
    /**
     * @brief Convert leak report to JS object
     */
    static JSValue leakReportToJSObject(JSContext* ctx, const LeakReport& report);
    
    static std::vector<HeapSnapshot> snapshots;
    static bool trackingAllocations;
    static HeapSnapshot trackingStartSnapshot;
};

} // namespace protojs

#endif // PROTOJS_MEMORYANALYZER_H
