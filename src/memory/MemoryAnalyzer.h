#ifndef PROTOJS_MEMORYANALYZER_H
#define PROTOJS_MEMORYANALYZER_H

#include "headers/protoCore.h"
#include <string>
#include <map>
#include <vector>
#include <ctime>

namespace protojs {

/**
 * @brief Memory analyzer — heap snapshots, leak detection, allocation
 *        tracking against the protoCore heap.  Migrated to
 *        protoCore-native: stats come from ProtoSpace::{heapSize,
 *        freeCellsCount} instead of QuickJS's JS_ComputeMemoryUsage.
 *        See docs/MIGRATION_QUICKJS_TO_PROTOCORE.md.
 */
class MemoryAnalyzer {
public:
    struct HeapSnapshot {
        time_t timestamp;
        size_t totalSize;
        std::map<std::string, size_t> objectCounts;
        std::map<std::string, size_t> memoryUsage;
    };

    struct LeakReport {
        std::vector<std::string> leakedTypes;
        std::map<std::string, size_t> leakCounts;
        std::map<std::string, size_t> leakSizes;
        size_t totalLeakedSize;
    };

    static const proto::ProtoObject* init(
        proto::ProtoContext* ctx,
        const proto::ProtoObject* globalObj);

    static std::vector<HeapSnapshot> snapshots;
    static bool trackingAllocations;
    static HeapSnapshot trackingStartSnapshot;
};

} // namespace protojs

#endif // PROTOJS_MEMORYANALYZER_H
