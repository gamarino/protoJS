#ifndef PROTOJS_MAPPROTOTYPE_H
#define PROTOJS_MAPPROTOTYPE_H

#include "headers/protoCore.h"

namespace protojs {

/**
 * Build the JS Map prototype (get, set, has, delete, clear, forEach,
 * keys, values, entries, size getter) as a child of objectProto.
 * Stores the prototype in a module-level variable for ensureMapConstructor.
 */
void BuildMapPrototype(proto::ProtoSpace* space, proto::ProtoContext* ctx,
                       const proto::ProtoObject* objectProto);

/**
 * Register the Map constructor in the global root.
 * Idempotent — no-op when "Map" is already present.
 */
void ensureMapConstructor(proto::ProtoContext* ctx,
                          const proto::ProtoObject** globalRoot);

/**
 * Register the WeakMap constructor in the global root.
 * Idempotent — no-op when "WeakMap" is already present.
 */
void ensureWeakMapConstructor(proto::ProtoContext* ctx,
                               const proto::ProtoObject** globalRoot);

} // namespace protojs

#endif // PROTOJS_MAPPROTOTYPE_H
