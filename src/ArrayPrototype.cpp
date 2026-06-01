#include "ArrayPrototype.h"
#include "ArrayElementsStorage.h"
#include "FunctionPrototype.h"
#include "runtime/ProtoInterpreter.h"
#include "JSSymbols.h"
#include "headers/protoCore.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace protojs {

static bool arrHas(proto::ProtoContext* ctx, const proto::ProtoObject* arr, unsigned long idx);
static bool arrHasProperty(proto::ProtoContext* ctx, const proto::ProtoObject* arr, unsigned long idx);

// ---------------------------------------------------------------------------
// Internal: compute UTF-16 code unit count from a UTF-8 std::string.
// ---------------------------------------------------------------------------
static size_t utf8ToUTF16Len(const std::string& s) {
    size_t count = 0;
    for (size_t i = 0; i < s.size(); ) {
        auto c = static_cast<unsigned char>(s[i]);
        int n;
        if      (c < 0x80) { n = 1; }
        else if (c < 0xE0) { n = 2; }
        else if (c < 0xF0) { n = 3; }
        else               { n = 4; }
        count += (n == 4) ? 2 : 1;
        i += static_cast<size_t>(n);
    }
    return count;
}

// Internal: return the idx-th UTF-16 code unit of a UTF-8 string as a
// UTF-8-encoded string of that single code unit, or "" if out of range.
static std::string utf16CharAt(const std::string& s, size_t idx) {
    size_t pos = 0;
    for (size_t i = 0; i < s.size(); ) {
        auto c = static_cast<unsigned char>(s[i]);
        int n;
        uint32_t cp = 0;
        if      (c < 0x80) { cp = c;        n = 1; }
        else if (c < 0xE0) { cp = c & 0x1F; n = 2; }
        else if (c < 0xF0) { cp = c & 0x0F; n = 3; }
        else               { cp = c & 0x07; n = 4; }
        for (int j = 1; j < n && i + (size_t)j < s.size(); j++)
            cp = (cp << 6) | (static_cast<unsigned char>(s[i + (size_t)j]) & 0x3F);
        i += static_cast<size_t>(n);
        if (cp < 0x10000) {
            if (pos == idx) {
                std::string r;
                if (cp < 0x80)       r += static_cast<char>(cp);
                else if (cp < 0x800) { r += static_cast<char>(0xC0 | (cp >> 6));
                                        r += static_cast<char>(0x80 | (cp & 0x3F)); }
                else                 { r += static_cast<char>(0xE0 | (cp >> 12));
                                        r += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                                        r += static_cast<char>(0x80 | (cp & 0x3F)); }
                return r;
            }
            pos++;
        } else {
            cp -= 0x10000;
            uint16_t hi = static_cast<uint16_t>(0xD800 + (cp >> 10));
            uint16_t lo = static_cast<uint16_t>(0xDC00 + (cp & 0x3FF));
            if (pos == idx) {
                std::string r;
                r += static_cast<char>(0xE0 | (hi >> 12));
                r += static_cast<char>(0x80 | ((hi >> 6) & 0x3F));
                r += static_cast<char>(0x80 | (hi & 0x3F));
                return r;
            }
            pos++;
            if (pos == idx) {
                std::string r;
                r += static_cast<char>(0xE0 | (lo >> 12));
                r += static_cast<char>(0x80 | ((lo >> 6) & 0x3F));
                r += static_cast<char>(0x80 | (lo & 0x3F));
                return r;
            }
            pos++;
        }
    }
    return "";
}

// ---------------------------------------------------------------------------
// Module-level array prototype pointer (set once during ensureArrayPrototype).
// Safe because protoJS runs JS on a single logical context per process.
// ---------------------------------------------------------------------------
static const proto::ProtoObject* s_arrayProto = nullptr;

// ---------------------------------------------------------------------------
// Internal helper: get the array prototype pointer (may be null before init).
// ---------------------------------------------------------------------------
static inline const proto::ProtoObject* getArrayProto() {
    return s_arrayProto;
}

// ---------------------------------------------------------------------------
// Low-level array element helpers using string index attributes.
// These work for all JS array types (array_from + new Array() + concat results).
// ---------------------------------------------------------------------------

static unsigned long arrLen(proto::ProtoContext* ctx,
                             const proto::ProtoObject* arr) {
    if (!arr || arr == PROTO_NONE) return 0;
    // Handle primitive string — length = UTF-16 code unit count.
    if (arr->isString(ctx)) {
        const proto::ProtoString* s = arr->asString(ctx);
        if (!s) return 0;
        std::string sv;
        s->toUTF8String(ctx, sv);
        return static_cast<unsigned long>(utf8ToUTF16Len(sv));
    }
    // Handle String wrapper object — extract __primitive_value__.
    {
        const proto::ProtoString* pvKey = JSSymbols::primitiveValue(ctx);
        if (pvKey) {
            const proto::ProtoObject* pv = arr->getAttribute(ctx, pvKey, false);
            if (pv && pv != PROTO_NONE && pv->isString(ctx)) {
                const proto::ProtoString* s = pv->asString(ctx);
                if (s) {
                    std::string sv;
                    s->toUTF8String(ctx, sv);
                    return static_cast<unsigned long>(utf8ToUTF16Len(sv));
                }
            }
        }
    }
    // FAST PATH: native ProtoList-backed dense array.  Length is the list
    // size; no separate `length` attribute is consulted (or maintained)
    // when native storage is in use.
    if (const proto::ProtoList* els = getArrayElements(ctx, arr)) {
        return static_cast<unsigned long>(els->getSize(ctx));
    }

    const proto::ProtoString* key = JSSymbols::length(ctx);
    if (!key) return 0;

    // Check for an OWN accessor getter for "length" FIRST.  An own getter must shadow
    // any inherited data property (e.g. child has own getter returning 2, prototype has
    // data length=3 → child.length must use the getter).  Without this check,
    // getAttribute("length", true) would find the inherited data and return the wrong value.
    {
        const proto::ProtoObject* gko = ctx->fromUTF8String("__get_length__");
        const proto::ProtoString* gk  = gko ? gko->asString(ctx) : nullptr;
        if (gk && arr->hasOwnAttribute(ctx, gk) == PROTO_TRUE) {
            const proto::ProtoObject* ownGetter = arr->getAttribute(ctx, gk, true);
            if (ownGetter && ownGetter != PROTO_NONE) {
                const proto::ProtoObject* fromGetter = callJSFunction(ctx, ownGetter, arr, ctx->newList());
                if (hasCallException() || !fromGetter || fromGetter == PROTO_NONE) return 0;
                // Parse the getter result as a length (fall through to numeric parsing below).
                if (fromGetter->isInteger(ctx)) {
                    long long v = fromGetter->asLong(ctx);
                    return (v > 0) ? static_cast<unsigned long>(v) : 0;
                }
                if (fromGetter->isDouble(ctx) || fromGetter->isFloat(ctx)) {
                    double d = fromGetter->asDouble(ctx);
                    if (d <= 0 || std::isnan(d) || std::isinf(d)) return 0;
                    return static_cast<unsigned long>(d);
                }
                if (fromGetter->isString(ctx)) {
                    const proto::ProtoString* s = fromGetter->asString(ctx);
                    if (s) {
                        std::string sv;
                        s->toUTF8String(ctx, sv);
                        try {
                            long long v = (sv.size() > 2 && sv[0] == '0' && (sv[1] == 'x' || sv[1] == 'X'))
                                ? std::stoll(sv, nullptr, 16)
                                : std::stoll(sv);
                            return (v > 0) ? static_cast<unsigned long>(v) : 0;
                        } catch (...) {}
                    }
                }
                return 0;
            }
            // Own setter-only "length" accessor — no getter → length is undefined → treat as 0.
            return 0;
        }
    }

    const proto::ProtoObject* lenObj = arr->getAttribute(ctx, key, true);
    if (!lenObj || lenObj == PROTO_NONE) {
        // Check for inherited length accessor getter: __get_length__
        const proto::ProtoObject* gko = ctx->fromUTF8String("__get_length__");
        const proto::ProtoString* gk  = gko ? gko->asString(ctx) : nullptr;
        if (gk) {
            const proto::ProtoObject* getter = arr->getAttribute(ctx, gk, true);
            if (getter && getter != PROTO_NONE) {
                lenObj = callJSFunction(ctx, getter, arr, ctx->newList());
                if (hasCallException() || !lenObj || lenObj == PROTO_NONE) return 0;
            }
        }
        if (!lenObj || lenObj == PROTO_NONE) return 0;
    }
    if (lenObj->isInteger(ctx)) {
        long long v = lenObj->asLong(ctx);
        return (v > 0) ? static_cast<unsigned long>(v) : 0;
    }
    if (lenObj->isDouble(ctx) || lenObj->isFloat(ctx)) {
        double d = lenObj->asDouble(ctx);
        if (d <= 0 || std::isnan(d) || std::isinf(d)) return 0;
        return static_cast<unsigned long>(d);
    }
    // String-encoded length — try parsing, handle hex (e.g. "0x0002").
    if (lenObj->isString(ctx)) {
        const proto::ProtoString* s = lenObj->asString(ctx);
        if (s) {
            std::string sv;
            s->toUTF8String(ctx, sv);
            try {
                long long v = (sv.size() > 2 && sv[0] == '0' && (sv[1] == 'x' || sv[1] == 'X'))
                    ? std::stoll(sv, nullptr, 16)
                    : std::stoll(sv);
                return (v > 0) ? static_cast<unsigned long>(v) : 0;
            } catch (...) {}
        }
    }
    return 0;
}

static const proto::ProtoObject* arrGet(proto::ProtoContext* ctx,
                                         const proto::ProtoObject* arr,
                                         unsigned long idx) {
    if (!arr || arr == PROTO_NONE) return PROTO_NONE;
    // Handle primitive string — return the character at the UTF-16 index.
    if (arr->isString(ctx)) {
        const proto::ProtoString* s = arr->asString(ctx);
        if (!s) return PROTO_NONE;
        std::string sv;
        s->toUTF8String(ctx, sv);
        std::string ch = utf16CharAt(sv, idx);
        if (ch.empty()) return PROTO_NONE;
        return ctx->fromUTF8String(ch.c_str());
    }
    // Handle String wrapper object — extract __primitive_value__ then index it.
    {
        const proto::ProtoString* pvKey = JSSymbols::primitiveValue(ctx);
        if (pvKey) {
            const proto::ProtoObject* pv = arr->getAttribute(ctx, pvKey, false);
            if (pv && pv != PROTO_NONE && pv->isString(ctx)) {
                const proto::ProtoString* s = pv->asString(ctx);
                if (s) {
                    std::string sv;
                    s->toUTF8String(ctx, sv);
                    std::string ch = utf16CharAt(sv, idx);
                    if (ch.empty()) return PROTO_NONE;
                    return ctx->fromUTF8String(ch.c_str());
                }
            }
        }
    }
    // FAST PATH: native ProtoList storage.  In-range read returns the
    // element; out-of-range returns PROTO_NONE (= JS undefined for a
    // real array, which never inherits indexed properties from any
    // built-in prototype anyway).
    if (const proto::ProtoObject* fastVal = arrayTryFastGet(ctx, arr, idx)) {
        return fastVal;
    }

    const proto::ProtoString* key = JSSymbols::indexKey(ctx, static_cast<uint32_t>(idx));
    if (!key) return PROTO_NONE;

    // Build accessor sidecar keys.
    // NOTE: getAttribute() always walks the prototype chain regardless of the 'callbacks'
    // flag. Use hasOwnAttribute() to check for OWN accessors specifically.
    std::string gkStr = "__get_" + std::to_string(idx) + "__";
    std::string skStr = "__set_" + std::to_string(idx) + "__";
    const proto::ProtoObject* gko = ctx->fromUTF8String(gkStr.c_str());
    const proto::ProtoObject* sko = ctx->fromUTF8String(skStr.c_str());
    const proto::ProtoString* gk  = gko ? gko->asString(ctx) : nullptr;
    const proto::ProtoString* sk  = sko ? sko->asString(ctx) : nullptr;

    // Step 1: Check for an OWN accessor (getter or setter-only) before reading data.
    // This ensures an own accessor-without-getter shadows an inherited getter.
    // Use hasOwnAttribute to restrict to own property — getAttribute always inherits.
    bool hasOwnGetter = gk && arr->hasOwnAttribute(ctx, gk) == PROTO_TRUE;
    bool hasOwnSetter = sk && arr->hasOwnAttribute(ctx, sk) == PROTO_TRUE;

    if (hasOwnGetter) {
        // Own getter found — invoke it.
        const proto::ProtoObject* ownGetter = arr->getAttribute(ctx, gk, true);
        const proto::ProtoObject* result = callJSFunction(ctx, ownGetter, arr, ctx->newList());
        return (hasCallException() || !result || result == PROTO_NONE) ? PROTO_NONE : result;
    }
    if (hasOwnSetter) {
        // Setter-only own accessor (no getter) — accessing returns undefined.
        return PROTO_NONE;
    }

    // Step 2: Read data value from own or inherited chain.
    const proto::ProtoObject* val = arr->getAttribute(ctx, key, true);
    if (val && val != PROTO_NONE) return val;

    // Step 3: Check for inherited accessor getter (no own accessor found above).
    if (gk) {
        const proto::ProtoObject* inheritedGetter = arr->getAttribute(ctx, gk, true);
        if (inheritedGetter && inheritedGetter != PROTO_NONE) {
            const proto::ProtoObject* result = callJSFunction(ctx, inheritedGetter, arr, ctx->newList());
            if (!hasCallException() && result && result != PROTO_NONE) return result;
        }
    }

    return PROTO_NONE;
}

