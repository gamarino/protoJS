#ifndef PROTOJS_FUNCTIONPROTOTYPE_H
#define PROTOJS_FUNCTIONPROTOTYPE_H

#include "protoCore.h"

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

/**
 * [[Call]] / [[Construct]] of %GeneratorFunction%, %AsyncFunction% and
 * %AsyncGeneratorFunction% (ECMA-262 §27.3.1.1, §27.7.1.1, §27.4.1.1):
 * CreateDynamicFunction for a generator, async or async generator function.
 * The constructor objects themselves are built by the interpreter with the
 * other function-kind intrinsics (functionKindIntrinsic in ProtoInterpreter).
 */
const proto::ProtoObject* generatorFunctionConstructorCall(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink* parent,
    const proto::ProtoList* args, const proto::ProtoSparseList* named);
const proto::ProtoObject* asyncFunctionConstructorCall(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink* parent,
    const proto::ProtoList* args, const proto::ProtoSparseList* named);
const proto::ProtoObject* asyncGeneratorFunctionConstructorCall(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink* parent,
    const proto::ProtoList* args, const proto::ProtoSparseList* named);

} // namespace protojs

#endif // PROTOJS_FUNCTIONPROTOTYPE_H
