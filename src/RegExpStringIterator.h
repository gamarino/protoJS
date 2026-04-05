#ifndef PROTOJS_REGEXP_STRING_ITERATOR_H
#define PROTOJS_REGEXP_STRING_ITERATOR_H

#include "headers/protoCore.h"

namespace protojs {

/**
 * Creates a RegExpStringIterator for use with String.prototype.matchAll
 * and RegExp.prototype[Symbol.matchAll].
 * Stores __iter_re__, __iter_str__, __iter_done__ as internal attributes.
 */
const proto::ProtoObject* makeRegExpStringIterator(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* regexp,
    const proto::ProtoObject* str);

/**
 * RegExp.prototype[Symbol.matchAll] implementation.
 */
const proto::ProtoObject* regexpSymbolMatchAll(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* parent,
    const proto::ProtoList* args,
    const proto::ProtoSparseList* sparse);

} // namespace protojs

#endif // PROTOJS_REGEXP_STRING_ITERATOR_H
