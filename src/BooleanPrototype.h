#ifndef PROTOJS_BOOLEANPROTOTYPE_H
#define PROTOJS_BOOLEANPROTOTYPE_H

#include "headers/protoCore.h"

namespace protojs {

/**
 * Build the JS Boolean prototype (valueOf, toString) and attach to
 * space->booleanPrototype as a child of objectProto.
 */
void BuildBooleanPrototype(proto::ProtoSpace* space, proto::ProtoContext* ctx,
                           const proto::ProtoObject* objectProto);

/**
 * Register the Boolean constructor in the global root.
 * Idempotent — no-op when "Boolean" is already present.
 */
void ensureBooleanConstructor(proto::ProtoContext* ctx,
                              const proto::ProtoObject** globalRoot);

} // namespace protojs

#endif // PROTOJS_BOOLEANPROTOTYPE_H
