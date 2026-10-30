#pragma once

// AccessorFlags -- the gate for the accessor sidecars of named keys.
//
// An accessor of key K lives in the sidecars __get_K__ / __set_K__.  Every
// object holding one is stamped with __has_accessor_props__ (the own hint the
// interpreter reads).  Read through the prototype chain that hint is useless:
// Object.prototype carries it for its built-in __proto__ accessor, so it is
// true for every object, and a [[Set]] of a new property built
// "__set_K__" / "__get_K__" as heap strings and probed them at every level of
// the chain -- names protoCore canonicalises against its symbol table on each
// call (PROTOCORE_NONINTERNED_NAMES).
//
// So every site that installs an accessor also stamps
// __has_accessors_beyond_proto__ (markHasAccessors), except the installation
// of Object.prototype's own __proto__ accessor.  Read through the chain, it
// says whether some accessor other than __proto__ may be inherited; a write of
// the key "__proto__" itself always takes the accessor walk.  The flag is
// never cleared: a stale flag costs the walk, never a wrong answer.
//
// Sites that install accessors (keep current; each calls markHasAccessors):
// objectDefineProperty; the interpreter's defineComputedAccessor,
// defineMethodNamed and the Symbol.prototype.description getter; the built-in
// getters of Function.prototype (caller, arguments), ArrayBuffer, DataView,
// %TypedArray%, Map and Set prototypes (size), RegExp.prototype; and the
// Symbol.species getters of Array, ArrayBuffer, Map, Set, RegExp and Promise.

#include "../JSSymbols.h"

#include <protoCore.h>

#include <string>

namespace protojs {

/** Stamp `obj` as holding an accessor; returns the resulting object. */
inline const proto::ProtoObject* markHasAccessors(proto::ProtoContext* ctx,
                                                  const proto::ProtoObject* obj) {
    if (!obj) return obj;
    if (const proto::ProtoString* k = JSSymbols::hasAccessorProps(ctx))
        obj = obj->setAttribute(ctx, k, PROTO_TRUE);
    if (const proto::ProtoString* k = JSSymbols::hasAccessorsBeyondProto(ctx))
        obj = obj->setAttribute(ctx, k, PROTO_TRUE);
    return obj;
}

/** True when `key` spells "__proto__". */
inline bool isProtoKey(proto::ProtoContext* ctx, const proto::ProtoString* key) {
    if (!key) return false;
    if (key == PROTOJS_NAME(ctx, "__proto__")) return true;
    if (key->isSymbol() || key->getSize(ctx) != 9) return false;
    std::string s;
    key->toUTF8String(ctx, s);
    return s == "__proto__";
}

/** True when `obj` may inherit (or hold) an accessor of `key`. */
inline bool mayInheritAccessor(proto::ProtoContext* ctx, const proto::ProtoObject* obj,
                               const proto::ProtoString* key) {
    const proto::ProtoString* k = JSSymbols::hasAccessorsBeyondProto(ctx);
    if (k && obj && obj->getAttribute(ctx, k, true) == PROTO_TRUE) return true;
    return isProtoKey(ctx, key);
}

}  // namespace protojs