// Set element at idx, also updates "length" if idx+1 > current length.
// For mutable arrays, modifies in-place; returns same pointer.
// For immutable arrays, returns new pointer (caller should capture).
static const proto::ProtoObject* arrSet(proto::ProtoContext* ctx,
                                         const proto::ProtoObject* arr,
                                         unsigned long idx,
                                         const proto::ProtoObject* val) {
    if (!arr) return PROTO_NONE;

    // Determine real-array status first — needed for both the native
    // fast path and the legacy length-bump branch below.
    const proto::ProtoString* isArrKey = JSSymbols::isArray(ctx);
    const proto::ProtoObject* isArrVal = isArrKey
        ? arr->getAttribute(ctx, isArrKey, true) : nullptr;
    const bool isRealArr = (isArrVal == PROTO_TRUE);

    // FAST PATH: real arrays always use native ProtoList storage.
    // Lazy-initialise an empty list if this is the array's first set.
    if (isRealArr) {
        if (!getArrayElements(ctx, arr)) {
            const proto::ProtoList* empty = ctx->newList();
            if (empty) setArrayElements(ctx, arr, empty);
        }
        if (arrayTryFastSet(ctx, arr, idx, val)) {
            return arr;
        }
        // Sparse-overflow (idx - size > kSparseFallbackThreshold).
        // Falls through to the legacy string-keyed path.
    }

    const proto::ProtoString* key = JSSymbols::indexKey(ctx, static_cast<uint32_t>(idx));
    if (!key) return arr;
    arr = arr->setAttribute(ctx, key, val ? val : PROTO_NONE);
    unsigned long curLen = arrLen(ctx, arr);
    if (idx + 1 > curLen) {
        if (isRealArr) {
            const proto::ProtoString* lenKey = JSSymbols::length(ctx);
            if (lenKey)
                arr = arr->setAttribute(ctx, lenKey,
                                        ctx->fromInteger(static_cast<long long>(idx + 1)));
        }
    }
    return arr;
}

// Fast path for callers (e.g. arrayPush, arrayConcat) that have already
// verified `arr` is a real array and that they will bump `length` exactly
// once at the end of a batch.  Skips the inline arrLen + __is_array__
// probe + length setAttribute on every element — at 100 K elements, those
// three extra mutable-CAS round-trips per element dominated the cost.
//
// Returns the same pointer for mutable arrays (in-place); for immutable
// arrays the caller is responsible for chaining the returned pointer,
// but at present every code path that reaches arrSetUnchecked is on a
// mutable array (the array is created by `[]` or new Array()).
static const proto::ProtoObject* arrSetUnchecked(proto::ProtoContext* ctx,
                                                  const proto::ProtoObject* arr,
                                                  unsigned long idx,
                                                  const proto::ProtoObject* val) {
    if (!arr) return PROTO_NONE;
    const proto::ProtoString* key = JSSymbols::indexKey(ctx, static_cast<uint32_t>(idx));
    if (!key) return arr;
    return arr->setAttribute(ctx, key, val ? val : PROTO_NONE);
}

static const proto::ProtoObject* arrSetLen(proto::ProtoContext* ctx,
                                            const proto::ProtoObject* arr,
                                            unsigned long newLen) {
    if (!arr) return PROTO_NONE;
    // Only bump magic length on real arrays (carrying __is_array__ marker).
    const proto::ProtoString* isArrKey = JSSymbols::isArray(ctx);
    const proto::ProtoObject* isArrVal = isArrKey
        ? arr->hasOwnAttribute(ctx, isArrKey) : nullptr;
    bool isRealArray = (isArrVal == PROTO_TRUE);

    if (isRealArray) {
        // FAST PATH: native ProtoList storage — truncate or pad in place.
        if (const proto::ProtoList* els = getArrayElements(ctx, arr)) {
            unsigned long size = static_cast<unsigned long>(els->getSize(ctx));
            if (newLen < size) {
                const proto::ProtoList* truncated = els->splitFirst(ctx, static_cast<int>(newLen));
                if (truncated) setArrayElements(ctx, arr, truncated);
            } else if (newLen > size) {
                // Pad with PROTO_NONE up to the new length.  Bounded by the
                // sparse threshold to avoid runaway pads from `arr.length = 2**32`.
                if (newLen - size > kSparseFallbackThreshold) {
                    // Fall through to legacy length-attribute set so that semantics
                    // are preserved (length set, elements untouched).
                } else {
                    const proto::ProtoList* padded = els;
                    for (unsigned long i = size; i < newLen; ++i) {
                        padded = padded->appendLast(ctx, PROTO_NONE);
                    }
                    setArrayElements(ctx, arr, padded);
                    return arr;
                }
            } else {
                return arr;  // size unchanged
            }
            return arr;
        }
    }

    const proto::ProtoString* key = JSSymbols::length(ctx);
    if (!key) return arr;
    return arr->setAttribute(ctx, key,
                             ctx->fromInteger(static_cast<long long>(newLen)));
}

// ---------------------------------------------------------------------------
// Value-to-string conversion for join (mirrors ProtoInterpreter::toString).
// ---------------------------------------------------------------------------
static std::string elemToString(proto::ProtoContext* ctx,
                                 const proto::ProtoObject* val) {
    // Per spec: null and undefined elements in join produce empty string.
    if (!val || val == PROTO_NONE || val == protojs::getNullSentinel()) return "";
    if (val->isString(ctx)) {
        const proto::ProtoString* s = val->asString(ctx);
        if (s) {
            std::string r;
            s->toUTF8String(ctx, r);
            return r;
        }
        return "";
    }
    if (val->isBoolean(ctx)) {
        return (val == PROTO_TRUE) ? "true" : "false";
    }
    if (val->isInteger(ctx)) {
        return std::to_string(val->asLong(ctx));
    }
    if (val->isDouble(ctx) || val->isFloat(ctx)) {
        double d = val->asDouble(ctx);
        if (std::isnan(d)) return "NaN";
        if (std::isinf(d)) return d > 0 ? "Infinity" : "-Infinity";
        char buf[64];
        snprintf(buf, sizeof(buf), "%.15g", d);
        return buf;
    }
    // Array-like objects: recursively join elements with "," (mirrors JS Array.prototype.toString).
    {
        const proto::ProtoString* lenKey = JSSymbols::length(ctx);
        if (lenKey) {
            const proto::ProtoObject* lenObj = val->getAttribute(ctx, lenKey, false);
            if (lenObj && lenObj != PROTO_NONE && lenObj->isInteger(ctx)) {
                unsigned long subLen = (unsigned long)lenObj->asLong(ctx);
                if (subLen > 0 && subLen < 100000UL) { // guard against degenerate lengths
                    std::string result;
                    for (unsigned long i = 0; i < subLen; i++) {
                        if (i > 0) result += ",";
                        result += elemToString(ctx, arrGet(ctx, val, i));
                    }
                    return result;
                }
            }
        }
    }
    return "[object Object]";
}

// ---------------------------------------------------------------------------
// Strict equality (===) for indexOf / lastIndexOf.
// ---------------------------------------------------------------------------
static bool strictEquals(proto::ProtoContext* ctx,
                          const proto::ProtoObject* a,
                          const proto::ProtoObject* b) {
    if (a == b) return true;
    bool aNone = !a || a == PROTO_NONE;
    bool bNone = !b || b == PROTO_NONE;
    if (aNone && bNone) return true;
    if (aNone || bNone) return false;
    bool aInt = a->isInteger(ctx);
    bool bInt = b->isInteger(ctx);
    bool aDbl = a->isDouble(ctx) || a->isFloat(ctx);
    bool bDbl = b->isDouble(ctx) || b->isFloat(ctx);
    if ((aInt || aDbl) && (bInt || bDbl)) {
        double da = aInt ? static_cast<double>(a->asLong(ctx)) : a->asDouble(ctx);
        double db = bInt ? static_cast<double>(b->asLong(ctx)) : b->asDouble(ctx);
        return da == db;
    }
    if (a->isString(ctx) && b->isString(ctx)) {
        const proto::ProtoString* sa = a->asString(ctx);
        const proto::ProtoString* sb = b->asString(ctx);
        if (sa && sb) {
            std::string ua, ub;
            sa->toUTF8String(ctx, ua);
            sb->toUTF8String(ctx, ub);
            return ua == ub;
        }
        return sa == sb;
    }
    if (a->isBoolean(ctx) && b->isBoolean(ctx)) return a == b;
    return false; // Objects: reference equality only
}

// SameValueZero: like === but NaN === NaN.
static bool sameValueZero(proto::ProtoContext* ctx,
                           const proto::ProtoObject* a,
                           const proto::ProtoObject* b) {
    bool aNaN = a && (a->isDouble(ctx) || a->isFloat(ctx)) && std::isnan(a->asDouble(ctx));
    bool bNaN = b && (b->isDouble(ctx) || b->isFloat(ctx)) && std::isnan(b->asDouble(ctx));
    if (aNaN && bNaN) return true;
    return strictEquals(ctx, a, b);
}

// Normalize negative or out-of-range index per ECMAScript spec.
static long long normalizeIdx(long long idx, long long len) {
    if (idx < 0) { idx += len; if (idx < 0) idx = 0; }
    return idx;
}
static long long normalizeIdxClamp(long long idx, long long len) {
    idx = normalizeIdx(idx, len);
    if (idx > len) idx = len;
    return idx;
}

// ---------------------------------------------------------------------------
// Public helper: create a new empty mutable array.
// ---------------------------------------------------------------------------
const proto::ProtoObject* createNewArray(proto::ProtoContext* ctx,
                                          const proto::ProtoObject* arrayProto) {
    const proto::ProtoObject* proto = arrayProto ? arrayProto : getArrayProto();
    const proto::ProtoObject* arr = proto
        ? proto->newChild(ctx, true)
        : ctx->newObject(true);
    const proto::ProtoString* isArrKey = JSSymbols::isArray(ctx);
    if (isArrKey) arr = arr->setAttribute(ctx, isArrKey, PROTO_TRUE);
    return arrSetLen(ctx, arr, 0);
}

/**
 * ES6+ ArraySpeciesCreate(originalArray, length) implementation.
 */
static const proto::ProtoObject* arraySpeciesCreate(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* originalArray,
    unsigned long length)
{
    if (!originalArray || originalArray == PROTO_NONE)
        return arrSetLen(ctx, createNewArray(ctx, nullptr), length);

    // IsArray check: per spec, species is only checked if originalArray is a real array.
    const proto::ProtoString* isArrayKey = JSSymbols::isArray(ctx);
    if (!isArrayKey || originalArray->hasOwnAttribute(ctx, isArrayKey) != PROTO_TRUE)
        return arrSetLen(ctx, createNewArray(ctx, nullptr), length);

    const proto::ProtoString* ctorKey = JSSymbols::constructor(ctx);
    const proto::ProtoObject* C = originalArray->getAttribute(ctx, ctorKey, true);

    // If C is a constructor, check its @@species.
    if (C && C != PROTO_NONE) {
        const proto::ProtoString* speciesKey = JSSymbols::symbolSpecies(ctx);
        if (speciesKey) {
            const proto::ProtoObject* species = C->getAttribute(ctx, speciesKey, true);
            if (species && species != PROTO_NONE) {
                C = species;
                // If species is null, use default Array (ES6 22.1.3.17.1 step 5.b).
                if (getNullSentinel() && C == getNullSentinel()) C = PROTO_NONE;
            } else {
                C = PROTO_NONE;
            }
        }
    }

    if (!C || C == PROTO_NONE)
        return arrSetLen(ctx, createNewArray(ctx, nullptr), length);

    // Step 8: Construct(C, [length])
    const proto::ProtoString* constructKey = ctx->fromUTF8String("__construct__")->asString(ctx);
    const proto::ProtoObject* constructFn = (constructKey && C) ? C->getAttribute(ctx, constructKey, false) : nullptr;
    
    if (constructFn && constructFn != PROTO_NONE && constructFn->isMethod(ctx)) {
        // Create newObj as child of C.prototype
        const proto::ProtoString* protoKey = JSSymbols::prototype(ctx);
        const proto::ProtoObject* proto = C->getAttribute(ctx, protoKey, true);
        const proto::ProtoObject* newObj = (proto && proto != PROTO_NONE) ? proto->newChild(ctx, true) : ctx->newObject(true);
        
        // Mark as array (necessary if the constructor doesn't do it)
        if (isArrayKey) newObj = newObj->setAttribute(ctx, isArrayKey, PROTO_TRUE);

        const proto::ProtoList* args = ctx->newList();
        args = args->appendLast(ctx, ctx->fromInteger(static_cast<long long>(length)));
        
        proto::ProtoMethod fn = constructFn->asMethod(ctx);
        const proto::ProtoObject* res = fn(ctx, newObj, nullptr, args, nullptr);
        
        // If res is object, return it, else newObj
        if (res && res != PROTO_NONE && !res->isInteger(ctx) && !res->isDouble(ctx) && !res->asString(ctx) && res != PROTO_TRUE && res != PROTO_FALSE)
            return res;
        return newObj;
    }

    // Default fallback
    return arrSetLen(ctx, createNewArray(ctx, nullptr), length);
}

