#pragma once

// IndexKeyFlags -- gates for the sidecars of integer-index keys.
//
// An accessor of index `i` lives in the sidecars __get_<i>__ / __set_<i>__ and
// a non-default descriptor in __pd_<i>__ (see ObjectPrototype.cpp).  Those
// names are spelled from the index, so asking protoCore for them means
// building a heap string and canonicalising it against the symbol table on
// every element access: the cost PROTOCORE_NONINTERNED_NAMES counts.  Such
// sidecars are rare, so every site that creates one also stamps a flag on the
// object that receives it, and the element paths ask for the flag (an
// interned name, one attribute lookup) before building any sidecar name:
//
//   __has_indexed_accessors__  an own __get_<i>__ or __set_<i>__ exists.
//                              Read through the prototype chain: an
//                              inherited index getter fires on a hole.
//   __has_index_descriptors__  an own __pd_<i>__ exists.  Read on the object
//                              itself: descriptors are never inherited.
//
// Sites that create these sidecars (all must stamp; keep this list current):
// objectDefineProperty (accessor and data paths), objectFreeze, objectSeal,
// dict::evict, and the interpreter's defineComputedAccessor,
// defineMethodComputed and defineMethodNamed.  The flags are only ever set,
// never cleared: a stale flag costs the slow path, never a wrong answer.

#include "../JSSymbols.h"

#include <protoCore.h>

#include <string>

namespace protojs {

/** True when `s` is a canonical array index: "0", or digits without a
 *  leading zero, below 2^32 - 1 (ECMA-262 §6.1.7). */
inline bool isCanonicalIndexSpelling(const std::string& s) {
    if (s.empty() || s.size() > 10) return false;
    if (s.size() > 1 && s[0] == '0') return false;
    unsigned long long v = 0;
    for (char c : s) {
        if (c < '0' || c > '9') return false;
        v = v * 10 + static_cast<unsigned>(c - '0');
    }
    return v < 4294967295ull;
}

/** Stamp `flag` on `obj` when `keySpelling` is a canonical index; returns the
 *  resulting object (setAttribute may return a new version). */
inline const proto::ProtoObject* stampIndexFlag(proto::ProtoContext* ctx,
                                                const proto::ProtoObject* obj,
                                                const proto::ProtoString* flag,
                                                const std::string& keySpelling) {
    if (!obj || !flag || !isCanonicalIndexSpelling(keySpelling)) return obj;
    return obj->setAttribute(ctx, flag, PROTO_TRUE);
}

/** After installing __get_K__ / __set_K__ on `obj`. */
inline const proto::ProtoObject* noteIndexAccessor(proto::ProtoContext* ctx,
                                                   const proto::ProtoObject* obj,
                                                   const std::string& keySpelling) {
    return stampIndexFlag(ctx, obj, JSSymbols::hasIndexedAccessors(ctx), keySpelling);
}

/** After writing __pd_K__ on `obj`. */
inline const proto::ProtoObject* noteIndexDescriptor(proto::ProtoContext* ctx,
                                                     const proto::ProtoObject* obj,
                                                     const std::string& keySpelling) {
    return stampIndexFlag(ctx, obj, JSSymbols::hasIndexDescriptors(ctx), keySpelling);
}

/** True when `obj` or one of its prototypes may hold an accessor of an
 *  integer-index key. */
inline bool mayHaveIndexAccessor(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    const proto::ProtoString* k = JSSymbols::hasIndexedAccessors(ctx);
    return k && obj && obj->getAttribute(ctx, k, true) == PROTO_TRUE;
}

/** True when `obj` itself may hold an accessor of an integer-index key. */
inline bool mayHaveOwnIndexAccessor(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    const proto::ProtoString* k = JSSymbols::hasIndexedAccessors(ctx);
    return k && obj && obj->getOwnAttributeDirect(ctx, k) == PROTO_TRUE;
}

/** True when `obj` itself may hold a __pd_<i>__ descriptor. */
inline bool mayHaveOwnIndexDescriptor(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    const proto::ProtoString* k = JSSymbols::hasIndexDescriptors(ctx);
    return k && obj && obj->getOwnAttributeDirect(ctx, k) == PROTO_TRUE;
}

}  // namespace protojs
