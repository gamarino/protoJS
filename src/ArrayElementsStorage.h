#pragma once

// ArrayElementsStorage — native ProtoList-backed storage for dense JS arrays.
//
// Background: until this refactor, JS arrays stored each element as a
// string-keyed attribute on a mutable ProtoObject — `arr[42] = x` did
// `setAttribute(symbol("42"), x)` on a tree that grew with the array.
// At 100 K elements that became one of the dominant interpreter costs
// (see CHANGELOG entry "fast-path arrayPush").
//
// This module switches to the natural representation: a single attribute
// `__elements__` holding a `ProtoList` (protoCore's persistent AVL list,
// which is integer-keyed and supports O(log N) appendLast / setAt /
// getAt natively).  Reads and writes for in-range indices go through
// the list; writes past the end pad with PROTO_NONE up to a configurable
// sparse-fallback threshold.
//
// The functions are inline because they're called from the interpreter
// hot path (OP_get_array_el, OP_put_array_el, ...) and from
// ArrayPrototype.cpp — keeping them inline lets the optimiser see the
// fast path (single attribute lookup → list dispatch) at every call site.

#include <protoCore.h>
#include "JSSymbols.h"

namespace protojs {

// Read the underlying ProtoList for `arr`, or nullptr when the array
// has not been migrated to native storage (legacy string-keyed path,
// or a non-array object the caller mistakenly passed in).
inline const proto::ProtoList* getArrayElements(proto::ProtoContext* ctx,
                                                 const proto::ProtoObject* arr) {
    if (!arr || arr == PROTO_NONE) return nullptr;
    const proto::ProtoString* k = JSSymbols::arrayElements(ctx);
    if (!k) return nullptr;
    const proto::ProtoObject* attr = arr->getAttribute(ctx, k, false);
    if (!attr || attr == PROTO_NONE) return nullptr;
    return attr->asList(ctx);
}

// Replace the underlying ProtoList.  For mutable arrays (the common
// case — every `[]` literal is mutable), this is in-place: the same
// `arr` handle now resolves to the new list on subsequent reads.
//
// Also writes the public `length` attribute so that JS code reading
// `arr.length` via the regular `getAttribute` path sees the right
// value.  This costs one extra setAttribute per publish but keeps
// every other read site (OP_get_field, JSON.stringify, console.log,
// for-in, etc.) working unmodified.
inline void setArrayElements(proto::ProtoContext* ctx,
                              const proto::ProtoObject* arr,
                              const proto::ProtoList* list) {
    if (!arr || !list) return;
    const proto::ProtoString* k = JSSymbols::arrayElements(ctx);
    if (!k) return;
    arr->setAttribute(ctx, k, list->asObject(ctx));
    const proto::ProtoString* lk = JSSymbols::length(ctx);
    if (lk) {
        arr->setAttribute(ctx, lk,
            ctx->fromInteger(static_cast<long long>(list->getSize(ctx))));
    }
}

// Coerce a JS-style index value (the operand of `arr[idx]`) to a non-
// negative array index, returning -1 if the value is not a valid index.
// Accepts:
//   - SmallInteger / long integers >= 0
//   - Doubles whose value is a non-negative integer (e.g. Math.floor /
//     Math.round results often come back as doubles even though they
//     represent whole numbers — `arr[Math.floor(n)]` would otherwise
//     miss the fast path).
// Strings, booleans, null/undefined return -1 — caller falls back to
// the slow string-keyed path which handles those uniformly.
inline long long
numericArrayIndexOrNeg(proto::ProtoContext* ctx,
                       const proto::ProtoObject* idx) {
    if (!idx || idx == PROTO_NONE) return -1;
    if (idx->isInteger(ctx)) {
        long long v = idx->asLong(ctx);
        return (v >= 0 && v < 4294967295LL) ? v : -1;
    }
    if (idx->isDouble(ctx) || idx->isFloat(ctx)) {
        double d = idx->asDouble(ctx);
        if (d < 0.0 || d != d /* NaN */ || d >= 4294967295.0) return -1;
        long long v = static_cast<long long>(d);
        return (static_cast<double>(v) == d) ? v : -1;
    }
    // §7.1.21 CanonicalNumericIndexString: string keys that round-trip
    // through ToString(ToUint32) ARE valid array indices and must hit
    // the native fast path. Pre-fix arr["0"] / arr["1" + ""] missed
    // the indexed slot (returned undefined) because they fell into the
    // string-keyed attribute path, while arr[0] / arr[0|0] worked —
    // observed in built-ins/Object/getOwnPropertyNames/15.2.3.4-4-42
    // (for-in iteration of a result array yields string indices).
    if (idx->isString(ctx)) {
        const proto::ProtoString* s = idx->asString(ctx);
        if (!s) return -1;
        std::string buf;
        s->toUTF8String(ctx, buf);
        if (buf.empty()) return -1;
        // Disallow leading zeros (other than "0" itself) and signs;
        // the canonical numeric string for n>=0 is std::to_string(n).
        if (buf[0] == '0' && buf.size() > 1) return -1;
        if (buf[0] < '0' || buf[0] > '9') return -1;
        long long v = 0;
        for (char c : buf) {
            if (c < '0' || c > '9') return -1;
            if (v > 0xFFFFFFFFLL / 10) return -1;
            v = v * 10 + (c - '0');
            if (v >= 0xFFFFFFFFLL) return -1;
        }
        return v;
    }
    return -1;
}

// Maximum index allowed under native storage before we fall back to
// string-keyed attributes.  Picked to cover all reasonable dense
// workloads while preventing accidental pad-to-2^32 from blowing the
// heap when JS code does `arr[2_000_000_000] = x` on an empty array.
inline constexpr unsigned long kSparseFallbackThreshold = 1u << 22; // 4 Mi entries

// Try to read `arr[idx]` from native storage.
//
// Returns:
//   non-null pointer  → caller has the element (may be PROTO_NONE for
//                        out-of-range, which is the JS undefined for
//                        a real array — caller should NOT fall back to
//                        prototype lookup beyond what an array sees).
//   nullptr           → `arr` has no native storage; caller must fall
//                        back to the legacy string-keyed path.
inline const proto::ProtoObject*
arrayTryFastGet(proto::ProtoContext* ctx,
                const proto::ProtoObject* arr,
                unsigned long idx) {
    const proto::ProtoList* els = getArrayElements(ctx, arr);
    if (!els) return nullptr;
    unsigned long size = els->getSize(ctx);
    if (idx >= size) return PROTO_NONE;
    const proto::ProtoObject* v = els->getAt(ctx, static_cast<int>(idx));
    return v ? v : PROTO_NONE;
}

// Try to write `arr[idx] = val` into native storage.  Returns true on
// success, false if the array has no native storage and the caller
// must fall back to the legacy path.
//
// Caller is responsible for checking that `arr` is a real array
// (carries `__is_array__` true) — this helper does NOT migrate
// non-array objects to native storage.
inline bool
arrayTryFastSet(proto::ProtoContext* ctx,
                const proto::ProtoObject* arr,
                unsigned long idx,
                const proto::ProtoObject* val) {
    const proto::ProtoList* els = getArrayElements(ctx, arr);
    if (!els) return false;
    unsigned long size = els->getSize(ctx);
    const proto::ProtoObject* v = val ? val : PROTO_NONE;
    if (idx == size) {
        els = els->appendLast(ctx, v);
    } else if (idx < size) {
        els = els->setAt(ctx, static_cast<int>(idx), v);
    } else {
        // Sparse — pad with PROTO_NONE up to idx, then append.
        if (idx - size > kSparseFallbackThreshold) return false;
        for (unsigned long i = size; i < idx; ++i) {
            els = els->appendLast(ctx, PROTO_NONE);
        }
        els = els->appendLast(ctx, v);
    }
    setArrayElements(ctx, arr, els);
    return true;
}

}  // namespace protojs
