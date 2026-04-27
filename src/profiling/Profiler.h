#ifndef PROTOJS_PROFILER_H
#define PROTOJS_PROFILER_H

#include "headers/protoCore.h"
#include <string>
#include <chrono>
#include <vector>

namespace protojs {

/**
 * @brief Performance profiler for protoJS — CPU + memory snapshots,
 *        all read-only.  Migrated to protoCore-native (no QuickJS
 *        bridge); see docs/MIGRATION_QUICKJS_TO_PROTOCORE.md.
 */
class Profiler {
public:
    static const proto::ProtoObject* init(
        proto::ProtoContext* ctx,
        const proto::ProtoObject* globalObj);

    // Public state model — kept on the class for binary-compat with
    // any internal code that wants to record entries without going
    // through JS (currently none, but the original module exposed
    // these as public).
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
