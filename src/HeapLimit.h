#ifndef PROTOJS_HEAP_LIMIT_H
#define PROTOJS_HEAP_LIMIT_H

/**
 * The default heap ceiling of a protoJS space.
 *
 * protoCore's collector runs as the heap approaches the configured ceiling,
 * and the ceiling is also where a live set that does not fit ends the
 * process ("out of memory", exit status 3).  The default is 75 % of the
 * memory the process may use: physical memory, or the cgroup memory limit
 * on Linux when one is set and smaller (containers, systemd MemoryMax).
 * PROTOCORE_HEAP_LIMIT_CELLS overrides it (see src/JSContext.cpp).
 *
 * The functions are separate so the policy (defaultHeapLimitCells) can be
 * tested with any inputs.
 */

namespace protojs {
namespace heaplimit {

constexpr long long kCellBytes = 64;
/** Used when the machine's memory cannot be determined: 640 MB of cells. */
constexpr long long kFallbackCells = 10'000'000;

/** Physical memory in bytes; 0 when unknown. */
unsigned long long physicalMemoryBytes();

/**
 * The tightest cgroup (v2, or v1) memory limit of this process in bytes;
 * 0 when there is none or the platform has no cgroups.
 */
unsigned long long cgroupMemoryLimitBytes();

/**
 * 75 % of the smaller of the non-zero inputs, in 64-byte cells, at most
 * INT_MAX (protoCore's ceiling is an int); kFallbackCells when both are 0.
 */
long long defaultHeapLimitCells(unsigned long long physicalBytes,
                                unsigned long long cgroupLimitBytes);

/** defaultHeapLimitCells for this process. */
long long processDefaultHeapLimitCells();

/**
 * The hard limit of a PROTOCORE_HEAP_LIMIT_CELLS value, "<hard>" or
 * "<soft>,<hard>", parsed as protoCore parses it (decimal digits, at most
 * INT_MAX); -1 when the value is absent or protoCore would ignore it.
 */
long long hardCellsFromEnv(const char* value);

/**
 * Which heap policy a space runs (see src/JSContext.cpp, "Memory policy").
 *
 * Automatic: the fixed hard limit above while the program is sequential;
 * protoCore's adaptive heap controller (ProtoSpace::enableAdaptiveHeap) from
 * the moment the space's DeferredPool starts its first task.  On / Off force
 * the controller on from the start / off for good.
 */
enum class AdaptiveHeapMode { Automatic, On, Off };

/** The mode a PROTOCORE_ADAPTIVE_HEAP value selects: "1" On, "0" Off, else
 *  (absent or any other value, which protoCore ignores too) Automatic. */
AdaptiveHeapMode adaptiveHeapModeFromEnv(const char* value);

/**
 * Whether a space runs the controller, given the mode, whether Deferreds have
 * started, and the PROTOCORE_HEAP_LIMIT_CELLS value.  An explicit hard limit
 * of 0 means "no limit", which is not a budget: Automatic then stays with the
 * fixed (unlimited) policy.
 */
bool adaptiveHeapWanted(AdaptiveHeapMode mode, bool deferredsStarted,
                        const char* heapLimitEnv);

}  // namespace heaplimit
}  // namespace protojs

#endif  // PROTOJS_HEAP_LIMIT_H
