#pragma once

// GcScopedCache -- a per-thread memo whose keys or values are protoCore cells.
//
// The collector does not see C++ containers. A cache keyed by a cell's address
// (a ProtoString*, a ProtoObject*) is therefore only valid while that cell is
// alive: once the cell is collected its address is reused for a new cell, and
// the new cell finds the dead one's entry. That is not a theoretical risk -- a
// key string 'property7' freed and its cell reused by 'unrelated12' made
// o['unrelated12'] read o.property7; a fresh `{}` at the address of a dead
// Object.create(null) reported a null prototype.
//
// A cell can be freed only by a collection's sweep, and protoCore increments
// ProtoSpace::gcCycleCount inside the stop-the-world pause that begins the
// cycle, before that cycle sweeps anything. A key or value the mutator inserted
// was alive at insertion (the mutator held it), so it can be freed only by a
// later cycle, which first advances the count. Clearing the whole cache whenever
// the count observed differs from the count at the last clear therefore drops
// every entry that could refer to a freed cell before it can be read. The
// stop-the-world handshake orders the increment before any reuse of a freed
// cell by this thread, so a relaxed read of the count suffices.
//
// The space is part of the check: a thread that switches spaces must not read
// entries made against another space's cells.
//
// Cost: one load and compare per access; the cache refills after each cycle.
// A cache keyed only by perennial cells (interned symbols, null-context
// allocations) does not need this.

#include <protoCore.h>

#include <cstdint>
#include <unordered_map>

namespace protojs {

template <typename Key, typename Value>
class GcScopedCache {
public:
    // The map, emptied first if a collection has started since the last call.
    std::unordered_map<Key, Value>& get(proto::ProtoContext* ctx) {
        const proto::ProtoSpace* space = ctx ? ctx->space : nullptr;
        const std::uint64_t cycle = space ? space->getGCCycleCount() : 0;
        if (space != space_ || cycle != cycle_) {
            map_.clear();
            space_ = space;
            cycle_ = cycle;
        }
        return map_;
    }

private:
    std::unordered_map<Key, Value> map_;
    const proto::ProtoSpace* space_ = nullptr;
    std::uint64_t cycle_ = 0;
};

// True once per collection cycle (and on the first call, or on a change of
// space): the caller must then drop whatever it caches about cell addresses.
// For caches that are not an unordered_map (a fixed array of slots).
class GcCycleWatch {
public:
    bool changed(proto::ProtoContext* ctx) {
        const proto::ProtoSpace* space = ctx ? ctx->space : nullptr;
        const std::uint64_t cycle = space ? space->getGCCycleCount() : 0;
        if (space == space_ && cycle == cycle_ && primed_) return false;
        space_ = space;
        cycle_ = cycle;
        primed_ = true;
        return true;
    }

private:
    const proto::ProtoSpace* space_ = nullptr;
    std::uint64_t cycle_ = 0;
    bool primed_ = false;
};

}  // namespace protojs
