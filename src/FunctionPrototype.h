#ifndef PROTOJS_FUNCTIONPROTOTYPE_H
#define PROTOJS_FUNCTIONPROTOTYPE_H

#include "headers/protoCore.h"

namespace protojs {

/**
 * Register Function.prototype (call, apply, bind) in the global root.
 * Stores the prototype at the internal key "__function_proto__" so that
 * OP_get_field can fall back to it when looking up call/apply/bind on any
 * closure or native method object.
 * Idempotent — no-op when "__function_proto__" is already present.
 */
void ensureFunctionPrototype(proto::ProtoContext* ctx,
                              const proto::ProtoObject** globalRoot);

} // namespace protojs

#endif // PROTOJS_FUNCTIONPROTOTYPE_H
