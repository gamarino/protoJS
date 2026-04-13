#ifndef PROTOJS_SETPROTOTYPE_H
#define PROTOJS_SETPROTOTYPE_H

#include "headers/protoCore.h"

namespace protojs {

/**
 * Build the JS Set prototype (add, has, delete, clear, forEach,
 * keys, values, entries, size getter) as a child of objectProto.
 * Stores the prototype in a module-level variable for ensureSetConstructor.
 */
void BuildSetPrototype(proto::ProtoSpace* space, proto::ProtoContext* ctx,
                       const proto::ProtoObject* objectProto);

/**
 * Register the Set constructor in the global root.
 * Idempotent — no-op when "Set" is already present.
 */
void ensureSetConstructor(proto::ProtoContext* ctx,
                          const proto::ProtoObject** globalRoot);

} // namespace protojs

#endif // PROTOJS_SETPROTOTYPE_H
