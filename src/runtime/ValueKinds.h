#pragma once

// Value-kind tests built from protoCore's public API only (headers/
// protoCore.h). protoJS never includes protoCore's private header and never
// copies its pointer tags, embedded types or cell layouts; the CTest case
// source/no-protocore-internals enforces it.
//
// Two documented facts of the public API carry the composition below:
//  - ProtoObject::getPrototype answers, for every value that is not an
//    object cell, the ProtoSpace prototype of its kind (listPrototype,
//    doublePrototype, ...); for an object cell it answers the object's first
//    parent, or objectPrototype when it has none.
//  - ProtoObject::getFirstParent answers PROTO_NONE for every value that is
//    not an object cell, and for a parentless object.
// Several kinds' ProtoSpace prototypes are aliases of objectPrototype (see
// isAliasedCellKind), so prototype identity is used only for kinds with a
// prototype of their own (lists, Unicode characters).

#include <protoCore.h>

namespace protojs {

// protoCore leaves the ProtoSpace prototype of several cell kinds aliased to
// objectPrototype (LargeIntegers, doubles, methods, byte buffers, threads,
// external pointers and buffers), so getPrototype cannot tell such a value
// from a parentless object. The public tag-only predicates do.
inline bool isAliasedCellKind(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    return v->isDouble(ctx) || v->isInteger(ctx) || v->isMethod(ctx) || v->isByteBuffer(ctx) ||
           v->asThread(ctx) != nullptr || v->asExternalPointer(ctx) != nullptr ||
           v->asExternalBuffer(ctx) != nullptr;
}

/** True when `v` is a protoCore object cell (an ordinary object, a function
 *  object, a Symbol or BigInt cell, a sentinel object, ...): not a
 *  SmallInteger, string, double, collection, method or other tagged value.
 *  Inline rejects first; then the common case, an object with a parent, is
 *  decided by one call. */
inline bool isObjectCell(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    if (!v || proto::isSmallInt(v) || proto::ProtoObject::isStringTagFast(v)) return false;
    if (v->getFirstParent(ctx) != PROTO_NONE) return true;
    if (!proto::ProtoObject::isCellPointer(v)) return false;
    const proto::ProtoObject* p = v->getPrototype(ctx);
    if (p != nullptr && p != ctx->space->objectPrototype) return false;
    return !isAliasedCellKind(ctx, v);
}

/** True when `v` is a protoCore double (ProtoObject::isDouble is a tag-only
 *  test behind one call). */
inline bool isDoubleValue(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    return v && !proto::isSmallInt(v) && v->isDouble(ctx);
}

} // namespace protojs
