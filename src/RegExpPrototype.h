#ifndef PROTOJS_REGEXPPROTOTYPE_H
#define PROTOJS_REGEXPPROTOTYPE_H

#include "headers/protoCore.h"

namespace protojs {

/**
 * Register RegExp.prototype methods on regexpProto.
 * Returns the updated prototype object (due to immutability).
 */
const proto::ProtoObject* BuildRegExpPrototype(proto::ProtoSpace* space, proto::ProtoContext* ctx,
                                               const proto::ProtoObject* regexpProto);

/**
 * Ensure the RegExp constructor is registered in the global root.
 * Idempotent — no-op when "RegExp" is already present.
 */
void ensureRegExpConstructor(proto::ProtoContext* ctx,
                             const proto::ProtoObject** globalRoot);

/**
 * Native RegExp constructor logic.
 */
const proto::ProtoObject* regexpConstructor(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink* parent, const proto::ProtoList* args,
    const proto::ProtoSparseList* kwargs);

/**
 * Core exec logic — shared with RegExpStringIterator.
 */
const proto::ProtoObject* regexpExec(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink* parent, const proto::ProtoList* args,
    const proto::ProtoSparseList* kwargs);

} // namespace protojs

#endif // PROTOJS_REGEXPPROTOTYPE_H
