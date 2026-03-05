#ifndef PROTOJS_PROTOARGUMENTSADAPTER_H
#define PROTOJS_PROTOARGUMENTSADAPTER_H

#include "headers/protoCore.h"

namespace protojs {

/**
 * Thin adapter over protoCore to represent JS Arguments objects.
 *
 * Representation strategy (pure protoCore):
 * - Backing object is a ProtoObject with two attributes:
 *   - "values": ProtoSparseList containing indexed argument values.
 *   - "length": numeric length as ProtoObject.
 *
 * This mirrors the observable state of an Arguments object without
 * reimplementing QuickJS's internal [[ParameterMap]] logic.
 */
class ProtoArgumentsAdapter {
public:
    /**
     * Create a new empty arguments backing object.
     */
    static const proto::ProtoObject* createArguments(proto::ProtoContext* ctx);

    /**
     * Get the argument value at the given index.
     *
     * Returns PROTO_NONE if the index is a hole / not present.
     */
    static const proto::ProtoObject* get(
        proto::ProtoContext* ctx,
        const proto::ProtoObject* argsObj,
        unsigned long index);

    /**
     * Set the argument value at the given index.
     *
     * Returns the new root arguments object (immutability). When extending
     * beyond current size, intermediate indices become holes.
     */
    static const proto::ProtoObject* set(
        proto::ProtoContext* ctx,
        const proto::ProtoObject* argsObj,
        unsigned long index,
        const proto::ProtoObject* value);

    /**
     * Return the logical length of the arguments object.
     */
    static unsigned long length(
        proto::ProtoContext* ctx,
        const proto::ProtoObject* argsObj);

    /**
     * Set the logical length. For now this only updates the stored length
     * field; QuickJS semantics for truncation are handled on the JS side.
     */
    static const proto::ProtoObject* setLength(
        proto::ProtoContext* ctx,
        const proto::ProtoObject* argsObj,
        unsigned long newLen);
};

} // namespace protojs

#endif // PROTOJS_PROTOARGUMENTSADAPTER_H

