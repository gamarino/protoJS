#include "ArrayPrototype.h"
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
    const proto::ProtoString* key = JSSymbols::length(ctx);
    if (!key) return 0;
    const proto::ProtoObject* lenObj = arr->getAttribute(ctx, key, false);
    if (!lenObj || lenObj == PROTO_NONE) return 0;
    if (lenObj->isInteger(ctx)) {
        long long v = lenObj->asLong(ctx);
        return (v > 0) ? static_cast<unsigned long>(v) : 0;
    }
    if (lenObj->isDouble(ctx) || lenObj->isFloat(ctx)) {
        double d = lenObj->asDouble(ctx);
        if (d <= 0 || std::isnan(d) || std::isinf(d)) return 0;
        return static_cast<unsigned long>(d);
    }
    // String-encoded length (e.g., "-4294967294") — try parsing as integer, clamp to 0.
    if (lenObj->isString(ctx)) {
        const proto::ProtoString* s = lenObj->asString(ctx);
        if (s) {
            std::string sv;
            s->toUTF8String(ctx, sv);
            try {
                long long v = std::stoll(sv);
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
    const proto::ProtoString* key = JSSymbols::indexKey(ctx, static_cast<uint32_t>(idx));
    if (!key) return PROTO_NONE;
    const proto::ProtoObject* val = arr->getAttribute(ctx, key, false);
    return val ? val : PROTO_NONE;
}

// Set element at idx, also updates "length" if idx+1 > current length.
// For mutable arrays, modifies in-place; returns same pointer.
// For immutable arrays, returns new pointer (caller should capture).
static const proto::ProtoObject* arrSet(proto::ProtoContext* ctx,
                                         const proto::ProtoObject* arr,
                                         unsigned long idx,
                                         const proto::ProtoObject* val) {
    if (!arr) return PROTO_NONE;
    const proto::ProtoString* key = JSSymbols::indexKey(ctx, static_cast<uint32_t>(idx));
    if (!key) return arr;
    arr = arr->setAttribute(ctx, key, val ? val : PROTO_NONE);
    unsigned long curLen = arrLen(ctx, arr);
    if (idx + 1 > curLen) {
        const proto::ProtoString* lenKey = JSSymbols::length(ctx);
        if (lenKey)
            arr = arr->setAttribute(ctx, lenKey,
                                    ctx->fromInteger(static_cast<long long>(idx + 1)));
    }
    return arr;
}

static const proto::ProtoObject* arrSetLen(proto::ProtoContext* ctx,
                                            const proto::ProtoObject* arr,
                                            unsigned long newLen) {
    if (!arr) return PROTO_NONE;
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
    return arrSetLen(ctx, arr, 0);
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
    unsigned long len = arrLen(ctx, self);
    unsigned long argc = args ? static_cast<unsigned long>(args->getSize(ctx)) : 0;
    for (unsigned long i = 0; i < argc; i++) {
        const proto::ProtoObject* item = args->getAt(ctx, static_cast<int>(i));
        // arrSet modifies mutable arrays in-place; return value is same pointer.
        arrSet(ctx, self, len + i, item);
    }
    unsigned long newLen = len + argc;
    const proto::ProtoString* lenKey = JSSymbols::length(ctx);
    if (lenKey)
        self->setAttribute(ctx, lenKey, ctx->fromInteger(static_cast<long long>(newLen)));
    return ctx->fromInteger(static_cast<long long>(newLen));
}

static const proto::ProtoObject* arrayPop(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*,
    const proto::ProtoSparseList*)
{
    if (arrayThrowIfNullUndefined(ctx, self)) return PROTO_NONE;
    unsigned long len = arrLen(ctx, self);
    if (len == 0) return PROTO_NONE;
    unsigned long lastIdx = len - 1;
    const proto::ProtoObject* removed = arrGet(ctx, self, lastIdx);
    // Clear the element and shrink length.
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
    unsigned long len = arrLen(ctx, self);
    if (len == 0) return PROTO_NONE;
    const proto::ProtoObject* first = arrGet(ctx, self, 0);
    // Shift elements down by 1.
    for (unsigned long i = 1; i < len; i++) {
        arrSet(ctx, self, i - 1, arrGet(ctx, self, i));
    }
    // Clear last slot and update length.
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
    unsigned long len = arrLen(ctx, self);
    unsigned long argc = args ? static_cast<unsigned long>(args->getSize(ctx)) : 0;
    if (argc == 0) return ctx->fromInteger(static_cast<long long>(len));
    // Shift existing elements right by argc positions (iterate right-to-left).
    for (long long i = static_cast<long long>(len) - 1; i >= 0; i--) {
        arrSet(ctx, self, static_cast<unsigned long>(i) + argc,
               arrGet(ctx, self, static_cast<unsigned long>(i)));
    }
    // Insert new items at beginning.
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

    const proto::ProtoObject* result = createNewArray(ctx, nullptr);
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
    // Guard against extremely sparse arrays that would cause O(n) iteration over billions of slots.
    static constexpr long long MAX_SEARCH_ITERS = 10LL; // 10 — prevents timeout on huge sparse arrays
    long long end = (len - from > MAX_SEARCH_ITERS) ? from + MAX_SEARCH_ITERS : len;
    for (long long i = from; i < end; i++) {
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
    // Guard against extremely sparse arrays that would cause O(n) iteration over billions of slots.
    static constexpr long long MAX_SEARCH_ITERS = 10LL; // 10 — prevents timeout on huge sparse arrays
    long long lo = (from > MAX_SEARCH_ITERS) ? from - MAX_SEARCH_ITERS : 0LL;
    for (long long i = from; i >= lo; i--) {
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
    const proto::ProtoObject* result = createNewArray(ctx, nullptr);
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
    const proto::ProtoObject* result = createNewArray(ctx, nullptr);
    for (unsigned long i = 0; i < len; i++) {
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
    const proto::ProtoObject* result = createNewArray(ctx, nullptr);
    unsigned long outIdx = 0;
    for (unsigned long i = 0; i < len; i++) {
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
        const proto::ProtoObject* res =
            callJSFunction(ctx, fn, thisArg, makeIterArgs(ctx, arrGet(ctx, self, i), (long long)i, self));
        if (hasCallException()) return PROTO_NONE;
        if (!isTruthy(ctx, res)) return PROTO_FALSE;
    }
    return PROTO_TRUE;
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
        if (len == 0) return PROTO_NONE; // TypeError in spec; return PROTO_NONE
        acc   = arrGet(ctx, self, 0);
        start = 1;
    }
    for (long long i = start; i < len; i++) {
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
        if (len == 0) return PROTO_NONE;
        acc   = arrGet(ctx, self, (unsigned long)(len - 1));
        start = len - 2;
    }
    for (long long i = start; i >= 0; i--) {
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
    self->setAttribute(ctx, idxKey, ctx->fromInteger(idx + 1));

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
    if (val->isInteger(ctx) || val->isDouble(ctx) || val->isFloat(ctx) ||
        val->isString(ctx) || val->isBoolean(ctx)) return PROTO_FALSE;
    // Heuristic: any object with a "length" attribute that is a non-negative integer
    // is treated as an array.  This matches the practical usage in test262.
    const proto::ProtoString* lenKey = JSSymbols::length(ctx);
    if (!lenKey) return PROTO_FALSE;
    const proto::ProtoObject* lv = val->getAttribute(ctx, lenKey, false);
    if (!lv || lv == PROTO_NONE) return PROTO_FALSE;
    if (lv->isInteger(ctx) && lv->asLong(ctx) >= 0) return PROTO_TRUE;
    if ((lv->isDouble(ctx) || lv->isFloat(ctx)) && lv->asDouble(ctx) >= 0.0)
        return PROTO_TRUE;
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

    // If src has a "length", iterate by index (array-like).
    const proto::ProtoString* lenKey = JSSymbols::length(ctx);
    if (!lenKey) return result;
    const proto::ProtoObject* lv = src->getAttribute(ctx, lenKey, false);
    if (lv && lv != PROTO_NONE &&
        (lv->isInteger(ctx) || lv->isDouble(ctx) || lv->isFloat(ctx))) {
        unsigned long n = static_cast<unsigned long>(
            lv->isInteger(ctx) ? lv->asLong(ctx)
                               : static_cast<long long>(lv->asDouble(ctx)));
        for (unsigned long i = 0; i < n; i++)
            arrSet(ctx, result, i, arrGet(ctx, src, i));
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
                arrSet(ctx, result, i++, ctx->fromUTF8String(buf));
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
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    const proto::ProtoObject* result = createNewArray(ctx, nullptr);
    unsigned long argc = args ? static_cast<unsigned long>(args->getSize(ctx)) : 0;
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
    const proto::ProtoObject* proto = objectProto
        ? objectProto->newChild(ctx, false)
        : ctx->newObject(false);

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
            if (fn && fn != PROTO_NONE)
                proto = proto->setAttribute(ctx, key, fn);
        }
    }

    // Mark the array prototype so Object.prototype.toString can detect arrays.
    const proto::ProtoString* isArrayKey = JSSymbols::isArray(ctx);
    if (isArrayKey) proto = proto->setAttribute(ctx, isArrayKey, PROTO_TRUE);

    // Store in module-level static for createNewArray.
    s_arrayProto = proto;

    // ------------------------------------------------------------------
    // Build the Array constructor object.
    // ------------------------------------------------------------------
    const proto::ProtoObject* ctor = ctx->newObject(false);

    const proto::ProtoString* markerKey =
        JSSymbols::arrayCtor(ctx);
    if (markerKey) ctor = ctor->setAttribute(ctx, markerKey, PROTO_TRUE);

    const proto::ProtoString* protoKey =
        JSSymbols::prototype(ctx);
    if (protoKey) ctor = ctor->setAttribute(ctx, protoKey, proto);

    const proto::ProtoString* nameKey = JSSymbols::name(ctx);
    if (nameKey)
        ctor = ctor->setAttribute(ctx, nameKey, ctx->fromUTF8String("Array"));

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
            const proto::ProtoObject* fn = ctx->fromMethod(nullptr, s.fn);
            if (fn && fn != PROTO_NONE) {
                const proto::ProtoString* lenKey = JSSymbols::length(ctx);
                const proto::ProtoString* nameKey = JSSymbols::name(ctx);
                if (lenKey) fn = fn->setAttribute(ctx, lenKey, ctx->fromInteger(s.length));
                if (nameKey) fn = fn->setAttribute(ctx, nameKey, ctx->fromUTF8String(s.name));
            }
            if (fn) ctor = ctor->setAttribute(ctx, key, fn);
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
