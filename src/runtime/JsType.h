#pragma once

// The ECMAScript language type (§6.1) of a protoJS value, decided by the
// JavaScript type model, not by protoCore's representation.
//
// protoJS carries the JavaScript primitives as follows:
//  - undefined: nullptr, PROTO_NONE or the undefined sentinel; null: the null
//    sentinel;
//  - Boolean: PROTO_TRUE / PROTO_FALSE (protoCore's only booleans);
//  - Number: a protoCore integer (SmallInteger or LargeInteger) or double;
//  - String: a protoCore string; the well-known Symbols are carried as
//    "Symbol.<name>" strings and are Symbols;
//  - Symbol: a value that carries the __is_symbol__ marker;
//  - BigInt: a value with its own __bigint_value__ slot (the marker
//    __is_bigint__ on BigInt.prototype does not make an object inheriting
//    from it a BigInt).
// Every other value is an Object, whatever protoCore cell carries it:
// ordinary objects, functions and methods, wrappers, Proxies.
//
// Strings are recognised by protoCore's tag-only test, never by
// ProtoObject::isString, which also reads an object's __data__ attribute: an
// object with a __data__ property is still an Object.

#include <protoCore.h>

namespace protojs {

enum class JsType : unsigned char { Undefined, Null, Boolean, Number, String, Symbol, BigInt, Object };

/** Type(v). Functions are Objects; typeof tells them apart itself. */
JsType jsTypeOf(proto::ProtoContext* ctx, const proto::ProtoObject* v);

/** Type(v) is Object. Equal to jsTypeOf(ctx, v) == JsType::Object, without
 *  telling strings from the well-known Symbols carried as strings. */
bool jsIsObject(proto::ProtoContext* ctx, const proto::ProtoObject* v);

/** The typeof name of a primitive type ("undefined", "object" for null,
 *  "boolean", ...); "object" for JsType::Object. */
const char* jsTypeName(JsType t);

}  // namespace protojs
