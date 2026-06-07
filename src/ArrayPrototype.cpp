#include "ArrayPrototype.h"
#include "ArrayElementsStorage.h"
#include "FunctionPrototype.h"
#include "JSContext.h"
#include "ObjectPrototype.h"
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
static bool arrayThrowIfCreateDataPropertyFails(proto::ProtoContext* ctx, const proto::ProtoObject* obj, unsigned long idx);
static const proto::ProtoObject* arrayCreateDataPropertyOrThrow(proto::ProtoContext* ctx, const proto::ProtoObject* obj, unsigned long idx, const proto::ProtoObject* val);

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
    // FAST PATH: native ProtoList-backed dense array.  The list size
    // is normally the authoritative length, but mixed literals like
    // `[a, b, , c]` (holes) and sparse writes (`arr[42] = x`) leave
    // entries living as string-keyed attributes while __elements__
    // tracks only the densely-written prefix.  In those cases the
    // canonical `length` attribute is larger; honour whichever is
    // bigger so consumers see every index up to the true length.
    if (const proto::ProtoList* els = getArrayElements(ctx, arr)) {
        unsigned long elsSize = static_cast<unsigned long>(els->getSize(ctx));
        const proto::ProtoString* lk = JSSymbols::length(ctx);
        if (lk) {
            const proto::ProtoObject* lv = arr->getAttribute(ctx, lk, false);
            if (lv && lv != PROTO_NONE) {
                long long lvN = 0;
                if (lv->isInteger(ctx)) lvN = lv->asLong(ctx);
                else if (lv->isDouble(ctx) || lv->isFloat(ctx)) {
                    double d = lv->asDouble(ctx);
                    if (!std::isnan(d) && !std::isinf(d) && d >= 0)
                        lvN = static_cast<long long>(d);
                }
                if (lvN > static_cast<long long>(elsSize))
                    return static_cast<unsigned long>(lvN);
            }
        }
        return elsSize;
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
                // §7.1.20 ToLength = ToIntegerOrInfinity ∘ ToNumber.
                // When the length getter returns an Object (e.g. an
                // {valueOf: …} wrapper), ToNumber must invoke that
                // valueOf to extract the spec-required numeric value.
                // Pre-fix the getter-result path returned 0 without
                // running ToNumber, so step 5 indexOf-side effects
                // never observed step 3 (built-ins/Array/prototype/
                // indexOf/15.4.4.14-5-27).
                {
                    const proto::ProtoObject* num = jsToNumber(ctx, fromGetter);
                    if (hasCallException() || !num || num == PROTO_NONE) return 0;
                    if (num->isInteger(ctx)) {
                        long long v = num->asLong(ctx);
                        return (v > 0) ? static_cast<unsigned long>(v) : 0;
                    }
                    if (num->isDouble(ctx) || num->isFloat(ctx)) {
                        double d = num->asDouble(ctx);
                        if (d <= 0 || std::isnan(d)) return 0;
                        if (std::isinf(d)) return static_cast<unsigned long>(0xFFFFFFFFul);
                        return static_cast<unsigned long>(d);
                    }
                }
                return 0;
            }
            // Own setter-only "length" accessor — no getter → length is undefined → treat as 0.
            return 0;
        }
    }

    const proto::ProtoObject* lenObj = arr->getAttribute(ctx, key, true);
    // §10.4.2 LengthOfArrayLike + §6.2.5 IsAccessorDescriptor: when
    // length is an accessor descriptor stored on the prototype chain,
    // the raw getAttribute returns the descriptor's data slot (which
    // is empty / a marker), NOT the getter's invocation result.
    // Probe __get_length__ on the chain too and route through it
    // whenever the data slot does not surface a numeric / string /
    // boolean value (built-ins/Array/prototype/indexOf/15.4.4.14-2-10
    // and the wider 'length is inherited accessor' family).
    bool lenIsUsable = lenObj && (lenObj->isInteger(ctx) || lenObj->isDouble(ctx)
                                  || lenObj->isFloat(ctx) || lenObj->isString(ctx)
                                  || lenObj == PROTO_TRUE || lenObj == PROTO_FALSE);
    if (!lenIsUsable) {
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
        if (std::isnan(d) || d <= 0) return 0;
        // ECMA-262 §7.1.20 ToLength clamps to min(len, 2^53-1).
        // +Infinity should therefore expose the maximum array
        // length and let the iteration helpers find any present
        // index (typically very small ones).  Pre-fix `length:
        // Infinity` collapsed to 0 so `{0:0, length: Infinity}`
        // never even probed index 0.  Cap at 2^32-1 to match the
        // standard array-length envelope; nothing useful comes of
        // larger indices.
        if (std::isinf(d)) return static_cast<unsigned long>(0xFFFFFFFFul);
        if (d > static_cast<double>(0xFFFFFFFFul)) return 0xFFFFFFFFul;
        return static_cast<unsigned long>(d);
    }
    // Boolean-encoded length: ECMA-262 §7.1.4 ToNumber(true)=1,
    // ToNumber(false)=0, followed by §7.1.20 ToLength.  Pre-fix
    // `{length:true}` evaluated to length 0 and every iteration
    // helper (reduce/forEach/map/filter/etc.) short-circuited
    // without invoking the callback.
    if (lenObj == PROTO_TRUE)  return 1;
    if (lenObj == PROTO_FALSE) return 0;
    // String-encoded length — try parsing, handle hex (e.g. "0x0002").
    // §7.1.4 ToNumber + §7.1.20 ToLength also accepts the literal
    // 'Infinity' / '+Infinity' / '-Infinity' forms.  Pre-fix the
    // stoll parse threw on those and the value collapsed to 0
    // (built-ins/Array/prototype/indexOf/15.4.4.14-3-14 with
    // length: 'Infinity' / '+Infinity' / '-Infinity').
    if (lenObj->isString(ctx)) {
        const proto::ProtoString* s = lenObj->asString(ctx);
        if (s) {
            std::string sv;
            s->toUTF8String(ctx, sv);
            // Trim leading whitespace per §7.1.4 StringToNumber.
            size_t firstNonWS = sv.find_first_not_of(" \t\n\r\v\f");
            std::string trimmed = (firstNonWS == std::string::npos) ? "" : sv.substr(firstNonWS);
            if (trimmed == "Infinity" || trimmed == "+Infinity")
                return static_cast<unsigned long>(0xFFFFFFFFul);
            if (trimmed == "-Infinity")
                return 0;
            // Strict whole-string parse: stoll/stod accept partial matches
            // ("123abc" → 123) but ECMA-262 §7.1.4 ToNumber returns NaN
            // for any trailing garbage; we must surface NaN → ToLength
            // → 0.  Use the appropriate parser (stoll for hex/integer,
            // stod for everything else including "2.5", "2E0",
            // "0002.00") with `pos` and require the parse to consume
            // the entire trimmed value (modulo trailing WS).
            size_t lastNonWS = trimmed.find_last_not_of(" \t\n\r\v\f");
            size_t consumedEnd = (lastNonWS == std::string::npos) ? 0 : lastNonWS + 1;
            bool isHex = (trimmed.size() > 2 && trimmed[0] == '0' && (trimmed[1] == 'x' || trimmed[1] == 'X'));
            if (isHex) {
                try {
                    size_t pos = 0;
                    long long v = std::stoll(trimmed, &pos, 16);
                    if (pos >= consumedEnd)
                        return (v > 0) ? static_cast<unsigned long>(v) : 0;
                } catch (...) {}
                return 0;
            }
            try {
                size_t pos = 0;
                double d = std::stod(trimmed, &pos);
                if (pos >= consumedEnd) {
                    if (std::isnan(d) || d <= 0) return 0;
                    if (std::isinf(d)) return static_cast<unsigned long>(0xFFFFFFFFul);
                    if (d > static_cast<double>(0xFFFFFFFFul)) return 0xFFFFFFFFul;
                    return static_cast<unsigned long>(d);
                }
                // Trailing garbage → ToNumber = NaN → ToLength = 0.
                return 0;
            } catch (...) {}
        }
    }
    // Fall back to full ToNumber for any other shape (objects with
    // valueOf/toString, etc.).  ECMA-262 §7.1.20 ToLength is
    // ToIntegerOrInfinity ∘ ToNumber.  Pre-fix `{length: {toString:
    // () => '2'}}` was treated as length 0 because the object branch
    // never reached the coercion logic that `Array.from` etc. already
    // apply elsewhere in this file.
    {
        const proto::ProtoObject* num = jsToNumber(ctx, lenObj);
        if (hasCallException()) return 0;
        if (num && num != PROTO_NONE) {
            if (num->isInteger(ctx)) {
                long long v = num->asLong(ctx);
                return (v > 0) ? static_cast<unsigned long>(v) : 0;
            }
            if (num->isDouble(ctx) || num->isFloat(ctx)) {
                double d = num->asDouble(ctx);
                if (d <= 0 || std::isnan(d) || std::isinf(d)) return 0;
                return static_cast<unsigned long>(d);
            }
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
    // Build accessor sidecar keys up-front so we can shadow the fast
    // path when an own accessor is installed.  §10.1.5 OrdinaryGet:
    // an own accessor descriptor takes precedence over any data slot
    // — including the native ProtoList storage real arrays use.
    // Pre-fix Object.defineProperty(arr, '0', {get:...}) on
    // arr=[0,1,2,'last'] left __elements__[0]=0 in place, so
    // arrayTryFastGet returned 0 and the getter never fired; tests
    // like every / indexOf / forEach with a length-truncating getter
    // on index 0 silently iterated all four slots
    // (built-ins/Array/prototype/indexOf/15.4.4.14-9-a-17 + family).
    const proto::ProtoString* key = JSSymbols::indexKey(ctx, static_cast<uint32_t>(idx));
    if (!key) return PROTO_NONE;
    std::string gkStr = "__get_" + std::to_string(idx) + "__";
    std::string skStr = "__set_" + std::to_string(idx) + "__";
    const proto::ProtoObject* gko = ctx->fromUTF8String(gkStr.c_str());
    const proto::ProtoObject* sko = ctx->fromUTF8String(skStr.c_str());
    const proto::ProtoString* gk  = gko ? gko->asString(ctx) : nullptr;
    const proto::ProtoString* sk  = sko ? sko->asString(ctx) : nullptr;

    // Step 1: OWN accessor probe BEFORE the data fast path.  hasOwnAttribute
    // hits the per-thread accessor cache after the first probe, so the
    // perf cost on hot reads is one branch — negligible compared to a
    // mis-ordered semantic that loses the getter call entirely.
    bool hasOwnGetter = gk && arr->hasOwnAttribute(ctx, gk) == PROTO_TRUE;
    bool hasOwnSetter = sk && arr->hasOwnAttribute(ctx, sk) == PROTO_TRUE;

    if (hasOwnGetter) {
        const proto::ProtoObject* ownGetter = arr->getAttribute(ctx, gk, true);
        const proto::ProtoObject* result = callJSFunction(ctx, ownGetter, arr, ctx->newList());
        return (hasCallException() || !result || result == PROTO_NONE) ? PROTO_NONE : result;
    }
    if (hasOwnSetter) {
        // Setter-only own accessor — read returns undefined.
        return PROTO_NONE;
    }

    // Step 2: FAST PATH (no own accessor) — native ProtoList storage.
    // In-range read returns the element directly; out-of-range falls
    // through to the string-key / accessor path below.
    if (const proto::ProtoObject* fastVal = arrayTryFastGet(ctx, arr, idx)) {
        if (fastVal != PROTO_NONE) return fastVal;
    }

    // Step 3: OWN data slot — overrides any inherited accessor per
    // §10.1.5 OrdinaryGet (own descriptor wins).
    if (arr->hasOwnAttribute(ctx, key) == PROTO_TRUE) {
        const proto::ProtoObject* val = arr->getAttribute(ctx, key, false);
        if (val && val != PROTO_NONE) return val;
    }

    // Step 4: Probe INHERITED accessor getter BEFORE the inherited
    // raw data read.  When Object.defineProperty installs an accessor
    // on the prototype chain, the data slot at the key may be a
    // placeholder (undefined sentinel).  Reading raw data first would
    // return that placeholder and never reach the inherited getter
    // (built-ins/Array/prototype/pop/set-length-array-length-is-non-
    // writable).
    if (gk) {
        const proto::ProtoObject* inheritedGetter = arr->getAttribute(ctx, gk, true);
        if (inheritedGetter && inheritedGetter != PROTO_NONE) {
            const proto::ProtoObject* result = callJSFunction(ctx, inheritedGetter, arr, ctx->newList());
            if (hasCallException()) return PROTO_NONE;
            if (result && result != PROTO_NONE) return result;
        }
    }

    // Step 5: inherited data slot.
    const proto::ProtoObject* val = arr->getAttribute(ctx, key, true);
    if (val && val != PROTO_NONE) return val;

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
        // §9.1.9 OrdinarySet: if no OWN descriptor exists at idx but
        // an INHERITED [[Set]] does, invoke the setter and do NOT
        // create an own data property.  When idx is past the dense
        // __elements__ tail, the slot has no own descriptor — probe
        // Array.prototype[idx] (or higher) for an inherited setter
        // and dispatch.  Pre-fix the fast path wrote __elements__
        // unconditionally, so `Object.defineProperty(Array.prototype,
        // '0', {set: f}); arr.unshift(1)` never fired f.
        const proto::ProtoList* els = getArrayElements(ctx, arr);
        unsigned long elsSize = els
            ? static_cast<unsigned long>(els->getSize(ctx)) : 0;
        // Per-prototype hint: the __set_<idx>__ probe walks the
        // prototype chain constructing a fresh ProtoString rope per
        // call.  Object.defineProperty tags any target getting an
        // indexed-key accessor with __has_indexed_setters__ = PROTO_TRUE;
        // arrays inherit the flag from Array.prototype through the
        // chain via hasAttribute(...,true).  When the flag is absent or
        // PROTO_FALSE, no indexed setter exists anywhere reachable and
        // the per-element probe is skippable.
        bool maybeHasIndexedSetters = false;
        if (idx >= elsSize) {
            const proto::ProtoString* hisKey = JSSymbols::hasIndexedSetters(ctx);
            maybeHasIndexedSetters = hisKey
                && (arr->hasAttribute(ctx, hisKey) == PROTO_TRUE)
                && (arr->getAttribute(ctx, hisKey, true) == PROTO_TRUE);
        }
        if (idx >= elsSize && maybeHasIndexedSetters) {
            std::string skStr = "__set_" + std::to_string(idx) + "__";
            const proto::ProtoObject* sko = ctx->fromUTF8String(skStr.c_str());
            const proto::ProtoString* sk  = sko ? sko->asString(ctx) : nullptr;
            if (sk && arr->hasAttribute(ctx, sk) == PROTO_TRUE
                && arr->hasOwnAttribute(ctx, sk) != PROTO_TRUE) {
                const proto::ProtoObject* setter = arr->getAttribute(ctx, sk, true);
                if (setter && setter != PROTO_NONE) {
                    const proto::ProtoList* sargs = ctx->newList();
                    sargs = sargs->appendLast(ctx, val ? val : PROTO_NONE);
                    callJSFunction(ctx, setter, arr, sargs);
                    if (hasCallException()) return arr;
                    return arr;
                }
            }
        }
        if (arrayTryFastSet(ctx, arr, idx, val)) {
            // If the slot also has a string-keyed attribute (from a
            // prior Object.defineProperty that stored the value as
            // an attribute rather than into __elements__), update it
            // too so Object.getOwnPropertyDescriptor reads the new
            // value.  Pre-fix the attribute lagged __elements__ and
            // descriptor.value drifted from the actual element value
            // (built-ins/Array/prototype/{slice,filter,map,...}/
            // target-array-with-non-writable-property family).
            const proto::ProtoString* idxKey =
                JSSymbols::indexKey(ctx, static_cast<uint32_t>(idx));
            if (idxKey && arr->hasOwnAttribute(ctx, idxKey) == PROTO_TRUE) {
                arr = arr->setAttribute(ctx, idxKey, val ? val : PROTO_NONE);
            }
            return arr;
        }
        // Sparse-overflow (idx - size > kSparseFallbackThreshold).
        // Falls through to the legacy string-keyed path.
    }

    const proto::ProtoString* key = JSSymbols::indexKey(ctx, static_cast<uint32_t>(idx));
    if (!key) return arr;

    // §9.1.9 OrdinarySet → §10.1.8 OrdinarySetWithOwnDescriptor: an
    // accessor descriptor on the prototype chain whose [[Set]] is
    // defined must be invoked instead of creating an own data slot.
    // Pre-fix arrSet went straight to setAttribute, which stored the
    // value at the data key and silently dropped the setter call —
    // copyWithin / splice / shift / unshift / fill into a slot with
    // an inherited or own setter never propagated the setter's abrupt
    // completion (built-ins/Array/prototype/copyWithin/return-abrupt-
    // from-set-target-value and the wider 'set-target-value' family).
    std::string skStr = "__set_" + std::to_string(idx) + "__";
    const proto::ProtoObject* sko = ctx->fromUTF8String(skStr.c_str());
    const proto::ProtoString* sk  = sko ? sko->asString(ctx) : nullptr;
    if (sk) {
        const proto::ProtoObject* setter = arr->getAttribute(ctx, sk, true);
        if (setter && setter != PROTO_NONE) {
            const proto::ProtoList* sargs = ctx->newList();
            sargs = sargs->appendLast(ctx, val ? val : PROTO_NONE);
            callJSFunction(ctx, setter, arr, sargs);
            if (hasCallException()) return arr;
            // Setter handled the assignment — do NOT also write a data
            // slot or bump length.
            return arr;
        }
    }

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

    // §10.4.2.1 ArraySetLength step 16 / §9.1.9 OrdinarySet: if
    // length is non-writable (own __pd_length__ bit 0 cleared)
    // AND length is a DATA descriptor (no __set_length__ accessor),
    // throw TypeError BEFORE mutating __elements__ or the data slot.
    // The throw fires even when newLen equals the current length —
    // OrdinarySetWithOwnDescriptor step 2.a returns false on any
    // write to a non-writable data slot, regardless of value.
    // Pre-fix push / pop / shift / unshift / splice (any caller that
    // routes through arrSetLen) silently succeeded against a frozen
    // length (built-ins/Array/prototype/{push,pop,shift,unshift}/
    // set-length-array-length-is-non-writable).
    // The accessor-descriptor guard prevents misfiring on
    // \`{get length(){...}, set length(v){...}}\` where the
    // descriptor has writable=false implicitly (accessor descriptors
    // don't carry the writable attribute, but our packed __pd__
    // happens to leave bit 0 cleared); built-ins/Array/prototype/
    // splice/set_length_no_args pins this contrast.
    {
        const proto::ProtoObject* sko = ctx->fromUTF8String("__set_length__");
        const proto::ProtoString* sk  = sko ? sko->asString(ctx) : nullptr;
        bool hasOwnSetter = sk && arr->hasOwnAttribute(ctx, sk) == PROTO_TRUE;
        const proto::ProtoObject* gko = ctx->fromUTF8String("__get_length__");
        const proto::ProtoString* gk  = gko ? gko->asString(ctx) : nullptr;
        bool hasOwnGetter = gk && arr->hasOwnAttribute(ctx, gk) == PROTO_TRUE;
        if (!hasOwnSetter && !hasOwnGetter) {
            const proto::ProtoString* pdk = JSSymbols::pdLength(ctx);
            if (pdk && arr->hasOwnAttribute(ctx, pdk) == PROTO_TRUE) {
                const proto::ProtoObject* pdv = arr->getAttribute(ctx, pdk, false);
                if (pdv && pdv->isInteger(ctx) && (pdv->asLong(ctx) & 0x1) == 0) {
                    signalNativeException(makeNativeError(ctx, "TypeError",
                        "Cannot assign to read-only property 'length'"));
                    return arr;
                }
            }
        }
    }

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

    // §9.1.9 OrdinarySet: if the receiver carries (own or inherited) a
    // __set_length__ accessor, dispatch through it instead of writing a
    // raw data slot.  The Array-like methods (splice / push / pop /
    // shift / unshift) must drive this user setter so that
    // `Object.defineProperty(obj, 'length', {set: f})` observes every
    // length write — built-ins/Array/prototype/splice/set_length_no_args
    // probes that splice() with no args still calls Set('length', len).
    const proto::ProtoObject* sko = ctx->fromUTF8String("__set_length__");
    const proto::ProtoString* sk  = sko ? sko->asString(ctx) : nullptr;
    if (sk) {
        const proto::ProtoObject* setter = arr->getAttribute(ctx, sk, true);
        if (setter && setter != PROTO_NONE) {
            const proto::ProtoList* sargs = ctx->newList();
            sargs = sargs->appendLast(ctx,
                       ctx->fromInteger(static_cast<long long>(newLen)));
            callJSFunction(ctx, setter, arr, sargs);
            return arr;
        }
    }

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
    // §7.1.1 OrdinaryToPrimitive(hint='string') — call user toString
    // first; if it returns a primitive, use it.  Pre-fix elemToString
    // skipped user toString entirely and returned the literal
    // '[object Object]' for any non-array object, so x.join(
    // {valueOf:()=>'+', toString:()=>'*'}) joined with '[object Object]'
    // instead of '*' (built-ins/Array/prototype/join/S15.4.4.5_A3.1_T2).
    {
        const proto::ProtoString* tsKey =
            ctx->fromUTF8String("toString") ? ctx->fromUTF8String("toString")->asString(ctx) : nullptr;
        if (tsKey) {
            const proto::ProtoObject* tsFn = val->getAttribute(ctx, tsKey, true);
            if (tsFn && tsFn != PROTO_NONE) {
                auto isCallable = [&](const proto::ProtoObject* fn) -> bool {
                    if (!fn || fn == PROTO_NONE) return false;
                    if (fn->isMethod(ctx)) return true;
                    const proto::ProtoString* bcK = JSSymbols::bytecodeId(ctx);
                    if (bcK && fn->hasAttribute(ctx, bcK) == PROTO_TRUE) return true;
                    const proto::ProtoString* nfK = JSSymbols::nativeFn(ctx);
                    if (nfK && fn->hasAttribute(ctx, nfK) == PROTO_TRUE) return true;
                    return false;
                };
                if (isCallable(tsFn)) {
                    const proto::ProtoObject* r = callJSFunction(ctx, tsFn, val, ctx->newList());
                    if (hasCallException()) return "";
                    if (r && r != PROTO_NONE) {
                        if (r->isString(ctx)) {
                            std::string s;
                            r->asString(ctx)->toUTF8String(ctx, s);
                            return s;
                        }
                        if (r->isInteger(ctx)) return std::to_string(r->asLong(ctx));
                        if (r->isBoolean(ctx)) return (r == PROTO_TRUE) ? "true" : "false";
                        // Non-primitive return: §7.1.1 step 5 falls
                        // back to valueOf below.
                    }
                }
                // §7.1.1 step 5 fallback: try valueOf when toString
                // returned a non-primitive (or wasn't callable).
                const proto::ProtoString* voK =
                    ctx->fromUTF8String("valueOf") ? ctx->fromUTF8String("valueOf")->asString(ctx) : nullptr;
                bool voCallableSeen = false;
                if (voK) {
                    const proto::ProtoObject* voFn = val->getAttribute(ctx, voK, true);
                    if (voFn && voFn != PROTO_NONE && isCallable(voFn)) {
                        voCallableSeen = true;
                        const proto::ProtoObject* r = callJSFunction(ctx, voFn, val, ctx->newList());
                        if (hasCallException()) return "";
                        if (r && r != PROTO_NONE) {
                            if (r->isString(ctx)) {
                                std::string s;
                                r->asString(ctx)->toUTF8String(ctx, s);
                                return s;
                            }
                            if (r->isInteger(ctx)) return std::to_string(r->asLong(ctx));
                            if (r->isBoolean(ctx)) return (r == PROTO_TRUE) ? "true" : "false";
                        }
                    }
                }
                // §7.1.1 step 6: both toString and valueOf returned
                // non-primitives — abrupt TypeError.  Only raise when
                // at least one user method was actually invoked
                // (otherwise the next fallback '[object Object]' is
                // the correct result for plain objects).
                if (voCallableSeen) {
                    signalNativeException(makeNativeError(ctx, "TypeError",
                        "Cannot convert object to primitive value"));
                    return "";
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
    // §6.1 collapses all undefined representations to a single value.
    // protoJS carries two: PROTO_NONE (the canonical "absent/undefined"
    // sentinel) and t_undefinedSentinel (the value bound to the global
    // `undefined` identifier and returned by uninitialised slots in
    // host helpers).  Pre-fix indexOf(undefined) returned the index of
    // the FIRST `undefined` literal pushed by user code rather than the
    // first sparse-or-uninitialised slot, because the two reps did not
    // strict-equal each other (15.4.4.14-9-4, 15.4.4.14-9-a-16, the
    // lastIndexOf parallel family, every includes-of-undefined fix).
    const proto::ProtoObject* undefSent = getUndefinedSentinel();
    bool aNone = !a || a == PROTO_NONE || a == undefSent;
    bool bNone = !b || b == PROTO_NONE || b == undefSent;
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
    arr = arrSetLen(ctx, arr, 0);
    // ECMA-262 §22.1.5.1: Array's own .length descriptor is
    // {writable:true, enumerable:false, configurable:false} — bits 0x1.
    // Without the sidecar the default is fully enumerable+configurable,
    // surfacing 'length' in for-in / Object.keys output and making
    // delete arr.length succeed silently. Apply the sidecar once at
    // creation so every fresh array honours the spec descriptor.
    const proto::ProtoString* pdLen = JSSymbols::pdLength(ctx);
    if (pdLen) arr = arr->setAttribute(ctx, pdLen, ctx->fromInteger(0x1LL));
    return arr;
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
    // §22.1.3.1.1 step 4: Let C be ? Get(O, 'constructor').  Get walks
    // accessors — if `constructor` is installed as a getter
    // (Object.defineProperty(arr, 'constructor', {get: ...})) the
    // getter must fire, and a throwing getter must propagate.
    // Pre-fix the raw getAttribute returned the descriptor's empty
    // data slot, so concat / slice / splice / filter / map / etc.
    // silently fell back to a fresh Array instead of surfacing the
    // user's abrupt completion (built-ins/Array/prototype/concat/
    // create-ctor-poisoned and the family).
    const proto::ProtoObject* C = nullptr;
    {
        const proto::ProtoObject* gcko = ctx->fromUTF8String("__get_constructor__");
        const proto::ProtoString* gck = gcko ? gcko->asString(ctx) : nullptr;
        if (gck) {
            const proto::ProtoObject* getter = originalArray->getAttribute(ctx, gck, true);
            if (getter && getter != PROTO_NONE) {
                C = callJSFunction(ctx, getter, originalArray, ctx->newList());
                if (hasCallException()) return PROTO_NONE;
            }
        }
        if (!C || C == PROTO_NONE)
            C = originalArray->getAttribute(ctx, ctorKey, true);
    }

    // ECMA-262 §22.1.3.1.1 ArraySpeciesCreate steps 7 / 9: when C is a
    // primitive that is neither Object nor undefined, throw TypeError.
    // Pre-fix the value was passed through to the constructor lookup,
    // which silently returned no constructor and produced a default
    // Array — matching V8 / SpiderMonkey would have thrown.
    if (C && C != PROTO_NONE && C != getUndefinedSentinel()) {
        if (C == getNullSentinel() || C->isInteger(ctx) || C->isDouble(ctx)
            || C->isFloat(ctx) || C->isBoolean(ctx) || C->isString(ctx)) {
            signalNativeException(makeNativeError(ctx, "TypeError",
                "Array species is not an object"));
            return PROTO_NONE;
        }
        // Symbol primitives are carried as objects with __is_symbol__
        // — Type(Symbol) is the Symbol type, not Object, per §6.1.5.
        const proto::ProtoString* isSymK = JSSymbols::isSymbol(ctx);
        if (isSymK && C->hasAttribute(ctx, isSymK) == PROTO_TRUE) {
            const proto::ProtoObject* mv = C->getAttribute(ctx, isSymK, true);
            if (mv == PROTO_TRUE) {
                signalNativeException(makeNativeError(ctx, "TypeError",
                    "Array species is not an object"));
                return PROTO_NONE;
            }
        }
    }

    // If C is a constructor, check its @@species.
    if (C && C != PROTO_NONE) {
        const proto::ProtoString* speciesKey = JSSymbols::symbolSpecies(ctx);
        if (speciesKey) {
            // Get(C, @@species) walks accessors — a Symbol.species
            // installed via Object.defineProperty(C, Symbol.species,
            // {get: f}) must fire on the read, and a throwing getter
            // must propagate (built-ins/Array/prototype/concat/
            // create-species-poisoned).  Probe the sidecar key first.
            std::string keyStr;
            speciesKey->toUTF8String(ctx, keyStr);
            std::string gkStr = "__get_" + keyStr + "__";
            const proto::ProtoObject* gko = ctx->fromUTF8String(gkStr.c_str());
            const proto::ProtoString* gk = gko ? gko->asString(ctx) : nullptr;
            const proto::ProtoObject* species = nullptr;
            if (gk) {
                const proto::ProtoObject* getter = C->getAttribute(ctx, gk, true);
                if (getter && getter != PROTO_NONE) {
                    species = callJSFunction(ctx, getter, C, ctx->newList());
                    if (hasCallException()) return PROTO_NONE;
                }
            }
            if (!species || species == PROTO_NONE)
                species = C->getAttribute(ctx, speciesKey, true);
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

    // Step 8: Construct(C, [length]).  Accept BOTH:
    //   - native __construct__ method (Array, Map, etc.)
    //   - bytecode user function (function F(){})  per §10.2.2
    //     IsConstructor (excluding arrow functions).
    // Pre-fix only the __construct__ path was honoured, so a species
    // function (user fn) silently fell back to a default Array — that
    // bypassed the species ctor's side effects (preventExtensions,
    // defineProperty(this, '0', ...)) and the CreateDataPropertyOrThrow
    // probes in slice / concat / splice never saw a throw
    // (built-ins/Array/prototype/slice/target-array-non-extensible,
    // target-array-with-non-configurable-property).
    const proto::ProtoString* constructKey = ctx->fromUTF8String("__construct__")->asString(ctx);
    const proto::ProtoObject* constructFn = (constructKey && C) ? C->getAttribute(ctx, constructKey, false) : nullptr;
    bool hasNativeCtor = constructFn && constructFn != PROTO_NONE
        && constructFn->isMethod(ctx);

    bool isBytecodeFn = false;
    if (!hasNativeCtor && C) {
        const proto::ProtoString* bcK = JSSymbols::bytecodeId(ctx);
        if (bcK && C->hasAttribute(ctx, bcK) == PROTO_TRUE) {
            const proto::ProtoObject* arrowKO = ctx->fromUTF8String("__is_arrow__");
            const proto::ProtoString* arrowK = arrowKO ? arrowKO->asString(ctx) : nullptr;
            if (!arrowK || C->getAttribute(ctx, arrowK, false) != PROTO_TRUE) {
                isBytecodeFn = true;
            }
        }
    }

    // §22.1.3.1.1 step 9: If IsConstructor(C) is false, throw TypeError.
    // A species that's callable but NOT a constructor (parseInt, isNaN,
    // arrow functions, etc.) must be rejected.  Built-in utility
    // functions carry __native_fn__ without any ctor marker; detect
    // those and throw (built-ins/Array/prototype/concat/create-species-
    // non-ctor pins this).
    if (C && C != PROTO_NONE && !hasNativeCtor && !isBytecodeFn) {
        const proto::ProtoString* nfKey = JSSymbols::nativeFn(ctx);
        if (nfKey && C->hasAttribute(ctx, nfKey) == PROTO_TRUE) {
            const proto::ProtoString* arrK = JSSymbols::arrayCtor(ctx);
            const proto::ProtoString* errK = JSSymbols::errorCtor(ctx);
            const proto::ProtoString* reK  = JSSymbols::regexpCtor(ctx);
            const proto::ProtoString* strK = JSSymbols::stringCtor(ctx);
            const proto::ProtoString* taK  = JSSymbols::taCtor(ctx);
            bool isCtor = (arrK && C->getAttribute(ctx, arrK, false) == PROTO_TRUE)
                       || (errK && C->hasAttribute(ctx, errK) == PROTO_TRUE)
                       || (reK  && C->getAttribute(ctx, reK,  false) == PROTO_TRUE)
                       || (strK && C->getAttribute(ctx, strK, false) == PROTO_TRUE)
                       || (taK  && C->hasAttribute(ctx, taK)  == PROTO_TRUE);
            if (!isCtor) {
                signalNativeException(makeNativeError(ctx, "TypeError",
                    "Array species is not a constructor"));
                return PROTO_NONE;
            }
        }
    }

    if (hasNativeCtor || isBytecodeFn) {
        // Create newObj as child of C.prototype
        const proto::ProtoString* protoKey = JSSymbols::prototype(ctx);
        const proto::ProtoObject* proto = C->getAttribute(ctx, protoKey, true);
        const proto::ProtoObject* newObj = (proto && proto != PROTO_NONE) ? proto->newChild(ctx, true) : ctx->newObject(true);

        // Mark as array (necessary if the constructor doesn't do it)
        if (isArrayKey) newObj = newObj->setAttribute(ctx, isArrayKey, PROTO_TRUE);

        const proto::ProtoList* args = ctx->newList();
        args = args->appendLast(ctx, ctx->fromInteger(static_cast<long long>(length)));

        const proto::ProtoObject* res = nullptr;
        if (hasNativeCtor) {
            proto::ProtoMethod fn = constructFn->asMethod(ctx);
            res = fn(ctx, newObj, nullptr, args, nullptr);
        } else {
            res = callJSFunction(ctx, C, newObj, args);
        }
        if (hasCallException()) return PROTO_NONE;

        // If res is an Object, return it, else newObj.
        if (res && res != PROTO_NONE
            && res != getUndefinedSentinel() && res != getNullSentinel()
            && !res->isInteger(ctx) && !res->isDouble(ctx) && !res->isFloat(ctx)
            && !res->isBoolean(ctx) && !res->isString(ctx)
            && res != PROTO_TRUE && res != PROTO_FALSE)
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
    const proto::ProtoObject* undefSentinel = getUndefinedSentinel();
    bool isNull = (self == nullSentinel);
    bool isUndefined = (!self || self == PROTO_NONE || self == undefSentinel);
    if (isNull || isUndefined) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Cannot convert undefined or null to object"));
        return true;
    }
    return false;
}

// Spec helper: callbackfn must be callable. ECMA-262 §22.1.3.18 (map),
// §22.1.3.7 (filter), §22.1.3.10 (forEach), §22.1.3.8 (find) etc. all
// step "If IsCallable(callbackfn) is false, throw a TypeError".
static bool arrayThrowIfCallbackNotCallable(proto::ProtoContext* ctx,
                                             const proto::ProtoObject* fn,
                                             const char* method) {
    if (fn && fn != PROTO_NONE) {
        if (fn->isMethod(ctx)) return false;
        const proto::ProtoString* bcKey = JSSymbols::bytecodeId(ctx);
        if (bcKey && fn->hasAttribute(ctx, bcKey) == PROTO_TRUE) return false;
        const proto::ProtoString* nfKey = JSSymbols::nativeFn(ctx);
        if (nfKey && fn->hasAttribute(ctx, nfKey) == PROTO_TRUE) return false;
        // Bound functions wrap a target callable behind a __bound_fn__
        // sentinel. callJSFunction unwraps them transparently, but the
        // callability probe missed the sentinel, so
        //   [0,0].flatMap(function(){return this;}.bind([1,2]))
        // threw 'callback is not a function' even though the bound
        // function IS callable.
        const proto::ProtoString* bfKey = JSSymbols::boundFn(ctx);
        if (bfKey && fn->hasAttribute(ctx, bfKey) == PROTO_TRUE) return false;
        // Built-in constructors (String, Number, Boolean, Array, Error,
        // RegExp, TypedArray, Object, ...) carry marker attributes
        // instead of __native_fn__.  typeof returns "function" for
        // them via the same set of probes — mirror that here so user
        // code like `[1,2].map(String)` /  `arr.map(Number)` is not
        // rejected as non-callable.  Harness compareArray.format uses
        // `Array.prototype.map.call(arrayLike, String)` so every
        // compareArray-using test depended on this.
        const proto::ProtoString* acK = JSSymbols::arrayCtor(ctx);
        if (acK && fn->getAttribute(ctx, acK, false) == PROTO_TRUE) return false;
        const proto::ProtoString* ecK = JSSymbols::errorCtor(ctx);
        if (ecK && fn->hasAttribute(ctx, ecK) == PROTO_TRUE) return false;
        const proto::ProtoString* reK = JSSymbols::regexpCtor(ctx);
        if (reK && fn->getAttribute(ctx, reK, false) == PROTO_TRUE) return false;
        const proto::ProtoString* taK = JSSymbols::taCtor(ctx);
        if (taK && fn->hasAttribute(ctx, taK) == PROTO_TRUE) return false;
        const proto::ProtoString* scK = JSSymbols::stringCtor(ctx);
        if (scK && fn->getAttribute(ctx, scK, false) == PROTO_TRUE) return false;
        const proto::ProtoString* conK = JSSymbols::construct(ctx);
        if (conK) {
            const proto::ProtoObject* cv = fn->getAttribute(ctx, conK, false);
            if (cv && cv != PROTO_NONE && cv->isMethod(ctx)) return false;
        }
    }
    signalNativeException(makeNativeError(ctx, "TypeError",
        (std::string(method) + " callback is not a function").c_str()));
    return true;
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
        // The global `undefined` identifier resolves to t_undefinedSentinel,
        // distinct from PROTO_NONE. Both must default to the spec ',' separator.
        if (!sepObj || sepObj == PROTO_NONE || sepObj == getUndefinedSentinel()) {
            sep = ",";  // undefined separator → default ","
        } else if (sepObj == protojs::getNullSentinel()) {
            sep = "null";  // null separator → "null" (ToString(null))
        } else {
            sep = elemToString(ctx, sepObj);
        }
    }

    // Spec §23.1.3.15 step 6.d: both null and undefined elements
    // contribute an empty string. The undefined sentinel must be
    // included alongside PROTO_NONE here — pre-fix it fell through to
    // elemToString and rendered as "[object Object]".
    const proto::ProtoObject* undefSent = getUndefinedSentinel();
    const proto::ProtoObject* nullSent  = getNullSentinel();
    std::string result;
    for (unsigned long i = 0; i < len; i++) {
        if (i > 0) result += sep;
        const proto::ProtoObject* elem = arrGet(ctx, self, i);
        if (elem && elem != PROTO_NONE
            && elem != undefSent && elem != nullSent) {
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
    // §23.1.3.36 step 1: Let array be ? ToObject(this value).  When
    // 'this' is a primitive (boolean / number / string), the resulting
    // wrapper has no callable 'join' method (Boolean.prototype etc.
    // don't override join), so step 3 falls back to
    // %Object.prototype.toString%.  Pre-fix arrayJoin was invoked on
    // the raw primitive and silently returned the empty string
    // (built-ins/Array/prototype/toString/call-with-boolean).  Emit
    // the spec-mandated '[object <Type>]' literal directly for
    // primitives instead of routing through the arrayJoin fallback.
    if (self && self != PROTO_NONE) {
        if (self->isBoolean(ctx)) return ctx->fromUTF8String("[object Boolean]");
        if (self->isInteger(ctx) || self->isDouble(ctx) || self->isFloat(ctx))
            return ctx->fromUTF8String("[object Number]");
        if (self->isString(ctx)) return ctx->fromUTF8String("[object String]");
    }
    // ECMA-262 §23.1.3.36 step 4-5: look up `join` on the receiver
    // and invoke it. Only when the receiver has no callable join do
    // we fall back to Object.prototype.toString. The previous
    // implementation unconditionally called the built-in arrayJoin,
    // so overrides like `arr.join = () => "custom"` were ignored.
    if (self && self != PROTO_NONE) {
        const proto::ProtoObject* joinObj = ctx->fromUTF8String("join");
        const proto::ProtoString* joinKey = joinObj ? joinObj->asString(ctx) : nullptr;
        if (joinKey) {
            const proto::ProtoObject* joinFn = self->getAttribute(ctx, joinKey, true);
            bool joinIsCallable = false;
            if (joinFn && joinFn != PROTO_NONE) {
                joinIsCallable = joinFn->isMethod(ctx);
                if (!joinIsCallable) {
                    const proto::ProtoString* bcKey = JSSymbols::bytecodeId(ctx);
                    if (bcKey && joinFn->hasAttribute(ctx, bcKey) == PROTO_TRUE) joinIsCallable = true;
                    else {
                        const proto::ProtoString* nfKey = JSSymbols::nativeFn(ctx);
                        if (nfKey && joinFn->hasAttribute(ctx, nfKey) == PROTO_TRUE) joinIsCallable = true;
                    }
                }
            }
            if (joinIsCallable) {
                return callJSFunction(ctx, joinFn, self,
                    args ? args : ctx->newList());
            }
            // §23.1.3.36 step 3: If IsCallable(func) is false, set
            // func to the intrinsic %Object.prototype.toString%.
            // Pre-fix we fell through to arrayJoin on a non-callable
            // join, which silently produced "" for non-array receivers
            // like `{join: null}`.  Synthesize the §20.1.3.6 result
            // here — pick up an optional Symbol.toStringTag from the
            // receiver, otherwise emit "[object Object]" (this branch
            // already excluded the primitive types above).
            const proto::ProtoString* tagKey = JSSymbols::symbolToStringTag(ctx);
            std::string tag = "Object";
            if (tagKey) {
                const proto::ProtoObject* tagV = self->getAttribute(ctx, tagKey, true);
                if (tagV && tagV != PROTO_NONE && tagV->isString(ctx)) {
                    std::string s;
                    tagV->asString(ctx)->toUTF8String(ctx, s);
                    if (!s.empty()) tag = s;
                }
            }
            std::string out = std::string("[object ") + tag + "]";
            return ctx->fromUTF8String(out.c_str());
        }
    }
    return arrayJoin(ctx, self, pl, nullptr, kw);
}

// Array.prototype.toLocaleString — ECMA-262 §23.1.3.31
// Calls toLocaleString on each element, joined with ",". The
// locale/options arguments are passed through to each element's
// toLocaleString (minimal implementation: separator is always ",").
static const proto::ProtoObject* arrayToLocaleString(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (arrayThrowIfNullUndefined(ctx, self)) return PROTO_NONE;
    unsigned long len = arrLen(ctx, self);
    const proto::ProtoObject* undefSent = getUndefinedSentinel();
    const proto::ProtoObject* nullSent  = getNullSentinel();
    std::string result;
    const proto::ProtoString* tlsKey = nullptr;
    {
        const proto::ProtoObject* tlsObj = ctx->fromUTF8String("toLocaleString");
        if (tlsObj) tlsKey = tlsObj->asString(ctx);
    }
    for (unsigned long i = 0; i < len; i++) {
        if (i > 0) result += ',';
        const proto::ProtoObject* elem = arrGet(ctx, self, i);
        if (!elem || elem == PROTO_NONE
            || elem == undefSent || elem == nullSent) continue;
        // Try elem.toLocaleString(); fall back to ToString on failure
        // (primitives respond to toLocaleString via Number/String
        // prototypes already).
        const proto::ProtoObject* tlsFn = tlsKey
            ? elem->getAttribute(ctx, tlsKey, true) : nullptr;
        if (tlsFn && tlsFn != PROTO_NONE) {
            // §23.1.3.34 step 5.b.iv (ES2024): Invoke(nextElement,
            // 'toLocaleString', « »).  ES2024 narrowed the spec — no
            // arguments are forwarded.  Pre-fix our impl forwarded
            // the locales / options args from arrayToLocaleString
            // itself, so an element's toLocaleString saw extra
            // unexpected arguments (built-ins/Array/prototype/
            // toLocaleString/invoke-element-tolocalestring).
            const proto::ProtoObject* r = callJSFunction(ctx, tlsFn, elem,
                ctx->newList());
            if (r && r != PROTO_NONE) {
                result += elemToString(ctx, r);
                continue;
            }
        }
        result += elemToString(ctx, elem);
    }
    return ctx->fromUTF8String(result.c_str());
}

static const proto::ProtoObject* arrayPush(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (arrayThrowIfNullUndefined(ctx, self)) return PROTO_NONE;
    // §23.1.3.20 step 6.b + Set(O, "length", ...): a primitive-string
    // receiver has a non-writable "length" property on its wrapper,
    // so the final Set raises TypeError.  Pre-fix push silently
    // succeeded by treating the primitive as an array-like with
    // length but no write side effect (built-ins/Array/prototype/
    // push/throws-with-string-receiver).
    if (self && self->isString(ctx)) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Cannot assign to read-only property 'length' of String"));
        return PROTO_NONE;
    }
    // §23.1.3.20 step 6: Set(O, 'length', len, Throw=true) runs
    // unconditionally — even array.push() with no items must Set
    // length and fail when length is non-writable
    // (built-ins/Array/prototype/push/set-length-zero-array-length-
    // is-non-writable).
    {
        const proto::ProtoString* pdk = JSSymbols::pdLength(ctx);
        if (pdk && self->hasAttribute(ctx, pdk) == PROTO_TRUE) {
            const proto::ProtoObject* pdv = self->getAttribute(ctx, pdk, false);
            if (pdv && pdv->isInteger(ctx) && (pdv->asLong(ctx) & 0x1) == 0) {
                signalNativeException(makeNativeError(ctx, "TypeError",
                    "Cannot assign to read-only property 'length'"));
                return PROTO_NONE;
            }
        }
    }
    unsigned long argc = args ? static_cast<unsigned long>(args->getSize(ctx)) : 0;

    const proto::ProtoString* isArrKey = JSSymbols::isArray(ctx);
    const proto::ProtoObject* isArrVal = isArrKey
        ? self->getAttribute(ctx, isArrKey, true) : nullptr;
    const bool isRealArray = (isArrVal == PROTO_TRUE);

    if (isRealArray) {
        // §23.1.3.20 step 6.c: for each argument, Set(O, ToString(len),
        // E, true).  In the common case (no inherited indexed setter
        // anywhere in the chain) this is structurally identical to a
        // batch of ProtoList::appendLast calls: read __elements__ once
        // up front, fold all arguments through ProtoList directly, write
        // back once.  The arrSet path that we previously called per
        // element re-read __elements__ AND wrote it back on every
        // iteration — 100K pushes paid for ~400K redundant attribute
        // ops.  This native-op path collapses to 1 getAttribute +
        // argc * appendLast + 1 setAttribute.
        unsigned long len = arrLen(ctx, self);
        if (hasCallException()) return PROTO_NONE;

        const proto::ProtoString* hisKey = JSSymbols::hasIndexedSetters(ctx);
        const bool maybeHasIndexedSetters = hisKey
            && (self->hasAttribute(ctx, hisKey) == PROTO_TRUE)
            && (self->getAttribute(ctx, hisKey, true) == PROTO_TRUE);

        if (!maybeHasIndexedSetters && argc > 0) {
            // NATIVE FAST PATH — direct ProtoList::appendLast calls.
            // No per-element getAttribute/setAttribute, no arrSet
            // ceremony, no indexKey probe.
            const proto::ProtoList* list = getArrayElements(ctx, self);
            if (!list) list = ctx->newList();
            for (unsigned long i = 0; i < argc; i++) {
                const proto::ProtoObject* item = args->getAt(ctx, static_cast<int>(i));
                list = list->appendLast(ctx, item ? item : PROTO_NONE);
            }
            // setArrayElements writes __elements__ AND length in one
            // helper call.  The __pd_length__ writability probe ran at
            // the top of arrayPush already, so the spec-mandated
            // TypeError fires before we get here.
            setArrayElements(ctx, self, list);
            return ctx->fromInteger(static_cast<long long>(len + argc));
        }

        // Slow path: inherited indexed setter exists, OR argc == 0
        // (still needs arrSetLen for the non-writable-length probe).
        for (unsigned long i = 0; i < argc; i++) {
            const proto::ProtoObject* item = args->getAt(ctx, static_cast<int>(i));
            arrSet(ctx, self, len + i, item ? item : PROTO_NONE);
            if (hasCallException()) return PROTO_NONE;
        }
        unsigned long newLen = len + argc;
        arrSetLen(ctx, self, newLen);
        if (hasCallException()) return PROTO_NONE;
        return ctx->fromInteger(static_cast<long long>(newLen));
    }

    // Plain object used as an array-like — write string-keyed indices
    // and bump `length` once at the end. Pre-fix arrSet only updated
    // length on real arrays, so Array.prototype.push.call(plainObj, …)
    // wrote the new indices but left plainObj.length stale.

    // §23.1.3.20 step 5: If len + argCount > 2^53 - 1, throw TypeError.
    // arrLen clamps Infinity to 2^32-1 (so iteration helpers behave),
    // which hides the overflow.  Read the raw length attribute and
    // check for Infinity / >2^53-1 before falling back to arrLen.
    // Sputnik S15.4.4.7_A2_T2 probes that
    //   obj.length = +Infinity; obj.push(-4)
    // throws TypeError without mutating obj.length.
    {
        const proto::ProtoString* lenK = JSSymbols::length(ctx);
        if (lenK) {
            const proto::ProtoObject* raw = self->getAttribute(ctx, lenK, false);
            if (raw && raw != PROTO_NONE && (raw->isDouble(ctx) || raw->isFloat(ctx))) {
                double d = raw->asDouble(ctx);
                constexpr double kMaxSafeInt = 9007199254740991.0; // 2^53 - 1
                // Only the POSITIVE side blows out — NEGATIVE infinity and
                // negative finite values clamp to 0 via ToLength, and NaN
                // clamps to 0 too (NaN > anything is false).  Both shapes
                // must keep flowing into the normal arrLen path so the
                // spec'd "push appends at index 0" semantics hold.
                if ((std::isinf(d) && d > 0) ||
                    d > kMaxSafeInt - static_cast<double>(argc)) {
                    signalNativeException(makeNativeError(ctx, "TypeError",
                        "Invalid array length"));
                    return PROTO_NONE;
                }
            }
        }
    }

    unsigned long len = arrLen(ctx, self);
    for (unsigned long i = 0; i < argc; i++) {
        const proto::ProtoObject* item = args->getAt(ctx, static_cast<int>(i));
        arrSet(ctx, self, len + i, item);
        if (hasCallException()) return PROTO_NONE;
    }
    unsigned long newLen = len + argc;
    // Route length write through arrSetLen so __pd_length__ writable
    // bit + user __set_length__ accessors both surface correctly.
    arrSetLen(ctx, self, newLen);
    if (hasCallException()) return PROTO_NONE;
    return ctx->fromInteger(static_cast<long long>(newLen));
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
    // §23.1.3.21 step 3.a + step 4.f both run Set(O, 'length', ...,
    // Throw=true).  §10.4.3 marks 'length' on the String exotic
    // wrapper as non-writable, so the Set fails and TypeError fires
    // even on '' / 'abc' (built-ins/Array/prototype/pop/throws-with-
    // string-receiver).  Mirror the parallel guard in push / shift.
    if (self && self->isString(ctx)) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Cannot assign to read-only property 'length' of String"));
        return PROTO_NONE;
    }

    // §23.1.3.21 step 3.a: when len = 0, the spec still runs
    // Set(O, 'length', +0, Throw=true).  A frozen array has non-
    // writable length, so that Set raises TypeError even though the
    // array is empty (built-ins/Array/prototype/pop/set-length-zero-
    // array-is-frozen).  Pre-fix the empty-array branch returned
    // undefined without exercising the Set, so the throw never fired.
    auto throwIfLengthFrozen = [&]() -> bool {
        const proto::ProtoString* pdk = JSSymbols::pdLength(ctx);
        if (pdk && self->hasAttribute(ctx, pdk) == PROTO_TRUE) {
            const proto::ProtoObject* pdv = self->getAttribute(ctx, pdk, false);
            if (pdv && pdv->isInteger(ctx)) {
                long long bits = pdv->asLong(ctx);
                if ((bits & 0x1) == 0) {
                    signalNativeException(makeNativeError(ctx, "TypeError",
                        "Cannot assign to read-only property 'length'"));
                    return true;
                }
            }
        }
        return false;
    };

    // Native ProtoList path.
    if (const proto::ProtoList* list = nativeArrayList(ctx, self)) {
        unsigned long size = static_cast<unsigned long>(list->getSize(ctx));
        // arr.length may be greater than __elements__.size (e.g.
        // `new Array(1)` carries length=1 but __elements__ is empty).
        // Use the spec'd LengthOfArrayLike — pop reads the LAST INDEX
        // by length-1, not by __elements__.size-1
        // (built-ins/Array/prototype/pop/set-length-array-length-is-
        // non-writable installs a writable:false length descriptor
        // INSIDE the Array.prototype[0] getter; the getter only fires
        // when pop reads index 0 of a `new Array(1)` whose
        // __elements__ size is 0).
        unsigned long lenSpec = arrLen(ctx, self);
        if (hasCallException()) return PROTO_NONE;
        if (lenSpec == 0) {
            if (throwIfLengthFrozen()) return PROTO_NONE;
            return PROTO_NONE;
        }
        if (size == 0 && lenSpec > 0) {
            // Sparse array with length > 0 but empty __elements__.
            // §23.1.3.21 step 4: read O[lenSpec-1], then Set length.
            const proto::ProtoObject* removed = arrGet(ctx, self, lenSpec - 1);
            if (hasCallException()) return PROTO_NONE;
            // Attempt Set(O, 'length', lenSpec-1, true) via arrSetLen
            // — picks up the __pd_length__ writable check (which the
            // getter above may have just cleared).
            arrSetLen(ctx, self, lenSpec - 1);
            if (hasCallException()) return PROTO_NONE;
            return (removed && removed != PROTO_NONE) ? removed : getUndefinedSentinel();
        }
        // §23.1.3.21 step 4.c: Let element be ? Get(O, ToString(F(newLen))).
        // Get walks the prototype chain — a hole at the last index
        // must surface the inherited value (Array.prototype[idx] data
        // or accessor).  Pre-fix we read list->getAt(size-1) directly
        // and a PROTO_NONE pad (from `x.length = N` extending past
        // the dense elements) shadowed Array.prototype[idx]
        // (Sputnik S15.4.4.6_A4_T1: Array.prototype[1] = 1; x = [0];
        //  x.length = 2; x.pop() should be 1, not undefined).
        const proto::ProtoObject* removed =
            arrGet(ctx, self, size - 1);
        if (hasCallException()) return PROTO_NONE;
        // §23.1.3.21 step 4.f: Set(O, 'length', newLen, true).  The
        // getter at step 4.c (arrGet above) may have made length non-
        // writable mid-call (built-ins/Array/prototype/pop/set-length-
        // array-length-is-non-writable installs the writable:false
        // descriptor inside the Array.prototype[idx] getter).  Probe
        // before mutating __elements__ so the truncation doesn't
        // happen on the throw path.
        if (throwIfLengthFrozen()) return PROTO_NONE;
        const proto::ProtoList* shrunk = list->removeLast(ctx);
        if (shrunk) setArrayElements(ctx, self, shrunk);
        return (removed && removed != PROTO_NONE) ? removed : getUndefinedSentinel();
    }

    // Legacy string-keyed path (array-likes only).
    unsigned long len = arrLen(ctx, self);
    if (len == 0) {
        if (throwIfLengthFrozen()) return PROTO_NONE;
        // §23.1.3.21 step 3.a: actually perform Set(O, 'length', 0).
        // ToUint32(arrLen) clamps NaN / negative / non-integer lengths
        // to 0 — the spec then writes the clamped value back so
        // subsequent reads see 0 rather than the raw input.  Pre-fix
        // the early empty return left obj.length unchanged
        // (Sputnik S15.4.4.6_A2_T2: obj.length = NaN; obj.pop();
        // obj.length is still NaN).
        const proto::ProtoString* lenK = JSSymbols::length(ctx);
        if (lenK) self->setAttribute(ctx, lenK, ctx->fromInteger(0LL));
        return PROTO_NONE;
    }
    unsigned long lastIdx = len - 1;
    const proto::ProtoObject* removed = arrGet(ctx, self, lastIdx);
    if (hasCallException()) return PROTO_NONE;
    // §23.1.3.21 step 4.e: DeletePropertyOrThrow(O, lastIdx).
    const proto::ProtoString* idxKey =
        JSSymbols::indexKey(ctx, static_cast<uint32_t>(lastIdx));
    if (idxKey) self->setAttribute(ctx, idxKey, PROTO_NONE);
    // §23.1.3.21 step 4.f: Set(O, 'length', lastIdx, true).  arrSetLen
    // honours __pd_length__ writable bit — if the user installed a
    // non-writable length descriptor (potentially inside the arrGet
    // above), TypeError fires.
    arrSetLen(ctx, self, lastIdx);
    if (hasCallException()) return PROTO_NONE;
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
    // §23.1.3.27 step 3.a + step 8 both call Set(O, 'length', ...,
    // Throw=true).  A primitive-string receiver has a non-writable
    // 'length' on its String wrapper, so either Set fails — TypeError.
    // Pre-fix shift silently succeeded on '' / 'abc' / function(){} etc.
    // (built-ins/Array/prototype/shift/throws-when-this-value-length-is-
    // writable-false).
    if (self && self->isString(ctx)) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Cannot assign to read-only property 'length' of String"));
        return PROTO_NONE;
    }

    // §23.1.3.27 step 3.a: when len = 0, Set(O, 'length', +0, Throw=true)
    // still runs.  An empty frozen array has the writable bit cleared
    // on __pd_length__, so the Set fails and abrupts as TypeError.
    // Pre-fix the native-list size==0 fast path returned undefined
    // without exercising the Set (built-ins/Array/prototype/shift/
    // set-length-zero-array-is-frozen).
    auto throwIfShiftLengthFrozen = [&]() -> bool {
        const proto::ProtoString* pdk = JSSymbols::pdLength(ctx);
        if (pdk && self->hasAttribute(ctx, pdk) == PROTO_TRUE) {
            const proto::ProtoObject* pdv = self->getAttribute(ctx, pdk, false);
            if (pdv && pdv->isInteger(ctx) && (pdv->asLong(ctx) & 0x1) == 0) {
                signalNativeException(makeNativeError(ctx, "TypeError",
                    "Cannot assign to read-only property 'length'"));
                return true;
            }
        }
        return false;
    };

    // §23.1.3.27 spec walk: read O[0], shift each k=1..len-1 to k-1
    // (Get + Set), delete O[len-1], Set length.  Route through arrGet
    // / arrSet / arrSetLen so inherited accessors fire and __pd_length__
    // / __set_length__ are honoured.
    unsigned long len = arrLen(ctx, self);
    if (hasCallException()) return PROTO_NONE;
    if (len == 0) {
        arrSetLen(ctx, self, 0);
        if (hasCallException()) return PROTO_NONE;
        return PROTO_NONE;
    }

    // Native fast path: when the prototype chain has no indexed setters
    // and no accessor properties at all, the entire O(N) spec walk
    // collapses to a single ProtoList::removeFirst.  __elements__ owns
    // every index that matters; there is no monkey-patched setter to
    // fire on the cascading writes and no getter to override the data
    // slots we'd otherwise read via arrGet.
    {
        const proto::ProtoString* hisKey = JSSymbols::hasIndexedSetters(ctx);
        const proto::ProtoString* hapKey = JSSymbols::hasAccessorProps(ctx);
        const bool maybeHasIndexedSetters = hisKey
            && (self->hasAttribute(ctx, hisKey) == PROTO_TRUE)
            && (self->getAttribute(ctx, hisKey, true) == PROTO_TRUE);
        const bool maybeHasAccessors = hapKey
            && (self->hasAttribute(ctx, hapKey) == PROTO_TRUE)
            && (self->getAttribute(ctx, hapKey, true) == PROTO_TRUE);
        if (!maybeHasIndexedSetters && !maybeHasAccessors) {
            if (const proto::ProtoList* list = nativeArrayList(ctx, self)) {
                if (list->getSize(ctx) == static_cast<int>(len)) {
                    const proto::ProtoObject* first = list->getAt(ctx, 0);
                    const proto::ProtoList* shrunk = list->removeFirst(ctx);
                    if (shrunk) setArrayElements(ctx, self, shrunk);
                    return (first && first != PROTO_NONE) ? first : getUndefinedSentinel();
                }
            }
        }
    }

    const proto::ProtoObject* first = arrGet(ctx, self, 0);
    if (hasCallException()) return PROTO_NONE;
    for (unsigned long i = 1; i < len; i++) {
        const proto::ProtoObject* v = arrGet(ctx, self, i);
        if (hasCallException()) return PROTO_NONE;
        arrSet(ctx, self, i - 1, v);
        if (hasCallException()) return PROTO_NONE;
    }
    // Delete final index.  For real arrays this is implicit in
    // arrSetLen's truncation; for array-likes the legacy length-
    // write doesn't truncate by itself.
    const proto::ProtoString* lastKey =
        JSSymbols::indexKey(ctx, static_cast<uint32_t>(len - 1));
    if (lastKey) self->setAttribute(ctx, lastKey, PROTO_NONE);
    arrSetLen(ctx, self, len - 1);
    if (hasCallException()) return PROTO_NONE;
    return (first && first != PROTO_NONE) ? first : getUndefinedSentinel();
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

    // §23.1.3.32 step 5 runs Set(O, 'length', len + argCount, Throw=true)
    // regardless of argCount.  A frozen-length receiver therefore takes
    // the abrupt path even on a zero-argument unshift().  Pre-fix the
    // empty-args fast path returned the cached size and the throw never
    // fired (built-ins/Array/prototype/unshift/set-length-zero-array-
    // length-is-non-writable).
    auto throwIfLengthFrozen = [&]() -> bool {
        const proto::ProtoString* pdk = JSSymbols::pdLength(ctx);
        if (pdk && self->hasAttribute(ctx, pdk) == PROTO_TRUE) {
            const proto::ProtoObject* pdv = self->getAttribute(ctx, pdk, false);
            if (pdv && pdv->isInteger(ctx) && (pdv->asLong(ctx) & 0x1) == 0) {
                signalNativeException(makeNativeError(ctx, "TypeError",
                    "Cannot assign to read-only property 'length'"));
                return true;
            }
        }
        return false;
    };
    if (throwIfLengthFrozen()) return PROTO_NONE;

    // §23.1.3.32 spec walk: shift each k=len-1..0 to k+argc, then
    // write args[0..argc-1] at indices 0..argc-1, then set length.
    // Route through arrGet / arrSet / arrSetLen so inherited
    // accessors fire and __pd_length__ / __set_length__ are honoured.
    unsigned long len = arrLen(ctx, self);
    if (hasCallException()) return PROTO_NONE;
    if (argc == 0) {
        // Spec still runs Set(O, 'length', len, true) — surface
        // __pd_length__ writable bit.
        arrSetLen(ctx, self, len);
        if (hasCallException()) return PROTO_NONE;
        return ctx->fromInteger(static_cast<long long>(len));
    }
    // Native fast path: when no inherited indexed setter / accessor
    // anywhere in the chain, the spec-mandated cascading writes have
    // no observer; collapse the entire O(len + argc) walk to argc
    // ProtoList::appendFirst calls.  Also skips the per-destination
    // __get_<i>__/__set_<i>__ probe block — the hasAccessorProps gate
    // already says there are no such accessors.
    {
        const proto::ProtoString* hisKey = JSSymbols::hasIndexedSetters(ctx);
        const proto::ProtoString* hapKey = JSSymbols::hasAccessorProps(ctx);
        const bool maybeHasIndexedSetters = hisKey
            && (self->hasAttribute(ctx, hisKey) == PROTO_TRUE)
            && (self->getAttribute(ctx, hisKey, true) == PROTO_TRUE);
        const bool maybeHasAccessors = hapKey
            && (self->hasAttribute(ctx, hapKey) == PROTO_TRUE)
            && (self->getAttribute(ctx, hapKey, true) == PROTO_TRUE);
        if (!maybeHasIndexedSetters && !maybeHasAccessors) {
            const proto::ProtoList* list = nativeArrayList(ctx, self);
            if (list && list->getSize(ctx) == static_cast<int>(len)) {
                for (long long i = static_cast<long long>(argc) - 1; i >= 0; i--) {
                    const proto::ProtoObject* v = args->getAt(ctx, static_cast<int>(i));
                    list = list->appendFirst(ctx, v ? v : PROTO_NONE);
                }
                setArrayElements(ctx, self, list);
                return ctx->fromInteger(static_cast<long long>(len + argc));
            }
        }
    }

    // §23.1.3.32 step 4.e.i Set(O, ToString(j), E, true) raises TypeError
    // when the target slot is a getter-only accessor (no [[Set]]).
    // Pre-fix the legacy arrSet path silently dropped the write on
    // such slots, so Array.prototype.unshift.call({get 0(){}}, 0)
    // succeeded instead of throwing (built-ins/Array/prototype/
    // unshift/read-only-property).  Probe the destination indices
    // for a getter without a paired setter before any work runs.
    for (unsigned long i = 0; i < argc; i++) {
        std::string gkStr = "__get_" + std::to_string(i) + "__";
        std::string skStr = "__set_" + std::to_string(i) + "__";
        const proto::ProtoObject* gko = ctx->fromUTF8String(gkStr.c_str());
        const proto::ProtoString* gk = gko ? gko->asString(ctx) : nullptr;
        const proto::ProtoObject* sko = ctx->fromUTF8String(skStr.c_str());
        const proto::ProtoString* sk = sko ? sko->asString(ctx) : nullptr;
        bool hasGetter = gk && self->hasAttribute(ctx, gk) == PROTO_TRUE;
        bool hasSetter = sk && self->hasAttribute(ctx, sk) == PROTO_TRUE;
        if (hasGetter && !hasSetter) {
            signalNativeException(makeNativeError(ctx, "TypeError",
                "Cannot assign to read-only property"));
            return PROTO_NONE;
        }
    }
    for (long long i = static_cast<long long>(len) - 1; i >= 0; i--) {
        const proto::ProtoObject* v = arrGet(ctx, self, static_cast<unsigned long>(i));
        if (hasCallException()) return PROTO_NONE;
        arrSet(ctx, self, static_cast<unsigned long>(i) + argc, v);
        if (hasCallException()) return PROTO_NONE;
    }
    for (unsigned long i = 0; i < argc; i++) {
        arrSet(ctx, self, i, args->getAt(ctx, static_cast<int>(i)));
        if (hasCallException()) return PROTO_NONE;
    }
    unsigned long newLen = len + argc;
    arrSetLen(ctx, self, newLen);
    if (hasCallException()) return PROTO_NONE;
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

    // ECMA-262 §23.1.3.28: ToIntegerOrInfinity on start/end. NaN → 0;
    // +Infinity / -Infinity preserved, then clamped to [0, len].
    // Non-primitive arguments (objects with valueOf, Boolean / Number
    // wrappers, ...) route through jsToNumber for the spec coercion;
    // pre-fix the helper returned defaultV and Sputnik
    // S15.4.4.10_A2.2_T5 produced len instead of 3 for
    // x.slice(0, {valueOf:()=>3, toString:()=>0}).
    auto toII = [&](const proto::ProtoObject* o, long long defaultV) -> long long {
        if (!o || o == PROTO_NONE || o == getUndefinedSentinel()) return defaultV;
        const proto::ProtoObject* num = o;
        if (!o->isInteger(ctx) && !o->isDouble(ctx) && !o->isFloat(ctx)) {
            num = jsToNumber(ctx, o);
            if (hasCallException() || !num) return defaultV;
        }
        if (num->isInteger(ctx)) return num->asLong(ctx);
        if (num->isDouble(ctx) || num->isFloat(ctx)) {
            double d = num->asDouble(ctx);
            if (std::isnan(d)) return 0;
            if (std::isinf(d)) return d > 0 ? len : -len - 1;
            return static_cast<long long>(d);
        }
        return defaultV;
    };
    if (args && args->getSize(ctx) > 0) {
        start = toII(args->getAt(ctx, 0), 0);
        if (hasCallException()) return PROTO_NONE;
        if (args->getSize(ctx) > 1) end = toII(args->getAt(ctx, 1), len);
        if (hasCallException()) return PROTO_NONE;
    }

    start = normalizeIdxClamp(start, len);
    end   = normalizeIdxClamp(end,   len);

    const proto::ProtoObject* result = arraySpeciesCreate(ctx, self, static_cast<unsigned long>(end - start));
    if (hasCallException()) return PROTO_NONE;

    // Native fast path: real array, no accessors / no inherited setters
    // on EITHER source or destination, source has no holes in the
    // [start, end) range, destination is also a real array.
    // ProtoList::getSlice does the whole copy in one O(log N) tree-
    // splice — no per-element arrGet + CreateDataPropertyOrThrow walk.
    {
        const proto::ProtoString* hisKey = JSSymbols::hasIndexedSetters(ctx);
        const proto::ProtoString* hapKey = JSSymbols::hasAccessorProps(ctx);
        auto checkFlag = [&](const proto::ProtoObject* obj, const proto::ProtoString* k) {
            return k && (obj->hasAttribute(ctx, k) == PROTO_TRUE)
                     && (obj->getAttribute(ctx, k, true) == PROTO_TRUE);
        };
        if (!checkFlag(self, hisKey) && !checkFlag(self, hapKey)
            && !checkFlag(result, hisKey) && !checkFlag(result, hapKey)) {
            const proto::ProtoList* srcList = nativeArrayList(ctx, self);
            const proto::ProtoList* dstList = nativeArrayList(ctx, result);
            if (srcList && dstList
                && srcList->getSize(ctx) == static_cast<int>(len)
                && dstList->getSize(ctx) == 0) {
                // No holes possible — __elements__ size == arrLen.
                const proto::ProtoList* slice = srcList->getSlice(
                    ctx, static_cast<int>(start), static_cast<int>(end));
                if (slice) {
                    setArrayElements(ctx, result, slice);
                    return result;
                }
            }
        }
    }

    // §23.1.3.28 step 13.c.ii: CreateDataPropertyOrThrow(A, ToString(n),
    // kValue).  Throws TypeError when the target is non-extensible OR
    // already holds a non-configurable descriptor at the slot.
    // built-ins/Array/prototype/slice/target-array-non-extensible and
    // target-array-with-non-configurable-property pin both branches.
    // §23.1.3.28 step 13: HasProperty(O, Pk) gates the
    // CreateDataPropertyOrThrow, but n advances UNCONDITIONALLY.
    // The result's final length is end-start (= n at loop exit), NOT
    // the count of present indices.  Pre-fix we copied holes as
    // arrGet(...) (PROTO_NONE / undefined), then set length = outIdx
    // which only counted writes — so a sparse source still produced
    // a packed-but-undefined-filled result.  Now mirror the spec:
    // skip writes for holes, advance outIdx regardless, end with
    // length = end-start.
    long long outIdx = 0;
    for (long long i = start; i < end; i++) {
        if (arrHasProperty(ctx, self, static_cast<unsigned long>(i))) {
            const proto::ProtoObject* v = arrGet(ctx, self, static_cast<unsigned long>(i));
            if (hasCallException()) return PROTO_NONE;
            result = arrayCreateDataPropertyOrThrow(ctx, result,
                        static_cast<unsigned long>(outIdx), v);
            if (hasCallException()) return PROTO_NONE;
        }
        outIdx++;
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
    long long len = static_cast<long long>(arrLen(ctx, self));
    // ECMA-262 §23.1.3.13 step 3: if len is 0, return -1 BEFORE
    // ToIntegerOrInfinity runs on fromIndex.  Pre-fix the fromIndex
    // coercion was attempted even on an empty receiver, surfacing
    // user-visible side effects of `fromIndex.valueOf()` that the
    // spec explicitly bypasses.
    if (len == 0) return ctx->fromInteger(-1LL);
    // §23.1.3.13 step 5: searchElement defaults to undefined when no
    // argument is supplied.  Pre-fix indexOf returned -1 unconditionally
    // on no-arg, so [undefined].indexOf() failed to locate idx 0
    // (built-ins/Array/prototype/indexOf/15.4.4.14-9-b-ii-2).
    const proto::ProtoObject* needle = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0)
        : getUndefinedSentinel();
    if (!args || args->getSize(ctx) == 0) args = ctx->newList(); // ensure non-null below
    long long from = 0;
    if (args->getSize(ctx) > 1) {
        const proto::ProtoObject* fi = args->getAt(ctx, 1);
        if (fi && fi != PROTO_NONE) {
            // ECMA-262 §23.1.3.13: fromIndex is run through
            // ToIntegerOrInfinity, which begins with ToNumber.
            // Pre-fix non-numeric `fromIndex` values silently
            // collapsed to 0 — `[1,2,1,2].indexOf(2, "2")` returned
            // 1 instead of the spec-required 3 because "2" never
            // got coerced.
            const proto::ProtoObject* num = fi;
            if (!fi->isInteger(ctx) && !fi->isDouble(ctx) && !fi->isFloat(ctx)) {
                num = jsToNumber(ctx, fi);
                if (hasCallException()) return PROTO_NONE;
            }
            if (num && num != PROTO_NONE) {
                if (num->isInteger(ctx)) from = num->asLong(ctx);
                else if (num->isDouble(ctx) || num->isFloat(ctx)) {
                    double d = num->asDouble(ctx);
                    // ToIntegerOrInfinity: NaN -> 0,
                    // +Infinity -> length (no match), -Infinity -> 0.
                    if (std::isnan(d)) from = 0;
                    else if (std::isinf(d)) {
                        if (d > 0) return ctx->fromInteger(-1LL);
                        from = 0;
                    }
                    else from = static_cast<long long>(d);
                }
            }
        }
    }
    from = normalizeIdx(from, len);
    // NaN-as-needle: indexOf uses Strict Equality, so NaN is never found.
    bool needleIsNaN = needle && (needle->isDouble(ctx) || needle->isFloat(ctx)) &&
                       std::isnan(needle->asDouble(ctx));
    if (needleIsNaN) return ctx->fromInteger(-1LL);
    // §23.1.3.13 step 6: kPresent = HasProperty(O, Pk); skip when false.
    // Pre-fix arrGet returned PROTO_NONE for absent slots which
    // strictEquals collapsed with the JS undefined sentinel — so
    // [0, , 2].indexOf(undefined) matched the hole at index 1 instead
    // of skipping it (built-ins/Array/prototype/indexOf/15.4.4.14-9-b-1).
    // Gate the HasProperty walk on needle === undefined so defined
    // needles keep the prior fast path (chain inheritance via arrGet).
    const proto::ProtoObject* undefSent2 = getUndefinedSentinel();
    bool needleIsUndefined = (!needle || needle == PROTO_NONE || needle == undefSent2);
    for (long long i = from; i < len; i++) {
        if (needleIsUndefined &&
            !arrHasProperty(ctx, self, static_cast<unsigned long>(i))) continue;
        const proto::ProtoObject* elem = arrGet(ctx, self, static_cast<unsigned long>(i));
        // §23.1.3.13 step 6.b Get(O, Pk) is the abrupt-completion site;
        // a throwing accessor must terminate iteration before later
        // indices are probed — parallel to lastIndexOf's fix in this
        // round.
        if (hasCallException()) return PROTO_NONE;
        if (strictEquals(ctx, elem, needle))
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
    long long len = static_cast<long long>(arrLen(ctx, self));
    // §23.1.3.16 step 3: empty receiver returns -1 BEFORE ToInteger.
    if (len == 0) return ctx->fromInteger(-1LL);
    // §23.1.3.16: searchElement defaults to undefined when no argument
    // is supplied.  Pre-fix lastIndexOf returned -1 unconditionally on
    // no-arg, so [undefined].lastIndexOf() failed to locate idx 0
    // (built-ins/Array/prototype/lastIndexOf/15.4.4.15-8-b-ii-2) —
    // mirrors the indexOf default just above.
    const proto::ProtoObject* needle = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0)
        : getUndefinedSentinel();
    if (!args) args = ctx->newList();
    long long from = len - 1;
    if (args->getSize(ctx) > 1) {
        const proto::ProtoObject* fi = args->getAt(ctx, 1);
        if (fi && fi != PROTO_NONE) {
            // ToIntegerOrInfinity coerces via ToNumber; see indexOf.
            const proto::ProtoObject* num = fi;
            if (!fi->isInteger(ctx) && !fi->isDouble(ctx) && !fi->isFloat(ctx)) {
                num = jsToNumber(ctx, fi);
                if (hasCallException()) return PROTO_NONE;
            }
            if (num && num != PROTO_NONE) {
                if (num->isInteger(ctx)) from = num->asLong(ctx);
                else if (num->isDouble(ctx) || num->isFloat(ctx)) {
                    double d = num->asDouble(ctx);
                    // ToIntegerOrInfinity: NaN -> 0,
                    // +Infinity / -Infinity preserved. lastIndexOf then
                    // clamps: +Infinity → len-1 (search whole array),
                    // -Infinity → -1 (no match possible because the
                    // backwards loop starts before index 0).
                    if (std::isnan(d)) from = 0;
                    else if (std::isinf(d)) {
                        if (d > 0) from = len - 1;
                        else return ctx->fromInteger(-1LL);
                    }
                    else from = static_cast<long long>(d);
                }
            }
        }
    }
    if (from < 0) from += len;
    if (from >= len) from = len - 1;
    // NaN is never found (Strict Equality).
    bool needleIsNaN = needle && (needle->isDouble(ctx) || needle->isFloat(ctx)) &&
                       std::isnan(needle->asDouble(ctx));
    if (needleIsNaN) return ctx->fromInteger(-1LL);
    // §23.1.3.18 lastIndexOf needle-specialised skip: when needle is
    // strictly NOT undefined, holes can NEVER match (since strict
    // equality won't equate any defined value with PROTO_NONE / the
    // undefined sentinel).  Adding HasProperty for the undefined case
    // is correct per spec but would regress receivers where explicit
    // undefined args store as PROTO_NONE (e.g. `new Array(undefined)`
    // — 15.4.4.14-9-4); gate the HasProperty walk on needle === undefined
    // so 15.4.4.15-2-1 ({1:null, 2:undefined, length:2}.lastIndexOf
    // (undefined) → -1) passes without breaking the undefined-element
    // family.
    const proto::ProtoObject* undefSent2 = getUndefinedSentinel();
    bool needleIsUndefined = (!needle || needle == PROTO_NONE || needle == undefSent2);
    for (long long i = from; i >= 0; i--) {
        if (needleIsUndefined &&
            !arrHasProperty(ctx, self, static_cast<unsigned long>(i))) continue;
        const proto::ProtoObject* elem = arrGet(ctx, self, static_cast<unsigned long>(i));
        // §23.1.3.18 step 7.a Get(O, Pk) is the abrupt-completion site
        // — an accessor at the current index must terminate iteration
        // (built-ins/Array/prototype/lastIndexOf/15.4.4.15-8-b-i-31
        // probes that the earlier index 1 getter must NEVER fire when
        // index 2's getter threw).  Pre-fix the loop swallowed the
        // throw and kept descending.
        if (hasCallException()) return PROTO_NONE;
        if (strictEquals(ctx, elem, needle))
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
    long long len = static_cast<long long>(arrLen(ctx, self));
    // §23.1.3.13 step 3: if len is 0, return false BEFORE the
    // fromIndex coercion runs.  Pre-fix arrayIncludes flowed into the
    // ToInteger(fromIndex) step even on an empty receiver — a
    // throwing valueOf was therefore invoked needlessly (built-ins/
    // Array/prototype/includes/length-zero-returns-false reports
    // calls=1 instead of 0).
    if (len == 0) return PROTO_FALSE;
    // §23.1.3.13 step 5: searchElement defaults to undefined when no
    // argument is supplied. Pre-fix arrayIncludes early-returned false
    // on no-arg, so `[undefined].includes()` reported false instead of
    // the spec-required true via SameValueZero(undefined, undefined)
    // (built-ins/Array/prototype/includes/no-arg.js).
    const proto::ProtoObject* needle = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0)
        : getUndefinedSentinel();
    long long from = 0;
    if (args && args->getSize(ctx) > 1) {
        const proto::ProtoObject* fi = args->getAt(ctx, 1);
        if (fi && fi != PROTO_NONE) {
            // §23.1.3.13 step 4: ToIntegerOrInfinity(fromIndex) starts
            // with ToNumber.  Pre-fix the helper accepted only raw
            // integer / double cells, so {valueOf(){throw}} silently
            // defaulted to 0 and the includes loop never propagated
            // the user's abrupt (built-ins/Array/prototype/includes/
            // return-abrupt-tointeger-fromindex).  Route non-numeric
            // values through jsToNumber to honour ToNumber's side
            // effects.
            const proto::ProtoObject* num = fi;
            if (!fi->isInteger(ctx) && !fi->isDouble(ctx) && !fi->isFloat(ctx)) {
                num = jsToNumber(ctx, fi);
                if (hasCallException()) return PROTO_NONE;
            }
            if (num && num->isInteger(ctx)) from = num->asLong(ctx);
            else if (num && (num->isDouble(ctx) || num->isFloat(ctx))) {
                double d = num->asDouble(ctx);
                // ECMA-262 ToIntegerOrInfinity for includes:
                //   NaN -> 0
                //   +Infinity -> return false (past end, no match)
                //   -Infinity -> 0 (search whole array)
                if (std::isnan(d)) from = 0;
                else if (std::isinf(d)) {
                    if (d > 0) return PROTO_FALSE;
                    from = 0;
                }
                else from = static_cast<long long>(d);
            }
        }
    }
    from = normalizeIdx(from, len);
    for (long long i = from; i < len; i++) {
        // §22.1.3.11 step 7.a Get(O, ! ToString(k)) is the abrupt-
        // completion site: when an own accessor throws, includes must
        // forward the exception and NOT continue probing later
        // indices.  Pre-fix arrGet stashed the throw in t_callException
        // but the loop kept advancing, so the test's stopped++ getter
        // at index 2 fired even after index 1's throw.
        const proto::ProtoObject* el = arrGet(ctx, self, static_cast<unsigned long>(i));
        if (hasCallException()) return PROTO_NONE;
        // §23.1.3.13 step 7.a: Get(O, ToString(k)). For a hole, Get
        // returns undefined (after walking the prototype chain). Our
        // arrGet maps both "real hole + no proto fallback" AND
        // "explicit undefined" to PROTO_NONE — but PROTO_NONE
        // compares as !== to the undefined sentinel under
        // SameValueZero. Normalise so [ , , , ].includes(undefined)
        // surfaces true (built-ins/Array/prototype/includes/sparse.js).
        if (!el || el == PROTO_NONE) el = getUndefinedSentinel();
        if (sameValueZero(ctx, el, needle))
            return PROTO_TRUE;
    }
    return PROTO_FALSE;
}

// Forward decl: iterReceiver is defined later in the file.
static const proto::ProtoObject* iterReceiver(proto::ProtoContext* ctx,
                                              const proto::ProtoObject* self);

static const proto::ProtoObject* arrayReverse(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*,
    const proto::ProtoSparseList*)
{
    if (arrayThrowIfNullUndefined(ctx, self)) return PROTO_NONE;
    // §23.1.3.27 step 1 + step 7: Let O be ? ToObject(this value);
    // Return O.  For a primitive receiver (boolean / number / string),
    // ToObject wraps it and the spec returns the wrapper.  Pre-fix
    // arrayReverse returned the raw primitive, so
    // \`Array.prototype.reverse.call(true) instanceof Boolean\` was
    // false (built-ins/Array/prototype/reverse/call-with-boolean).
    const proto::ProtoObject* O = iterReceiver(ctx, self);
    unsigned long len = arrLen(ctx, self);
    for (unsigned long i = 0; i < len / 2; i++) {
        unsigned long j = len - 1 - i;
        const proto::ProtoObject* a = arrGet(ctx, self, i);
        const proto::ProtoObject* b = arrGet(ctx, self, j);
        arrSet(ctx, self, i, b);
        arrSet(ctx, self, j, a);
    }
    return O ? O : self;
}

// Forward decls: arraySort / arraySplice are defined later.
static const proto::ProtoObject* arraySort(proto::ProtoContext*, const proto::ProtoObject*,
                                           const proto::ParentLink*,
                                           const proto::ProtoList*, const proto::ProtoSparseList*);
static const proto::ProtoObject* arraySplice(proto::ProtoContext*, const proto::ProtoObject*,
                                             const proto::ParentLink*,
                                             const proto::ProtoList*, const proto::ProtoSparseList*);

// ES2023 immutable equivalents: produce a copy, then apply the
// mutating operation on the copy.  Spec: §23.1.3.32, 33, 34, 35.

static const proto::ProtoObject* arrayCloneShallow(proto::ProtoContext* ctx,
                                                   const proto::ProtoObject* self)
{
    if (!self || self == PROTO_NONE) return PROTO_NONE;
    unsigned long len = arrLen(ctx, self);
    // §22.1.3.1.1 ArraySpeciesCreate raises TypeError on non-Object /
    // non-undefined custom .constructor.  Pre-fix arrayCloneShallow
    // ignored the abrupt completion and the toReversed / toSorted /
    // toSpliced / with shim built on top of it proceeded silently.
    const proto::ProtoObject* dst = arraySpeciesCreate(ctx, self, len);
    if (hasCallException()) return PROTO_NONE;
    if (!dst) return PROTO_NONE;
    const proto::ProtoList* els = ctx->newList();
    // ECMA-262 §23.1.3.{toReversed,toSorted,toSpliced,with} all step
    // through "Let fromValue be ? Get(O, from); CreateDataProperty-
    // OrThrow(A, Pk, fromValue)" for every index in [0, len). A hole
    // (Get returns undefined) still gets an EXPLICIT own undefined
    // property on the destination, distinct from "no value" — so
    // hasOwnProperty(k) on the result must return true even when the
    // source had a hole or returned undefined via the prototype chain.
    // Pre-fix the clone stored PROTO_NONE for an undefined read, and
    // that aliased the destination slot to "fall through to the
    // prototype" on subsequent reads. (built-ins/Array/prototype/
    // toReversed/holes-not-preserved.js + toSorted/toSpliced/with
    // variants.)
    for (unsigned long i = 0; i < len; i++) {
        const proto::ProtoObject* v = arrGet(ctx, self, i);
        if (!v || v == PROTO_NONE) v = getUndefinedSentinel();
        els = els->appendLast(ctx, v);
    }
    setArrayElements(ctx, dst, els);
    const proto::ProtoString* lk = JSSymbols::length(ctx);
    if (lk) dst = dst->setAttribute(ctx, lk, ctx->fromInteger(static_cast<long long>(len)));
    return dst;
}

static const proto::ProtoObject* arrayToReversed(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{
    if (arrayThrowIfNullUndefined(ctx, self)) return PROTO_NONE;
    // §23.1.3.36 step 5: walk k = 0..len, reading O[len-k-1] each step.
    // The READ ORDER is len-1, len-2, ..., 0 — observable via accessor
    // getters.  Pre-fix toReversed did arrayCloneShallow (ascending
    // reads) then arrayReverse, so user-visible getter order was
    // 0,1,2,...  Test built-ins/Array/prototype/toReversed/get-
    // descending-order probes the spec-correct sequence.
    long long len = static_cast<long long>(arrLen(ctx, self));
    if (hasCallException()) return PROTO_NONE;
    const proto::ProtoObject* result = createNewArray(ctx, nullptr);
    if (!result) return PROTO_NONE;
    const proto::ProtoObject* undefSent = getUndefinedSentinel();
    for (long long k = 0; k < len; k++) {
        const proto::ProtoObject* v =
            arrGet(ctx, self, static_cast<unsigned long>(len - k - 1));
        if (hasCallException()) return PROTO_NONE;
        // CreateDataPropertyOrThrow even on holes — toReversed
        // collapses holes into own undefined data properties
        // (built-ins/Array/prototype/toReversed/holes-not-preserved).
        if (!v || v == PROTO_NONE) v = undefSent;
        arrSet(ctx, result, static_cast<unsigned long>(k), v);
    }
    arrSetLen(ctx, result, static_cast<unsigned long>(len > 0 ? len : 0));
    return result;
}

static const proto::ProtoObject* arrayToSorted(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    if (arrayThrowIfNullUndefined(ctx, self)) return PROTO_NONE;
    // §23.1.3.34 toSorted explicitly uses ArrayCreate(len), NOT
    // ArraySpeciesCreate — Symbol.species and any custom .constructor
    // are ignored.  Pre-fix toSorted delegated to arrayCloneShallow
    // (which DOES walk species), so a poisoned .constructor getter
    // fired and toSorted bubbled the user's abrupt completion
    // (built-ins/Array/prototype/toSorted/ignores-species).
    long long len = static_cast<long long>(arrLen(ctx, self));
    if (hasCallException()) return PROTO_NONE;
    const proto::ProtoObject* copy = createNewArray(ctx, nullptr);
    if (!copy) return PROTO_NONE;
    const proto::ProtoObject* undefSent = getUndefinedSentinel();
    for (long long k = 0; k < len; k++) {
        const proto::ProtoObject* v = arrGet(ctx, self, static_cast<unsigned long>(k));
        if (hasCallException()) return PROTO_NONE;
        if (!v || v == PROTO_NONE) v = undefSent;
        arrSet(ctx, copy, static_cast<unsigned long>(k), v);
    }
    arrSetLen(ctx, copy, static_cast<unsigned long>(len > 0 ? len : 0));
    return arraySort(ctx, copy, nullptr, args, nullptr);
}

static const proto::ProtoObject* arrayToSpliced(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    if (arrayThrowIfNullUndefined(ctx, self)) return PROTO_NONE;
    // §23.1.3.37 toSpliced walks the source twice: once for the
    // prefix [0, actualStart), once for the suffix [actualStart +
    // actualDeleteCount, len) — the deleted window is NEVER [[Get]].
    // Pre-fix toSpliced cloned the whole source via arrayCloneShallow
    // (reads every index) then mutated via splice, so a throwing
    // accessor inside the deleted window fired even though the spec
    // skips it (built-ins/Array/prototype/toSpliced/discarded-
    // element-not-read).
    long long len = static_cast<long long>(arrLen(ctx, self));
    if (hasCallException()) return PROTO_NONE;
    long long n = args ? static_cast<long long>(args->getSize(ctx)) : 0LL;

    // ToIntegerOrInfinity helper (mirrors splice/slice).
    auto toII = [&](const proto::ProtoObject* o, long long defaultV) -> long long {
        if (!o || o == PROTO_NONE || o == getUndefinedSentinel()) return defaultV;
        const proto::ProtoObject* num = o;
        if (!o->isInteger(ctx) && !o->isDouble(ctx) && !o->isFloat(ctx)) {
            num = jsToNumber(ctx, o);
            if (hasCallException() || !num) return defaultV;
        }
        if (num->isInteger(ctx)) return num->asLong(ctx);
        if (num->isDouble(ctx) || num->isFloat(ctx)) {
            double d = num->asDouble(ctx);
            if (std::isnan(d)) return 0;
            if (std::isinf(d)) return d > 0 ? len : -len - 1;
            return static_cast<long long>(d);
        }
        return defaultV;
    };

    long long start = n >= 1 ? toII(args->getAt(ctx, 0), 0) : 0;
    if (hasCallException()) return PROTO_NONE;
    if (start < 0) { start += len; if (start < 0) start = 0; }
    if (start > len) start = len;

    long long delCount;
    if (n == 0)      delCount = 0;
    else if (n == 1) delCount = len - start;
    else {
        delCount = toII(args->getAt(ctx, 1), 0);
        if (hasCallException()) return PROTO_NONE;
        if (delCount < 0) delCount = 0;
        if (delCount > len - start) delCount = len - start;
    }

    long long insertCount = n >= 2 ? n - 2 : 0;
    long long newLen = len - delCount + insertCount;

    const proto::ProtoObject* result = createNewArray(ctx, nullptr);
    if (!result) return PROTO_NONE;
    long long i = 0;
    const proto::ProtoObject* undefSent = getUndefinedSentinel();
    // Spec §23.1.3.37 step 13/18 uses CreateDataPropertyOrThrow for
    // EVERY visited slot — toSpliced collapses holes into own
    // `undefined` data properties.  Use the JS undefined sentinel
    // instead of PROTO_NONE so the slot becomes a real own attr
    // (built-ins/Array/prototype/toSpliced/holes-not-preserved).
    auto write = [&](long long ti, const proto::ProtoObject* v) {
        if (!v || v == PROTO_NONE) v = undefSent;
        arrSet(ctx, result, static_cast<unsigned long>(ti), v);
    };
    // Prefix: copy O[0..start).
    for (long long k = 0; k < start; k++, i++) {
        const proto::ProtoObject* v = arrGet(ctx, self, static_cast<unsigned long>(k));
        if (hasCallException()) return PROTO_NONE;
        write(i, v);
    }
    // Inserts: args[2..n).
    for (long long j = 0; j < insertCount; j++, i++) {
        write(i, args->getAt(ctx, static_cast<int>(2 + j)));
    }
    // Suffix: copy O[start + delCount .. len).  The deleted window is
    // NEVER touched.
    for (long long k = start + delCount; k < len; k++, i++) {
        const proto::ProtoObject* v = arrGet(ctx, self, static_cast<unsigned long>(k));
        if (hasCallException()) return PROTO_NONE;
        write(i, v);
    }
    arrSetLen(ctx, result, static_cast<unsigned long>(newLen > 0 ? newLen : 0));
    return result;
}

static const proto::ProtoObject* arrayWith(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    if (arrayThrowIfNullUndefined(ctx, self)) return PROTO_NONE;
    if (!args || args->getSize(ctx) < 1) return arrayCloneShallow(ctx, self);
    long long idx = 0;
    const proto::ProtoObject* iv = args->getAt(ctx, 0);
    // §23.1.3.39 step 4: relativeIndex := ? ToIntegerOrInfinity(index).
    // ToIntegerOrInfinity begins with ToNumber, which runs valueOf /
    // [Symbol.toPrimitive] / toString.  Pre-fix arrayWith only honoured
    // raw integer / double cells and silently defaulted to 0 on a
    // throwing valueOf object — the test then saw the synthetic
    // RangeError from the empty-range check instead of the user's
    // MyError (built-ins/Array/prototype/with/index-throw-completion).
    if (iv && iv->isInteger(ctx)) idx = iv->asLong(ctx);
    else if (iv && iv->isDouble(ctx)) {
        double d = iv->asDouble(ctx);
        // §7.1.5 ToIntegerOrInfinity: NaN → 0.  Pre-fix the bare cast
        // produced LLONG_MIN on a NaN argument and arrayWith then
        // raised a spurious RangeError (built-ins/Array/prototype/
        // with/index-casted-to-number).
        if (std::isnan(d)) idx = 0;
        else if (std::isinf(d)) idx = d > 0 ? LLONG_MAX : LLONG_MIN;
        else idx = static_cast<long long>(d);
    }
    else if (iv && iv != PROTO_NONE && iv != getUndefinedSentinel()) {
        const proto::ProtoObject* num = jsToNumber(ctx, iv);
        if (hasCallException()) return PROTO_NONE;
        if (num && num->isInteger(ctx)) idx = num->asLong(ctx);
        else if (num && (num->isDouble(ctx) || num->isFloat(ctx))) {
            double d = num->asDouble(ctx);
            if (std::isnan(d)) idx = 0;
            else if (std::isinf(d)) idx = d > 0 ? LLONG_MAX : LLONG_MIN;
            else idx = static_cast<long long>(d);
        }
    }
    long long len = static_cast<long long>(arrLen(ctx, self));
    if (idx < 0) idx += len;
    if (idx < 0 || idx >= len) {
        // Spec §23.1.3.39 step 5: throw RangeError when the actual
        // index is outside [0, len). Pre-fix we silently cloned.
        signalNativeException(makeNativeError(ctx, "RangeError",
            "Invalid index"));
        return PROTO_NONE;
    }
    const proto::ProtoObject* val = args->getSize(ctx) > 1 ? args->getAt(ctx, 1) : PROTO_NONE;
    // §23.1.3.39 step 5.b: when k === actualIndex, fromValue := value
    // (the user-supplied replacement) — do NOT [[Get]] the source at
    // the replaced index.  Pre-fix arrayCloneShallow read every index
    // including idx, so a throwing accessor at idx fired and a side-
    // effecting getter ran when the spec forbids it
    // (built-ins/Array/prototype/with/no-get-replaced-index).
    const proto::ProtoObject* result = createNewArray(ctx, nullptr);
    if (!result) return PROTO_NONE;
    const proto::ProtoObject* undefSent = getUndefinedSentinel();
    for (long long k = 0; k < len; k++) {
        const proto::ProtoObject* fromValue;
        if (k == idx) {
            fromValue = val ? val : undefSent;
        } else {
            fromValue = arrGet(ctx, self, static_cast<unsigned long>(k));
            if (hasCallException()) return PROTO_NONE;
        }
        // CreateDataPropertyOrThrow even on holes — `with` collapses
        // holes into own undefined data properties
        // (built-ins/Array/prototype/with/holes-not-preserved).
        if (!fromValue || fromValue == PROTO_NONE) fromValue = undefSent;
        arrSet(ctx, result, static_cast<unsigned long>(k), fromValue);
    }
    arrSetLen(ctx, result, static_cast<unsigned long>(len > 0 ? len : 0));
    return result;
}

static const proto::ProtoObject* arrayConcat(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (arrayThrowIfNullUndefined(ctx, self)) return PROTO_NONE;
    // Spec §22.1.3.1 step 1: O = ToObject(this). For primitives the
    // wrapper participates as a non-spreadable Object so the result
    // contains the wrapper itself, not the primitive. Pre-fix concat
    // appended the raw primitive — `Array.prototype.concat.call(101)[0]`
    // came back as the number 101 instead of `new Number(101)`.
    auto boxPrimitive = [&](const proto::ProtoObject* v) -> const proto::ProtoObject* {
        if (!v || v == PROTO_NONE) return v;
        const proto::ProtoObject* parent = nullptr;
        if (v->isInteger(ctx)) parent = ctx->space ? ctx->space->smallIntegerPrototype : nullptr;
        else if (v->isDouble(ctx) || v->isFloat(ctx)) parent = ctx->space ? ctx->space->doublePrototype : nullptr;
        else if (v->isBoolean(ctx) || v == PROTO_TRUE || v == PROTO_FALSE) parent = ctx->space ? ctx->space->booleanPrototype : nullptr;
        else if (v->isString(ctx)) parent = ctx->space ? ctx->space->stringPrototype : nullptr;
        else return v;
        const proto::ProtoObject* wrap = parent ? parent->newChild(ctx, true) : ctx->newObject(true);
        if (!wrap) return v;
        const proto::ProtoString* pvK = JSSymbols::primitiveValue(ctx);
        if (pvK) wrap = wrap->setAttribute(ctx, pvK, v);
        return wrap;
    };
    if (self && (self->isInteger(ctx) || self->isDouble(ctx) || self->isFloat(ctx)
                 || self->isString(ctx) || self->isBoolean(ctx))) {
        self = boxPrimitive(self);
    }

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
    if (hasCallException()) return PROTO_NONE;
    unsigned long outIdx = 0;

    // ECMA-262 §22.1.3.1.1 IsConcatSpreadable:
    //   1. If Type(O) is not Object, return false.
    //   2. spreadable = Get(O, @@isConcatSpreadable).
    //   3. If spreadable is not undefined, return ToBoolean(spreadable).
    //   4. Return IsArray(O).
    // Pre-fix the lambda used a "has .length" heuristic — arrays were
    // always spread (even with Symbol.isConcatSpreadable = false) and
    // array-like objects were always spread (even without the symbol
    // being explicitly set), both diverging from V8 / SpiderMonkey.
    auto isSpreadable = [&](const proto::ProtoObject* obj) -> bool {
        if (!obj || obj == PROTO_NONE) return false;
        if (obj->isInteger(ctx) || obj->isDouble(ctx) || obj->isFloat(ctx) ||
            obj->isString(ctx) || obj->isBoolean(ctx)) return false;
        // Step 2: probe @@isConcatSpreadable via the WKS string key.
        const proto::ProtoObject* spreadObj =
            ctx->fromUTF8String("Symbol.isConcatSpreadable");
        const proto::ProtoString* spreadKey =
            spreadObj ? spreadObj->asString(ctx) : nullptr;
        if (spreadKey) {
            const proto::ProtoObject* sv = obj->getAttribute(ctx, spreadKey, true);
            // Object.defineProperty(o, Symbol.isConcatSpreadable, {get:...})
            // stores the undefinedSentinel placeholder under the property
            // key and the actual getter under __get_Symbol.isConcatSpreadable__.
            // Pre-fix the accessor went undetected so the throwing getter
            // never fired and ToBoolean defaulted to false. Invoke the
            // getter when the placeholder fires (or when no data is found).
            if (!sv || sv == PROTO_NONE || sv == getUndefinedSentinel()) {
                const proto::ProtoObject* gko =
                    ctx->fromUTF8String("__get_Symbol.isConcatSpreadable__");
                const proto::ProtoString* gks = gko ? gko->asString(ctx) : nullptr;
                if (gks) {
                    const proto::ProtoObject* getter = obj->getAttribute(ctx, gks, true);
                    if (getter && getter != PROTO_NONE) {
                        sv = callJSFunction(ctx, getter, obj, ctx->newList());
                        if (hasCallException()) return false;
                    }
                }
            }
            if (sv && sv != PROTO_NONE && sv != getUndefinedSentinel()) {
                // §7.1.2 ToBoolean — pre-fix returned true for 0 / NaN
                // / '' / null because the fallback only excluded the
                // null sentinel. Now apply the full ToBoolean ruleset.
                if (sv == PROTO_TRUE) return true;
                if (sv == PROTO_FALSE) return false;
                if (sv == getNullSentinel()) return false;
                if (sv->isBoolean(ctx)) return sv->asBoolean(ctx);
                if (sv->isInteger(ctx)) return sv->asLong(ctx) != 0;
                if (sv->isDouble(ctx) || sv->isFloat(ctx)) {
                    double d = sv->asDouble(ctx);
                    return d != 0.0 && !std::isnan(d);
                }
                if (sv->isString(ctx)) {
                    const proto::ProtoString* ps = sv->asString(ctx);
                    if (!ps) return false;
                    std::string s; ps->toUTF8String(ctx, s);
                    return !s.empty();
                }
                return true;  // Objects coerce to true.
            }
        }
        // Step 4: IsArray(O) — probe the __is_array__ marker.
        const proto::ProtoString* isArrKey = JSSymbols::isArray(ctx);
        if (isArrKey) {
            const proto::ProtoObject* ia = obj->getAttribute(ctx, isArrKey, true);
            if (ia == PROTO_TRUE) return true;
        }
        return false;
    };

    // §23.1.3.1 step 5.b.iii / 5.c.iii.6: CreateDataPropertyOrThrow
    // for every appended slot.  Surface TypeError when the species-
    // built result is non-extensible OR carries a non-configurable
    // descriptor at the destination index (built-ins/Array/prototype/
    // concat/target-array-non-extensible / target-array-with-non-
    // configurable-property).
    auto writeOrThrow = [&](const proto::ProtoObject* v) -> bool {
        result = arrayCreateDataPropertyOrThrow(ctx, result, outIdx, v);
        if (hasCallException()) return true;
        outIdx++;
        return false;
    };

    // Spread self.
    if (isSpreadable(self)) {
        unsigned long n = arrLen(ctx, self);
        for (unsigned long i = 0; i < n; i++) {
            if (writeOrThrow(arrGet(ctx, self, i))) return PROTO_NONE;
        }
    } else if (self && self != PROTO_NONE) {
        if (writeOrThrow(self)) return PROTO_NONE;
    }

    // Spread each argument.
    if (args) {
        unsigned long argc = static_cast<unsigned long>(args->getSize(ctx));
        for (unsigned long ai = 0; ai < argc; ai++) {
            const proto::ProtoObject* item = args->getAt(ctx, static_cast<int>(ai));
            if (isSpreadable(item)) {
                unsigned long n = arrLen(ctx, item);
                for (unsigned long i = 0; i < n; i++) {
                    if (writeOrThrow(arrGet(ctx, item, i))) return PROTO_NONE;
                }
            } else {
                if (writeOrThrow(item)) return PROTO_NONE;
            }
        }
    }

    result = arrSetLen(ctx, result, outIdx);
    return result;
}

static const proto::ProtoObject* iterReceiver(proto::ProtoContext* ctx,
                                              const proto::ProtoObject* self);

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
    // ECMA-262 §23.1.3.6: ToIntegerOrInfinity on start/end.  NaN → 0;
    // +Infinity → length (clamp); -Infinity → 0 after the negative
    // normalisation.  Non-primitive arguments route through
    // jsToNumber so a Symbol surfaces the spec-required TypeError
    // (built-ins/Array/prototype/fill/return-abrupt-from-end-as-symbol)
    // instead of silently collapsing to len.
    auto toII = [&](const proto::ProtoObject* o, long long defaultV) -> long long {
        if (!o || o == PROTO_NONE || o == getUndefinedSentinel()) return defaultV;
        const proto::ProtoObject* num = o;
        if (!o->isInteger(ctx) && !o->isDouble(ctx) && !o->isFloat(ctx)) {
            num = jsToNumber(ctx, o);
            if (hasCallException() || !num) return defaultV;
        }
        if (num->isInteger(ctx)) return num->asLong(ctx);
        if (num->isDouble(ctx) || num->isFloat(ctx)) {
            double d = num->asDouble(ctx);
            if (std::isnan(d)) return 0;
            if (std::isinf(d)) return d > 0 ? len : -len - 1;
            return static_cast<long long>(d);
        }
        return defaultV;
    };
    if (args && args->getSize(ctx) > 1) {
        start = toII(args->getAt(ctx, 1), 0);
        if (hasCallException()) return PROTO_NONE;
    }
    if (args && args->getSize(ctx) > 2) {
        end   = toII(args->getAt(ctx, 2), len);
        if (hasCallException()) return PROTO_NONE;
    }

    start = normalizeIdxClamp(start, len);
    end   = normalizeIdxClamp(end,   len);

    // Save the user-visible length: arrSet via arrayTryFastSet's sparse
    // path calls setArrayElements which syncs the length attribute to
    // __elements__.size, which would shrink a sparse array's
    // user-visible length even when fill only mutates inner slots.
    // \`[,,,, 0].fill(8, 1, 3).length\` must stay 5
    // (built-ins/Array/prototype/fill/fill-values-custom-start-and-end).
    unsigned long savedLen = static_cast<unsigned long>(len);
    bool wrote = false;

    for (long long i = start; i < end; i++) {
        arrSet(ctx, self, static_cast<unsigned long>(i), value);
        if (hasCallException()) return PROTO_NONE;
        wrote = true;
    }

    // Restore the saved length attribute — but only when we actually
    // wrote anything.  A no-op fill (start >= end) must not Set
    // 'length' at all, so a frozen-length empty array doesn't throw
    // (built-ins/Array/prototype/fill/return-abrupt-from-setting-
    // property-value pins \`Object.freeze([]); [].fill(1)\`).
    if (wrote) {
        arrSetLen(ctx, self, savedLen);
        if (hasCallException()) return PROTO_NONE;
    }

    // §23.1.3.6 step 1: Let O be ? ToObject(this).  step 9 returns O.
    // Wrap primitive receivers so Array.prototype.fill.call(true)
    // instanceof Boolean is true (built-ins/Array/prototype/fill/
    // call-with-boolean).
    return iterReceiver(ctx, self);
}

static const proto::ProtoObject* iterReceiver(proto::ProtoContext* ctx,
                                              const proto::ProtoObject* self);

static const proto::ProtoObject* arrayCopyWithin(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (arrayThrowIfNullUndefined(ctx, self)) return PROTO_NONE;
    // §23.1.3.4 step 1: Let O be ? ToObject(this value). step 14 returns O.
    // Pre-fix copyWithin returned the bare primitive (Array.prototype.
    // copyWithin.call(true) returned true), so user code probing
    // `instanceof Boolean` saw false against the spec's true.
    const proto::ProtoObject* O = iterReceiver(ctx, self);
    // §23.1.3.4 step 3 runs len := ToLength(? Get(O, 'length')) BEFORE
    // any arg processing — a throwing length accessor must surface its
    // abrupt completion even on a no-arg call.  Pre-fix the empty-args
    // fast path returned O without exercising the length read, so the
    // user's getter never fired (built-ins/Array/prototype/copyWithin/
    // return-abrupt-from-this-length).
    long long len = static_cast<long long>(arrLen(ctx, self));
    if (hasCallException()) return PROTO_NONE;
    if (!args || args->getSize(ctx) == 0)
        return O ? O : PROTO_NONE;
    // ECMA-262 §23.1.3.4 steps 4-9: ToIntegerOrInfinity on target,
    // start, end. Same lambda used by slice/splice/flat/fill.  Non-
    // numeric inputs route through jsToNumber so a Symbol (or other
    // un-coerceable value) raises the spec-required TypeError instead
    // of silently defaulting to 0 (built-ins/Array/prototype/copyWithin
    // /return-abrupt-from-target-as-symbol).
    auto toII = [&](const proto::ProtoObject* o, long long defaultV) -> long long {
        if (!o || o == PROTO_NONE || o == getUndefinedSentinel()) return defaultV;
        const proto::ProtoObject* num = o;
        if (!o->isInteger(ctx) && !o->isDouble(ctx) && !o->isFloat(ctx)) {
            num = jsToNumber(ctx, o);
            if (hasCallException() || !num) return defaultV;
        }
        if (num->isInteger(ctx)) return num->asLong(ctx);
        if (num->isDouble(ctx) || num->isFloat(ctx)) {
            double d = num->asDouble(ctx);
            if (std::isnan(d)) return 0;
            if (std::isinf(d)) return d > 0 ? len : -len - 1;
            return static_cast<long long>(d);
        }
        return defaultV;
    };
    long long target = toII(args->getAt(ctx, 0), 0);
    if (hasCallException()) return PROTO_NONE;
    long long start  = (args->getSize(ctx) > 1) ? toII(args->getAt(ctx, 1), 0)   : 0;
    if (hasCallException()) return PROTO_NONE;
    long long end    = (args->getSize(ctx) > 2) ? toII(args->getAt(ctx, 2), len) : len;
    if (hasCallException()) return PROTO_NONE;

    target = normalizeIdxClamp(target, len);
    start  = normalizeIdxClamp(start,  len);
    end    = normalizeIdxClamp(end,    len);

    long long count = std::min(end - start, len - target);
    if (count <= 0) return O;
    // Save the user-visible length: setArrayElements (called by
    // arrayTryFastSet's sparse-grow path below) writes back
    // __elements__.size to the length attribute, which can shrink a
    // sparse array's user-visible length when copyWithin only mutates
    // inner slots — \`[0, 1, , , 1].copyWithin(0, 1, 4).length\` must
    // stay 5 (built-ins/Array/prototype/copyWithin/fill-holes).
    unsigned long savedLen = static_cast<unsigned long>(len);

    // Read source range into a temporary buffer to handle overlaps.
    // Track fromPresent per index so step 17.f's
    // DeletePropertyOrThrow(O, toKey) runs for absent sources.
    std::vector<const proto::ProtoObject*> tmp;
    std::vector<bool> fromPresent;
    tmp.reserve(static_cast<size_t>(count));
    fromPresent.reserve(static_cast<size_t>(count));
    for (long long i = 0; i < count; i++) {
        bool present = arrHasProperty(ctx, self, static_cast<unsigned long>(start + i));
        if (hasCallException()) return PROTO_NONE;
        fromPresent.push_back(present);
        if (present) {
            tmp.push_back(arrGet(ctx, self, static_cast<unsigned long>(start + i)));
            if (hasCallException()) return PROTO_NONE;
        } else {
            tmp.push_back(PROTO_NONE);
        }
    }
    for (long long i = 0; i < count; i++) {
        unsigned long toIdx = static_cast<unsigned long>(target + i);
        if (fromPresent[static_cast<size_t>(i)]) {
            arrSet(ctx, self, toIdx, tmp[static_cast<size_t>(i)]);
            if (hasCallException()) return PROTO_NONE;
        } else {
            // §23.1.3.4 step 17.f: DeletePropertyOrThrow(O, toKey).
            // A non-configurable own data slot can't be deleted —
            // throw TypeError (built-ins/Array/prototype/copyWithin/
            // return-abrupt-from-delete-target).
            const proto::ProtoString* tk =
                JSSymbols::indexKey(ctx, static_cast<uint32_t>(toIdx));
            if (tk) {
                // Check own configurable bit before clearing.
                std::string pdStr = "__pd_" + std::to_string(toIdx) + "__";
                const proto::ProtoObject* pdko = ctx->fromUTF8String(pdStr.c_str());
                const proto::ProtoString* pdk = pdko ? pdko->asString(ctx) : nullptr;
                if (pdk && self->hasOwnAttribute(ctx, pdk) == PROTO_TRUE) {
                    const proto::ProtoObject* pdv = self->getAttribute(ctx, pdk, false);
                    if (pdv && pdv->isInteger(ctx) && (pdv->asLong(ctx) & 0x2) == 0) {
                        signalNativeException(makeNativeError(ctx, "TypeError",
                            "Cannot delete non-configurable property"));
                        return PROTO_NONE;
                    }
                }
                // Clear the own data attribute.  For real arrays the
                // data lives in __elements__, NOT in the string-keyed
                // attribute — setAttribute(tk, PROTO_NONE) doesn't
                // affect the dense storage at all, and using it
                // (paradoxically) drops the array length.  Use
                // arrayTryFastSet to write PROTO_NONE into __elements__
                // so the slot becomes a hole; setAttribute remains for
                // the array-like (non-native-storage) path.
                const proto::ProtoString* isArrKey = JSSymbols::isArray(ctx);
                bool isRealArr = isArrKey
                    && self->getAttribute(ctx, isArrKey, true) == PROTO_TRUE
                    && getArrayElements(ctx, self) != nullptr;
                if (isRealArr) {
                    arrayTryFastSet(ctx, self, toIdx, PROTO_NONE);
                } else {
                    self->setAttribute(ctx, tk, PROTO_NONE);
                }
            }
        }
    }

    // Restore the saved length attribute — copyWithin must NEVER shrink
    // the array (it only mutates / deletes inner slots).
    arrSetLen(ctx, self, savedLen);
    if (hasCallException()) return PROTO_NONE;

    return O;
}

// ---------------------------------------------------------------------------
// JS truthiness helper used by callback-taking methods.
// ---------------------------------------------------------------------------
static bool isTruthy(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    // §7.1.1 ToBoolean: undefined and null are falsy. PROTO_NONE
    // doubles as protoJS's internal "absent" marker; both the
    // user-visible undefined sentinel (the `undefined` identifier) and
    // the null sentinel must also map to false.  Pre-fix isTruthy
    // dropped through to the generic "objects are truthy" branch for
    // both sentinels, so every Array.prototype.{every, some, find,
    // findIndex, filter, ...} callback that returned `undefined`
    // looked truthy — `[1,2,3].every(x => undefined)` evaluated to
    // true where the spec demands false (built-ins/Array/prototype/
    // every/15.4.4.16-7-c-iii-1 caught this).
    if (!v || v == PROTO_NONE) return false;
    if (v == getUndefinedSentinel()) return false;
    if (v == getNullSentinel()) return false;
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

// Helper: ToObject wrapping for the array iteration receiver.
//
// §23.1.3.* iteration methods (every/forEach/map/some/filter/find/
// reduce/...) step 1 does `Let O be ? ToObject(this value)` and then
// passes `O` as the callback's third argument.  When `this` is a
// primitive string ToObject yields a String wrapper carrying
// [[StringData]] — without this wrapping `obj instanceof String`
// in the callback returned false (every-on-string regressions).
//
// We don't actually retarget the read path through the wrapper —
// arrLen / arrGet already handle the primitive directly — we only
// need the wrapper as the value visible to user code as the third
// callback argument.  Symbol primitives are rare receivers for
// Array methods so we leave them as-is; if they ever surface they
// remain valid Array-like at length 0.
//
// Boolean / Number primitives are wrapped too so that
// Array.prototype.copyWithin.call(true) instanceof Boolean === true
// (built-ins/Array/prototype/copyWithin/call-with-boolean).
static const proto::ProtoObject* iterReceiver(proto::ProtoContext* ctx,
                                              const proto::ProtoObject* self) {
    if (!self || self == PROTO_NONE) return self;
    if (self->isString(ctx)) {
        if (!ctx->space || !ctx->space->stringPrototype) return self;
        const proto::ProtoObject* wrap = ctx->space->stringPrototype->newChild(ctx, true);
        if (!wrap) return self;
        const proto::ProtoString* pvKey = JSSymbols::primitiveValue(ctx);
        if (pvKey) wrap = wrap->setAttribute(ctx, pvKey, self);
        return wrap;
    }
    if (self->isBoolean(ctx)) {
        if (!ctx->space || !ctx->space->booleanPrototype) return self;
        const proto::ProtoObject* wrap = ctx->space->booleanPrototype->newChild(ctx, true);
        if (!wrap) return self;
        const proto::ProtoString* pvKey = JSSymbols::primitiveValue(ctx);
        if (pvKey) wrap = wrap->setAttribute(ctx, pvKey, self);
        return wrap;
    }
    if (self->isInteger(ctx) || self->isDouble(ctx) || self->isFloat(ctx)) {
        if (!ctx->space || (!ctx->space->smallIntegerPrototype
                             && !ctx->space->doublePrototype)) return self;
        const proto::ProtoObject* parent = self->isInteger(ctx)
            ? ctx->space->smallIntegerPrototype
            : ctx->space->doublePrototype;
        if (!parent) return self;
        const proto::ProtoObject* wrap = parent->newChild(ctx, true);
        if (!wrap) return self;
        const proto::ProtoString* pvKey = JSSymbols::primitiveValue(ctx);
        if (pvKey) wrap = wrap->setAttribute(ctx, pvKey, self);
        return wrap;
    }
    return self;
}

// Helper: build the three-argument list [element, index, array] for iteration callbacks.
static const proto::ProtoList* makeIterArgs(proto::ProtoContext* ctx,
                                             const proto::ProtoObject* elem,
                                             long long idx,
                                             const proto::ProtoObject* arr) {
    const proto::ProtoList* a = ctx->newList();
    a = a->appendLast(ctx, elem ? elem : PROTO_NONE);
    a = a->appendLast(ctx, ctx->fromInteger(idx));
    a = a->appendLast(ctx, iterReceiver(ctx, arr));
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
    // §23.1.3.15 step ordering: LengthOfArrayLike precedes IsCallable
    // so a throwing `length` accessor surfaces its own exception
    // instead of a synthetic TypeError.
    unsigned long len = arrLen(ctx, self);
    if (hasCallException()) return PROTO_NONE;
    const proto::ProtoObject* fn      = getCallbackArg(ctx, args, 0);
    const proto::ProtoObject* thisArg = getCallbackArg(ctx, args, 1);
    if (arrayThrowIfCallbackNotCallable(ctx, fn, "Array.prototype.forEach")) return PROTO_NONE;
    for (unsigned long i = 0; i < len; i++) {
        if (!arrHasProperty(ctx, self, i)) continue;
        const proto::ProtoObject* elem = arrGet(ctx, self, i);
        // §23.1.3.15 step 6.b.iii.1: ? Get(O, Pk).  Throwing getter
        // terminates iteration BEFORE callback runs.
        if (hasCallException()) return PROTO_NONE;
        callJSFunction(ctx, fn, thisArg, makeIterArgs(ctx, elem, (long long)i, self));
        if (hasCallException()) return PROTO_NONE;
    }
    return PROTO_NONE;
}

// ---------------------------------------------------------------------------
// CreateDataPropertyOrThrow probe: returns true and signals TypeError
// when the index write would fail per §7.3.5/§7.3.6 (target is non-
// extensible AND the slot is new; OR the slot already has a non-
// configurable own descriptor).  Used by every spec method that uses
// CreateDataPropertyOrThrow on its species-created result:
// map / filter / slice / splice / concat / flatMap / Array.from /
// Array.of / toReversed / toSpliced / toSorted / with.
// ---------------------------------------------------------------------------
// CreateDataPropertyOrThrow define-write: handles the failure probe
// (non-extensible + new slot, or non-configurable existing slot →
// TypeError), then writes value + resets __pd_<idx>__ to default
// flags so a ctor-installed (writable:false / enumerable:false) slot
// is replaced WHOLESALE rather than just having its value updated.
//
// Returns the (possibly new) result object — mirrors arrSet's
// signature so callers can chain.
static const proto::ProtoObject* arrayCreateDataPropertyOrThrow(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* obj,
    unsigned long idx,
    const proto::ProtoObject* val);

static bool arrayThrowIfCreateDataPropertyFails(proto::ProtoContext* ctx,
                                                 const proto::ProtoObject* obj,
                                                 unsigned long idx) {
    const proto::ProtoString* k =
        JSSymbols::indexKey(ctx, static_cast<uint32_t>(idx));
    JSContextWrapper* wrapper = JSContextWrapper::current();
    bool nonExtensible = wrapper
        && obj->hasParent(ctx, wrapper->getNonExtensibleMarker());
    bool hasOwnK = k && obj->hasOwnAttribute(ctx, k) == PROTO_TRUE;
    if (nonExtensible && !hasOwnK) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Cannot define property: object is not extensible"));
        return true;
    }
    if (hasOwnK) {
        std::string pdStr = "__pd_" + std::to_string(idx) + "__";
        const proto::ProtoObject* pdko = ctx->fromUTF8String(pdStr.c_str());
        const proto::ProtoString* pdk = pdko ? pdko->asString(ctx) : nullptr;
        if (pdk && obj->hasOwnAttribute(ctx, pdk) == PROTO_TRUE) {
            const proto::ProtoObject* pdv = obj->getAttribute(ctx, pdk, false);
            if (pdv && pdv->isInteger(ctx)
                && (pdv->asLong(ctx) & 0x2) == 0) {
                signalNativeException(makeNativeError(ctx, "TypeError",
                    "Cannot redefine non-configurable data property"));
                return true;
            }
        }
    }
    return false;
}

static const proto::ProtoObject* arrayCreateDataPropertyOrThrow(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* obj,
    unsigned long idx,
    const proto::ProtoObject* val) {
    if (arrayThrowIfCreateDataPropertyFails(ctx, obj, idx)) return obj;
    // Write the value via arrSet so __elements__ is updated for real
    // arrays.
    const proto::ProtoObject* updated = arrSet(ctx, obj, idx, val);
    if (hasCallException()) return updated;
    // Reset __pd_<idx>__ to defaults so a ctor-installed
    // (writable:false, enumerable:false) descriptor is replaced
    // wholesale.  CreateDataProperty installs a FRESH data
    // descriptor; the spec wants {value, writable:true,
    // enumerable:true, configurable:true} regardless of prior state.
    constexpr long long kDefaultPdBits = 0x7;
    std::string pdStr = "__pd_" + std::to_string(idx) + "__";
    const proto::ProtoObject* pdko = ctx->fromUTF8String(pdStr.c_str());
    const proto::ProtoString* pdk = pdko ? pdko->asString(ctx) : nullptr;
    if (pdk) updated = updated->setAttribute(ctx, pdk,
                          ctx->fromInteger(kDefaultPdBits));
    return updated;
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
    // §23.1.3.18: LengthOfArrayLike precedes IsCallable.
    unsigned long len = arrLen(ctx, self);
    if (hasCallException()) return PROTO_NONE;
    const proto::ProtoObject* fn      = getCallbackArg(ctx, args, 0);
    const proto::ProtoObject* thisArg = getCallbackArg(ctx, args, 1);
    if (arrayThrowIfCallbackNotCallable(ctx, fn, "Array.prototype.map")) return PROTO_NONE;
    // §22.1.3.1.1 ArraySpeciesCreate raises TypeError when a custom
    // .constructor is a non-Object primitive (null / number / string /
    // boolean / Symbol).  Pre-fix arrayMap (and the flatMap shim built
    // on top of it) called the helper but did not check the abrupt
    // — the loop kept running on a PROTO_NONE result and the throw
    // was lost (built-ins/Array/prototype/flatMap/this-value-ctor-
    // non-object).
    const proto::ProtoObject* result = arraySpeciesCreate(ctx, self, len);
    if (hasCallException()) return PROTO_NONE;
    // §23.1.3.18 step 6.f.iii: CreateDataPropertyOrThrow(A, k, mappedValue)
    // — every accepted slot must become an OWN data property on the
    // result.  Writing PROTO_NONE clears the attribute rather than
    // creating an undefined slot, leaving newArr[k] to fall through
    // to Array.prototype[k] inherited data.  Convert PROTO_NONE to
    // the JS undefined sentinel so the own data property lands.
    // Pre-fix [1,2,3].map(() => undefined) returned an array whose
    // [k] reads bled through to Array.prototype inheritance.
    const proto::ProtoObject* undefSent = getUndefinedSentinel();
    for (unsigned long i = 0; i < len; i++) {
        if (!arrHasProperty(ctx, self, i)) continue;
        const proto::ProtoObject* elem = arrGet(ctx, self, i);
        // §23.1.3.18 step 6.f.ii: ? Get(O, Pk).  Throwing getter
        // terminates iteration BEFORE callback runs.
        if (hasCallException()) return PROTO_NONE;
        const proto::ProtoObject* mapped =
            callJSFunction(ctx, fn, thisArg, makeIterArgs(ctx, elem, (long long)i, self));
        if (hasCallException()) return PROTO_NONE;
        if (!mapped || mapped == PROTO_NONE) mapped = undefSent;
        result = arrayCreateDataPropertyOrThrow(ctx, result, i, mapped);
        if (hasCallException()) return PROTO_NONE;
    }
    // §23.1.3.18: A was created via ArraySpeciesCreate(O, len).  The
    // SPEC says A.length must equal len AT RETURN — visiting holes
    // doesn't shrink the result.  Pre-fix sparse sources like
    // \`new Array(10).map(...)\` returned A.length == max(written) + 1
    // (built-ins/Array/prototype/map/15.4.4.19-8-5).  Explicitly set
    // length back to the original probe.
    arrSetLen(ctx, result, len);
    if (hasCallException()) return PROTO_NONE;
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
    // §23.1.3.7: LengthOfArrayLike precedes IsCallable.
    unsigned long len = arrLen(ctx, self);
    if (hasCallException()) return PROTO_NONE;
    const proto::ProtoObject* fn      = getCallbackArg(ctx, args, 0);
    const proto::ProtoObject* thisArg = getCallbackArg(ctx, args, 1);
    if (arrayThrowIfCallbackNotCallable(ctx, fn, "Array.prototype.filter")) return PROTO_NONE;
    // §22.1.3.1.1 ArraySpeciesCreate raises TypeError when a custom
    // .constructor is a non-Object primitive — propagate the abrupt
    // before the iteration starts (parallel to the map fix in this
    // round).
    const proto::ProtoObject* result = arraySpeciesCreate(ctx, self, 0);
    if (hasCallException()) return PROTO_NONE;
    // §23.1.3.7 step 7.c.iii.2: CreateDataPropertyOrThrow(A, toIdx,
    // kValue) — every accepted slot must become an OWN data
    // property on the result.  Writing PROTO_NONE clears the
    // attribute rather than creating an undefined slot, leaving
    // newArr[k] to fall through to Array.prototype[k] inherited
    // data (built-ins/Array/prototype/filter/15.4.4.20-9-c-i-20:
    // own setter-only on arr[0] + Array.prototype[0]=100 must
    // surface undefined, not 100, in the filter result).
    const proto::ProtoObject* undefSent = getUndefinedSentinel();
    unsigned long outIdx = 0;
    for (unsigned long i = 0; i < len; i++) {
        if (!arrHasProperty(ctx, self, i)) continue;
        const proto::ProtoObject* elem = arrGet(ctx, self, i);
        // §23.1.3.7 step 7.c.i: ? Get(O, Pk).  Throwing getter
        // terminates iteration BEFORE callback runs.
        if (hasCallException()) return PROTO_NONE;
        const proto::ProtoObject* keep =
            callJSFunction(ctx, fn, thisArg, makeIterArgs(ctx, elem, (long long)i, self));
        if (hasCallException()) return PROTO_NONE;
        if (isTruthy(ctx, keep)) {
            if (!elem || elem == PROTO_NONE) elem = undefSent;
            result = arrayCreateDataPropertyOrThrow(ctx, result, outIdx, elem);
            if (hasCallException()) return PROTO_NONE;
            outIdx++;
        }
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
    // §23.1.3.8: LengthOfArrayLike precedes IsCallable.
    unsigned long len = arrLen(ctx, self);
    if (hasCallException()) return PROTO_NONE;
    const proto::ProtoObject* fn      = getCallbackArg(ctx, args, 0);
    const proto::ProtoObject* thisArg = getCallbackArg(ctx, args, 1);
    if (arrayThrowIfCallbackNotCallable(ctx, fn, "Array.prototype.find")) return PROTO_NONE;
    for (unsigned long i = 0; i < len; i++) {
        const proto::ProtoObject* elem = arrGet(ctx, self, i);
        if (hasCallException()) return PROTO_NONE;
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
    // §23.1.3.9: LengthOfArrayLike precedes IsCallable.
    unsigned long len = arrLen(ctx, self);
    if (hasCallException()) return PROTO_NONE;
    const proto::ProtoObject* fn      = getCallbackArg(ctx, args, 0);
    const proto::ProtoObject* thisArg = getCallbackArg(ctx, args, 1);
    if (arrayThrowIfCallbackNotCallable(ctx, fn, "Array.prototype.findIndex")) return PROTO_NONE;
    for (unsigned long i = 0; i < len; i++) {
        const proto::ProtoObject* elem = arrGet(ctx, self, i);
        if (hasCallException()) return PROTO_NONE;
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
    // §23.1.3.10: LengthOfArrayLike precedes IsCallable.
    unsigned long len = arrLen(ctx, self);
    if (hasCallException()) return PROTO_NONE;
    const proto::ProtoObject* fn      = getCallbackArg(ctx, args, 0);
    const proto::ProtoObject* thisArg = getCallbackArg(ctx, args, 1);
    if (arrayThrowIfCallbackNotCallable(ctx, fn, "Array.prototype.findLast")) return PROTO_NONE;
    for (long long i = (long long)len - 1; i >= 0; i--) {
        const proto::ProtoObject* elem = arrGet(ctx, self, (unsigned long)i);
        if (hasCallException()) return PROTO_NONE;
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
    // §23.1.3.11: LengthOfArrayLike precedes IsCallable.
    unsigned long len = arrLen(ctx, self);
    if (hasCallException()) return PROTO_NONE;
    const proto::ProtoObject* fn      = getCallbackArg(ctx, args, 0);
    const proto::ProtoObject* thisArg = getCallbackArg(ctx, args, 1);
    if (arrayThrowIfCallbackNotCallable(ctx, fn, "Array.prototype.findLastIndex")) return PROTO_NONE;
    for (long long i = (long long)len - 1; i >= 0; i--) {
        const proto::ProtoObject* elem = arrGet(ctx, self, (unsigned long)i);
        if (hasCallException()) return PROTO_NONE;
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
    // §23.1.3.28: LengthOfArrayLike precedes IsCallable.
    unsigned long len = arrLen(ctx, self);
    if (hasCallException()) return PROTO_NONE;
    const proto::ProtoObject* fn      = getCallbackArg(ctx, args, 0);
    const proto::ProtoObject* thisArg = getCallbackArg(ctx, args, 1);
    if (arrayThrowIfCallbackNotCallable(ctx, fn, "Array.prototype.some")) return PROTO_NONE;
    for (unsigned long i = 0; i < len; i++) {
        if (!arrHasProperty(ctx, self, i)) continue;
        const proto::ProtoObject* elem = arrGet(ctx, self, i);
        if (hasCallException()) return PROTO_NONE;
        const proto::ProtoObject* res =
            callJSFunction(ctx, fn, thisArg, makeIterArgs(ctx, elem, (long long)i, self));
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
    // §23.1.3.6: LengthOfArrayLike precedes IsCallable.
    unsigned long len = arrLen(ctx, self);
    if (hasCallException()) return PROTO_NONE;
    const proto::ProtoObject* fn      = getCallbackArg(ctx, args, 0);
    const proto::ProtoObject* thisArg = getCallbackArg(ctx, args, 1);
    if (arrayThrowIfCallbackNotCallable(ctx, fn, "Array.prototype.every")) return PROTO_NONE;
    for (unsigned long i = 0; i < len; i++) {
        if (!arrHasProperty(ctx, self, i)) continue;
        const proto::ProtoObject* elem = arrGet(ctx, self, i);
        if (hasCallException()) return PROTO_NONE;
        const proto::ProtoObject* res =
            callJSFunction(ctx, fn, thisArg, makeIterArgs(ctx, elem, (long long)i, self));
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

    // Native ProtoList storage: PROTO_NONE represents the simulated
    // hole that arraySpeciesCreate's pre-pad and the sparse-set
    // fallback leave behind. Treat such slots as absent so the spec's
    // HasProperty check returns false — otherwise flat / flatMap /
    // forEach / etc. revisit padded slots and surface them as
    // 'undefined' (e.g. flatMap on {length:3, 0:1, 2:21} leaked a
    // null between the two mapped entries).
    if (const proto::ProtoList* els = getArrayElements(ctx, arr)) {
        if (idx < static_cast<unsigned long>(els->getSize(ctx))) {
            const proto::ProtoObject* v = els->getAt(ctx, static_cast<int>(idx));
            if (v && v != PROTO_NONE) return true;
            // Padded PROTO_NONE — fall through to the attribute probe
            // so genuine explicit writes (e.g. arr[i] = undefined)
            // still count via the indexed-attribute sidecar.
        }
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
    // ECMA-262 §23.1.3.26 step ordering: LengthOfArrayLike(O)
    // (which may throw if length is an accessor) precedes the
    // IsCallable(callbackfn) check.  Pre-fix we reversed that and
    // surfaced a synthetic TypeError "not callable" whenever the
    // length getter threw, instead of letting the user's exception
    // propagate.
    long long len = (long long)arrLen(ctx, self);
    if (hasCallException()) return PROTO_NONE;
    const proto::ProtoObject* fn = getCallbackArg(ctx, args, 0);
    if (arrayThrowIfCallbackNotCallable(ctx, fn, "Array.prototype.reduce")) return PROTO_NONE;
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
    // §23.1.3.26 step 6.b passes O (= ToObject(this)) as the
    // callback's fourth argument.  Wrap primitive receivers so
    // Array.prototype.reduce.call('abc', cb, init) hands cb a String
    // wrapper rather than the raw primitive (built-ins/Array/
    // prototype/reduce/15.4.4.21-1-7).
    const proto::ProtoObject* O = iterReceiver(ctx, self);
    for (long long i = start; i < len; i++) {
        // Skip holes — use HasProperty (includes prototype chain) per spec.
        if (!arrHasProperty(ctx, self, static_cast<unsigned long>(i))) continue;
        const proto::ProtoObject* elem = arrGet(ctx, self, (unsigned long)i);
        // §23.1.3.26 step 8.b.iii.1: ? Get(O, Pk).  If the getter
        // throws (built-ins/Array/prototype/reduce/15.4.4.21-9-c-i-32
        // installs a throwing accessor on index 1), the callback at
        // that index MUST NOT be invoked.  Pre-fix arrGet returned
        // PROTO_NONE on abrupt; the loop then invoked callback with
        // it, setting accessed=true and clobbering testResult.
        if (hasCallException()) return PROTO_NONE;
        const proto::ProtoList* cbArgs = ctx->newList();
        cbArgs = cbArgs->appendLast(ctx, acc   ? acc   : PROTO_NONE);
        cbArgs = cbArgs->appendLast(ctx, elem  ? elem  : PROTO_NONE);
        cbArgs = cbArgs->appendLast(ctx, ctx->fromInteger(i));
        cbArgs = cbArgs->appendLast(ctx, O);
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
    // §23.1.3.27: LengthOfArrayLike precedes IsCallable; see arrayReduce.
    long long len = (long long)arrLen(ctx, self);
    if (hasCallException()) return PROTO_NONE;
    const proto::ProtoObject* fn = getCallbackArg(ctx, args, 0);
    if (arrayThrowIfCallbackNotCallable(ctx, fn, "Array.prototype.reduceRight")) return PROTO_NONE;
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
    // §23.1.3.27 step 6.b passes O as the callback's array argument
    // (parallel to reduce).  Pre-fix reduceRight forwarded self
    // verbatim — primitive-string receivers showed as 'string', not
    // a String wrapper, in the callback.
    const proto::ProtoObject* ORr = iterReceiver(ctx, self);
    for (long long i = start; i >= 0; i--) {
        // Skip holes — use HasProperty (includes prototype chain) per spec.
        if (!arrHasProperty(ctx, self, static_cast<unsigned long>(i))) continue;
        const proto::ProtoObject* elem = arrGet(ctx, self, (unsigned long)i);
        // §23.1.3.27 step 9.b.iii.1: ? Get(O, Pk).  Throwing getter
        // must terminate iteration before callback runs.
        if (hasCallException()) return PROTO_NONE;
        const proto::ProtoList* cbArgs = ctx->newList();
        cbArgs = cbArgs->appendLast(ctx, acc   ? acc   : PROTO_NONE);
        cbArgs = cbArgs->appendLast(ctx, elem  ? elem  : PROTO_NONE);
        cbArgs = cbArgs->appendLast(ctx, ctx->fromInteger(i));
        cbArgs = cbArgs->appendLast(ctx, ORr);
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
    // ECMA-262 §23.1.3.30 step 1: if comparefn is neither undefined
    // nor callable, throw TypeError. Pre-fix any non-callable
    // argument was silently treated as "no comparator" and the
    // string default fired — masking programmer errors.
    const proto::ProtoObject* fn = getCallbackArg(ctx, args, 0);
    if (fn == getUndefinedSentinel()) fn = nullptr;
    bool hasFn = fn && fn != PROTO_NONE;
    if (hasFn && arrayThrowIfCallbackNotCallable(ctx, fn, "Array.prototype.sort"))
        return PROTO_NONE;

    // §23.1.3.30 step 1: Let obj be ? ToObject(this value), step 12
    // returns obj.  Pre-fix sort returned the bare primitive
    // receiver, so [].sort.call(false) instanceof Boolean was false
    // (built-ins/Array/prototype/sort/call-with-primitive).
    const proto::ProtoObject* O = iterReceiver(ctx, self);
    unsigned long len = arrLen(ctx, self);
    if (len < 2) return O;

    // Separate elements into three categories per ECMAScript spec:
    //   1. Defined values  — present AND not undefined
    //   2. Undefined slots — present BUT value is undefined
    //   3. Holes           — absent (no own property at that index)
    // Sort order: defined (sorted) < undefined < holes.
    std::vector<const proto::ProtoObject*> defined;
    unsigned long undefinedCount = 0;
    unsigned long holeCount = 0;

    defined.reserve(len);
    const proto::ProtoObject* undefSent = getUndefinedSentinel();
    for (unsigned long i = 0; i < len; i++) {
        if (!arrHas(ctx, self, i)) {
            holeCount++;
            continue;
        }
        const proto::ProtoObject* elem = arrGet(ctx, self, i);
        // ECMA-262 §23.1.3.30 SortCompare: a value of Type undefined
        // sorts AFTER all defined values, regardless of whether it
        // was a hole, a PROTO_NONE-padded slot, or an explicit user
        // `undefined`. Pre-fix only PROTO_NONE was bucketed as
        // undefined, so `new Array(undefined, 1).sort()` saw the
        // explicit undefined as a "real" string-keyed value and the
        // result was [undefined, 1] instead of [1, undefined]
        // (Sputnik S15.4.4.11_A1.4_T2).
        if (!elem || elem == PROTO_NONE || elem == undefSent) {
            undefinedCount++;
        } else {
            defined.push_back(elem);
        }
    }

    // Sort only the defined elements.  §23.1.3.30 SortIndexedProperties
    // step 5: 'If any such call returns an abrupt completion, stop
    // before performing any further calls'.  Pre-fix the lambda
    // swallowed the abrupt and returned false, so toSorted /
    // toSorted-via-sort kept hammering the comparator after the
    // first throw (built-ins/Array/prototype/toSorted/comparefn-
    // stop-after-error).  std::stable_sort cannot be cancelled
    // mid-call, so use a sticky abort flag — once set, the lambda
    // short-circuits and we propagate the exception after the sort
    // returns.
    bool comparatorAborted = false;
    auto less = [&](const proto::ProtoObject* a, const proto::ProtoObject* b) -> bool {
        if (comparatorAborted) return false;
        if (hasFn) {
            const proto::ProtoList* cbArgs = ctx->newList();
            cbArgs = cbArgs->appendLast(ctx, a ? a : PROTO_NONE);
            cbArgs = cbArgs->appendLast(ctx, b ? b : PROTO_NONE);
            const proto::ProtoObject* res = callJSFunction(ctx, fn, PROTO_NONE, cbArgs);
            if (hasCallException()) { comparatorAborted = true; return false; }
            if (!res || res == PROTO_NONE) return false;
            if (res->isInteger(ctx)) return res->asLong(ctx) < 0;
            if (res->isDouble(ctx) || res->isFloat(ctx)) return res->asDouble(ctx) < 0.0;
            return false;
        }
        // Default: lexicographic by ToString(x) — invokes obj.toString() if available.
        return sortKey(ctx, a) < sortKey(ctx, b);
    };

    std::stable_sort(defined.begin(), defined.end(), less);
    if (comparatorAborted) return PROTO_NONE;

    // Write back: sorted defined values, then undefined, then holes (as undefined,
    // since protoCore has no attribute-delete; absent vs explicit-undefined is
    // indistinguishable via x[i] access anyway).
    //
    // PROTO_NONE in __elements__ is interpreted by arrGet as a hole and
    // falls through to the string-key sidecar / prototype chain.  For
    // a previously sparse array (e.g. `new Array(2); x[1] = 1`) the
    // sidecar attribute "1" carries the old value, so writing
    // PROTO_NONE to __elements__[1] caused x[1] to surface the stale
    // 1 after sort (Sputnik S15.4.4.11_A1.2_T1).  Use the user-visible
    // undefined sentinel for the explicit-undefined trailing slots —
    // arrGet sees the sentinel as a non-hole and returns it directly.
    unsigned long writeIdx = 0;
    for (const auto* v : defined)
        arrSet(ctx, self, writeIdx++, v);
    const proto::ProtoObject* undefMarker = getUndefinedSentinel();
    for (unsigned long i = 0; i < undefinedCount; i++)
        arrSet(ctx, self, writeIdx++, undefMarker);
    for (unsigned long i = 0; i < holeCount; i++)
        arrSet(ctx, self, writeIdx++, undefMarker);

    return O;
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
        // Per spec FlattenIntoArray step 3.b: skip the source index
        // when HasProperty returns false. flatMap = map + flat(1),
        // and map already skips holes — but flat itself was emitting
        // the hole as undefined / null, so flatMap on an array-like
        // with gaps (e.g. `{length:3, 0:1, 2:21}`) leaked nulls into
        // the output. arrHasProperty consults __elements__ + indexed
        // attribute sidecar so the spec's HasProperty semantics are
        // approximated.
        if (!arrHasProperty(ctx, src, i)) continue;
        const proto::ProtoObject* elem = arrGet(ctx, src, i);
        const proto::ProtoString* isArrKey = JSSymbols::isArray(ctx);
        const proto::ProtoObject* isArrAttr = (elem && isArrKey)
            ? elem->getAttribute(ctx, isArrKey, true) : PROTO_NONE;
        bool isArr = (isArrAttr == PROTO_TRUE);
        if (isArr && depth > 0)
            flatInto(ctx, elem, dest, outIdx, depth - 1);
        else {
            dest = arrayCreateDataPropertyOrThrow(ctx, dest, outIdx, elem);
            if (hasCallException()) return;
            outIdx++;
        }
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
    // ECMA-262 §23.1.3.10 step 3-4: depth = ToIntegerOrInfinity(arg).
    // The spec coerces via ToNumber first, so a non-numeric string,
    // a plain object, true/false, etc. all funnel to NaN → 0. Pre-fix
    // we only handled Integer/Double and fell through to depth=1 for
    // everything else, so a.flat("TestString") wrongly flattened
    // (depth=1) instead of returning a copy (depth=0). Default 1
    // applies only when the arg is absent or undefined.
    int depth = 1;
    if (args && args->getSize(ctx) > 0) {
        const proto::ProtoObject* d = args->getAt(ctx, 0);
        bool isUndef = (!d || d == PROTO_NONE || d == getUndefinedSentinel());
        if (!isUndef) {
            double dv = std::nan("");
            if (d == PROTO_TRUE) dv = 1.0;
            else if (d == PROTO_FALSE) dv = 0.0;
            else if (d->isInteger(ctx)) dv = static_cast<double>(d->asLong(ctx));
            else if (d->isDouble(ctx) || d->isFloat(ctx)) dv = d->asDouble(ctx);
            else if (d->isString(ctx)) {
                // String coercion handled directly below.
                std::string s;
                d->asString(ctx)->toUTF8String(ctx, s);
                // Trim — Number(" 7 ") is 7. Empty/whitespace → 0.
                size_t b = s.find_first_not_of(" \t\n\r\f\v");
                size_t e = s.find_last_not_of(" \t\n\r\f\v");
                if (b == std::string::npos) dv = 0.0;
                else {
                    std::string t = s.substr(b, e - b + 1);
                    try {
                        size_t idx = 0;
                        dv = std::stod(t, &idx);
                        if (idx != t.size()) dv = std::nan("");
                    } catch (...) { dv = std::nan(""); }
                }
            }
            // §23.1.3.10 step 3 ToIntegerOrInfinity(depthNum) per
            // §7.1.5 begins with ToNumber.  Object / Symbol arguments
            // must take the §7.1.4 abrupt-completion branch (Symbol →
            // TypeError, Object.create(null) → TypeError because
            // neither toString nor valueOf is callable).  Pre-fix the
            // 'anything else' silently collapsed to NaN → depth 0
            // (built-ins/Array/prototype/flat/symbol-object-create-
            // null-depth-throws).
            if (std::isnan(dv) && d && !d->isString(ctx)
                && d != PROTO_TRUE && d != PROTO_FALSE) {
                const proto::ProtoObject* num = jsToNumber(ctx, d);
                if (hasCallException()) return PROTO_NONE;
                if (num && num->isInteger(ctx)) dv = static_cast<double>(num->asLong(ctx));
                else if (num && (num->isDouble(ctx) || num->isFloat(ctx))) dv = num->asDouble(ctx);
            }
            if (std::isnan(dv))      depth = 0;
            else if (std::isinf(dv)) depth = (dv > 0) ? 2147483647 : 0;
            else if (dv > 2147483647.0) depth = 2147483647;
            else                       depth = static_cast<int>(dv);
        }
    }
    if (depth < 0) depth = 0;
    // §23.1.3.10 step 5: A = ArraySpeciesCreate(O, 0). This is where
    // a primitive .constructor throws TypeError, matching V8/SpiderMonkey.
    const proto::ProtoObject* result = arraySpeciesCreate(ctx, self, 0);
    if (!result || result == PROTO_NONE) return PROTO_NONE;
    unsigned long outIdx = 0;
    flatInto(ctx, self, result, outIdx, depth);
    if (hasCallException()) return PROTO_NONE;
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

    // ECMA-262 §23.1.3.30 step 4: if no arguments, return empty array
    // without modifying the receiver. Pre-fix splice() collapsed to
    // delCount=0, insert nothing — same effect on the receiver, but
    // returned the FULL array via the `removed` collector logic when
    // delCount > len-start; harmless until you used the return value.
    if (n == 0) {
        // §23.1.3.29 step 9: A := ? ArraySpeciesCreate(O, 0).  Even
        // when n = 0 the constructor lookup must run — a primitive
        // .constructor surfaces TypeError per §22.1.3.1.1 steps 7/9.
        // Pre-fix the no-args fast path skipped the species check and
        // the throw was lost (built-ins/Array/prototype/splice/
        // create-ctor-non-object).
        const proto::ProtoObject* result = arraySpeciesCreate(ctx, self, 0);
        if (hasCallException()) return PROTO_NONE;
        // §23.1.3.29 step 24: Set(O, 'length', len – 0 + 0, true) still
        // runs — a user setter on 'length' observes every splice call
        // even when no mutation occurs (built-ins/Array/prototype/
        // splice/set_length_no_args).
        arrSetLen(ctx, self, (unsigned long)(len > 0 ? len : 0));
        if (hasCallException()) return PROTO_NONE;
        return result ? result : PROTO_NONE;
    }

    // §23.1.3.29 step 16 ends with Set(O, 'length', ..., true).  If
    // length is non-writable the abrupt completion bubbles out as
    // TypeError BEFORE any user-visible mutation lands.  Pre-fix
    // splice silently proceeded on a frozen-length array
    // (built-ins/Array/prototype/splice/S15.4.4.12_A6.1_T2).
    {
        const proto::ProtoString* pdk = JSSymbols::pdLength(ctx);
        if (pdk && self->hasAttribute(ctx, pdk) == PROTO_TRUE) {
            const proto::ProtoObject* pdv = self->getAttribute(ctx, pdk, false);
            if (pdv && pdv->isInteger(ctx)) {
                long long bits = pdv->asLong(ctx);
                if ((bits & 0x1) == 0) {
                    signalNativeException(makeNativeError(ctx, "TypeError",
                        "Cannot assign to read-only property 'length'"));
                    return PROTO_NONE;
                }
            }
        }
    }

    // ECMA-262 §23.1.3.29 step 3 / 10.a: ToIntegerOrInfinity on
    // start / deleteCount.  ToIntegerOrInfinity begins with ToNumber,
    // which exercises valueOf / Symbol.toPrimitive / toString.  Pre-fix
    // the helper only honoured raw integer / double cells, so a
    // {valueOf:()=>3, toString:()=>0} deleteCount silently defaulted
    // to 0 (Sputnik S15.4.4.12_A2.2_T5).  Route non-numeric values
    // through jsToNumber and propagate any abrupt.
    auto toII = [&](const proto::ProtoObject* o, long long defaultV) -> long long {
        if (!o || o == PROTO_NONE || o == getUndefinedSentinel()) return defaultV;
        const proto::ProtoObject* num = o;
        if (!o->isInteger(ctx) && !o->isDouble(ctx) && !o->isFloat(ctx)) {
            num = jsToNumber(ctx, o);
            if (hasCallException() || !num) return defaultV;
        }
        if (num->isInteger(ctx)) return num->asLong(ctx);
        if (num->isDouble(ctx) || num->isFloat(ctx)) {
            double d = num->asDouble(ctx);
            if (std::isnan(d)) return 0;
            if (std::isinf(d)) return d > 0 ? len : -len - 1;
            return static_cast<long long>(d);
        }
        return defaultV;
    };

    // Parse start.
    long long start = n >= 1 ? toII(args->getAt(ctx, 0), 0) : 0;
    if (start < 0) { start += len; if (start < 0) start = 0; }
    if (start > len) start = len;

    // Parse deleteCount per spec step 7: with exactly one argument
    // deleteCount = len - start (remove the tail); with two or more
    // arguments, ToIntegerOrInfinity then clamp to [0, len-start].
    long long delCount;
    if (n == 1) {
        delCount = len - start;
    } else {
        delCount = toII(args->getAt(ctx, 1), 0);
        if (delCount < 0) delCount = 0;
        if (delCount > len - start) delCount = len - start;
    }

    // §23.1.3.29 step 9: A = ? ArraySpeciesCreate(O, actualDeleteCount).
    // The "removed" array must come from the species, not a fresh
    // default array — a species function may install
    // preventExtensions / non-configurable descriptors on the
    // instance that CreateDataPropertyOrThrow must surface as
    // TypeError (built-ins/Array/prototype/splice/target-array-
    // non-extensible / target-array-with-non-configurable-property).
    const proto::ProtoObject* removed =
        arraySpeciesCreate(ctx, self, static_cast<unsigned long>(delCount));
    if (hasCallException()) return PROTO_NONE;
    for (long long i = 0; i < delCount; i++) {
        const proto::ProtoObject* v =
            arrGet(ctx, self, (unsigned long)(start + i));
        if (hasCallException()) return PROTO_NONE;
        removed = arrayCreateDataPropertyOrThrow(ctx, removed,
                       (unsigned long)i, v);
        if (hasCallException()) return PROTO_NONE;
    }

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

    // §23.1.3.29 step 21.d: when insertCount < deleteCount, delete the
    // now-vacated tail indices via DeletePropertyOrThrow(O, k).  For
    // real arrays, arrSetLen below truncates __elements__ and that
    // implicitly clears the data; for non-array array-likes the
    // legacy length-setAttribute path leaves obj[newLen..len-1]
    // observable as their original values.  Mirror the spec by
    // removing the string-keyed attributes explicitly.
    // Sputnik S15.4.4.12_A2_T1 (obj.splice(0,3,4,5) on
    // {0:0,1:1,2:2,3:3, length:4}) probed that obj[3] is undefined
    // post-splice.
    if (newLen < len) {
        for (long long k = len - 1; k >= newLen; k--) {
            const proto::ProtoString* delKey =
                JSSymbols::indexKey(ctx, static_cast<uint32_t>(k));
            if (delKey) {
                // setAttribute(PROTO_NONE) is the protoCore-canonical
                // "delete own data attribute" path used elsewhere in
                // this file (mirrors arrayPop's last-index clear).
                self->setAttribute(ctx, delKey, PROTO_NONE);
            }
        }
    }

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
    // ECMA-262 §23.1.3.1 step 2: relativeIndex = ToIntegerOrInfinity(index).
    // Route every non-integer / non-double through jsToNumber so:
    //   • booleans coerce via ToNumber (true → 1, false → 0)
    //   • {valueOf(){...}} fires its valueOf
    //   • Symbol throws TypeError (jsToNumber raises a callable
    //     exception we propagate)
    // Pre-fix the helper only handled raw integer/double, so a.at(true)
    // returned a[0] (undefined) instead of a[1], a.at({valueOf:()=>1})
    // similarly returned a[0], and a.at(Symbol()) silently returned
    // undefined (built-ins/Array/prototype/at/index-non-numeric-
    // argument-tointeger{,-invalid}.js).
    long long idx = 0;
    if (args && args->getSize(ctx) > 0) {
        const proto::ProtoObject* a = args->getAt(ctx, 0);
        if (a && a != PROTO_NONE && a != getUndefinedSentinel()) {
            const proto::ProtoObject* num = a;
            if (!a->isInteger(ctx) && !a->isDouble(ctx) && !a->isFloat(ctx)) {
                num = jsToNumber(ctx, a);
                if (hasCallException()) return PROTO_NONE;
            }
            if (num) {
                if (num->isInteger(ctx)) idx = num->asLong(ctx);
                else if (num->isDouble(ctx) || num->isFloat(ctx)) {
                    double d = num->asDouble(ctx);
                    if (std::isnan(d)) idx = 0;
                    else if (std::isinf(d)) return PROTO_NONE; // ±Inf out of range
                    else idx = (long long)d;
                }
            }
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

    // Sticky-done guard per ECMA-262 §23.1.5.2.1: once a CreateArrayIterator
    // result has yielded {done: true} every subsequent call must keep
    // doing so even if the underlying array grows.  Pre-fix the
    // iterator only tracked idx/length, so push() after exhaustion
    // re-enabled iteration (built-ins/Array/prototype/values/iteration-
    // mutable observed the second 'b' surface after done).
    const proto::ProtoObject* doneKo = ctx->fromUTF8String("__iter_done__");
    const proto::ProtoString* doneKs = doneKo ? doneKo->asString(ctx) : nullptr;
    if (doneKs && self->hasAttribute(ctx, doneKs) == PROTO_TRUE) {
        const proto::ProtoObject* d = self->getAttribute(ctx, doneKs, false);
        if (d == PROTO_TRUE) {
            const proto::ProtoObject* r = ctx->newObject(true);
            r = r->setAttribute(ctx, valueK, PROTO_NONE);
            r = r->setAttribute(ctx, doneK,  PROTO_TRUE);
            return r;
        }
    }

    const proto::ProtoObject* arrRef  = self->getAttribute(ctx, refKey,  false);
    const proto::ProtoObject* idxVal  = self->getAttribute(ctx, idxKey,  false);
    const proto::ProtoObject* kindObj = self->getAttribute(ctx, kindKey, false);

    long long idx = (idxVal && idxVal != PROTO_NONE && idxVal->isInteger(ctx))
                    ? idxVal->asLong(ctx) : 0LL;
    unsigned long arrLen_ = arrLen(ctx, arrRef);

    // Build result object.
    const proto::ProtoObject* r = ctx->newObject(true);

    if ((unsigned long)idx >= arrLen_) {
        // Iteration done — mark sticky so future calls stay done.
        if (doneKs) self->setAttribute(ctx, doneKs, PROTO_TRUE);
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
// %ArrayIteratorPrototype% — shared parent object for the iterators
// returned by Array.prototype.{keys,values,entries}.  Lazily created.
// §22.1.5.2.2: carries Symbol.toStringTag = 'Array Iterator' with
// descriptor {writable:false, enumerable:false, configurable:true}
// (sidecar bits 0x2).  Pre-fix every iterator was a bare newObject
// child of Object.prototype, so Object.getPrototypeOf(iter)[Symbol
// .toStringTag] surfaced undefined (built-ins/ArrayIteratorPrototype/
// Symbol.toStringTag/property-descriptor).
static const proto::ProtoObject* s_arrayIteratorProto = nullptr;

static const proto::ProtoObject* getArrayIteratorProto(proto::ProtoContext* ctx) {
    if (s_arrayIteratorProto) return s_arrayIteratorProto;
    const proto::ProtoObject* objProto =
        ctx->space ? ctx->space->objectPrototype : nullptr;
    const proto::ProtoObject* proto = objProto
        ? objProto->newChild(ctx, true) : ctx->newObject(true);
    if (!proto) return nullptr;
    const proto::ProtoString* tagInt = JSSymbols::toStringTag(ctx);
    if (tagInt) proto = proto->setAttribute(ctx, tagInt,
        ctx->fromUTF8String("Array Iterator"));
    const proto::ProtoString* tagUser = JSSymbols::symbolToStringTag(ctx);
    if (tagUser) {
        proto = proto->setAttribute(ctx, tagUser,
            ctx->fromUTF8String("Array Iterator"));
        const proto::ProtoObject* pdo = ctx->fromUTF8String("__pd_Symbol.toStringTag__");
        const proto::ProtoString* pdk = pdo ? pdo->asString(ctx) : nullptr;
        if (pdk) proto = proto->setAttribute(ctx, pdk, ctx->fromInteger(0x2LL));
    }
    // §22.1.5.2.1 + §17: %ArrayIteratorPrototype%.next has the §17
    // function-shape with name='next' / length=0 own properties and
    // {writable:true, enumerable:false, configurable:true} on its
    // installed slot (0x3).  Pre-fix every iterator received its own
    // copy via a bare ProtoMethod, so ArrayIteratorProto.next had no
    // descriptor surface (built-ins/ArrayIteratorPrototype/next/name).
    // Install once on the shared parent here.
    {
        const proto::ProtoString* nextKey = JSSymbols::next(ctx);
        if (nextKey && ctx->space && ctx->space->methodPrototype) {
            const proto::ProtoObject* wrapper =
                ctx->space->methodPrototype->newChild(ctx, true);
            if (wrapper) {
                const proto::ProtoString* nfk = JSSymbols::nativeFn(ctx);
                if (nfk) wrapper = wrapper->setAttribute(ctx, nfk,
                    ctx->fromMethod(nullptr, arrayIteratorNext));
                const proto::ProtoString* lk = JSSymbols::length(ctx);
                if (lk) {
                    wrapper = wrapper->setAttribute(ctx, lk, ctx->fromInteger(0LL));
                    const proto::ProtoString* pdlk = JSSymbols::pdLength(ctx);
                    if (pdlk) wrapper = wrapper->setAttribute(ctx, pdlk, ctx->fromInteger(0x2LL));
                }
                const proto::ProtoString* nk = JSSymbols::name(ctx);
                if (nk) {
                    wrapper = wrapper->setAttribute(ctx, nk, ctx->fromUTF8String("next"));
                    const proto::ProtoString* pdnk = JSSymbols::pdName(ctx);
                    if (pdnk) wrapper = wrapper->setAttribute(ctx, pdnk, ctx->fromInteger(0x2LL));
                }
                proto = proto->setAttribute(ctx, nextKey, wrapper);
                const proto::ProtoObject* pdno = ctx->fromUTF8String("__pd_next__");
                const proto::ProtoString* pdnk = pdno ? pdno->asString(ctx) : nullptr;
                if (pdnk) proto = proto->setAttribute(ctx, pdnk, ctx->fromInteger(0x3LL));
            }
        }
    }
    s_arrayIteratorProto = proto;
    return s_arrayIteratorProto;
}

static const proto::ProtoObject* makeArrayIterator(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* arr,
    const char* kind)
{
    const proto::ProtoObject* protoParent = getArrayIteratorProto(ctx);
    const proto::ProtoObject* iter = protoParent
        ? protoParent->newChild(ctx, true) : ctx->newObject(true);
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
{
    // §23.1.3.4 step 1: Let O be ? ToObject(this value).
    // ReturnIfAbrupt on null/undefined — mirrors the keys/values
    // fix from the prior round.  Pre-fix Array.prototype.entries
    // .call(null) silently produced an iterator with a PROTO_NONE
    // backing array (built-ins/Array/prototype/entries/return-
    // abrupt-from-this).
    if (arrayThrowIfNullUndefined(ctx, self)) return PROTO_NONE;
    return makeArrayIterator(ctx, self, "entries");
}

static const proto::ProtoObject* arrayKeys(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{
    // §23.1.3.16 step 1: Let O be ? ToObject(this value).
    // ReturnIfAbrupt on null/undefined.  Pre-fix Array.prototype.keys
    // .call(undefined) silently produced an iterator over a
    // PROTO_NONE receiver instead of throwing (built-ins/Array/
    // prototype/keys/return-abrupt-from-this).
    if (arrayThrowIfNullUndefined(ctx, self)) return PROTO_NONE;
    return makeArrayIterator(ctx, self, "keys");
}

static const proto::ProtoObject* arrayValues(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{
    if (arrayThrowIfNullUndefined(ctx, self)) return PROTO_NONE;
    return makeArrayIterator(ctx, self, "values");
}

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
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    // ECMA-262 §23.1.2.1 step 1: Let items be ? ToObject(items).
    // null / undefined are not coercible — throw TypeError. The
    // zero-argument case (`Array.from()`) is equivalent to passing
    // undefined and must throw the same TypeError.
    const proto::ProtoObject* src = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0) : PROTO_NONE;
    if (arrayThrowIfNullUndefined(ctx, src)) return PROTO_NONE;
    // §23.1.2.1 steps 4.a / 7.a: when `this` is a constructor distinct
    // from Array, the result is `Construct(this)`/`Construct(this, [len])`,
    // not a fresh Array.  Detect the constructor case by looking for a
    // `__construct__` method or the generic constructor flag, then
    // invoke it with no args (matches V8 semantics for both the
    // iterator and array-like branches).  Pre-fix the path always
    // produced an Array, so `Array.from.call(Object, []).constructor`
    // was Array instead of the spec-required Object.
    // §23.1.2.1 IsConstructor(C) probe — defer construction until we
    // know which branch (iterator vs array-like) we're taking.  The
    // iterator branch wants Construct(C) with NO args; the array-like
    // branch wants Construct(C, «len»).  Pre-fix we constructed C up
    // front with no args, so a user ctor like
    //   function MyCollection() { this.args = arguments; }
    // saw arguments.length == 0 from the array-like path
    // (built-ins/Array/from/Array.from_forwards-length-for-array-likes).
    const proto::ProtoObject* result = nullptr;
    const proto::ProtoObject* ctorFn = nullptr;
    bool isBytecodeFn = false;
    {
        const proto::ProtoString* constructKey = JSSymbols::construct(ctx);
        if (self && self != PROTO_NONE && self != getUndefinedSentinel() && constructKey) {
            ctorFn = self->getAttribute(ctx, constructKey, false);
            if (ctorFn && ctorFn != PROTO_NONE && !ctorFn->isMethod(ctx))
                ctorFn = nullptr;
        }
        if ((!ctorFn || ctorFn == PROTO_NONE)
            && self && self != PROTO_NONE && self != getUndefinedSentinel()) {
            const proto::ProtoString* bcK = JSSymbols::bytecodeId(ctx);
            if (bcK && self->hasAttribute(ctx, bcK) == PROTO_TRUE) {
                const proto::ProtoObject* arrowKO = ctx->fromUTF8String("__is_arrow__");
                const proto::ProtoString* arrowK = arrowKO ? arrowKO->asString(ctx) : nullptr;
                if (!arrowK || self->getAttribute(ctx, arrowK, false) != PROTO_TRUE) {
                    isBytecodeFn = true;
                }
            }
        }
    }
    bool hasCtor = (ctorFn && ctorFn != PROTO_NONE) || isBytecodeFn;
    auto constructC = [&](long long lenArg, bool withLen) -> const proto::ProtoObject* {
        const proto::ProtoString* protoKey = JSSymbols::prototype(ctx);
        const proto::ProtoObject* cProto = protoKey
            ? self->getAttribute(ctx, protoKey, false) : nullptr;
        const proto::ProtoObject* res = (cProto && cProto != PROTO_NONE)
            ? cProto->newChild(ctx, true)
            : ctx->newObject(true);
        const proto::ProtoList* ctorArgs = ctx->newList();
        if (withLen) ctorArgs = ctorArgs->appendLast(ctx,
            ctx->fromInteger(lenArg));
        const proto::ProtoObject* alt = nullptr;
        if (ctorFn && ctorFn != PROTO_NONE) {
            proto::ProtoMethod fn = ctorFn->asMethod(ctx);
            if (fn) alt = fn(ctx, res, nullptr, ctorArgs, nullptr);
        } else {
            alt = callJSFunction(ctx, self, res, ctorArgs);
        }
        if (hasCallException()) return res;
        if (alt && alt != PROTO_NONE
            && alt != getUndefinedSentinel() && alt != getNullSentinel()
            && !alt->isInteger(ctx) && !alt->isDouble(ctx) && !alt->isFloat(ctx)
            && !alt->isBoolean(ctx) && !alt->isString(ctx)
            && alt != PROTO_TRUE && alt != PROTO_FALSE) {
            return alt;
        }
        return res;
    };
    // (result is constructed per-branch below — iterator branch with
    // no args, array-like branch with «len».)

    // Optional map function (Array.from(src, mapFn[, thisArg])).
    const proto::ProtoObject* mapFn = (args->getSize(ctx) > 1) ? args->getAt(ctx, 1) : nullptr;
    const proto::ProtoObject* mapThis = (args->getSize(ctx) > 2) ? args->getAt(ctx, 2) : PROTO_NONE;
    if (mapFn == PROTO_NONE || mapFn == getUndefinedSentinel()) mapFn = nullptr;
    // Spec §23.1.2.1 step 2: if mapFn is not undefined and not
    // callable, throw TypeError.
    if (mapFn) {
        if (arrayThrowIfCallbackNotCallable(ctx, mapFn, "Array.from"))
            return PROTO_NONE;
    }
    auto applyMap = [&](const proto::ProtoObject* v, long long idx) -> const proto::ProtoObject* {
        if (!mapFn) return v;
        const proto::ProtoList* margs = ctx->newList();
        margs = margs->appendLast(ctx, v ? v : PROTO_NONE);
        margs = margs->appendLast(ctx, ctx->fromInteger(idx));
        return callJSFunction(ctx, mapFn, mapThis ? mapThis : PROTO_NONE, margs);
    };

    // First check Symbol.iterator for generators, Sets, Maps, etc.
    // If the source has no Symbol.iterator but already exposes .next,
    // it IS an iterator (Set.values(), Map.entries() etc.) — use it
    // directly.  Pre-fix Array.from(iter) returned [] for these.
    //
    // ECMA-262 §7.3.10 GetMethod(V, P): invokes [[Get]], which fires
    // any accessor descriptor.  Probe `__get_<symKey>__` first so a
    // throwing `get [Symbol.iterator]()` propagates correctly; only
    // fall back to the raw data slot when no accessor is installed.
    // Pre-fix the data-only read silently produced the placeholder
    // and Array.from swallowed the user's abrupt completion.
    const proto::ProtoString* symIterKey = JSSymbols::symbolIterator(ctx);
    const proto::ProtoObject* iterFn = nullptr;
    if (symIterKey) {
        std::string keyStr;
        symIterKey->toUTF8String(ctx, keyStr);
        std::string gkStr = "__get_" + keyStr + "__";
        const proto::ProtoObject* gko = ctx->fromUTF8String(gkStr.c_str());
        const proto::ProtoString* gk = gko ? gko->asString(ctx) : nullptr;
        if (gk) {
            const proto::ProtoObject* getter = src->getAttribute(ctx, gk, true);
            if (getter && getter != PROTO_NONE) {
                iterFn = callJSFunction(ctx, getter, src, ctx->newList());
                if (hasCallException()) return PROTO_NONE;
            }
        }
        if (!iterFn || iterFn == PROTO_NONE)
            iterFn = src->getAttribute(ctx, symIterKey, true);
    }
    const proto::ProtoObject* iter = nullptr;
    if (iterFn && iterFn != PROTO_NONE) {
        const proto::ProtoList* noArgs = ctx->newList();
        iter = callJSFunction(ctx, iterFn, src, noArgs);
    } else {
        const proto::ProtoString* probeNextK = JSSymbols::next(ctx);
        const proto::ProtoObject* probeNext = probeNextK
            ? src->getAttribute(ctx, probeNextK, true) : nullptr;
        if (probeNext && probeNext != PROTO_NONE)
            iter = src;
    }
    if (iter) {
        // §23.1.2.1 step 4.a: if IsConstructor(C), A := Construct(C)
        // with NO arguments at iterator-branch entry.  Items are
        // written into A as the iterator yields.
        if (hasCtor) {
            result = constructC(0, /*withLen=*/false);
            if (hasCallException()) return PROTO_NONE;
        } else {
            result = createNewArray(ctx, nullptr);
        }
        {
            (void)iter; // keep block shape
            const proto::ProtoString* nextKey = JSSymbols::next(ctx);
            const proto::ProtoString* doneKey = JSSymbols::done(ctx);
            const proto::ProtoString* valueKey = JSSymbols::value(ctx);
            const proto::ProtoObject* nextFn = iter->getAttribute(ctx, nextKey, true);
            const proto::ProtoList* resultEls = ctx->newList();
            long long idx = 0;
            // §23.1.2.1 step 6.h.ii / 6.h.v: every iterator step is
            // an abrupt-completion site — both the IteratorNext call
            // and the Get(result, "value") read MUST propagate any
            // throw. Pre-fix the loop swallowed exceptions: a poisoned
            // .value getter threw Test262Error but Array.from kept
            // looping, then returned the partially-built array
            // (built-ins/Array/from/iter-get-iter-val-err.js).
            //
            // Two additions per iteration:
            //   - hasCallException check after callJSFunction(nextFn)
            //   - probe __get_value__ / __get_done__ accessors before
            //     the raw getAttribute so a throwing getter surfaces
            //     correctly, and check again after the invocation.
            auto invokeAccessor = [&](const proto::ProtoObject* obj,
                                       const proto::ProtoString* key)
                -> const proto::ProtoObject* {
                if (!key) return PROTO_NONE;
                std::string keyStr;
                key->toUTF8String(ctx, keyStr);
                std::string gkStr = "__get_" + keyStr + "__";
                const proto::ProtoObject* gko = ctx->fromUTF8String(gkStr.c_str());
                const proto::ProtoString* gk = gko ? gko->asString(ctx) : nullptr;
                if (!gk) return PROTO_NONE;
                const proto::ProtoObject* getter = obj->getAttribute(ctx, gk, true);
                if (!getter || getter == PROTO_NONE) return PROTO_NONE;
                return callJSFunction(ctx, getter, obj, ctx->newList());
            };
            // Forward-declare iterator-close helper used by the abrupt
            // paths below.  §7.4.6 IteratorClose discards exceptions
            // from return() when the outer completion is already abrupt.
            auto closeIteratorPre = [&]() {
                const proto::ProtoObject* returnKo = ctx->fromUTF8String("return");
                const proto::ProtoString* returnK = returnKo ? returnKo->asString(ctx) : nullptr;
                if (!returnK) return;
                const proto::ProtoObject* returnFn =
                    iter->getAttribute(ctx, returnK, true);
                if (!returnFn || returnFn == PROTO_NONE) return;
                callJSFunction(ctx, returnFn, iter, ctx->newList());
            };
            while (nextFn && nextFn != PROTO_NONE) {
                const proto::ProtoList* nArgs = ctx->newList();
                const proto::ProtoObject* res = callJSFunction(ctx, nextFn, iter, nArgs);
                if (hasCallException()) return PROTO_NONE;
                if (!res || res == PROTO_NONE) break;
                const proto::ProtoObject* dv = invokeAccessor(res, doneKey);
                if (hasCallException()) return PROTO_NONE;
                if (!dv || dv == PROTO_NONE)
                    dv = res->getAttribute(ctx, doneKey, false);
                if (dv == PROTO_TRUE) break;
                const proto::ProtoObject* vv = invokeAccessor(res, valueKey);
                if (hasCallException()) { closeIteratorPre(); return PROTO_NONE; }
                if (!vv || vv == PROTO_NONE)
                    vv = res->getAttribute(ctx, valueKey, false);
                const proto::ProtoObject* mapped = applyMap(vv, idx);
                if (hasCallException()) { closeIteratorPre(); return PROTO_NONE; }
                resultEls = resultEls->appendLast(ctx, mapped ? mapped : PROTO_NONE);
                idx++;
                if (idx > 100000) break; // safety
            }
            // Iterator branch also uses CreateDataPropertyOrThrow per
            // §23.1.2.1 step 6.h.viii — reset __pd_<i>__ to default
            // flags so a ctor-installed (writable:false, ...) own
            // descriptor is overwritten rather than just having its
            // value replaced (built-ins/Array/from/iter-set-elem-
            // prop-non-writable).
            constexpr long long kDefaultPdBits = 0x7;
            // Write directly to string-keyed indices and reset __pd__.
            // Probe CreateDataPropertyOrThrow's failure conditions
            // before each write so a species ctor's
            // preventExtensions(this) / Object.defineProperty(this,0,
            // {configurable:false}) side effects surface as TypeError
            // (built-ins/Array/from/iter-set-elem-prop-err and
            // iter-set-length-err).
            // §7.4.6 IteratorClose: any abrupt completion during write
            // must invoke iter.return() before propagating.  Pre-fix
            // built-ins/Array/from/iter-set-elem-prop-err probed
            // closeCount==1 — the test installs a ctor that makes
            // index 0 non-configurable, so CreateDataPropertyOrThrow
            // throws on the first write and iter.return() must fire.
            auto closeIterator = [&]() {
                const proto::ProtoObject* returnKo = ctx->fromUTF8String("return");
                const proto::ProtoString* returnK = returnKo ? returnKo->asString(ctx) : nullptr;
                if (!returnK) return;
                const proto::ProtoObject* returnFn =
                    iter->getAttribute(ctx, returnK, true);
                if (!returnFn || returnFn == PROTO_NONE) return;
                // The return() invocation may itself throw — those
                // abrupt completions are discarded when the outer
                // completion is also abrupt (§7.4.6 IteratorClose step
                // 6).  Our simpler model: best-effort call; if it
                // raises, the original exception is still in flight.
                callJSFunction(ctx, returnFn, iter, ctx->newList());
            };
            for (long long i = 0; i < idx; i++) {
                if (arrayThrowIfCreateDataPropertyFails(ctx, result,
                        static_cast<unsigned long>(i))) {
                    closeIterator();
                    return PROTO_NONE;
                }
                const proto::ProtoObject* v = resultEls->getAt(ctx, static_cast<int>(i));
                const proto::ProtoString* k =
                    JSSymbols::indexKey(ctx, static_cast<uint32_t>(i));
                if (k) result = result->setAttribute(ctx, k, v ? v : PROTO_NONE);
                std::string pdStr = "__pd_" + std::to_string(i) + "__";
                const proto::ProtoObject* pdko = ctx->fromUTF8String(pdStr.c_str());
                const proto::ProtoString* pdk = pdko ? pdko->asString(ctx) : nullptr;
                if (pdk) result = result->setAttribute(ctx, pdk,
                                      ctx->fromInteger(kDefaultPdBits));
            }
            // Also publish __elements__ for fast-path readers.
            setArrayElements(ctx, result, resultEls);
            // §23.1.2.1 step 6.g.iv.1: Set(A, 'length', k, true).
            // Route through arrSetLen so an inherited __set_length__
            // accessor on C.prototype fires (built-ins/Array/from/
            // iter-set-length-err pins a poisoned-length setter).
            arrSetLen(ctx, result, static_cast<unsigned long>(idx));
            if (hasCallException()) return PROTO_NONE;
            return result;
        }
    }

    // If src has a "length", iterate by index (array-like).
    const proto::ProtoString* lenKey = JSSymbols::length(ctx);
    if (!lenKey) return result;
    const proto::ProtoObject* lv = src->getAttribute(ctx, lenKey, false);
    if (lv && lv != PROTO_NONE) {
        // ECMA-262 §23.1.2.1 step 5: ToLength(len) runs ToInteger which
        // first calls ToNumber — so a string length like {length: '3'}
        // must coerce to 3, and {length: null} to 0. Pre-fix the
        // attribute path only matched numeric types directly, so any
        // non-numeric length produced an empty result.
        double d = 0.0;
        bool ok = false;
        if (lv->isInteger(ctx))       { d = static_cast<double>(lv->asLong(ctx)); ok = true; }
        else if (lv->isDouble(ctx) || lv->isFloat(ctx)) { d = lv->asDouble(ctx); ok = true; }
        else {
            const proto::ProtoObject* num = jsToNumber(ctx, lv);
            if (num) {
                if (num->isInteger(ctx)) { d = static_cast<double>(num->asLong(ctx)); ok = true; }
                else if (num->isDouble(ctx) || num->isFloat(ctx)) { d = num->asDouble(ctx); ok = true; }
            }
        }
        if (ok) {
            long long nSigned = 0;
            if (std::isnan(d) || d < 0) nSigned = 0;
            else if (std::isinf(d)) nSigned = 0;
            else nSigned = static_cast<long long>(d);
            if (nSigned < 0) nSigned = 0;
            unsigned long n = static_cast<unsigned long>(nSigned);
            // §23.1.2.1 step 9: if IsConstructor(C), A := Construct(C,
            // «len») — forward the length as the ctor argument.
            // Pre-fix we constructed C at the top with no args, so a
            // user ctor that captures arguments saw an empty args list
            // (built-ins/Array/from/Array.from_forwards-length-for-
            // array-likes).
            if (hasCtor) {
                result = constructC(static_cast<long long>(n), /*withLen=*/true);
                if (hasCallException()) return PROTO_NONE;
            } else {
                result = createNewArray(ctx, nullptr);
            }
            // §23.1.2.1 step 12.h: CreateDataPropertyOrThrow(A, Pk,
            // mappedValue) — define a fresh own data descriptor, NOT
            // OrdinarySet.  A ctor-installed (writable:false, ...)
            // slot must be replaced wholesale, and an inherited
            // setter on the chain must NOT fire.  Bypass arrSet's
            // OrdinarySet semantics by writing the index attribute
            // and resetting __pd_<i>__ to default flags
            // (built-ins/Array/from/source-object-length-set-elem-
            // prop-non-writable).
            constexpr long long kDefaultPdBits = 0x7;
            for (unsigned long i = 0; i < n; i++) {
                const proto::ProtoObject* v = arrGet(ctx, src, i);
                if (hasCallException()) return PROTO_NONE;
                const proto::ProtoObject* mapped = applyMap(v, static_cast<long long>(i));
                if (hasCallException()) return PROTO_NONE;
                if (arrayThrowIfCreateDataPropertyFails(ctx, result, i))
                    return PROTO_NONE;
                const proto::ProtoString* k =
                    JSSymbols::indexKey(ctx, static_cast<uint32_t>(i));
                if (k) result = result->setAttribute(ctx, k,
                                    mapped ? mapped : PROTO_NONE);
                std::string pdStr = "__pd_" + std::to_string(i) + "__";
                const proto::ProtoObject* pdko = ctx->fromUTF8String(pdStr.c_str());
                const proto::ProtoString* pdk = pdko ? pdko->asString(ctx) : nullptr;
                if (pdk) result = result->setAttribute(ctx, pdk,
                                      ctx->fromInteger(kDefaultPdBits));
            }
            result = arrSetLen(ctx, result, n);
            return result;
        }
    }

    // String: iterate characters.
    if (src->isString(ctx)) {
        const proto::ProtoString* s = src->asString(ctx);
        if (s) {
            std::string str;
            s->toUTF8String(ctx, str);
            // Construct now (string path doesn't go through the
            // iterator / array-like branches above).
            if (!result) {
                if (hasCtor) {
                    result = constructC(static_cast<long long>(str.size()),
                                        /*withLen=*/true);
                    if (hasCallException()) return PROTO_NONE;
                } else {
                    result = createNewArray(ctx, nullptr);
                }
            }
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

    if (!result) result = createNewArray(ctx, nullptr);
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
    // §23.1.2.2 Array.of step 4: If IsConstructor(C) is true,
    //   let A = ? Construct(C, « len »).  Else
    //   let A = ? ArrayCreate(len).
    // Pre-fix isCtor only matched __construct__-style builtins;
    // user functions (which carry __bytecode_id__) fell into the
    // default-array branch, so Array.of.call(Coop, ...) never
    // returned a Coop instance (built-ins/Array/of/return-a-custom-
    // instance) and Pack's __set_length__ never fired
    // (built-ins/Array/of/sets-length).
    unsigned long argc = args ? static_cast<unsigned long>(args->getSize(ctx)) : 0;
    const proto::ProtoObject* result = nullptr;

    const proto::ProtoString* constructKey = JSSymbols::construct(ctx);
    const proto::ProtoObject* constructFn = (constructKey && self && self != PROTO_NONE)
        ? self->getAttribute(ctx, constructKey, false) : nullptr;
    bool hasNativeCtor = constructFn && constructFn != PROTO_NONE && constructFn->isMethod(ctx);

    bool isBytecodeFn = false;
    if (self && self != PROTO_NONE) {
        const proto::ProtoString* bcK = JSSymbols::bytecodeId(ctx);
        if (bcK && self->hasAttribute(ctx, bcK) == PROTO_TRUE) {
            // Arrow functions are NOT constructible per §10.2.2.
            const proto::ProtoObject* arrowKO = ctx->fromUTF8String("__is_arrow__");
            const proto::ProtoString* arrowK = arrowKO ? arrowKO->asString(ctx) : nullptr;
            if (!arrowK || self->getAttribute(ctx, arrowK, false) != PROTO_TRUE) {
                isBytecodeFn = true;
            }
        }
    }

    if (hasNativeCtor || isBytecodeFn) {
        const proto::ProtoString* protoKey = JSSymbols::prototype(ctx);
        const proto::ProtoObject* proto = protoKey
            ? self->getAttribute(ctx, protoKey, true) : nullptr;
        result = (proto && proto != PROTO_NONE)
            ? proto->newChild(ctx, true) : ctx->newObject(true);
        const proto::ProtoList* ctorArgs = ctx->newList();
        ctorArgs = ctorArgs->appendLast(ctx,
            ctx->fromInteger(static_cast<long long>(argc)));
        const proto::ProtoObject* res = nullptr;
        if (hasNativeCtor) {
            proto::ProtoMethod fn = constructFn->asMethod(ctx);
            res = fn(ctx, result, nullptr, ctorArgs, nullptr);
        } else {
            res = callJSFunction(ctx, self, result, ctorArgs);
        }
        if (hasCallException()) return PROTO_NONE;
        // §10.1.13 OrdinaryCallEvaluateBody: Object result wins, else
        // the freshly allocated receiver.
        if (res && res != PROTO_NONE
            && res != getUndefinedSentinel() && res != getNullSentinel()
            && !res->isInteger(ctx) && !res->isDouble(ctx) && !res->isFloat(ctx)
            && !res->isBoolean(ctx) && !res->isString(ctx)
            && res != PROTO_TRUE && res != PROTO_FALSE) {
            result = res;
        }
    } else {
        result = createNewArray(ctx, nullptr);
    }

    // §23.1.2.2 step 8: CreateDataPropertyOrThrow(A, k, kValue).
    // This is DEFINE-OWN-DATA, NOT OrdinarySet — an inherited setter
    // on the receiver's prototype chain (Array.prototype[k] = setter)
    // MUST be ignored, and a pre-existing __pd_<idx>__ describing the
    // slot as non-writable/non-enumerable must be reset to the
    // default attribute flags ({writable: true, enumerable: true,
    // configurable: true}) because CreateDataProperty installs a
    // FRESH descriptor, not a value-only update.
    // built-ins/Array/of/does-not-use-prototype-properties (skip
    // inherited setter) and built-ins/Array/from/source-object-
    // length-set-elem-prop-non-writable (reset descriptor flags
    // when ctor-installed slot had writable:false) both pin this.
    constexpr long long kDefaultPdBits = 0x7; // writable|configurable|enumerable
    for (unsigned long i = 0; i < argc; i++) {
        const proto::ProtoObject* v = args->getAt(ctx, static_cast<int>(i));
        const proto::ProtoString* k =
            JSSymbols::indexKey(ctx, static_cast<uint32_t>(i));
        // §7.3.5 CreateDataProperty: if O is non-extensible AND k is
        // not already an own property, the descriptor cannot be
        // installed — success is false, CreateDataPropertyOrThrow
        // raises TypeError.  Also: if k IS an own property but its
        // descriptor is non-configurable, redefining it as a fresh
        // data descriptor likewise fails.  Two T-cases pin this:
        //   T1 = function(){Object.preventExtensions(this)}
        //     → throw because non-extensible.
        //   T2 = function(){Object.defineProperty(this,0,{configurable:false,
        //        writable:true,enumerable:true})}
        //     → throw because existing slot is non-configurable.
        // built-ins/Array/of/return-abrupt-from-data-property.
        {
            JSContextWrapper* wrapper = JSContextWrapper::current();
            bool nonExtensible = wrapper
                && result->hasParent(ctx, wrapper->getNonExtensibleMarker());
            bool hasOwnK = k && result->hasOwnAttribute(ctx, k) == PROTO_TRUE;
            if (nonExtensible && !hasOwnK) {
                signalNativeException(makeNativeError(ctx, "TypeError",
                    "Cannot define property: object is not extensible"));
                return PROTO_NONE;
            }
            if (hasOwnK) {
                std::string pdStr0 = "__pd_" + std::to_string(i) + "__";
                const proto::ProtoObject* pdko0 = ctx->fromUTF8String(pdStr0.c_str());
                const proto::ProtoString* pdk0 = pdko0 ? pdko0->asString(ctx) : nullptr;
                if (pdk0 && result->hasOwnAttribute(ctx, pdk0) == PROTO_TRUE) {
                    const proto::ProtoObject* pdv = result->getAttribute(ctx, pdk0, false);
                    if (pdv && pdv->isInteger(ctx)
                        && (pdv->asLong(ctx) & 0x2) == 0) {
                        signalNativeException(makeNativeError(ctx, "TypeError",
                            "Cannot redefine non-configurable data property"));
                        return PROTO_NONE;
                    }
                }
            }
        }
        if (k) result = result->setAttribute(ctx, k, v ? v : PROTO_NONE);
        std::string pdStr = "__pd_" + std::to_string(i) + "__";
        const proto::ProtoObject* pdko = ctx->fromUTF8String(pdStr.c_str());
        const proto::ProtoString* pdk = pdko ? pdko->asString(ctx) : nullptr;
        if (pdk) result = result->setAttribute(ctx, pdk,
                              ctx->fromInteger(kDefaultPdBits));
    }
    // §23.1.2.2 step 9: Set(A, 'length', len, true).  arrSetLen
    // honours user __set_length__ accessors (package-3 commit) so
    // a user-supplied length setter on the constructor-installed
    // receiver fires correctly.
    arrSetLen(ctx, result, argc);
    if (hasCallException()) return PROTO_NONE;
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
        { "toLocaleString", arrayToLocaleString, 0 },
        { "push",           arrayPush,          1 },
        { "pop",            arrayPop,           0 },
        { "shift",          arrayShift,         0 },
        { "unshift",        arrayUnshift,       1 },
        { "slice",          arraySlice,         2 },
        { "indexOf",        arrayIndexOf,       1 },
        { "lastIndexOf",    arrayLastIndexOf,   1 },
        { "includes",       arrayIncludes,      1 },
        { "reverse",        arrayReverse,       0 },
        { "toReversed",     arrayToReversed,    0 },
        { "toSorted",       arrayToSorted,      1 },
        { "toSpliced",      arrayToSpliced,     2 },
        { "with",           arrayWith,          2 },
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
        const proto::ProtoString* pdlk = JSSymbols::pdLength(ctx);
        if (pdlk) proto = proto->setAttribute(ctx, pdlk, ctx->fromInteger(0x1)); // writable, !configurable, !enumerable
    }

    // Array.prototype is itself an array (exotic object).
    const proto::ProtoString* isArrKey = JSSymbols::isArray(ctx);
    if (isArrKey) proto = proto->setAttribute(ctx, isArrKey, PROTO_TRUE);

    // §23.1.3.32 Array.prototype[@@unscopables] is a plain object
    // whose own enumerable string-keyed slots match the ES2015+
    // method names that should not be visible to with-bound name
    // resolution. The slot's descriptor is {writable:false,
    // enumerable:false, configurable:true} (bits 0x2). Pre-fix the
    // slot was absent so for-in over Array.prototype skipped it (no
    // observable break), but built-ins/Array/prototype/Symbol.
    // unscopables/* fixtures and Object.prototype.toString sweeps that
    // probe its existence failed.
    {
        const proto::ProtoObject* unsObj = ctx->newObject(true);
        if (unsObj) {
            // §23.1.3.32 step 2: OrdinaryObjectCreate(null) — the
            // unscopables list has a null [[Prototype]].  Pre-fix the
            // newObject parented on Object.prototype, so
            // Object.getPrototypeOf(unscopables) returned the object
            // prototype instead of null (built-ins/Array/prototype/
            // Symbol.unscopables/array-find-from-last and friends).
            setJSProtoOverride(unsObj, getNullSentinel());
            static const char* kUnscopables[] = {
                "at", "copyWithin", "entries", "fill", "find",
                "findIndex", "findLast", "findLastIndex", "flat",
                "flatMap", "includes", "keys", "toReversed",
                "toSorted", "toSpliced", "values", "group", "groupToMap",
                nullptr
            };
            for (int i = 0; kUnscopables[i]; ++i) {
                const proto::ProtoString* k = ctx->fromUTF8String(kUnscopables[i])
                    ? ctx->fromUTF8String(kUnscopables[i])->asString(ctx) : nullptr;
                if (k) unsObj = unsObj->setAttribute(ctx, k, PROTO_TRUE);
            }
            const proto::ProtoObject* uko = ctx->fromUTF8String("Symbol.unscopables");
            const proto::ProtoString* uk = uko ? uko->asString(ctx) : nullptr;
            if (uk) {
                proto = proto->setAttribute(ctx, uk, unsObj);
                const proto::ProtoObject* pdo = ctx->fromUTF8String("__pd_Symbol.unscopables__");
                const proto::ProtoString* pdk = pdo ? pdo->asString(ctx) : nullptr;
                if (pdk) proto = proto->setAttribute(ctx, pdk, ctx->fromInteger(0x2LL));
            }
        }
    }

    // Store in module-level static for createNewArray.
    s_arrayProto = proto;

    // Sync wrapper-level Array prototype so TypeBridge::fromJS and
    // JSONBuiltin::parse stamp newly imported QuickJS arrays with this
    // populated proto (instead of the empty placeholder built by
    // BootstrapJSPrototypes). Without this sync, JSON.parse('[1,2,3]')
    // returned an array whose prototype was an empty Object child —
    // so `a.join`, `a.map`, and every other Array.prototype method
    // surfaced as undefined.
    if (JSContextWrapper* w = JSContextWrapper::current()) {
        w->setJSArrayPrototype(s_arrayProto);
    }

    // ------------------------------------------------------------------
    // Build the Array constructor object.
    //
    // Parent: Function.prototype (space->methodPrototype) so that
    // \`Array.apply\`, \`Array.call\`, \`Array.bind\` resolve via the
    // standard chain walk.  Pre-fix the constructor was an orphan
    // newObject(false) child — every Function.prototype method was
    // invisible to it.  Number / Boolean / String constructors
    // already follow this pattern; this fix brings Array in line.
    // ------------------------------------------------------------------
    // §17: built-in constructor objects are extensible. Pre-fix the
    // Array constructor was created immutable (newChild(ctx, false)),
    // so `Array.myProperty = 1` produced a fresh detached copy that
    // the global root could not see — `Array.myProperty` always read
    // undefined on the next access, breaking S15.4.3_A1.1 and the
    // `Function.prototype` inheritance check (a property installed on
    // Function.prototype was visible only on the orphan copy).
    const proto::ProtoObject* ctorParent =
        (ctx->space && ctx->space->methodPrototype) ? ctx->space->methodPrototype : nullptr;
    const proto::ProtoObject* ctor = ctorParent
        ? ctorParent->newChild(ctx, true)
        : ctx->newObject(true);

    const proto::ProtoString* markerKey =
        JSSymbols::arrayCtor(ctx);
    if (markerKey) ctor = ctor->setAttribute(ctx, markerKey, PROTO_TRUE);

    // Explicitly mark as a constructor for OP_call_constructor.
    const proto::ProtoString* isCtorKey = ctx->fromUTF8String("__is_constructor__")->asString(ctx);
    if (isCtorKey) ctor = ctor->setAttribute(ctx, isCtorKey, PROTO_TRUE);

    // §23.1.2.2: Array.prototype is non-writable, non-enumerable,
    // non-configurable.  Pre-fix the property was fully enumerable.
    const proto::ProtoString* protoKey =
        JSSymbols::prototype(ctx);
    if (protoKey) {
        ctor = ctor->setAttribute(ctx, protoKey, proto);
        const proto::ProtoObject* pdpo = ctx->fromUTF8String("__pd_prototype__");
        const proto::ProtoString* pdpk = pdpo ? pdpo->asString(ctx) : nullptr;
        if (pdpk) ctor = ctor->setAttribute(ctx, pdpk, ctx->fromInteger(0x0LL));
    }

    const proto::ProtoString* nameKey = JSSymbols::name(ctx);
    if (nameKey) {
        ctor = ctor->setAttribute(ctx, nameKey, ctx->fromUTF8String("Array"));
        const proto::ProtoString* pdnk = JSSymbols::pdName(ctx);
        if (pdnk) ctor = ctor->setAttribute(ctx, pdnk, ctx->fromInteger(0x2)); // configurable, !writable, !enumerable
    }
    lengthKey = JSSymbols::length(ctx);
    if (lengthKey) {
        ctor = ctor->setAttribute(ctx, lengthKey, ctx->fromInteger(1));
        const proto::ProtoString* pdlk = JSSymbols::pdLength(ctx);
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

    // get Array[Symbol.species] — install with §17 name/length descriptors
    // and the well-known __get_Symbol.species__ accessor key, plus the
    // accessor property descriptor on the constructor itself per
    // §22.1.2.5: {enumerable: false, configurable: true} → 0x2.  Pre-
    // fix the getter had no name/length attributes (built-ins/Array/
    // Symbol.species/{length,symbol-species-name}) and the species
    // slot itself defaulted to fully-enumerable (built-ins/Array/
    // Symbol.species/symbol-species).  Mirrors the Set install pattern.
    {
        const proto::ProtoString* speciesKey = JSSymbols::symbolSpecies(ctx);
        if (speciesKey) {
            const proto::ProtoObject* parent =
                (ctx->space && ctx->space->methodPrototype)
                ? ctx->space->methodPrototype : nullptr;
            const proto::ProtoObject* getter = parent
                ? parent->newChild(ctx, true) : ctx->newObject(true);
            if (getter) {
                const proto::ProtoString* nfKey = JSSymbols::nativeFn(ctx);
                if (nfKey) {
                    proto::ProtoObject* mGetter = const_cast<proto::ProtoObject*>(getter);
                    const proto::ProtoObject* raw = ctx->fromMethod(mGetter, arraySpeciesGetter);
                    if (raw) getter = getter->setAttribute(ctx, nfKey, raw);
                }
                const proto::ProtoString* lenKey = JSSymbols::length(ctx);
                if (lenKey) {
                    getter = getter->setAttribute(ctx, lenKey, ctx->fromInteger(0LL));
                    const proto::ProtoString* pdls = JSSymbols::pdLength(ctx);
                    if (pdls) getter = getter->setAttribute(ctx, pdls, ctx->fromInteger(0x2LL));
                }
                const proto::ProtoString* nmKey = JSSymbols::name(ctx);
                if (nmKey) {
                    getter = getter->setAttribute(ctx, nmKey, ctx->fromUTF8String("get [Symbol.species]"));
                    const proto::ProtoString* pdns = JSSymbols::pdName(ctx);
                    if (pdns) getter = getter->setAttribute(ctx, pdns, ctx->fromInteger(0x2LL));
                }
                const proto::ProtoString* gksSym =
                    ctx->fromUTF8String("__get_Symbol.species__")->asString(ctx);
                if (gksSym) ctor = ctor->setAttribute(ctx, gksSym, getter);
                // Descriptor for the species property on Array:
                // accessor with {enumerable:false, configurable:true} → 0x2.
                const proto::ProtoObject* pdo = ctx->fromUTF8String("__pd_Symbol.species__");
                const proto::ProtoString* pdk = pdo ? pdo->asString(ctx) : nullptr;
                if (pdk) ctor = ctor->setAttribute(ctx, pdk, ctx->fromInteger(0x2LL));
            }
        }
    }

    // Set Array.prototype.constructor = Array (required by ECMAScript).
    // Descriptor per §22.1.3.2: {writable:true, enumerable:false,
    // configurable:true} → bits 0x3. Without the sidecar the
    // default is fully enumerable, so for-in over any array surfaced
    // `constructor` alongside the indices — breaking idiomatic
    // numeric-key iteration.
    {
        const proto::ProtoString* ctorKey = JSSymbols::constructor(ctx);
        if (ctorKey) {
            proto = proto->setAttribute(ctx, ctorKey, ctor);
            const proto::ProtoString* pdk = JSSymbols::pdConstructor(ctx);
            if (pdk) proto = proto->setAttribute(ctx, pdk, ctx->fromInteger(0x3LL));
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
    // §17 globalThis.Array descriptor 0x3.
    {
        const proto::ProtoObject* pdo = ctx->fromUTF8String("__pd_Array__");
        const proto::ProtoString* pdk = pdo ? pdo->asString(ctx) : nullptr;
        if (pdk) *globalRoot = (*globalRoot)->setAttribute(ctx, pdk,
            ctx->fromInteger(0x3LL));
    }

    // Fast-path key "__array_proto__" for OP_array_from lookup.
    const proto::ProtoString* fastProtoKey =
        JSSymbols::arrayProto(ctx);
    if (fastProtoKey)
        *globalRoot = (*globalRoot)->setAttribute(ctx, fastProtoKey, proto);
}

} // namespace protojs
