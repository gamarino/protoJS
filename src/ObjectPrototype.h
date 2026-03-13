#ifndef PROTOJS_OBJECTPROTOTYPE_H
#define PROTOJS_OBJECTPROTOTYPE_H

#include "headers/protoCore.h"

namespace protojs {

/**
 * Ensure the Object constructor with static methods is registered in the global root.
 * Idempotent — no-op when "Object" is already present.
 */
void ensureObjectConstructor(proto::ProtoContext* ctx,
                             const proto::ProtoObject** globalRoot);

} // namespace protojs

#endif // PROTOJS_OBJECTPROTOTYPE_H
