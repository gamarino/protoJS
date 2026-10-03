#ifndef PROTOJS_ARRAYPROTOTYPE_H
#define PROTOJS_ARRAYPROTOTYPE_H

#include "protoCore.h"

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
/** One step of an Array Iterator: true when done, else `value` is set and
 *  the iterator advanced.  for-of uses it to skip the result object. */
bool arrayIteratorStep(proto::ProtoContext* ctx, const proto::ProtoObject* iterator,
                       const proto::ProtoObject*& value);
/** True for the native next() of Array Iterators. */
bool isArrayIteratorNext(proto::ProtoMethod m);

const proto::ProtoObject* createNewArray(proto::ProtoContext* ctx,
                                         const proto::ProtoObject* arrayProto);

/**
 * The values of an iterable or array-like object, as Array.from(...args)
 * collects them (args: items[, mapFn[, thisArg]]): the iterator protocol when
 * items has a Symbol.iterator method, otherwise its length and indexed
 * elements.  Returns nullptr with the exception signalled (hasCallException)
 * when Array.from throws.  Used by the %TypedArray% constructors and
 * %TypedArray%.from, whose source-list steps (ECMA-262 §23.2.5.1.4,
 * §23.2.2.1) are the same iteration.
 */
const proto::ProtoList* collectArrayFromValues(proto::ProtoContext* ctx,
                                               const proto::ProtoList* args);

} // namespace protojs

#endif // PROTOJS_ARRAYPROTOTYPE_H
