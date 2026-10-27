#ifndef PROTOJS_STRINGPROTOTYPE_H
#define PROTOJS_STRINGPROTOTYPE_H

#include "protoCore.h"

namespace protojs {

/**
 * Register String.prototype methods on space->stringPrototype.
 * Inherits from any existing protoCore string prototype, then adds methods
 * for the JS standard built-ins.  Safe to call multiple times (idempotent via
 * the __string_proto_built__ sentinel attribute).
 */
void BuildStringPrototype(proto::ProtoSpace* space, proto::ProtoContext* ctx,
                          const proto::ProtoObject* objectProto);

/** Re-installs String.prototype methods using installNonEnumerableMethod so each
 *  wrapper inherits from methodPrototype and exposes .call/.apply/.bind.
 *  Must be called AFTER ensureFunctionPrototype() sets ctx->space->methodPrototype. */
void ReinstallStringPrototypeMethods(proto::ProtoContext* ctx);

/**
 * Ensure the String constructor is registered in the global root.
 * Adds String.fromCharCode and String.fromCodePoint as static methods.
 * Idempotent — no-op when "String" is already present.
 */
void ensureStringConstructor(proto::ProtoContext* ctx,
                             const proto::ProtoObject** globalRoot);

/**
 * The code point at `index` of `str` (protoCore indexes strings by code
 * point) as a one-code-point string, or nullptr when `index` is out of range.
 *
 * ProtoString::getAt walks the rope in O(log n); the result is an inline
 * string, so the call allocates no cell. It replaces converting the whole
 * string to UTF-8 and walking the bytes up to `index`, which made every
 * indexed read O(n) and a loop over a string O(n^2).
 */
const proto::ProtoObject* codePointStringAt(proto::ProtoContext* ctx,
                                            const proto::ProtoString* str,
                                            long long index);

/**
 * One step of a built-in String Iterator (String.prototype[Symbol.iterator])
 * without the {value, done} result object: answers true when the iterator is
 * done, otherwise sets `value` to the next code point's string and advances
 * the iterator, exactly as %StringIteratorPrototype%.next does.
 */
bool stringIteratorStep(proto::ProtoContext* ctx, const proto::ProtoObject* iterator,
                        const proto::ProtoObject*& value);

/** True when `fn` is %StringIteratorPrototype%.next, the built-in. */
bool isStringIteratorNext(proto::ProtoContext* ctx, const proto::ProtoObject* fn);

} // namespace protojs

#endif // PROTOJS_STRINGPROTOTYPE_H
