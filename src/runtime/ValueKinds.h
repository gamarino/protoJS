#pragma once

// Value-kind tests built from protoCore's public API only (headers/
// protoCore.h). protoJS never includes protoCore's private header and never
// copies its pointer tags, embedded types or cell layouts; the CTest case
// source/no-protocore-internals enforces it.
//
// protoJS does not ask whether a value is a "plain object cell": protoCore
// answers getAttribute, hasOwnAttribute and the other lookups on every value
// through its prototype, so the runtime looks up or dispatches directly. Where
// the language itself asks about a value's type (Type(x) is Object, typeof),
// the JavaScript type model of runtime/JsType.h answers.

#include <protoCore.h>

namespace protojs {

/** True when `v` is a protoCore double (ProtoObject::isDouble is a tag-only
 *  test behind one call). */
inline bool isDoubleValue(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    return v && !proto::isSmallInt(v) && v->isDouble(ctx);
}

} // namespace protojs
