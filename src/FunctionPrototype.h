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

/**
 * Wrap a native ProtoMethod in a ProtoObjectCell so that .length and .name
 * attributes can be stored and retrieved correctly.
 *
 * The returned object is a child of Function.prototype and carries:
 *   - __native_fn__  : the raw ProtoMethod pointer
 *   - length         : the parameter count
 *   - name           : the function name string
 *
 * Dispatch sites (OP_call, OP_call_method, callJSFunction) must check for
 * __native_fn__ and call through it.
 *
 * globalRoot may be nullptr; when present, Function.prototype lookup is used
 * as parent for the wrapper.
 */
const proto::ProtoObject* wrapNativeFunction(proto::ProtoContext* ctx,
                                              proto::ProtoMethod fn,
                                              const char* name,
                                              long long length,
                                              const proto::ProtoObject** globalRoot);

} // namespace protojs

#endif // PROTOJS_FUNCTIONPROTOTYPE_H
