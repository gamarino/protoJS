#ifndef PROTOJS_HASHED_COLLECTION_H
#define PROTOJS_HASHED_COLLECTION_H

/**
 * Storage of JavaScript Map and Set (src/MapPrototype.cpp,
 * src/SetPrototype.cpp) on protoCore's hashed-collection helper.
 *
 * A collection's whole state is one immutable record, held in one hidden
 * attribute of the Map or Set object, so every mutation publishes exactly one
 * new snapshot of the object:
 *
 *   [entries, index, next, size]
 *   - entries: ProtoSparseList, insertion slot -> the element (a Set) or a
 *     two-element list [key, value] (a Map).  Slots only grow: a delete leaves
 *     a hole and a re-added key takes a new slot at the end, which is the
 *     iteration order ECMA-262 requires (insertion order, entries added during
 *     an iteration visited, deleted ones skipped).
 *   - index: ProtoMap from key to slot, driven by hashedPut / hashedGet /
 *     hashedRemove with SameValueZero semantics (jsKeySemantics): NaN equals
 *     NaN, +0 equals -0, numbers by value, strings by content, BigInts by
 *     value, everything else by identity.
 *   - next: the slot the next new key takes; size: the number of entries.
 *
 * Every operation is O(log n): lookups probe the index, never the entries.
 */

#include "protoCore.h"

namespace protojs {
namespace coll {

/** SameValueZero, the equality of Map keys and Set elements. */
bool sameValueZero(proto::ProtoContext* ctx, const proto::ProtoObject* a,
                   const proto::ProtoObject* b);

/** -0 becomes +0 (ECMA-262 Map.prototype.set / Set.prototype.add step). */
const proto::ProtoObject* normalizeKey(proto::ProtoContext* ctx, const proto::ProtoObject* key);

/** The KeySemantics handed to protoCore's hashed-collection helper. */
const proto::KeySemantics& jsKeySemantics();

struct State {
    const proto::ProtoSparseList* entries = nullptr;
    const proto::ProtoMap* index = nullptr;
    proto::proto_ulong next = 0;
    proto::proto_ulong size = 0;
};

/** An empty state. */
State emptyState(proto::ProtoContext* ctx);

/** Reads the state stored under `slot` on `obj`; false when there is none
 *  (`obj` is not a collection of that kind). */
bool load(proto::ProtoContext* ctx, const proto::ProtoObject* obj,
          const proto::ProtoString* slot, State& out);

/** Publishes `st` as `obj`'s state (one attribute write). */
void store(proto::ProtoContext* ctx, const proto::ProtoObject* obj,
           const proto::ProtoString* slot, const State& st);

/** The slot holding `key` (already normalised), if present. */
bool find(proto::ProtoContext* ctx, const State& st, const proto::ProtoObject* key,
          proto::proto_ulong& slot);

/** Appends `entry` for `key` (absent, normalised) at slot `st.next`. */
void insert(proto::ProtoContext* ctx, State& st, const proto::ProtoObject* key,
            const proto::ProtoObject* entry);

/** Replaces the entry at `slot` (the key keeps its slot). */
void replace(proto::ProtoContext* ctx, State& st, proto::proto_ulong slot,
             const proto::ProtoObject* entry);

/** Removes `key` (normalised) found at `slot`. */
void remove(proto::ProtoContext* ctx, State& st, const proto::ProtoObject* key,
            proto::proto_ulong slot);

/** The first used slot >= `from`, or false when there is none. */
bool nextUsedSlot(proto::ProtoContext* ctx, const State& st, proto::proto_ulong from,
                  proto::proto_ulong& slot);

/** A Map entry [key, value], and its parts. */
const proto::ProtoObject* makePair(proto::ProtoContext* ctx, const proto::ProtoObject* key,
                                   const proto::ProtoObject* value);
const proto::ProtoObject* pairKey(proto::ProtoContext* ctx, const proto::ProtoObject* pair);
const proto::ProtoObject* pairValue(proto::ProtoContext* ctx, const proto::ProtoObject* pair);

}  // namespace coll
}  // namespace protojs

#endif  // PROTOJS_HASHED_COLLECTION_H
