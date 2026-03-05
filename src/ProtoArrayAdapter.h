#ifndef PROTOJS_PROTOARRAYADAPTER_H
#define PROTOJS_PROTOARRAYADAPTER_H

#include "headers/protoCore.h"

namespace protojs {

/**
 * Thin adapter over protoCore lists/sparse-lists to represent JS Arrays.
 *
 * All functions are pure and side-effect free outside of protoCore; they never
 * use STL containers or explicit locking. Immutability is preserved by always
 * returning a new root when the array content changes.
 */
class ProtoArrayAdapter {
public:
    /**
     * Create a new empty array backed by a protoCore list.
     */
    static const proto::ProtoObject* createArray(proto::ProtoContext* ctx);

    /**
     * Get the element at the given index.
     *
     * Returns PROTO_NONE if the index is a hole / not present.
     */
    static const proto::ProtoObject* get(
        proto::ProtoContext* ctx,
        const proto::ProtoObject* arrayObj,
        unsigned long index);

    /**
     * Set the element at the given index.
     *
     * Returns the new root array object (immutability). When extending beyond
     * current size, intermediate indices become holes (represented as PROTO_NONE).
     */
    static const proto::ProtoObject* set(
        proto::ProtoContext* ctx,
        const proto::ProtoObject* arrayObj,
        unsigned long index,
        const proto::ProtoObject* value);

    /**
     * Return the logical length of the array.
     */
    static unsigned long length(
        proto::ProtoContext* ctx,
        const proto::ProtoObject* arrayObj);

    /**
     * Truncate or extend the array to newLen.
     *
     * Returns the new root array object (immutability). Truncation drops
     * elements >= newLen; extension leaves new slots as holes.
     */
    static const proto::ProtoObject* setLength(
        proto::ProtoContext* ctx,
        const proto::ProtoObject* arrayObj,
        unsigned long newLen);
};

} // namespace protojs

#endif // PROTOJS_PROTOARRAYADAPTER_H

