#ifndef PROTOJS_ARRAYPROTOTYPE_H
#define PROTOJS_ARRAYPROTOTYPE_H

#include "headers/protoCore.h"

namespace protojs {

/**
 * Ensure the Array constructor and Array.prototype are registered in the
 * global root.  Safe to call multiple times — it is a no-op when "Array"
 * is already present.  Must be called before any JavaScript code that
 * creates arrays runs.
 *
 * Registers on *globalRoot:
 *   "Array"            → constructor object (has __array_ctor__ + prototype)
 *   "__array_proto__"  → Array.prototype object (for fast OP_array_from lookup)
 */
void ensureArrayPrototype(proto::ProtoContext* ctx,
                          const proto::ProtoObject** globalRoot);

/**
 * Create a new empty JS array that inherits from Array.prototype.
 * Equivalent to [] in JavaScript.  If arrayProto is null, falls back to
 * a plain mutable object.
 */
const proto::ProtoObject* createNewArray(proto::ProtoContext* ctx,
                                         const proto::ProtoObject* arrayProto);

} // namespace protojs

#endif // PROTOJS_ARRAYPROTOTYPE_H
