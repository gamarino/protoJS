#ifndef PROTOJS_REGEXPPROTOTYPE_H
#define PROTOJS_REGEXPPROTOTYPE_H

#include "protoCore.h"
#include <string>

namespace protojs {

/**
 * Register RegExp.prototype methods on regexpProto.
 * Returns the updated prototype object (due to immutability).
 */
const proto::ProtoObject* BuildRegExpPrototype(proto::ProtoSpace* space, proto::ProtoContext* ctx,
                                               const proto::ProtoObject* regexpProto);

/**
 * Ensure the RegExp constructor is registered in the global root.
 * Idempotent — no-op when "RegExp" is already present.
 */
void ensureRegExpConstructor(proto::ProtoContext* ctx,
                             const proto::ProtoObject** globalRoot);

/**
 * Native RegExp constructor logic.
 */
const proto::ProtoObject* regexpConstructor(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink* parent, const proto::ProtoList* args,
    const proto::ProtoSparseList* kwargs);

/**
 * Core exec logic — shared with RegExpStringIterator.
 */
const proto::ProtoObject* regexpExec(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink* parent, const proto::ProtoList* args,
    const proto::ProtoSparseList* kwargs);

/**
 * After an empty match of a global walk over `input` (UTF-8): set re.lastIndex
 * to AdvanceStringIndex(input, lastIndex, fullUnicode) (ECMA-262 §22.2.7.3),
 * i.e. one code point on with /u or /v, one code unit otherwise.
 */
void regexpAdvanceAfterEmptyMatch(proto::ProtoContext* ctx,
                                  const proto::ProtoObject* re,
                                  const std::string& input);

} // namespace protojs

#endif // PROTOJS_REGEXPPROTOTYPE_H