// ---------------------------------------------------------------------------
// Null/undefined `this` guard for Array prototype methods.
// Per ECMAScript spec, all Array.prototype methods must throw a TypeError when
// called on null or undefined (ToObject step).
// ---------------------------------------------------------------------------

static bool arrayThrowIfNullUndefined(proto::ProtoContext* ctx,
                                       const proto::ProtoObject* self) {
    const proto::ProtoObject* nullSentinel = getNullSentinel();
    bool isNull = (self == nullSentinel);
    bool isUndefined = (!self || self == PROTO_NONE);
    if (isNull || isUndefined) {
        const char* msg = isNull
            ? "Cannot convert undefined or null to object"
            : "Cannot convert undefined or null to object";
        signalNativeException(makeNativeError(ctx, "TypeError", msg));
        return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Array.prototype methods
// ---------------------------------------------------------------------------

static const proto::ProtoObject* arrayJoin(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (arrayThrowIfNullUndefined(ctx, self)) return PROTO_NONE;
    unsigned long len = arrLen(ctx, self);

    std::string sep = ",";
    if (args && args->getSize(ctx) > 0) {
        const proto::ProtoObject* sepObj = args->getAt(ctx, 0);
        if (!sepObj || sepObj == PROTO_NONE) {
            sep = ",";  // undefined separator → default ","
        } else if (sepObj == protojs::getNullSentinel()) {
            sep = "null";  // null separator → "null" (ToString(null))
        } else {
            sep = elemToString(ctx, sepObj);
        }
    }

    std::string result;
    for (unsigned long i = 0; i < len; i++) {
        if (i > 0) result += sep;
        const proto::ProtoObject* elem = arrGet(ctx, self, i);
        if (elem && elem != PROTO_NONE) {
            result += elemToString(ctx, elem);
        }
    }
    return ctx->fromUTF8String(result.c_str());
}

static const proto::ProtoObject* arrayToString(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* pl,
    const proto::ProtoList* args,
    const proto::ProtoSparseList* kw)
{
    return arrayJoin(ctx, self, pl, nullptr, kw);
}

static const proto::ProtoObject* arrayPush(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (arrayThrowIfNullUndefined(ctx, self)) return PROTO_NONE;
    unsigned long argc = args ? static_cast<unsigned long>(args->getSize(ctx)) : 0;

    const proto::ProtoString* isArrKey = JSSymbols::isArray(ctx);
    const proto::ProtoObject* isArrVal = isArrKey
        ? self->getAttribute(ctx, isArrKey, true) : nullptr;
    const bool isRealArray = (isArrVal == PROTO_TRUE);

    if (isRealArray) {
        // Native ProtoList path: build the new list in a local pointer
        // (each appendLast is O(log N) and produces a fresh AVL node),
        // then publish via a single setAttribute(__elements__, …).  This
        // is the lazy-publish pattern from the microbench (~3 us/op vs
        // ~5 us/op for per-element setAttribute on a string-keyed tree).
        //
        // GC critical section: each appendLast produces a fresh ProtoList
        // root that is reachable only via the C++ local `list` until the
        // closing setArrayElements publishes it through setAttribute.
        // With PROTOCORE_GC_REINCLUDE_SURVIVORS=ON, every cell appears in
        // dirtySegments after each sweep, so a concurrent mark cycle that
        // misses the C++ local will free the half-built list under us
        // (manifested as a getHash segfault inside the AVL rebuild from
        // setAttribute).  CriticalSection bars STW + threshold submission
        // for this thread, so the young chain holds the new cells until
        // the publish lands.
        proto::ProtoContext::CriticalSection cs(ctx);
        const proto::ProtoList* list = getArrayElements(ctx, self);
        if (!list) list = ctx->newList();
        for (unsigned long i = 0; i < argc; i++) {
            const proto::ProtoObject* item = args->getAt(ctx, static_cast<int>(i));
            list = list->appendLast(ctx, item ? item : PROTO_NONE);
        }
        setArrayElements(ctx, self, list);
        return ctx->fromInteger(static_cast<long long>(list->getSize(ctx)));
    }

    // Plain object used as an array-like — keep legacy semantics (write
    // string-keyed indices and bump `length` once).  This path is rare;
    // it only fires for `Array.prototype.push.call(plainObj, ...)`.
    unsigned long len = arrLen(ctx, self);
    for (unsigned long i = 0; i < argc; i++) {
        const proto::ProtoObject* item = args->getAt(ctx, static_cast<int>(i));
        arrSet(ctx, self, len + i, item);
    }
    return ctx->fromInteger(static_cast<long long>(len + argc));
}

// Helper: detect "real array" with native ProtoList storage in one shot.
// Returns the underlying list (non-null) when both __is_array__ is true
// and __elements__ is present; nullptr otherwise.  Avoids two separate
// getAttribute calls for the common case in pop/shift/unshift fast paths.
static const proto::ProtoList* nativeArrayList(proto::ProtoContext* ctx,
                                                 const proto::ProtoObject* self) {
    const proto::ProtoString* isArrKey = JSSymbols::isArray(ctx);
    const proto::ProtoObject* isArrVal = isArrKey
        ? self->getAttribute(ctx, isArrKey, true) : nullptr;
    if (isArrVal != PROTO_TRUE) return nullptr;
    return getArrayElements(ctx, self);
}

static const proto::ProtoObject* arrayPop(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*,
    const proto::ProtoSparseList*)
{
    if (arrayThrowIfNullUndefined(ctx, self)) return PROTO_NONE;

    // Native ProtoList path.
    if (const proto::ProtoList* list = nativeArrayList(ctx, self)) {
        unsigned long size = static_cast<unsigned long>(list->getSize(ctx));
        if (size == 0) return PROTO_NONE;
        const proto::ProtoObject* removed = list->getAt(ctx, static_cast<int>(size - 1));
        const proto::ProtoList* shrunk = list->removeLast(ctx);
        if (shrunk) setArrayElements(ctx, self, shrunk);
        return removed ? removed : PROTO_NONE;
    }

    // Legacy string-keyed path (array-likes only).
    unsigned long len = arrLen(ctx, self);
    if (len == 0) return PROTO_NONE;
    unsigned long lastIdx = len - 1;
    const proto::ProtoObject* removed = arrGet(ctx, self, lastIdx);
    const proto::ProtoString* idxKey =
        JSSymbols::indexKey(ctx, static_cast<uint32_t>(lastIdx));
    if (idxKey) self->setAttribute(ctx, idxKey, PROTO_NONE);
    const proto::ProtoString* lenKey = JSSymbols::length(ctx);
    if (lenKey)
        self->setAttribute(ctx, lenKey,
                           ctx->fromInteger(static_cast<long long>(lastIdx)));
    return removed ? removed : PROTO_NONE;
}

static const proto::ProtoObject* arrayShift(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*,
    const proto::ProtoSparseList*)
{
    if (arrayThrowIfNullUndefined(ctx, self)) return PROTO_NONE;

    // Native ProtoList path: removeFirst is O(log N) and preserves all
    // remaining elements without the manual shift loop.
    if (const proto::ProtoList* list = nativeArrayList(ctx, self)) {
        unsigned long size = static_cast<unsigned long>(list->getSize(ctx));
        if (size == 0) return PROTO_NONE;
        const proto::ProtoObject* first = list->getAt(ctx, 0);
        const proto::ProtoList* shrunk = list->removeFirst(ctx);
        if (shrunk) setArrayElements(ctx, self, shrunk);
        return first ? first : PROTO_NONE;
    }

    // Legacy string-keyed path.
    unsigned long len = arrLen(ctx, self);
    if (len == 0) return PROTO_NONE;
    const proto::ProtoObject* first = arrGet(ctx, self, 0);
    for (unsigned long i = 1; i < len; i++) {
        arrSet(ctx, self, i - 1, arrGet(ctx, self, i));
    }
    const proto::ProtoString* lastKey =
        JSSymbols::indexKey(ctx, static_cast<uint32_t>(len - 1));
    if (lastKey) self->setAttribute(ctx, lastKey, PROTO_NONE);
    const proto::ProtoString* lenKey = JSSymbols::length(ctx);
    if (lenKey)
        self->setAttribute(ctx, lenKey,
                           ctx->fromInteger(static_cast<long long>(len - 1)));
    return first ? first : PROTO_NONE;
}

static const proto::ProtoObject* arrayUnshift(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (arrayThrowIfNullUndefined(ctx, self)) return PROTO_NONE;
    unsigned long argc = args ? static_cast<unsigned long>(args->getSize(ctx)) : 0;

    // Native ProtoList path: appendFirst inserts at index 0 in O(log N)
    // per element, no manual right-shift loop needed.
    if (const proto::ProtoList* list = nativeArrayList(ctx, self)) {
        unsigned long size = static_cast<unsigned long>(list->getSize(ctx));
        if (argc == 0) return ctx->fromInteger(static_cast<long long>(size));
        const proto::ProtoList* newList = list;
        // Insert args in reverse so that args[0] ends up at index 0
        // (each appendFirst pushes the previous head right).
        for (long long i = static_cast<long long>(argc) - 1; i >= 0; --i) {
            const proto::ProtoObject* item = args->getAt(ctx, static_cast<int>(i));
            newList = newList->appendFirst(ctx, item ? item : PROTO_NONE);
        }
        setArrayElements(ctx, self, newList);
        return ctx->fromInteger(static_cast<long long>(newList->getSize(ctx)));
    }

    // Legacy string-keyed path.
    unsigned long len = arrLen(ctx, self);
    if (argc == 0) return ctx->fromInteger(static_cast<long long>(len));
    for (long long i = static_cast<long long>(len) - 1; i >= 0; i--) {
        arrSet(ctx, self, static_cast<unsigned long>(i) + argc,
               arrGet(ctx, self, static_cast<unsigned long>(i)));
    }
    for (unsigned long i = 0; i < argc; i++) {
        arrSet(ctx, self, i, args->getAt(ctx, static_cast<int>(i)));
    }
    unsigned long newLen = len + argc;
    const proto::ProtoString* lenKey = JSSymbols::length(ctx);
    if (lenKey)
        self->setAttribute(ctx, lenKey,
                           ctx->fromInteger(static_cast<long long>(newLen)));
    return ctx->fromInteger(static_cast<long long>(newLen));
}

static const proto::ProtoObject* arraySlice(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (arrayThrowIfNullUndefined(ctx, self)) return PROTO_NONE;
    long long len = static_cast<long long>(arrLen(ctx, self));
    long long start = 0, end = len;

    if (args && args->getSize(ctx) > 0) {
        const proto::ProtoObject* s = args->getAt(ctx, 0);
        if (s && s != PROTO_NONE) {
            if (s->isInteger(ctx)) start = s->asLong(ctx);
            else if (s->isDouble(ctx) || s->isFloat(ctx))
                start = static_cast<long long>(s->asDouble(ctx));
        }
        if (args->getSize(ctx) > 1) {
            const proto::ProtoObject* e = args->getAt(ctx, 1);
            if (e && e != PROTO_NONE) {
                if (e->isInteger(ctx)) end = e->asLong(ctx);
                else if (e->isDouble(ctx) || e->isFloat(ctx))
                    end = static_cast<long long>(e->asDouble(ctx));
            }
        }
    }

    start = normalizeIdxClamp(start, len);
    end   = normalizeIdxClamp(end,   len);

    const proto::ProtoObject* result = arraySpeciesCreate(ctx, self, static_cast<unsigned long>(end - start));
    long long outIdx = 0;
    for (long long i = start; i < end; i++) {
        arrSet(ctx, result,
               static_cast<unsigned long>(outIdx++),
               arrGet(ctx, self, static_cast<unsigned long>(i)));
    }
    result = arrSetLen(ctx, result, static_cast<unsigned long>(outIdx));
    return result;
}

static const proto::ProtoObject* arrayIndexOf(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (arrayThrowIfNullUndefined(ctx, self)) return PROTO_NONE;
    if (!args || args->getSize(ctx) == 0)
        return ctx->fromInteger(-1LL);
    long long len = static_cast<long long>(arrLen(ctx, self));
    const proto::ProtoObject* needle = args->getAt(ctx, 0);
    long long from = 0;
    if (args->getSize(ctx) > 1) {
        const proto::ProtoObject* fi = args->getAt(ctx, 1);
        if (fi && fi != PROTO_NONE) {
            if (fi->isInteger(ctx)) from = fi->asLong(ctx);
            else if (fi->isDouble(ctx) || fi->isFloat(ctx))
                from = static_cast<long long>(fi->asDouble(ctx));
        }
    }
    from = normalizeIdx(from, len);
    // NaN-as-needle: indexOf uses Strict Equality, so NaN is never found.
    bool needleIsNaN = needle && (needle->isDouble(ctx) || needle->isFloat(ctx)) &&
                       std::isnan(needle->asDouble(ctx));
    if (needleIsNaN) return ctx->fromInteger(-1LL);
    // Iterate the full array.  The prior 10-iteration cap was a sparse-array
    // timeout guard but silently truncated indexOf for any 11+ length array.
    for (long long i = from; i < len; i++) {
        if (strictEquals(ctx, arrGet(ctx, self, static_cast<unsigned long>(i)), needle))
            return ctx->fromInteger(i);
    }
    return ctx->fromInteger(-1LL);
}

static const proto::ProtoObject* arrayLastIndexOf(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (arrayThrowIfNullUndefined(ctx, self)) return PROTO_NONE;
    if (!args || args->getSize(ctx) == 0)
        return ctx->fromInteger(-1LL);
    long long len = static_cast<long long>(arrLen(ctx, self));
    const proto::ProtoObject* needle = args->getAt(ctx, 0);
    long long from = len - 1;
    if (args->getSize(ctx) > 1) {
        const proto::ProtoObject* fi = args->getAt(ctx, 1);
        if (fi && fi != PROTO_NONE) {
            if (fi->isInteger(ctx)) from = fi->asLong(ctx);
            else if (fi->isDouble(ctx) || fi->isFloat(ctx))
                from = static_cast<long long>(fi->asDouble(ctx));
        }
    }
    if (from < 0) from += len;
    if (from >= len) from = len - 1;
    // NaN is never found (Strict Equality).
    bool needleIsNaN = needle && (needle->isDouble(ctx) || needle->isFloat(ctx)) &&
                       std::isnan(needle->asDouble(ctx));
    if (needleIsNaN) return ctx->fromInteger(-1LL);
    for (long long i = from; i >= 0; i--) {
        if (strictEquals(ctx, arrGet(ctx, self, static_cast<unsigned long>(i)), needle))
            return ctx->fromInteger(i);
    }
    return ctx->fromInteger(-1LL);
}

static const proto::ProtoObject* arrayIncludes(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (arrayThrowIfNullUndefined(ctx, self)) return PROTO_NONE;
    if (!args || args->getSize(ctx) == 0)
        return PROTO_FALSE;
    long long len = static_cast<long long>(arrLen(ctx, self));
    const proto::ProtoObject* needle = args->getAt(ctx, 0);
    long long from = 0;
    if (args->getSize(ctx) > 1) {
        const proto::ProtoObject* fi = args->getAt(ctx, 1);
        if (fi && fi != PROTO_NONE) {
            if (fi->isInteger(ctx)) from = fi->asLong(ctx);
            else if (fi->isDouble(ctx) || fi->isFloat(ctx))
                from = static_cast<long long>(fi->asDouble(ctx));
        }
    }
    from = normalizeIdx(from, len);
    for (long long i = from; i < len; i++) {
        if (sameValueZero(ctx, arrGet(ctx, self, static_cast<unsigned long>(i)), needle))
            return PROTO_TRUE;
    }
    return PROTO_FALSE;
}

static const proto::ProtoObject* arrayReverse(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*,
    const proto::ProtoSparseList*)
{
    if (arrayThrowIfNullUndefined(ctx, self)) return PROTO_NONE;
    unsigned long len = arrLen(ctx, self);
    for (unsigned long i = 0; i < len / 2; i++) {
        unsigned long j = len - 1 - i;
        const proto::ProtoObject* a = arrGet(ctx, self, i);
        const proto::ProtoObject* b = arrGet(ctx, self, j);
        arrSet(ctx, self, i, b);
        arrSet(ctx, self, j, a);
    }
    return self;
}

static const proto::ProtoObject* arrayConcat(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (arrayThrowIfNullUndefined(ctx, self)) return PROTO_NONE;

    // Concat length calculation (pre-scan)
    unsigned long totalLen = 0;
    auto countLen = [&](const proto::ProtoObject* obj) {
        if (!obj || obj == PROTO_NONE) { totalLen++; return; }
        if (obj->isInteger(ctx) || obj->isDouble(ctx) || obj->isFloat(ctx) ||
            obj->isString(ctx) || obj->isBoolean(ctx)) { totalLen++; return; }
        const proto::ProtoString* lenKey = JSSymbols::length(ctx);
        const proto::ProtoObject* lv = lenKey ? obj->getAttribute(ctx, lenKey, false) : nullptr;
        if (lv && lv != PROTO_NONE && (lv->isInteger(ctx) || lv->isDouble(ctx) || lv->isFloat(ctx))) {
            totalLen += static_cast<unsigned long>(lv->asLong(ctx));
        } else {
            totalLen++;
        }
    };
    countLen(self);
    if (args) {
        unsigned long argc = static_cast<unsigned long>(args->getSize(ctx));
        for (unsigned long ai = 0; ai < argc; ai++) countLen(args->getAt(ctx, static_cast<int>(ai)));
    }

    const proto::ProtoObject* result = arraySpeciesCreate(ctx, self, totalLen);
    unsigned long outIdx = 0;

    // Helper: determine if a value should be spread (array-like with "length").
    auto isSpreadable = [&](const proto::ProtoObject* obj) -> bool {
        if (!obj || obj == PROTO_NONE) return false;
        if (obj->isInteger(ctx) || obj->isDouble(ctx) || obj->isFloat(ctx) ||
            obj->isString(ctx) || obj->isBoolean(ctx)) return false;
        const proto::ProtoString* lenKey = JSSymbols::length(ctx);
        if (!lenKey) return false;
        const proto::ProtoObject* lv = obj->getAttribute(ctx, lenKey, false);
        return lv && lv != PROTO_NONE &&
               (lv->isInteger(ctx) || lv->isDouble(ctx) || lv->isFloat(ctx));
    };

    // Spread self.
    if (isSpreadable(self)) {
        unsigned long n = arrLen(ctx, self);
        for (unsigned long i = 0; i < n; i++)
            arrSet(ctx, result, outIdx++, arrGet(ctx, self, i));
    } else if (self && self != PROTO_NONE) {
        arrSet(ctx, result, outIdx++, self);
    }

    // Spread each argument.
    if (args) {
        unsigned long argc = static_cast<unsigned long>(args->getSize(ctx));
        for (unsigned long ai = 0; ai < argc; ai++) {
            const proto::ProtoObject* item = args->getAt(ctx, static_cast<int>(ai));
            if (isSpreadable(item)) {
                unsigned long n = arrLen(ctx, item);
                for (unsigned long i = 0; i < n; i++)
                    arrSet(ctx, result, outIdx++, arrGet(ctx, item, i));
            } else {
                arrSet(ctx, result, outIdx++, item);
            }
        }
    }

    result = arrSetLen(ctx, result, outIdx);
    return result;
}

static const proto::ProtoObject* arrayFill(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (arrayThrowIfNullUndefined(ctx, self)) return PROTO_NONE;
    long long len = static_cast<long long>(arrLen(ctx, self));
    const proto::ProtoObject* value = PROTO_NONE;
    long long start = 0, end = len;

    if (args && args->getSize(ctx) > 0) {
        value = args->getAt(ctx, 0);
        if (!value) value = PROTO_NONE;
    }
    if (args && args->getSize(ctx) > 1) {
        const proto::ProtoObject* s = args->getAt(ctx, 1);
        if (s && s != PROTO_NONE) {
            if (s->isInteger(ctx)) start = s->asLong(ctx);
            else if (s->isDouble(ctx) || s->isFloat(ctx))
                start = static_cast<long long>(s->asDouble(ctx));
        }
    }
    if (args && args->getSize(ctx) > 2) {
        const proto::ProtoObject* e = args->getAt(ctx, 2);
        if (e && e != PROTO_NONE) {
            if (e->isInteger(ctx)) end = e->asLong(ctx);
            else if (e->isDouble(ctx) || e->isFloat(ctx))
                end = static_cast<long long>(e->asDouble(ctx));
        }
    }

    start = normalizeIdxClamp(start, len);
    end   = normalizeIdxClamp(end,   len);

    for (long long i = start; i < end; i++)
        arrSet(ctx, self, static_cast<unsigned long>(i), value);

    return self;
}

static const proto::ProtoObject* arrayCopyWithin(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (arrayThrowIfNullUndefined(ctx, self)) return PROTO_NONE;
    if (!args || args->getSize(ctx) == 0)
        return self ? self : PROTO_NONE;

    long long len = static_cast<long long>(arrLen(ctx, self));
    long long target = 0, start = 0, end = len;

    const proto::ProtoObject* tObj = args->getAt(ctx, 0);
    if (tObj && tObj != PROTO_NONE) {
        if (tObj->isInteger(ctx)) target = tObj->asLong(ctx);
        else if (tObj->isDouble(ctx) || tObj->isFloat(ctx))
            target = static_cast<long long>(tObj->asDouble(ctx));
    }
    if (args->getSize(ctx) > 1) {
        const proto::ProtoObject* sObj = args->getAt(ctx, 1);
        if (sObj && sObj != PROTO_NONE) {
            if (sObj->isInteger(ctx)) start = sObj->asLong(ctx);
            else if (sObj->isDouble(ctx) || sObj->isFloat(ctx))
                start = static_cast<long long>(sObj->asDouble(ctx));
        }
    }
    if (args->getSize(ctx) > 2) {
        const proto::ProtoObject* eObj = args->getAt(ctx, 2);
        if (eObj && eObj != PROTO_NONE) {
            if (eObj->isInteger(ctx)) end = eObj->asLong(ctx);
            else if (eObj->isDouble(ctx) || eObj->isFloat(ctx))
                end = static_cast<long long>(eObj->asDouble(ctx));
        }
    }

    target = normalizeIdxClamp(target, len);
    start  = normalizeIdxClamp(start,  len);
    end    = normalizeIdxClamp(end,    len);

    long long count = std::min(end - start, len - target);
    if (count <= 0) return self;

    // Read source range into a temporary buffer to handle overlaps.
    std::vector<const proto::ProtoObject*> tmp;
    tmp.reserve(static_cast<size_t>(count));
    for (long long i = 0; i < count; i++)
        tmp.push_back(arrGet(ctx, self, static_cast<unsigned long>(start + i)));
    for (long long i = 0; i < count; i++)
        arrSet(ctx, self, static_cast<unsigned long>(target + i),
               tmp[static_cast<size_t>(i)]);

    return self;
}

// ---------------------------------------------------------------------------
// JS truthiness helper used by callback-taking methods.
// ---------------------------------------------------------------------------
static bool isTruthy(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    if (!v || v == PROTO_NONE) return false;
    if (v == PROTO_TRUE) return true;
    if (v == PROTO_FALSE) return false;
    if (v->isBoolean(ctx)) return v->asBoolean(ctx);
    if (v->isInteger(ctx)) return v->asLong(ctx) != 0;
    if (v->isDouble(ctx) || v->isFloat(ctx)) {
        double d = v->asDouble(ctx);
        return d != 0.0 && !std::isnan(d);
    }
    if (v->isString(ctx)) {
        const proto::ProtoString* s = v->asString(ctx);
        return s && s->getSize(ctx) != 0;
    }
    return true; // objects / lists are truthy
}

// Helper: build the three-argument list [element, index, array] for iteration callbacks.
static const proto::ProtoList* makeIterArgs(proto::ProtoContext* ctx,
                                             const proto::ProtoObject* elem,
                                             long long idx,
                                             const proto::ProtoObject* arr) {
    const proto::ProtoList* a = ctx->newList();
    a = a->appendLast(ctx, elem ? elem : PROTO_NONE);
    a = a->appendLast(ctx, ctx->fromInteger(idx));
    a = a->appendLast(ctx, arr ? arr : PROTO_NONE);
    return a;
}

// Helper: extract callback and optional thisArg from args.
static const proto::ProtoObject* getCallbackArg(proto::ProtoContext* ctx,
                                                  const proto::ProtoList* args,
                                                  int pos = 0) {
    if (!args) return PROTO_NONE;
    long long n = static_cast<long long>(args->getSize(ctx));
    return (pos < n) ? args->getAt(ctx, pos) : PROTO_NONE;
}

// ---------------------------------------------------------------------------
// forEach(callback[, thisArg])
// ---------------------------------------------------------------------------
static const proto::ProtoObject* arrayForEach(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (arrayThrowIfNullUndefined(ctx, self)) return PROTO_NONE;
    const proto::ProtoObject* fn      = getCallbackArg(ctx, args, 0);
    const proto::ProtoObject* thisArg = getCallbackArg(ctx, args, 1);
    if (!fn || fn == PROTO_NONE) return PROTO_NONE;
    unsigned long len = arrLen(ctx, self);
    for (unsigned long i = 0; i < len; i++) {
        if (!arrHasProperty(ctx, self, i)) continue;
        callJSFunction(ctx, fn, thisArg, makeIterArgs(ctx, arrGet(ctx, self, i), (long long)i, self));
        if (hasCallException()) return PROTO_NONE;
    }
    return PROTO_NONE;
}

// ---------------------------------------------------------------------------
// map(callback[, thisArg])
// ---------------------------------------------------------------------------
static const proto::ProtoObject* arrayMap(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (arrayThrowIfNullUndefined(ctx, self)) return PROTO_NONE;
    const proto::ProtoObject* fn      = getCallbackArg(ctx, args, 0);
    const proto::ProtoObject* thisArg = getCallbackArg(ctx, args, 1);
    if (!fn || fn == PROTO_NONE) return PROTO_NONE;
    unsigned long len = arrLen(ctx, self);
    const proto::ProtoObject* result = arraySpeciesCreate(ctx, self, len);
    for (unsigned long i = 0; i < len; i++) {
        if (!arrHasProperty(ctx, self, i)) continue;
        const proto::ProtoObject* mapped =
            callJSFunction(ctx, fn, thisArg, makeIterArgs(ctx, arrGet(ctx, self, i), (long long)i, self));
        if (hasCallException()) return PROTO_NONE;
        result = arrSet(ctx, result, i, mapped ? mapped : PROTO_NONE);
    }
    return result;
}

// ---------------------------------------------------------------------------
// filter(callback[, thisArg])
// ---------------------------------------------------------------------------
static const proto::ProtoObject* arrayFilter(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (arrayThrowIfNullUndefined(ctx, self)) return PROTO_NONE;
    const proto::ProtoObject* fn      = getCallbackArg(ctx, args, 0);
    const proto::ProtoObject* thisArg = getCallbackArg(ctx, args, 1);
    if (!fn || fn == PROTO_NONE) return PROTO_NONE;
    unsigned long len = arrLen(ctx, self);
    const proto::ProtoObject* result = arraySpeciesCreate(ctx, self, 0);
    unsigned long outIdx = 0;
    for (unsigned long i = 0; i < len; i++) {
        if (!arrHasProperty(ctx, self, i)) continue;
        const proto::ProtoObject* elem = arrGet(ctx, self, i);
        const proto::ProtoObject* keep =
            callJSFunction(ctx, fn, thisArg, makeIterArgs(ctx, elem, (long long)i, self));
        if (hasCallException()) return PROTO_NONE;
        if (isTruthy(ctx, keep))
            result = arrSet(ctx, result, outIdx++, elem);
    }
    return result;
}

// ---------------------------------------------------------------------------
// find(callback[, thisArg])
// ---------------------------------------------------------------------------
static const proto::ProtoObject* arrayFind(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (arrayThrowIfNullUndefined(ctx, self)) return PROTO_NONE;
    const proto::ProtoObject* fn      = getCallbackArg(ctx, args, 0);
    const proto::ProtoObject* thisArg = getCallbackArg(ctx, args, 1);
    if (!fn || fn == PROTO_NONE) return PROTO_NONE;
    unsigned long len = arrLen(ctx, self);
    for (unsigned long i = 0; i < len; i++) {
        const proto::ProtoObject* elem = arrGet(ctx, self, i);
        const proto::ProtoObject* res  =
            callJSFunction(ctx, fn, thisArg, makeIterArgs(ctx, elem, (long long)i, self));
        if (hasCallException()) return PROTO_NONE;
        if (isTruthy(ctx, res)) return elem;
    }
    return PROTO_NONE;
}

// ---------------------------------------------------------------------------
// findIndex(callback[, thisArg])
// ---------------------------------------------------------------------------
static const proto::ProtoObject* arrayFindIndex(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (arrayThrowIfNullUndefined(ctx, self)) return PROTO_NONE;
    const proto::ProtoObject* fn      = getCallbackArg(ctx, args, 0);
    const proto::ProtoObject* thisArg = getCallbackArg(ctx, args, 1);
    if (!fn || fn == PROTO_NONE) return ctx->fromInteger(-1LL);
    unsigned long len = arrLen(ctx, self);
    for (unsigned long i = 0; i < len; i++) {
        const proto::ProtoObject* elem = arrGet(ctx, self, i);
        const proto::ProtoObject* res  =
            callJSFunction(ctx, fn, thisArg, makeIterArgs(ctx, elem, (long long)i, self));
        if (hasCallException()) return PROTO_NONE;
        if (isTruthy(ctx, res)) return ctx->fromInteger((long long)i);
    }
    return ctx->fromInteger(-1LL);
}

// ---------------------------------------------------------------------------
// findLast(callback[, thisArg])
// ---------------------------------------------------------------------------
static const proto::ProtoObject* arrayFindLast(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (arrayThrowIfNullUndefined(ctx, self)) return PROTO_NONE;
    const proto::ProtoObject* fn      = getCallbackArg(ctx, args, 0);
    const proto::ProtoObject* thisArg = getCallbackArg(ctx, args, 1);
    if (!fn || fn == PROTO_NONE) return PROTO_NONE;
    unsigned long len = arrLen(ctx, self);
    for (long long i = (long long)len - 1; i >= 0; i--) {
        const proto::ProtoObject* elem = arrGet(ctx, self, (unsigned long)i);
        const proto::ProtoObject* res  =
            callJSFunction(ctx, fn, thisArg, makeIterArgs(ctx, elem, i, self));
        if (hasCallException()) return PROTO_NONE;
        if (isTruthy(ctx, res)) return elem;
    }
    return PROTO_NONE;
}

// ---------------------------------------------------------------------------
// findLastIndex(callback[, thisArg])
// ---------------------------------------------------------------------------
static const proto::ProtoObject* arrayFindLastIndex(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (arrayThrowIfNullUndefined(ctx, self)) return PROTO_NONE;
    const proto::ProtoObject* fn      = getCallbackArg(ctx, args, 0);
    const proto::ProtoObject* thisArg = getCallbackArg(ctx, args, 1);
    if (!fn || fn == PROTO_NONE) return ctx->fromInteger(-1LL);
    unsigned long len = arrLen(ctx, self);
    for (long long i = (long long)len - 1; i >= 0; i--) {
        const proto::ProtoObject* elem = arrGet(ctx, self, (unsigned long)i);
        const proto::ProtoObject* res  =
            callJSFunction(ctx, fn, thisArg, makeIterArgs(ctx, elem, i, self));
        if (hasCallException()) return PROTO_NONE;
        if (isTruthy(ctx, res)) return ctx->fromInteger(i);
    }
    return ctx->fromInteger(-1LL);
}

// ---------------------------------------------------------------------------
// some(callback[, thisArg])
// ---------------------------------------------------------------------------
static const proto::ProtoObject* arraySome(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (arrayThrowIfNullUndefined(ctx, self)) return PROTO_NONE;
    const proto::ProtoObject* fn      = getCallbackArg(ctx, args, 0);
    const proto::ProtoObject* thisArg = getCallbackArg(ctx, args, 1);
    if (!fn || fn == PROTO_NONE) return PROTO_FALSE;
    unsigned long len = arrLen(ctx, self);
    for (unsigned long i = 0; i < len; i++) {
        if (!arrHasProperty(ctx, self, i)) continue;
        const proto::ProtoObject* res =
            callJSFunction(ctx, fn, thisArg, makeIterArgs(ctx, arrGet(ctx, self, i), (long long)i, self));
        if (hasCallException()) return PROTO_NONE;
        if (isTruthy(ctx, res)) return PROTO_TRUE;
    }
    return PROTO_FALSE;
}

// ---------------------------------------------------------------------------
// every(callback[, thisArg])
// ---------------------------------------------------------------------------
static const proto::ProtoObject* arrayEvery(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (arrayThrowIfNullUndefined(ctx, self)) return PROTO_NONE;
    const proto::ProtoObject* fn      = getCallbackArg(ctx, args, 0);
    const proto::ProtoObject* thisArg = getCallbackArg(ctx, args, 1);
    if (!fn || fn == PROTO_NONE) return PROTO_TRUE;
    unsigned long len = arrLen(ctx, self);
    for (unsigned long i = 0; i < len; i++) {
        if (!arrHasProperty(ctx, self, i)) continue;
        const proto::ProtoObject* res =
            callJSFunction(ctx, fn, thisArg, makeIterArgs(ctx, arrGet(ctx, self, i), (long long)i, self));
        if (hasCallException()) return PROTO_NONE;
        if (!isTruthy(ctx, res)) return PROTO_FALSE;
    }
    return PROTO_TRUE;
}

// Forward declaration (defined in the sort section below).
static bool arrHas(proto::ProtoContext* ctx, const proto::ProtoObject* arr, unsigned long idx);

// HasProperty check per ECMAScript spec — used by reduce/reduceRight.
// A property "exists" if:
//   - The object is a String primitive/wrapper and idx is within its length, OR
//   - A data key for idx exists anywhere in the prototype chain (includes accessor
//     properties whose data key is cleared to PROTO_NONE by defineProperty), OR
//   - A getter or setter sidecar for idx exists anywhere in the prototype chain.
// This avoids the false-negative of arrGet for setter-only accessors (Get returns
// undefined / PROTO_NONE, but HasProperty must still return true).
static bool arrHasProperty(proto::ProtoContext* ctx,
                            const proto::ProtoObject* arr,
                            unsigned long idx) {
    if (!arr || arr == PROTO_NONE) return false;

    // String primitive — every valid UTF-16 index has a character.
    if (arr->isString(ctx)) {
        return idx < arrLen(ctx, arr);
    }

    // String wrapper object — check __primitive_value__ length.
    {
        const proto::ProtoString* pvKey = JSSymbols::primitiveValue(ctx);
        if (pvKey) {
            const proto::ProtoObject* pv = arr->getAttribute(ctx, pvKey, false);
            if (pv && pv != PROTO_NONE && pv->isString(ctx)) {
                return idx < arrLen(ctx, arr);
            }
        }
    }

    // Native ProtoList storage: in-range index always has a value (PROTO_NONE
    // for padded slots, real value otherwise) — never a "hole" in the spec
    // sense.  Out-of-range falls through to the legacy attribute path
    // because user code may still write `arr[1000] = x` and we then drop
    // out of fast-path; see arrayTryFastSet's sparse-overflow branch.
    if (const proto::ProtoList* els = getArrayElements(ctx, arr)) {
        if (idx < static_cast<unsigned long>(els->getSize(ctx))) return true;
    }

    const proto::ProtoString* key = JSSymbols::indexKey(ctx, static_cast<uint32_t>(idx));
    if (!key) return false;

    // Data key (own or inherited) — includes accessor properties that cleared their
    // data slot to PROTO_NONE via Object.defineProperty.
    if (arr->hasAttribute(ctx, key) == PROTO_TRUE) return true;

    // Accessor sidecar (getter or setter), own or inherited.
    std::string gkStr = "__get_" + std::to_string(idx) + "__";
    std::string skStr = "__set_" + std::to_string(idx) + "__";
    const proto::ProtoObject* gko = ctx->fromUTF8String(gkStr.c_str());
    const proto::ProtoObject* sko = ctx->fromUTF8String(skStr.c_str());
    const proto::ProtoString* gk  = gko ? gko->asString(ctx) : nullptr;
    const proto::ProtoString* sk  = sko ? sko->asString(ctx) : nullptr;

    if (gk && arr->hasAttribute(ctx, gk) == PROTO_TRUE) return true;
    if (sk && arr->hasAttribute(ctx, sk) == PROTO_TRUE) return true;

    return false;
}

// ---------------------------------------------------------------------------
// reduce(callback[, initialValue])
// ---------------------------------------------------------------------------
static const proto::ProtoObject* arrayReduce(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (arrayThrowIfNullUndefined(ctx, self)) return PROTO_NONE;
    const proto::ProtoObject* fn = getCallbackArg(ctx, args, 0);
    if (!fn || fn == PROTO_NONE) return PROTO_NONE;
    long long len = (long long)arrLen(ctx, self);
    long long n   = args ? (long long)args->getSize(ctx) : 0LL;
    bool hasInit  = n >= 2;
    const proto::ProtoObject* acc;
    long long start;
    if (hasInit) {
        acc   = args->getAt(ctx, 1);
        start = 0;
    } else {
        // Find first non-hole element to use as accumulator (spec 23.1.3.26 step 8).
        start = -1;
        for (long long k = 0; k < len; k++) {
            if (arrHasProperty(ctx, self, static_cast<unsigned long>(k))) {
                acc   = arrGet(ctx, self, static_cast<unsigned long>(k));
                start = k + 1;
                break;
            }
        }
        if (start < 0) {
            signalNativeException(makeNativeError(ctx, "TypeError",
                "Reduce of empty array with no initial value"));
            return PROTO_NONE;
        }
    }
    for (long long i = start; i < len; i++) {
        // Skip holes — use HasProperty (includes prototype chain) per spec.
        if (!arrHasProperty(ctx, self, static_cast<unsigned long>(i))) continue;
        const proto::ProtoObject* elem = arrGet(ctx, self, (unsigned long)i);
        const proto::ProtoList* cbArgs = ctx->newList();
        cbArgs = cbArgs->appendLast(ctx, acc   ? acc   : PROTO_NONE);
        cbArgs = cbArgs->appendLast(ctx, elem  ? elem  : PROTO_NONE);
        cbArgs = cbArgs->appendLast(ctx, ctx->fromInteger(i));
        cbArgs = cbArgs->appendLast(ctx, self);
        acc = callJSFunction(ctx, fn, PROTO_NONE, cbArgs);
        if (hasCallException()) return PROTO_NONE;
    }
    return acc ? acc : PROTO_NONE;
}

// ---------------------------------------------------------------------------
// reduceRight(callback[, initialValue])
// ---------------------------------------------------------------------------
static const proto::ProtoObject* arrayReduceRight(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (arrayThrowIfNullUndefined(ctx, self)) return PROTO_NONE;
    const proto::ProtoObject* fn = getCallbackArg(ctx, args, 0);
    if (!fn || fn == PROTO_NONE) return PROTO_NONE;
    long long len = (long long)arrLen(ctx, self);
    long long n   = args ? (long long)args->getSize(ctx) : 0LL;
    bool hasInit  = n >= 2;
    const proto::ProtoObject* acc;
    long long start;
    if (hasInit) {
        acc   = args->getAt(ctx, 1);
        start = len - 1;
    } else {
        // Find last non-hole element to use as accumulator (spec 23.1.3.27 step 8).
        start = len;
        for (long long k = len - 1; k >= 0; k--) {
            if (arrHasProperty(ctx, self, static_cast<unsigned long>(k))) {
                acc   = arrGet(ctx, self, static_cast<unsigned long>(k));
                start = k - 1;
                break;
            }
        }
        if (start == len) {
            signalNativeException(makeNativeError(ctx, "TypeError",
                "Reduce of empty array with no initial value"));
            return PROTO_NONE;
        }
    }
    for (long long i = start; i >= 0; i--) {
        // Skip holes — use HasProperty (includes prototype chain) per spec.
        if (!arrHasProperty(ctx, self, static_cast<unsigned long>(i))) continue;
        const proto::ProtoObject* elem = arrGet(ctx, self, (unsigned long)i);
        const proto::ProtoList* cbArgs = ctx->newList();
        cbArgs = cbArgs->appendLast(ctx, acc   ? acc   : PROTO_NONE);
        cbArgs = cbArgs->appendLast(ctx, elem  ? elem  : PROTO_NONE);
        cbArgs = cbArgs->appendLast(ctx, ctx->fromInteger(i));
        cbArgs = cbArgs->appendLast(ctx, self);
        acc = callJSFunction(ctx, fn, PROTO_NONE, cbArgs);
        if (hasCallException()) return PROTO_NONE;
    }
    return acc ? acc : PROTO_NONE;
}

// ---------------------------------------------------------------------------
// sort([compareFn])
// ---------------------------------------------------------------------------

// Check whether the array has an own property at the given numeric index.
static bool arrHas(proto::ProtoContext* ctx,
                   const proto::ProtoObject* arr,
                   unsigned long idx) {
    if (!arr || arr == PROTO_NONE) return false;
    // Native ProtoList storage: every in-range index is "present" (PROTO_NONE
    // padded slots count as undefined-but-present, matching how arrays produced
    // by `[]` + push behave for sort/forEach/etc. — there are no real holes).
    if (const proto::ProtoList* els = getArrayElements(ctx, arr)) {
        if (idx < static_cast<unsigned long>(els->getSize(ctx))) return true;
    }
    const proto::ProtoString* key = JSSymbols::indexKey(ctx, static_cast<uint32_t>(idx));
    if (!key) return false;
    const proto::ProtoObject* result = arr->hasOwnAttribute(ctx, key);
    return result == PROTO_TRUE;
}

// Produce the sort key for an element, calling its toString() method when available.
// This implements the ES spec SortCompare step: "Let xString be ? ToString(x)".
// Both native (ProtoMethod) and bytecode JS closures are handled by callJSFunction.
static std::string sortKey(proto::ProtoContext* ctx,
                           const proto::ProtoObject* val) {
    if (!val || val == PROTO_NONE) return "";
    // Look up and invoke toString() — works for both native methods and JS closures.
    const proto::ProtoString* tsKey = ctx->fromUTF8String("toString")
        ? ctx->fromUTF8String("toString")->asString(ctx) : nullptr;
    if (tsKey) {
        const proto::ProtoObject* tsFn = val->getAttribute(ctx, tsKey, true);
        if (tsFn && tsFn != PROTO_NONE) {
            const proto::ProtoList* emptyArgs = ctx->newList();
            const proto::ProtoObject* result = callJSFunction(ctx, tsFn, val, emptyArgs);
            if (result && result != PROTO_NONE && result->isString(ctx)) {
                std::string r;
                if (const proto::ProtoString* s = result->asString(ctx)) {
                    s->toUTF8String(ctx, r);
                    return r;
                }
            }
        }
    }
    return elemToString(ctx, val);
}

static const proto::ProtoObject* arraySort(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (arrayThrowIfNullUndefined(ctx, self)) return PROTO_NONE;
    const proto::ProtoObject* fn = getCallbackArg(ctx, args, 0);
    bool hasFn = fn && fn != PROTO_NONE;

    unsigned long len = arrLen(ctx, self);
    if (len < 2) return self;

    // Separate elements into three categories per ECMAScript spec:
    //   1. Defined values  — present AND not undefined
    //   2. Undefined slots — present BUT value is undefined
    //   3. Holes           — absent (no own property at that index)
    // Sort order: defined (sorted) < undefined < holes.
    std::vector<const proto::ProtoObject*> defined;
    unsigned long undefinedCount = 0;
    unsigned long holeCount = 0;

    defined.reserve(len);
    for (unsigned long i = 0; i < len; i++) {
        if (!arrHas(ctx, self, i)) {
            holeCount++;
            continue;
        }
        const proto::ProtoObject* elem = arrGet(ctx, self, i);
        if (!elem || elem == PROTO_NONE) {
            undefinedCount++;
        } else {
            defined.push_back(elem);
        }
    }

    // Sort only the defined elements.
    auto less = [&](const proto::ProtoObject* a, const proto::ProtoObject* b) -> bool {
        if (hasFn) {
            const proto::ProtoList* cbArgs = ctx->newList();
            cbArgs = cbArgs->appendLast(ctx, a ? a : PROTO_NONE);
            cbArgs = cbArgs->appendLast(ctx, b ? b : PROTO_NONE);
            const proto::ProtoObject* res = callJSFunction(ctx, fn, PROTO_NONE, cbArgs);
            if (!res || res == PROTO_NONE) return false;
            if (res->isInteger(ctx)) return res->asLong(ctx) < 0;
            if (res->isDouble(ctx) || res->isFloat(ctx)) return res->asDouble(ctx) < 0.0;
            return false;
        }
        // Default: lexicographic by ToString(x) — invokes obj.toString() if available.
        return sortKey(ctx, a) < sortKey(ctx, b);
    };

    std::stable_sort(defined.begin(), defined.end(), less);

    // Write back: sorted defined values, then undefined, then holes (as undefined,
    // since protoCore has no attribute-delete; absent vs explicit-undefined is
    // indistinguishable via x[i] access anyway).
    unsigned long writeIdx = 0;
    for (const auto* v : defined)
        arrSet(ctx, self, writeIdx++, v);
    for (unsigned long i = 0; i < undefinedCount; i++)
        arrSet(ctx, self, writeIdx++, PROTO_NONE);
    for (unsigned long i = 0; i < holeCount; i++)
        arrSet(ctx, self, writeIdx++, PROTO_NONE);

    return self;
}

// ---------------------------------------------------------------------------
// flat([depth=1])  — no callback, flatten nested arrays.
// ---------------------------------------------------------------------------
static void flatInto(proto::ProtoContext* ctx,
                     const proto::ProtoObject* src,
                     const proto::ProtoObject*& dest,
                     unsigned long& outIdx,
                     int depth) {
    unsigned long len = arrLen(ctx, src);
    for (unsigned long i = 0; i < len; i++) {
        const proto::ProtoObject* elem = arrGet(ctx, src, i);
        bool isArr = elem && elem != PROTO_NONE &&
                     !elem->isString(ctx) && !elem->isInteger(ctx) &&
                     !elem->isDouble(ctx) && !elem->isBoolean(ctx) &&
                     arrLen(ctx, elem) > 0;
        if (isArr && depth > 0)
            flatInto(ctx, elem, dest, outIdx, depth - 1);
        else
            dest = arrSet(ctx, dest, outIdx++, elem);
    }
}

static const proto::ProtoObject* arrayFlat(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (arrayThrowIfNullUndefined(ctx, self)) return PROTO_NONE;
    int depth = 1;
    if (args && args->getSize(ctx) > 0) {
        const proto::ProtoObject* d = args->getAt(ctx, 0);
        if (d && d != PROTO_NONE && d->isInteger(ctx)) depth = (int)d->asLong(ctx);
        else if (d && d != PROTO_NONE && (d->isDouble(ctx) || d->isFloat(ctx)))
            depth = (int)d->asDouble(ctx);
    }
    const proto::ProtoObject* result = createNewArray(ctx, nullptr);
    unsigned long outIdx = 0;
    flatInto(ctx, self, result, outIdx, depth);
    return result;
}

// ---------------------------------------------------------------------------
// flatMap(callback[, thisArg])  — map then flat(1).
// ---------------------------------------------------------------------------
static const proto::ProtoObject* arrayFlatMap(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* pl,
    const proto::ProtoList* args,
    const proto::ProtoSparseList* kw)
{
    if (arrayThrowIfNullUndefined(ctx, self)) return PROTO_NONE;
    // map first
    const proto::ProtoObject* mapped = arrayMap(ctx, self, pl, args, kw);
    if (!mapped || mapped == PROTO_NONE) return PROTO_NONE;
    // flat(1)
    const proto::ProtoList* flatArgs = ctx->newList();
    flatArgs = flatArgs->appendLast(ctx, ctx->fromInteger(1LL));
    return arrayFlat(ctx, mapped, nullptr, flatArgs, nullptr);
}

// ---------------------------------------------------------------------------
// splice(start[, deleteCount[, ...items]])
// ---------------------------------------------------------------------------
static const proto::ProtoObject* arraySplice(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (arrayThrowIfNullUndefined(ctx, self)) return PROTO_NONE;
    long long len = (long long)arrLen(ctx, self);
    long long n   = args ? (long long)args->getSize(ctx) : 0LL;

    // Parse start.
    long long start = 0;
    if (n >= 1) {
        const proto::ProtoObject* sv = args->getAt(ctx, 0);
        if (sv && sv != PROTO_NONE) {
            if (sv->isInteger(ctx)) start = sv->asLong(ctx);
            else if (sv->isDouble(ctx) || sv->isFloat(ctx)) start = (long long)sv->asDouble(ctx);
        }
    }
    if (start < 0) { start += len; if (start < 0) start = 0; }
    if (start > len) start = len;

    // Parse deleteCount.
    long long delCount = len - start;
    if (n >= 2) {
        const proto::ProtoObject* dv = args->getAt(ctx, 1);
        if (dv && dv != PROTO_NONE) {
            if (dv->isInteger(ctx)) delCount = dv->asLong(ctx);
            else if (dv->isDouble(ctx) || dv->isFloat(ctx)) delCount = (long long)dv->asDouble(ctx);
        }
        if (delCount < 0) delCount = 0;
        if (delCount > len - start) delCount = len - start;
    }

    // Collect removed elements.
    const proto::ProtoObject* removed = createNewArray(ctx, nullptr);
    for (long long i = 0; i < delCount; i++)
        removed = arrSet(ctx, removed, (unsigned long)i, arrGet(ctx, self, (unsigned long)(start + i)));

    // Collect items to insert.
    long long insertCount = n >= 2 ? n - 2 : 0;

    // Collect elements after the removed section.
    std::vector<const proto::ProtoObject*> tail;
    for (long long i = start + delCount; i < len; i++)
        tail.push_back(arrGet(ctx, self, (unsigned long)i));

    // Write insert items starting at `start`.
    for (long long i = 0; i < insertCount; i++)
        arrSet(ctx, self, (unsigned long)(start + i), args->getAt(ctx, (int)(2 + i)));

    // Write tail after inserted items.
    long long tailStart = start + insertCount;
    for (size_t i = 0; i < tail.size(); i++)
        arrSet(ctx, self, (unsigned long)(tailStart + (long long)i), tail[i]);

    // Update length.
    long long newLen = len - delCount + insertCount;
    arrSetLen(ctx, self, (unsigned long)(newLen > 0 ? newLen : 0));

    return removed;
}

// ---------------------------------------------------------------------------
// at(index)  — supports negative indices.
// ---------------------------------------------------------------------------
static const proto::ProtoObject* arrayAt(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (arrayThrowIfNullUndefined(ctx, self)) return PROTO_NONE;
    long long len = (long long)arrLen(ctx, self);
    long long idx = 0;
    if (args && args->getSize(ctx) > 0) {
        const proto::ProtoObject* a = args->getAt(ctx, 0);
        if (a && a != PROTO_NONE) {
            if (a->isInteger(ctx)) idx = a->asLong(ctx);
            else if (a->isDouble(ctx) || a->isFloat(ctx)) idx = (long long)a->asDouble(ctx);
        }
    }
    if (idx < 0) idx += len;
    if (idx < 0 || idx >= len) return PROTO_NONE;
    return arrGet(ctx, self, (unsigned long)idx);
}

// ---------------------------------------------------------------------------
// Array iterators: entries(), keys(), values()
// ---------------------------------------------------------------------------

/** Native next() for all three iterator kinds (controlled by __iter_kind__ attribute). */
static const proto::ProtoObject* arrayIteratorNext(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*,
    const proto::ProtoSparseList*)
{
    if (!self || self == PROTO_NONE) {
        // Return {value: undefined, done: true}.
        const proto::ProtoObject* r = ctx->newObject(true);
        const proto::ProtoString* vk = JSSymbols::value(ctx);
        const proto::ProtoString* dk = JSSymbols::done(ctx);
        if (vk) r = r->setAttribute(ctx, vk, PROTO_NONE);
        if (dk) r = r->setAttribute(ctx, dk, PROTO_TRUE);
        return r;
    }

    const proto::ProtoString* idxKey  = JSSymbols::iterIdx(ctx);
    const proto::ProtoString* refKey  = JSSymbols::iterArr(ctx);
    const proto::ProtoString* kindKey = JSSymbols::iterKind(ctx);
    const proto::ProtoString* valueK  = JSSymbols::value(ctx);
    const proto::ProtoString* doneK   = JSSymbols::done(ctx);

    if (!idxKey || !refKey || !kindKey || !valueK || !doneK) return PROTO_NONE;

    const proto::ProtoObject* arrRef  = self->getAttribute(ctx, refKey,  false);
    const proto::ProtoObject* idxVal  = self->getAttribute(ctx, idxKey,  false);
    const proto::ProtoObject* kindObj = self->getAttribute(ctx, kindKey, false);

    long long idx = (idxVal && idxVal != PROTO_NONE && idxVal->isInteger(ctx))
                    ? idxVal->asLong(ctx) : 0LL;
    unsigned long arrLen_ = arrLen(ctx, arrRef);

    // Build result object.
    const proto::ProtoObject* r = ctx->newObject(true);

    if ((unsigned long)idx >= arrLen_) {
        // Iteration done.
        r = r->setAttribute(ctx, valueK, PROTO_NONE);
        r = r->setAttribute(ctx, doneK,  PROTO_TRUE);
        return r;
    }

    // Advance index in-place (self is mutable).
    const proto::ProtoObject* nextSelf = self->setAttribute(ctx, idxKey, ctx->fromInteger(idx + 1));

    // Determine value based on kind.
    std::string kind = "values";
    if (kindObj && kindObj != PROTO_NONE && kindObj->isString(ctx)) {
        const proto::ProtoString* ks = kindObj->asString(ctx);
        if (ks) ks->toUTF8String(ctx, kind);
    }

    const proto::ProtoObject* value;
    if (kind == "keys") {
        value = ctx->fromInteger(idx);
    } else if (kind == "entries") {
        // [index, element]
        const proto::ProtoObject* elem = arrGet(ctx, arrRef, (unsigned long)idx);
        const proto::ProtoObject* pair = createNewArray(ctx, nullptr);
        pair = arrSet(ctx, pair, 0, ctx->fromInteger(idx));
        pair = arrSet(ctx, pair, 1, elem);
        value = pair;
    } else { // "values"
        value = arrGet(ctx, arrRef, (unsigned long)idx);
    }

    r = r->setAttribute(ctx, valueK, value ? value : PROTO_NONE);
    r = r->setAttribute(ctx, doneK,  PROTO_FALSE);
    return r;
}

/** Create an iterator object for the given array and kind. */
static const proto::ProtoObject* makeArrayIterator(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* arr,
    const char* kind)
{
    const proto::ProtoObject* iter = ctx->newObject(true);
    const proto::ProtoString* idxKey  = JSSymbols::iterIdx(ctx);
    const proto::ProtoString* refKey  = JSSymbols::iterArr(ctx);
    const proto::ProtoString* kindKey = JSSymbols::iterKind(ctx);
    const proto::ProtoString* nextKey = JSSymbols::next(ctx);
    if (idxKey)  iter = iter->setAttribute(ctx, idxKey,  ctx->fromInteger(0LL));
    if (refKey)  iter = iter->setAttribute(ctx, refKey,  arr ? arr : PROTO_NONE);
    if (kindKey) iter = iter->setAttribute(ctx, kindKey, ctx->fromUTF8String(kind));
    if (nextKey) {
        const proto::ProtoObject* nextFn = ctx->fromMethod(nullptr, arrayIteratorNext);
        if (nextFn) iter = iter->setAttribute(ctx, nextKey, nextFn);
    }
    return iter;
}

static const proto::ProtoObject* arrayEntries(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{ return makeArrayIterator(ctx, self, "entries"); }

static const proto::ProtoObject* arrayKeys(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{ return makeArrayIterator(ctx, self, "keys"); }

static const proto::ProtoObject* arrayValues(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{ return makeArrayIterator(ctx, self, "values"); }

/** get Array[Symbol.species] */
static const proto::ProtoObject* arraySpeciesGetter(
    proto::ProtoContext* /*ctx*/,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*,
    const proto::ProtoSparseList*)
{
    return self;
}

// ---------------------------------------------------------------------------
// Array.isArray static method
// ---------------------------------------------------------------------------
static const proto::ProtoObject* arrayIsArray(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!args || args->getSize(ctx) == 0) return PROTO_FALSE;
    const proto::ProtoObject* val = args->getAt(ctx, 0);
    if (!val || val == PROTO_NONE) return PROTO_FALSE;
    const proto::ProtoString* isArrayKey = JSSymbols::isArray(ctx);
    if (isArrayKey) {
        if (val->hasOwnAttribute(ctx, isArrayKey) == PROTO_TRUE)
            return PROTO_TRUE;
    }
    return PROTO_FALSE;
}

// ---------------------------------------------------------------------------
// Array.from static method (iterable / array-like → new array)
// ---------------------------------------------------------------------------
static const proto::ProtoObject* arrayFrom(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!args || args->getSize(ctx) == 0) return createNewArray(ctx, nullptr);
    const proto::ProtoObject* src = args->getAt(ctx, 0);
    const proto::ProtoObject* result = createNewArray(ctx, nullptr);
    if (!src || src == PROTO_NONE) return result;

    // Optional map function (Array.from(src, mapFn[, thisArg])).
    const proto::ProtoObject* mapFn = (args->getSize(ctx) > 1) ? args->getAt(ctx, 1) : nullptr;
    const proto::ProtoObject* mapThis = (args->getSize(ctx) > 2) ? args->getAt(ctx, 2) : PROTO_NONE;
    if (mapFn == PROTO_NONE) mapFn = nullptr;
    auto applyMap = [&](const proto::ProtoObject* v, long long idx) -> const proto::ProtoObject* {
        if (!mapFn) return v;
        const proto::ProtoList* margs = ctx->newList();
        margs = margs->appendLast(ctx, v ? v : PROTO_NONE);
        margs = margs->appendLast(ctx, ctx->fromInteger(idx));
        return callJSFunction(ctx, mapFn, mapThis ? mapThis : PROTO_NONE, margs);
    };

    // First check Symbol.iterator for generators, Sets, Maps, etc.
    const proto::ProtoString* symIterKey = JSSymbols::symbolIterator(ctx);
    const proto::ProtoObject* iterFn = symIterKey
        ? src->getAttribute(ctx, symIterKey, true) : nullptr;
    if (iterFn && iterFn != PROTO_NONE) {
        const proto::ProtoList* noArgs = ctx->newList();
        const proto::ProtoObject* iter = callJSFunction(ctx, iterFn, src, noArgs);
        if (iter && iter != PROTO_NONE) {
            const proto::ProtoString* nextKey = JSSymbols::next(ctx);
            const proto::ProtoString* doneKey = JSSymbols::done(ctx);
            const proto::ProtoString* valueKey = JSSymbols::value(ctx);
            const proto::ProtoObject* nextFn = iter->getAttribute(ctx, nextKey, true);
            const proto::ProtoList* resultEls = ctx->newList();
            long long idx = 0;
            while (nextFn && nextFn != PROTO_NONE) {
                const proto::ProtoList* nArgs = ctx->newList();
                const proto::ProtoObject* res = callJSFunction(ctx, nextFn, iter, nArgs);
                if (!res || res == PROTO_NONE) break;
                const proto::ProtoObject* dv = res->getAttribute(ctx, doneKey, false);
                if (dv == PROTO_TRUE) break;
                const proto::ProtoObject* vv = res->getAttribute(ctx, valueKey, false);
                const proto::ProtoObject* mapped = applyMap(vv, idx);
                resultEls = resultEls->appendLast(ctx, mapped ? mapped : PROTO_NONE);
                idx++;
                if (idx > 100000) break; // safety
            }
            setArrayElements(ctx, result, resultEls);
            const proto::ProtoString* lk = JSSymbols::length(ctx);
            if (lk) result = result->setAttribute(ctx, lk, ctx->fromInteger(idx));
            return result;
        }
    }

    // If src has a "length", iterate by index (array-like).
    const proto::ProtoString* lenKey = JSSymbols::length(ctx);
    if (!lenKey) return result;
    const proto::ProtoObject* lv = src->getAttribute(ctx, lenKey, false);
    if (lv && lv != PROTO_NONE &&
        (lv->isInteger(ctx) || lv->isDouble(ctx) || lv->isFloat(ctx))) {
        unsigned long n = static_cast<unsigned long>(
            lv->isInteger(ctx) ? lv->asLong(ctx)
                               : static_cast<long long>(lv->asDouble(ctx)));
        for (unsigned long i = 0; i < n; i++) {
            const proto::ProtoObject* v = arrGet(ctx, src, i);
            arrSet(ctx, result, i, applyMap(v, static_cast<long long>(i)));
        }
        result = arrSetLen(ctx, result, n);
        return result;
    }

    // String: iterate characters.
    if (src->isString(ctx)) {
        const proto::ProtoString* s = src->asString(ctx);
        if (s) {
            std::string str;
            s->toUTF8String(ctx, str);
            unsigned long i = 0;
            for (unsigned char c : str) {
                char buf[2] = {static_cast<char>(c), '\0'};
                const proto::ProtoObject* v = ctx->fromUTF8String(buf);
                arrSet(ctx, result, i, applyMap(v, static_cast<long long>(i)));
                i++;
            }
            result = arrSetLen(ctx, result, i);
        }
        return result;
    }

    return result;
}

static const proto::ProtoObject* arrayFromAsync(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    // Throw a TypeError if not called properly.
    if (!args || args->getSize(ctx) == 0) {
        return PROTO_NONE;
    }
    const proto::ProtoObject* src = args->getAt(ctx, 0);
    if (!src || src == PROTO_NONE) {
        return PROTO_NONE;
    }
    return PROTO_NONE;
}

// ---------------------------------------------------------------------------
// Array.of static method
// ---------------------------------------------------------------------------
static const proto::ProtoObject* arrayOf(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    const proto::ProtoObject* result;
    const proto::ProtoString* constructKey = ctx->fromUTF8String("__construct__")->asString(ctx);
    const proto::ProtoObject* constructFn = (constructKey && self && self != PROTO_NONE) ? self->getAttribute(ctx, constructKey, false) : nullptr;
    bool isCtor = constructFn && constructFn != PROTO_NONE && constructFn->isMethod(ctx);
    unsigned long argc = args ? static_cast<unsigned long>(args->getSize(ctx)) : 0;

    if (isCtor) {
        const proto::ProtoString* protoKey = JSSymbols::prototype(ctx);
        const proto::ProtoObject* proto = self->getAttribute(ctx, protoKey, true);
        result = (proto && proto != PROTO_NONE) ? proto->newChild(ctx, true) : ctx->newObject(true);
        const proto::ProtoList* ctorArgs = ctx->newList();
        ctorArgs = ctorArgs->appendLast(ctx, ctx->fromInteger(static_cast<long long>(argc)));
        proto::ProtoMethod fn = constructFn->asMethod(ctx);
        const proto::ProtoObject* res = fn(ctx, result, nullptr, ctorArgs, nullptr);
        if (res && res != PROTO_NONE && !res->isInteger(ctx) && !res->isDouble(ctx) && !res->asString(ctx) && res != PROTO_TRUE && res != PROTO_FALSE)
            result = res;
    } else {
        result = createNewArray(ctx, nullptr);
    }
    
    for (unsigned long i = 0; i < argc; i++)
        arrSet(ctx, result, i, args->getAt(ctx, static_cast<int>(i)));
    result = arrSetLen(ctx, result, argc);
    return result;
}

// ---------------------------------------------------------------------------
// ensureArrayPrototype — idempotent, called once per runBytecode invocation.
// ---------------------------------------------------------------------------
void ensureArrayPrototype(proto::ProtoContext* ctx,
                          const proto::ProtoObject** globalRoot) {
    if (!ctx || !globalRoot || !*globalRoot) return;

    // Check if already initialised.
    const proto::ProtoString* arrayKey = JSSymbols::Array(ctx);
    if (!arrayKey) return;
    const proto::ProtoObject* existing =
        (*globalRoot)->getAttribute(ctx, arrayKey, false);
    if (existing && existing != PROTO_NONE) return;

    // ------------------------------------------------------------------
    // Build Array.prototype (immutable shared prototype).
    // Parent: objectPrototype (so Object.prototype methods are inherited).
    // ------------------------------------------------------------------
    const proto::ProtoObject* objectProto = ctx->space->objectPrototype;
    // Must be mutable so JS-level mutations (Array.prototype.x = y) modify the
    // object in-place.  An immutable prototype would produce a new snapshot on
    // every setAttribute, causing s_arrayProto to go stale and breaking
    // Array.prototype extension from user code.
    const proto::ProtoObject* proto = objectProto
        ? objectProto->newChild(ctx, true)
        : ctx->newObject(true);

    // Convenience: add each method.
    struct { const char* name; proto::ProtoMethod fn; long long length; } methods[] = {
        // Non-callback methods (Phase 9)
        { "join",           arrayJoin,          1 },
        { "toString",       arrayToString,      0 },
        { "push",           arrayPush,          1 },
        { "pop",            arrayPop,           0 },
        { "shift",          arrayShift,         0 },
        { "unshift",        arrayUnshift,       1 },
        { "slice",          arraySlice,         2 },
        { "indexOf",        arrayIndexOf,       1 },
        { "lastIndexOf",    arrayLastIndexOf,   1 },
        { "includes",       arrayIncludes,      1 },
        { "reverse",        arrayReverse,       0 },
        { "concat",         arrayConcat,        1 },
        { "fill",           arrayFill,          1 },
        { "copyWithin",     arrayCopyWithin,    2 },
        { "splice",         arraySplice,        2 },
        { "at",             arrayAt,            1 },
        // Callback methods (Phase 10)
        { "forEach",        arrayForEach,       1 },
        { "map",            arrayMap,           1 },
        { "filter",         arrayFilter,        1 },
        { "find",           arrayFind,          1 },
        { "findIndex",      arrayFindIndex,     1 },
        { "findLast",       arrayFindLast,      1 },
        { "findLastIndex",  arrayFindLastIndex, 1 },
        { "some",           arraySome,          1 },
        { "every",          arrayEvery,         1 },
        { "reduce",         arrayReduce,        1 },
        { "reduceRight",    arrayReduceRight,   1 },
        { "sort",           arraySort,          1 },
        { "flat",           arrayFlat,          0 },
        { "flatMap",        arrayFlatMap,       1 },
        // Iterators (Phase 10)
        { "entries",        arrayEntries,       0 },
        { "keys",           arrayKeys,          0 },
        { "values",         arrayValues,        0 },
    };
    for (auto& m : methods) {
        const proto::ProtoString* key = ctx->fromUTF8String(m.name)->asString(ctx);
        if (key) {
            const proto::ProtoObject* fn = wrapNativeFunction(ctx, m.fn, m.name, m.length, globalRoot);
            if (fn && fn != PROTO_NONE) {
                proto = proto->setAttribute(ctx, key, fn);
                // ECMA-262: Array.prototype methods are
                // { writable:true, enumerable:false, configurable:true }
                // bits: writable(0x1) | configurable(0x2) = 0x3.
                // Pre-fix the descriptor was absent → defaulted to all-true
                // (incl. enumerable), so for-in over any array enumerated
                // every method ("at,map,pop,find,...") alongside the indices.
                std::string pdName = std::string("__pd_") + m.name + "__";
                const proto::ProtoObject* pdo = ctx->fromUTF8String(pdName.c_str());
                const proto::ProtoString* pdk = pdo ? pdo->asString(ctx) : nullptr;
                if (pdk) proto = proto->setAttribute(ctx, pdk, ctx->fromInteger(0x3LL));
            }
        }
    }

    // Array.prototype has a length property (0, non-configurable).
    const proto::ProtoString* lengthKey = JSSymbols::length(ctx);
    if (lengthKey) {
        proto = proto->setAttribute(ctx, lengthKey, ctx->fromInteger(0));
        const proto::ProtoString* pdlk = ctx->fromUTF8String("__pd_length__")->asString(ctx);
        if (pdlk) proto = proto->setAttribute(ctx, pdlk, ctx->fromInteger(0x1)); // writable, !configurable, !enumerable
    }

    // Array.prototype is itself an array (exotic object).
    const proto::ProtoString* isArrKey = JSSymbols::isArray(ctx);
    if (isArrKey) proto = proto->setAttribute(ctx, isArrKey, PROTO_TRUE);

    // Store in module-level static for createNewArray.
    s_arrayProto = proto;

    // ------------------------------------------------------------------
    // Build the Array constructor object.
    // ------------------------------------------------------------------
    const proto::ProtoObject* ctor = ctx->newObject(false);

    const proto::ProtoString* markerKey =
        JSSymbols::arrayCtor(ctx);
    if (markerKey) ctor = ctor->setAttribute(ctx, markerKey, PROTO_TRUE);

    // Explicitly mark as a constructor for OP_call_constructor.
    const proto::ProtoString* isCtorKey = ctx->fromUTF8String("__is_constructor__")->asString(ctx);
    if (isCtorKey) ctor = ctor->setAttribute(ctx, isCtorKey, PROTO_TRUE);

    const proto::ProtoString* protoKey =
        JSSymbols::prototype(ctx);
    if (protoKey) ctor = ctor->setAttribute(ctx, protoKey, proto);

    const proto::ProtoString* nameKey = JSSymbols::name(ctx);
    if (nameKey) {
        ctor = ctor->setAttribute(ctx, nameKey, ctx->fromUTF8String("Array"));
        const proto::ProtoString* pdnk = ctx->fromUTF8String("__pd_name__")->asString(ctx);
        if (pdnk) ctor = ctor->setAttribute(ctx, pdnk, ctx->fromInteger(0x2)); // configurable, !writable, !enumerable
    }
    lengthKey = JSSymbols::length(ctx);
    if (lengthKey) {
        ctor = ctor->setAttribute(ctx, lengthKey, ctx->fromInteger(1));
        const proto::ProtoString* pdlk = ctx->fromUTF8String("__pd_length__")->asString(ctx);
        if (pdlk) ctor = ctor->setAttribute(ctx, pdlk, ctx->fromInteger(0x2)); // configurable, !writable, !enumerable
    }

    // Add static methods: isArray, from, of.
    struct { const char* name; proto::ProtoMethod fn; long long length; } statics[] = {
        { "isArray",   arrayIsArray,   1 },
        { "from",      arrayFrom,      1 },
        { "fromAsync", arrayFromAsync, 1 },
        { "of",        arrayOf,        0 },
    };
    for (auto& s : statics) {
        const proto::ProtoString* key = ctx->fromUTF8String(s.name)->asString(ctx);
        if (key) {
            const proto::ProtoObject* fn = wrapNativeFunction(ctx, s.fn, s.name, s.length, globalRoot);
            if (fn && fn != PROTO_NONE) {
                ctor = ctor->setAttribute(ctx, key, fn);
                // Set descriptor: {writable: true, enumerable: false, configurable: true}
                // bits: 0=1 (w), 1=1 (c), 2=0 (e) -> 0x3
                std::string pdKeyStr = "__pd_" + std::string(s.name) + "__";
                const proto::ProtoString* pdk = ctx->fromUTF8String(pdKeyStr.c_str())->asString(ctx);
                if (pdk) ctor = ctor->setAttribute(ctx, pdk, ctx->fromInteger(0x3));
            }
        }
    }

    // get Array[Symbol.species]
    {
        const proto::ProtoString* speciesKey = JSSymbols::symbolSpecies(ctx);
        if (speciesKey) {
            const proto::ProtoObject* getter = ctx->fromMethod(nullptr, arraySpeciesGetter);
            if (getter) {
                // Register as a getter using the internal __get_<name>__ convention.
                // Well-known symbols use their string representation "Symbol.species".
                const proto::ProtoString* gksSym = ctx->fromUTF8String("__get_Symbol.species__")->asString(ctx);
                if (gksSym) ctor = ctor->setAttribute(ctx, gksSym, getter);
            }
        }
    }

    // Set Array.prototype.constructor = Array (required by ECMAScript).
    {
        const proto::ProtoString* ctorKey = JSSymbols::constructor(ctx);
        if (ctorKey) {
            proto = proto->setAttribute(ctx, ctorKey, ctor);
            s_arrayProto = proto;
        }
    }

    // Symbol.iterator = values
    {
        const proto::ProtoString* symIterKey = JSSymbols::symbolIterator(ctx);
        if (symIterKey) {
            const proto::ProtoString* valuesKey = ctx->fromUTF8String("values")->asString(ctx);
            const proto::ProtoObject* valuesFn = valuesKey
                ? proto->getAttribute(ctx, valuesKey, false) : nullptr;
            if (valuesFn && valuesFn != PROTO_NONE) {
                proto = proto->setAttribute(ctx, symIterKey, valuesFn);
                const proto::ProtoString* pdk = ctx->fromUTF8String("__pd_Symbol.iterator__")->asString(ctx);
                if (pdk) proto = proto->setAttribute(ctx, pdk, ctx->fromInteger(0x3LL));
            }
        }
    }

    // ------------------------------------------------------------------
    // Register on global root.
    // ------------------------------------------------------------------
    *globalRoot = (*globalRoot)->setAttribute(ctx, arrayKey, ctor);

    // Fast-path key "__array_proto__" for OP_array_from lookup.
    const proto::ProtoString* fastProtoKey =
        JSSymbols::arrayProto(ctx);
    if (fastProtoKey)
        *globalRoot = (*globalRoot)->setAttribute(ctx, fastProtoKey, proto);
}

} // namespace protojs
