#ifndef PROTOJS_STRINGPROTOTYPE_H
#define PROTOJS_STRINGPROTOTYPE_H

#include "headers/protoCore.h"

namespace protojs {

/**
 * Register String.prototype methods on space->stringPrototype.
 * Inherits from any existing protoCore string prototype, then adds methods
 * for the JS standard built-ins.  Safe to call multiple times (idempotent via
 * the __string_proto_built__ sentinel attribute).
 */
void BuildStringPrototype(proto::ProtoSpace* space, proto::ProtoContext* ctx,
                          const proto::ProtoObject* objectProto);

/**
 * Ensure the String constructor is registered in the global root.
 * Adds String.fromCharCode and String.fromCodePoint as static methods.
 * Idempotent — no-op when "String" is already present.
 */
void ensureStringConstructor(proto::ProtoContext* ctx,
                             const proto::ProtoObject** globalRoot);

} // namespace protojs

#endif // PROTOJS_STRINGPROTOTYPE_H
