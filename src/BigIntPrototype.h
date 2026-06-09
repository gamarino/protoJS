#ifndef PROTOJS_BIGINTPROTOTYPE_H
#define PROTOJS_BIGINTPROTOTYPE_H

#include "headers/protoCore.h"

namespace protojs {

// BigInt representation in protoJS.
//
// Every BigInt JS value is a heap-allocated object whose:
//   * prototype is BigInt.prototype (carrying the marker __is_bigint__
//     so type-checks via the chain succeed without per-instance storage)
//   * own attribute __bigint_value__ holds a protoCore Integer — either
//     a tagged SmallInt (range ±2^53) or a heap LargeInteger
//
// We reuse protoCore's existing arbitrary-precision integer machinery
// (add/subtract/multiply/divide/modulo, bitwise, shifts, fromString,
// asIntegerString) so BigInt arithmetic is bignum-correct without
// reimplementing the underlying math.  protoJS's own Number primitive
// also uses SmallInt/LargeInteger as backing — the distinction between
// Number and BigInt is purely the __is_bigint__ marker.
//
// Spec coverage: §21.2 (BigInt object), §6.1.6.2 (BigInt type),
// §7.1.13 (ToBigInt), §7.1.13.1 (NumericToBigInt), §13.5.3 (typeof).

/**
 * Build the BigInt.prototype object carrying:
 *   __is_bigint__ : PROTO_TRUE          (typeof marker)
 *   constructor : BigInt                 (back-reference)
 *   toString : <native, length 0>
 *   valueOf  : <native, length 0>
 *   [Symbol.toPrimitive] : <native>
 *   [Symbol.toStringTag] : "BigInt"
 *
 * Attached as bigIntegerPrototype on the space.  Idempotent.
 */
void buildBigIntPrototype(proto::ProtoSpace* space, proto::ProtoContext* ctx,
                          const proto::ProtoObject* objectProto);

/**
 * Install the BigInt constructor on globalRoot.  Idempotent — checks
 * for an existing "BigInt" entry before installing.  Supports:
 *
 *   BigInt(v)         — coerce v to BigInt (number, string, boolean, bigint)
 *   BigInt(largeStr)  — parse arbitrarily large decimal/hex
 *   BigInt.asIntN(width, v)
 *   BigInt.asUintN(width, v)
 *
 * `new BigInt(...)` throws TypeError per §21.2.1.1.
 */
void ensureBigIntConstructor(proto::ProtoContext* ctx,
                             const proto::ProtoObject** globalRoot);

// ---------------------------------------------------------------------
// Boxing / unboxing helpers — exported so the runtime arithmetic
// dispatch (L_OP_add, L_OP_subtract, ...) and TypeBridge can mark
// values and the @@toPrimitive chain can inspect them.
// ---------------------------------------------------------------------

/** True if v is a BigInt JS value (carries __is_bigint__ via chain). */
bool isBigInt(proto::ProtoContext* ctx, const proto::ProtoObject* v);

/**
 * Extract the underlying protoCore Integer from a BigInt wrapper.
 * Returns PROTO_NONE if v is not a BigInt.  The returned Integer is
 * either a tagged SmallInt or a heap LargeInteger.
 */
const proto::ProtoObject* unwrapBigInt(proto::ProtoContext* ctx,
                                       const proto::ProtoObject* v);

/**
 * Wrap a protoCore Integer into a BigInt JS value (a fresh object with
 * BigInt.prototype as parent and __bigint_value__ = integer).  Returns
 * PROTO_NONE on failure.
 */
const proto::ProtoObject* wrapBigInt(proto::ProtoContext* ctx,
                                     const proto::ProtoObject* integer);

} // namespace protojs

#endif // PROTOJS_BIGINTPROTOTYPE_H
