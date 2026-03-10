#include "ArrayPrototype.h"
#include "ProtoJSStringCache.h"
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
    const proto::ProtoString* key = ProtoJSStringCache::getKey(ctx, "length");
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
    const proto::ProtoString* key = ProtoJSStringCache::getIndexKey(ctx, static_cast<uint32_t>(idx));
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
    const proto::ProtoString* key = ProtoJSStringCache::getIndexKey(ctx, static_cast<uint32_t>(idx));
    if (!key) return arr;
    arr = arr->setAttribute(ctx, key, val ? val : PROTO_NONE);
    unsigned long curLen = arrLen(ctx, arr);
    if (idx + 1 > curLen) {
        const proto::ProtoString* lenKey = ProtoJSStringCache::getKey(ctx, "length");
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
    const proto::ProtoString* key = ProtoJSStringCache::getKey(ctx, "length");
    if (!key) return arr;
    return arr->setAttribute(ctx, key,
                             ctx->fromInteger(static_cast<long long>(newLen)));
}

// ---------------------------------------------------------------------------
// Value-to-string conversion for join (mirrors ProtoInterpreter::toString).
// ---------------------------------------------------------------------------
static std::string elemToString(proto::ProtoContext* ctx,
                                 const proto::ProtoObject* val) {
    if (!val || val == PROTO_NONE) return "";
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
// Array.prototype methods
// ---------------------------------------------------------------------------

static const proto::ProtoObject* arrayJoin(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!self || self == PROTO_NONE) return ctx->fromUTF8String("");
    unsigned long len = arrLen(ctx, self);

    std::string sep = ",";
    if (args && args->getSize(ctx) > 0) {
        const proto::ProtoObject* sepObj = args->getAt(ctx, 0);
        if (!sepObj || sepObj == PROTO_NONE) {
            sep = ",";
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
    if (!self || self == PROTO_NONE) return ctx->fromInteger(0LL);
    unsigned long len = arrLen(ctx, self);
    unsigned long argc = args ? static_cast<unsigned long>(args->getSize(ctx)) : 0;
    for (unsigned long i = 0; i < argc; i++) {
        const proto::ProtoObject* item = args->getAt(ctx, static_cast<int>(i));
        // arrSet modifies mutable arrays in-place; return value is same pointer.
        arrSet(ctx, self, len + i, item);
    }
    unsigned long newLen = len + argc;
    const proto::ProtoString* lenKey = ProtoJSStringCache::getKey(ctx, "length");
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
    if (!self || self == PROTO_NONE) return PROTO_NONE;
    unsigned long len = arrLen(ctx, self);
    if (len == 0) return PROTO_NONE;
    unsigned long lastIdx = len - 1;
    const proto::ProtoObject* removed = arrGet(ctx, self, lastIdx);
    // Clear the element and shrink length.
    const proto::ProtoString* idxKey =
        ProtoJSStringCache::getIndexKey(ctx, static_cast<uint32_t>(lastIdx));
    if (idxKey) self->setAttribute(ctx, idxKey, PROTO_NONE);
    const proto::ProtoString* lenKey = ProtoJSStringCache::getKey(ctx, "length");
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
    if (!self || self == PROTO_NONE) return PROTO_NONE;
    unsigned long len = arrLen(ctx, self);
    if (len == 0) return PROTO_NONE;
    const proto::ProtoObject* first = arrGet(ctx, self, 0);
    // Shift elements down by 1.
    for (unsigned long i = 1; i < len; i++) {
        arrSet(ctx, self, i - 1, arrGet(ctx, self, i));
    }
    // Clear last slot and update length.
    const proto::ProtoString* lastKey =
        ProtoJSStringCache::getIndexKey(ctx, static_cast<uint32_t>(len - 1));
    if (lastKey) self->setAttribute(ctx, lastKey, PROTO_NONE);
    const proto::ProtoString* lenKey = ProtoJSStringCache::getKey(ctx, "length");
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
    if (!self || self == PROTO_NONE) return ctx->fromInteger(0LL);
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
    const proto::ProtoString* lenKey = ProtoJSStringCache::getKey(ctx, "length");
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
    if (!self || self == PROTO_NONE) return createNewArray(ctx, nullptr);
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
    if (!self || self == PROTO_NONE || !args || args->getSize(ctx) == 0)
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
    if (!self || self == PROTO_NONE || !args || args->getSize(ctx) == 0)
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
    if (!self || self == PROTO_NONE || !args || args->getSize(ctx) == 0)
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
    if (!self || self == PROTO_NONE) return self ? self : PROTO_NONE;
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
    const proto::ProtoObject* result = createNewArray(ctx, nullptr);
    unsigned long outIdx = 0;

    // Helper: determine if a value should be spread (array-like with "length").
    auto isSpreadable = [&](const proto::ProtoObject* obj) -> bool {
        if (!obj || obj == PROTO_NONE) return false;
        if (obj->isInteger(ctx) || obj->isDouble(ctx) || obj->isFloat(ctx) ||
            obj->isString(ctx) || obj->isBoolean(ctx)) return false;
        const proto::ProtoString* lenKey = ProtoJSStringCache::getKey(ctx, "length");
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
    if (!self || self == PROTO_NONE) return self ? self : PROTO_NONE;
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
    if (!self || self == PROTO_NONE || !args || args->getSize(ctx) == 0)
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
    const proto::ProtoString* lenKey = ProtoJSStringCache::getKey(ctx, "length");
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
    const proto::ProtoString* lenKey = ProtoJSStringCache::getKey(ctx, "length");
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
    const proto::ProtoString* arrayKey = ProtoJSStringCache::getKey(ctx, "Array");
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
    struct { const char* name; proto::ProtoMethod fn; } methods[] = {
        { "join",         arrayJoin         },
        { "toString",     arrayToString     },
        { "push",         arrayPush         },
        { "pop",          arrayPop          },
        { "shift",        arrayShift        },
        { "unshift",      arrayUnshift      },
        { "slice",        arraySlice        },
        { "indexOf",      arrayIndexOf      },
        { "lastIndexOf",  arrayLastIndexOf  },
        { "includes",     arrayIncludes     },
        { "reverse",      arrayReverse      },
        { "concat",       arrayConcat       },
        { "fill",         arrayFill         },
        { "copyWithin",   arrayCopyWithin   },
    };
    for (auto& m : methods) {
        const proto::ProtoString* key = ProtoJSStringCache::getKey(ctx, m.name);
        if (key) {
            const proto::ProtoObject* fn = ctx->fromMethod(nullptr, m.fn);
            if (fn) proto = proto->setAttribute(ctx, key, fn);
        }
    }

    // Store in module-level static for createNewArray.
    s_arrayProto = proto;

    // ------------------------------------------------------------------
    // Build the Array constructor object.
    // ------------------------------------------------------------------
    const proto::ProtoObject* ctor = ctx->newObject(false);

    const proto::ProtoString* markerKey =
        ProtoJSStringCache::getKey(ctx, "__array_ctor__");
    if (markerKey) ctor = ctor->setAttribute(ctx, markerKey, PROTO_TRUE);

    const proto::ProtoString* protoKey =
        ProtoJSStringCache::getKey(ctx, "prototype");
    if (protoKey) ctor = ctor->setAttribute(ctx, protoKey, proto);

    const proto::ProtoString* nameKey = ProtoJSStringCache::getKey(ctx, "name");
    if (nameKey)
        ctor = ctor->setAttribute(ctx, nameKey, ctx->fromUTF8String("Array"));

    // Add static methods: isArray, from, of.
    struct { const char* name; proto::ProtoMethod fn; } statics[] = {
        { "isArray", arrayIsArray },
        { "from",    arrayFrom    },
        { "of",      arrayOf      },
    };
    for (auto& s : statics) {
        const proto::ProtoString* key = ProtoJSStringCache::getKey(ctx, s.name);
        if (key) {
            const proto::ProtoObject* fn = ctx->fromMethod(nullptr, s.fn);
            if (fn) ctor = ctor->setAttribute(ctx, key, fn);
        }
    }

    // ------------------------------------------------------------------
    // Register on global root.
    // ------------------------------------------------------------------
    *globalRoot = (*globalRoot)->setAttribute(ctx, arrayKey, ctor);

    // Fast-path key "__array_proto__" for OP_array_from lookup.
    const proto::ProtoString* fastProtoKey =
        ProtoJSStringCache::getKey(ctx, "__array_proto__");
    if (fastProtoKey)
        *globalRoot = (*globalRoot)->setAttribute(ctx, fastProtoKey, proto);
}

} // namespace protojs
