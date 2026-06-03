#include "StringPrototype.h"
#include "ArrayPrototype.h"
#include "ArrayElementsStorage.h"
#include "FunctionPrototype.h"
#include "JSSymbols.h"
#include "PrototypeUtils.h"
#include "TypeBridge.h"
#include "headers/protoCore.h"
#include "runtime/ProtoInterpreter.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

namespace protojs {

namespace {

// ---------------------------------------------------------------------------
// Internal helpers
// ---------------------------------------------------------------------------

/** Convert any ProtoObject to its string representation. */
static std::string objToStr(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    if (!obj || obj == PROTO_NONE || obj == getUndefinedSentinel()) return "undefined";
    if (obj == getNullSentinel()) return "null";
    std::string r;
    if (obj->isString(ctx)) {
        obj->asString(ctx)->toUTF8String(ctx, r);
        return r;
    }
    if (obj->isInteger(ctx)) return std::to_string(obj->asLong(ctx));
    if (obj->isDouble(ctx)) {
        double d = obj->asDouble(ctx);
        if (std::isnan(d))  return "NaN";
        if (std::isinf(d))  return d > 0 ? "Infinity" : "-Infinity";
        // ECMA-262 §7.1.12.1 Number::toString: ToString(-0) === "0".
        if (d == 0.0) return "0";
        char buf[64];
        snprintf(buf, sizeof(buf), "%.15g", d);
        return buf;
    }
    if (obj->isBoolean(ctx)) return obj->asBoolean(ctx) ? "true" : "false";
    // String wrapper object: unwrap __primitive_value__ before falling back.
    {
        const proto::ProtoString* pvKey = JSSymbols::primitiveValue(ctx);
        if (pvKey) {
            const proto::ProtoObject* pv = obj->getAttribute(ctx, pvKey, false);
            if (pv && pv != PROTO_NONE && pv->isString(ctx)) {
                pv->asString(ctx)->toUTF8String(ctx, r);
                return r;
            }
        }
    }
    // Object: try toString() from the prototype chain. Handle three
    // shapes: a raw ProtoMethod (calls directly), a wrapped function
    // object with __native_fn__ (extract the method and call), or a
    // JS bytecode closure (requires interpreter re-entry — done via
    // callJSFunction so template-literal / .concat coercion works
    // for ANY user-defined toString).
    {
        const proto::ProtoObject* tsKey_o = ctx->fromUTF8String("toString");
        const proto::ProtoString* tsKey = tsKey_o ? tsKey_o->asString(ctx) : nullptr;
        if (tsKey) {
            const proto::ProtoObject* tsFn = obj->getAttribute(ctx, tsKey, true);
            if (tsFn && tsFn != PROTO_NONE) {
                const proto::ProtoObject* result = nullptr;
                if (tsFn->isMethod(ctx)) {
                    proto::ProtoMethod nativeFn = tsFn->asMethod(ctx);
                    if (nativeFn) {
                        result = nativeFn(ctx, obj, nullptr, ctx->newList(), nullptr);
                    }
                } else {
                    // Wrapped or JS function — go through the full
                    // call path so Array.prototype.toString (join) and
                    // user-defined toString both work.
                    result = callJSFunction(ctx, tsFn, obj, ctx->newList());
                }
                if (result && result != PROTO_NONE) {
                    const proto::ProtoString* rs = result->asString(ctx);
                    if (rs) {
                        rs->toUTF8String(ctx, r);
                        return r;
                    }
                }
            }
        }
    }
    return "[object Object]";
}

// Forward declaration — defined further below.
static bool isRegExp(proto::ProtoContext* ctx, const proto::ProtoObject* obj);

/** Extract a numeric argument.  NaN → 0; ±Infinity → LLONG extremes. */
static long long getIntArg(proto::ProtoContext* ctx, const proto::ProtoList* args,
                            unsigned idx, long long defaultVal) {
    if (!args || static_cast<unsigned long>(args->getSize(ctx)) <= idx) return defaultVal;
    const proto::ProtoObject* a = args->getAt(ctx, static_cast<int>(idx));
    if (!a || a == PROTO_NONE) return defaultVal;
    // Spec ToInteger: string args coerce through ToNumber first
    // ("1" → 1, "NaN" → 0). Pre-fix the helper short-circuited to
    // the default for any non-numeric input, so `"abc".charAt("1")`
    // returned the default-index character instead of the second.
    if (a->isString(ctx)) {
        const proto::ProtoObject* num = jsToNumber(ctx, a);
        if (num) {
            if (num->isInteger(ctx)) return num->asLong(ctx);
            if (num->isDouble(ctx) || num->isFloat(ctx)) {
                double d = num->asDouble(ctx);
                if (std::isnan(d)) return 0LL;
                if (std::isinf(d)) return d > 0
                    ? std::numeric_limits<long long>::max()
                    : std::numeric_limits<long long>::min();
                return static_cast<long long>(d);
            }
        }
        return defaultVal;
    }
    if (a->isInteger(ctx)) return a->asLong(ctx);
    if (a->isDouble(ctx)) {
        double d = a->asDouble(ctx);
        if (std::isnan(d))  return 0LL;
        if (std::isinf(d))  return d > 0
            ? std::numeric_limits<long long>::max()
            : std::numeric_limits<long long>::min();
        return static_cast<long long>(d);
    }
    // String argument: try parsing as integer
    return defaultVal;
}

/** Extract a string argument (returns empty string if absent / undefined). */
static std::string getStrArg(proto::ProtoContext* ctx, const proto::ProtoList* args,
                              unsigned idx) {
    if (!args || static_cast<unsigned long>(args->getSize(ctx)) <= idx) return "";
    const proto::ProtoObject* a = args->getAt(ctx, static_cast<int>(idx));
    if (!a || a == PROTO_NONE) return "";
    return objToStr(ctx, a);
}

/** Extract a string argument applying full ToString semantics. Missing /
 *  undefined argument becomes literal "undefined"; null becomes "null".
 *  Required by spec for indexOf/startsWith/endsWith/includes etc.
 */
static std::string getStrArgWithUndef(proto::ProtoContext* ctx, const proto::ProtoList* args,
                                       unsigned idx) {
    if (!args || static_cast<unsigned long>(args->getSize(ctx)) <= idx) return "undefined";
    const proto::ProtoObject* a = args->getAt(ctx, static_cast<int>(idx));
    if (!a || a == PROTO_NONE || a == getUndefinedSentinel()) return "undefined";
    if (a == getNullSentinel()) return "null";
    return objToStr(ctx, a);
}

// ---------------------------------------------------------------------------
// UTF-8 ↔ UTF-16 conversion (needed for correct JS string indexing semantics)
// ---------------------------------------------------------------------------

/** Decode UTF-8 to a sequence of UTF-16 code units. */
static std::vector<uint16_t> utf8ToUTF16(const std::string& s) {
    std::vector<uint16_t> out;
    for (size_t i = 0; i < s.size(); ) {
        auto c = static_cast<unsigned char>(s[i]);
        uint32_t cp = 0;
        int n;
        if      (c < 0x80) { cp = c;        n = 1; }
        else if (c < 0xE0) { cp = c & 0x1F; n = 2; }
        else if (c < 0xF0) { cp = c & 0x0F; n = 3; }
        else               { cp = c & 0x07; n = 4; }
        for (int j = 1; j < n && i + j < s.size(); j++)
            cp = (cp << 6) | (static_cast<unsigned char>(s[i + j]) & 0x3F);
        i += n;
        if (cp < 0x10000) {
            out.push_back(static_cast<uint16_t>(cp));
        } else {
            cp -= 0x10000;
            out.push_back(static_cast<uint16_t>(0xD800 + (cp >> 10)));
            out.push_back(static_cast<uint16_t>(0xDC00 + (cp & 0x3FF)));
        }
    }
    return out;
}

/** Encode a slice [from, to) of UTF-16 code units back to UTF-8. */
static std::string utf16ToUTF8(const std::vector<uint16_t>& u,
                                size_t from = 0, size_t to = SIZE_MAX) {
    if (to > u.size()) to = u.size();
    std::string out;
    for (size_t i = from; i < to; ) {
        uint32_t cp;
        uint16_t h = u[i++];
        if (h >= 0xD800 && h <= 0xDBFF && i < to &&
                u[i] >= 0xDC00 && u[i] <= 0xDFFF) {
            cp = 0x10000u + (static_cast<uint32_t>(h - 0xD800) << 10)
                           + (u[i++] - 0xDC00);
        } else {
            cp = h;
        }
        if      (cp < 0x80)    { out += static_cast<char>(cp); }
        else if (cp < 0x800)   { out += static_cast<char>(0xC0 | (cp >> 6));
                                  out += static_cast<char>(0x80 | (cp & 0x3F)); }
        else if (cp < 0x10000) { out += static_cast<char>(0xE0 | (cp >> 12));
                                  out += static_cast<char>(0x80 | ((cp >>  6) & 0x3F));
                                  out += static_cast<char>(0x80 | ( cp        & 0x3F)); }
        else                   { out += static_cast<char>(0xF0 | (cp >> 18));
                                  out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
                                  out += static_cast<char>(0x80 | ((cp >>  6) & 0x3F));
                                  out += static_cast<char>(0x80 | ( cp        & 0x3F)); }
    }
    return out;
}

// ---------------------------------------------------------------------------
// String.prototype method implementations
// ---------------------------------------------------------------------------

/** Call at the start of every String.prototype instance method.
 *  Signals TypeError and returns false if `self` is null or undefined.
 *  Spec reference: ECMA-262 §21.1 RequireObjectCoercible. */
static bool requireStringThis(proto::ProtoContext* ctx,
                               const proto::ProtoObject* self) {
    if (!self || self == PROTO_NONE || self->isNone(ctx)) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "String.prototype method called on null or undefined"));
        return false;
    }
    const proto::ProtoObject* nullSentinel = getNullSentinel();
    if (nullSentinel && self == nullSentinel) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "String.prototype method called on null or undefined"));
        return false;
    }
    const proto::ProtoObject* undefSentinel = getUndefinedSentinel();
    if (undefSentinel && self == undefSentinel) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "String.prototype method called on null or undefined"));
        return false;
    }
    return true;
}

const proto::ProtoObject* stringValueOf(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{
    if (!requireStringThis(ctx, self)) return PROTO_NONE;
    // Primitive string: return as-is.
    if (self->isString(ctx)) return self;
    // String wrapper object: extract __primitive_value__.
    {
        const proto::ProtoString* pvKey = JSSymbols::primitiveValue(ctx);
        if (pvKey) {
            const proto::ProtoObject* pv = self->getAttribute(ctx, pvKey, false);
            if (pv && pv != PROTO_NONE && pv->isString(ctx)) return pv;
        }
    }
    signalNativeException(makeNativeError(ctx, "TypeError",
        "String.prototype.valueOf called on incompatible receiver"));
    return PROTO_NONE;
}

const proto::ProtoObject* stringToString(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{
    if (!requireStringThis(ctx, self)) return PROTO_NONE;
    if (self && self != PROTO_NONE && self->isString(ctx)) return self;
    // String wrapper object: extract __primitive_value__ and return it directly.
    {
        const proto::ProtoString* pvKey = JSSymbols::primitiveValue(ctx);
        if (pvKey) {
            const proto::ProtoObject* pv = self->getAttribute(ctx, pvKey, false);
            if (pv && pv != PROTO_NONE && pv->isString(ctx)) return pv;
        }
    }
    // §22.1.3.27: receiver must be a String value (primitive or
    // String wrapper). Plain objects, arrays, numbers, etc. throw
    // TypeError per ThisStringValue.
    signalNativeException(makeNativeError(ctx, "TypeError",
        "String.prototype.toString called on incompatible receiver"));
    return PROTO_NONE;
}

const proto::ProtoObject* stringCharAt(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!requireStringThis(ctx, self)) return PROTO_NONE;
    std::string s = objToStr(ctx, self);
    auto u16 = utf8ToUTF16(s);
    long long idx = getIntArg(ctx, args, 0, 0);
    if (idx < 0 || static_cast<size_t>(idx) >= u16.size())
        return ctx->fromUTF8String("");
    return ctx->fromUTF8String(
        utf16ToUTF8(u16, static_cast<size_t>(idx), static_cast<size_t>(idx) + 1).c_str());
}

const proto::ProtoObject* stringCharCodeAt(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!requireStringThis(ctx, self)) return PROTO_NONE;
    std::string s = objToStr(ctx, self);
    auto u16 = utf8ToUTF16(s);
    long long idx = getIntArg(ctx, args, 0, 0);
    if (idx < 0 || static_cast<size_t>(idx) >= u16.size())
        return ctx->fromDouble(std::numeric_limits<double>::quiet_NaN());
    return ctx->fromInteger(static_cast<long long>(u16[static_cast<size_t>(idx)]));
}

const proto::ProtoObject* stringCodePointAt(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!requireStringThis(ctx, self)) return PROTO_NONE;
    std::string s = objToStr(ctx, self);
    auto u16 = utf8ToUTF16(s);
    long long idx = getIntArg(ctx, args, 0, 0);
    if (idx < 0 || static_cast<size_t>(idx) >= u16.size())
        return PROTO_NONE;
    uint16_t h = u16[static_cast<size_t>(idx)];
    size_t next = static_cast<size_t>(idx) + 1;
    if (h >= 0xD800 && h <= 0xDBFF && next < u16.size() &&
            u16[next] >= 0xDC00 && u16[next] <= 0xDFFF) {
        uint32_t cp = 0x10000u + (static_cast<uint32_t>(h - 0xD800) << 10)
                                + (u16[next] - 0xDC00);
        return ctx->fromInteger(static_cast<long long>(cp));
    }
    return ctx->fromInteger(static_cast<long long>(h));
}

const proto::ProtoObject* stringAt(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!requireStringThis(ctx, self)) return PROTO_NONE;
    std::string s = objToStr(ctx, self);
    auto u16 = utf8ToUTF16(s);
    long long len = static_cast<long long>(u16.size());
    long long idx = getIntArg(ctx, args, 0, 0);
    if (idx < 0) idx += len;
    if (idx < 0 || idx >= len) return PROTO_NONE;
    return ctx->fromUTF8String(
        utf16ToUTF8(u16, static_cast<size_t>(idx), static_cast<size_t>(idx) + 1).c_str());
}

const proto::ProtoObject* stringConcat(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!requireStringThis(ctx, self)) return PROTO_NONE;
    std::string result = objToStr(ctx, self);
    unsigned long argc = args ? static_cast<unsigned long>(args->getSize(ctx)) : 0;
    for (unsigned long i = 0; i < argc; i++)
        result += objToStr(ctx, args->getAt(ctx, static_cast<int>(i)));
    return ctx->fromUTF8String(result.c_str());
}

const proto::ProtoObject* stringIndexOf(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!requireStringThis(ctx, self)) return PROTO_NONE;
    std::string s    = objToStr(ctx, self);
    // Spec §22.1.3.7: ToString on searchString; undefined coerces to
    // the literal "undefined". Pre-fix `"abc".indexOf()` returned 0
    // because the missing arg defaulted to "".
    std::string srch = getStrArgWithUndef(ctx, args, 0);
    auto su16 = utf8ToUTF16(s);
    auto se16 = utf8ToUTF16(srch);
    long long fromIdx = getIntArg(ctx, args, 1, 0);
    if (fromIdx < 0) fromIdx = 0;
    size_t pos = static_cast<size_t>(fromIdx);
    if (se16.empty()) {
        if (pos > su16.size()) return ctx->fromInteger(static_cast<long long>(su16.size()));
        return ctx->fromInteger(static_cast<long long>(pos));
    }
    if (pos + se16.size() > su16.size()) return ctx->fromInteger(-1LL);
    for (size_t i = pos; i + se16.size() <= su16.size(); i++) {
        if (std::equal(se16.begin(), se16.end(), su16.begin() + i))
            return ctx->fromInteger(static_cast<long long>(i));
    }
    return ctx->fromInteger(-1LL);
}

const proto::ProtoObject* stringLastIndexOf(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!requireStringThis(ctx, self)) return PROTO_NONE;
    std::string s    = objToStr(ctx, self);
    std::string srch = getStrArg(ctx, args, 0);
    auto su16 = utf8ToUTF16(s);
    auto se16 = utf8ToUTF16(srch);
    long long fromIdx = static_cast<long long>(su16.size());
    if (args && args->getSize(ctx) > 1) {
        const proto::ProtoObject* fa = args->getAt(ctx, 1);
        if (fa && fa != PROTO_NONE) {
            double d = fa->isDouble(ctx)  ? fa->asDouble(ctx)
                     : fa->isInteger(ctx) ? static_cast<double>(fa->asLong(ctx))
                     : 0.0;
            if (!std::isnan(d)) fromIdx = static_cast<long long>(d);
        }
    }
    if (fromIdx < 0) fromIdx = 0;
    if (se16.empty()) {
        size_t cap = static_cast<size_t>(
            std::min(fromIdx, static_cast<long long>(su16.size())));
        return ctx->fromInteger(static_cast<long long>(cap));
    }
    if (su16.size() < se16.size()) return ctx->fromInteger(-1LL);
    long long maxStart = static_cast<long long>(
        std::min(static_cast<size_t>(fromIdx), su16.size() - se16.size()));
    for (long long i = maxStart; i >= 0; i--) {
        if (std::equal(se16.begin(), se16.end(),
                       su16.begin() + static_cast<ptrdiff_t>(i)))
            return ctx->fromInteger(i);
    }
    return ctx->fromInteger(-1LL);
}

const proto::ProtoObject* stringSlice(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!requireStringThis(ctx, self)) return PROTO_NONE;
    std::string s = objToStr(ctx, self);
    auto u16 = utf8ToUTF16(s);
    long long len = static_cast<long long>(u16.size());
    long long start = 0, end = len;

    if (args && args->getSize(ctx) >= 1) {
        start = getIntArg(ctx, args, 0, 0);
        if (start < 0) start = std::max(len + start, 0LL);
        else           start = std::min(start, len);
    }
    if (args && args->getSize(ctx) >= 2) {
        const proto::ProtoObject* ea = args->getAt(ctx, 1);
        if (ea && ea != PROTO_NONE) {
            end = getIntArg(ctx, args, 1, len);
            if (end < 0) end = std::max(len + end, 0LL);
            else         end = std::min(end, len);
        }
    }
    if (end <= start) return ctx->fromUTF8String("");
    return ctx->fromUTF8String(
        utf16ToUTF8(u16, static_cast<size_t>(start), static_cast<size_t>(end)).c_str());
}

const proto::ProtoObject* stringSubstring(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!requireStringThis(ctx, self)) return PROTO_NONE;
    std::string s = objToStr(ctx, self);
    auto u16 = utf8ToUTF16(s);
    long long len = static_cast<long long>(u16.size());
    long long start = 0, end = len;

    if (args && args->getSize(ctx) >= 1) {
        start = getIntArg(ctx, args, 0, 0);   // NaN → 0
        start = std::max(0LL, std::min(start, len));
    }
    if (args && args->getSize(ctx) >= 2) {
        const proto::ProtoObject* ea = args->getAt(ctx, 1);
        if (ea && ea != PROTO_NONE) {
            end = getIntArg(ctx, args, 1, len);
            end = std::max(0LL, std::min(end, len));
        }
    }
    // substring swaps start/end if start > end (unlike slice)
    if (start > end) std::swap(start, end);
    return ctx->fromUTF8String(
        utf16ToUTF8(u16, static_cast<size_t>(start), static_cast<size_t>(end)).c_str());
}

/** Legacy substr(start, length) — not in the spec but widely used. */
const proto::ProtoObject* stringSubstr(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!requireStringThis(ctx, self)) return PROTO_NONE;
    std::string s = objToStr(ctx, self);
    auto u16 = utf8ToUTF16(s);
    long long len = static_cast<long long>(u16.size());
    long long start = getIntArg(ctx, args, 0, 0);
    if (start < 0) start = std::max(len + start, 0LL);
    else           start = std::min(start, len);

    long long length = len - start;
    if (args && args->getSize(ctx) >= 2) {
        const proto::ProtoObject* la = args->getAt(ctx, 1);
        if (la && la != PROTO_NONE) {
            length = getIntArg(ctx, args, 1, length);
            if (length < 0) length = 0;
        }
    }
    long long end = std::min(start + length, len);
    if (end <= start) return ctx->fromUTF8String("");
    return ctx->fromUTF8String(
        utf16ToUTF8(u16, static_cast<size_t>(start), static_cast<size_t>(end)).c_str());
}

const proto::ProtoObject* stringToLowerCase(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{
    if (!requireStringThis(ctx, self)) return PROTO_NONE;
    std::string s = objToStr(ctx, self);
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ctx->fromUTF8String(s.c_str());
}

const proto::ProtoObject* stringToUpperCase(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{
    if (!requireStringThis(ctx, self)) return PROTO_NONE;
    std::string s = objToStr(ctx, self);
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::toupper(c)); });
    return ctx->fromUTF8String(s.c_str());
}

const proto::ProtoObject* stringRepeat(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!requireStringThis(ctx, self)) return PROTO_NONE;
    std::string s = objToStr(ctx, self);
    // ECMA-262 §22.1.3.16: ToInteger(count); negative or +Infinity
    // throws RangeError. The previous implementation silently treated
    // negative as "", and capped Infinity to a runaway 65536-character
    // allocation.
    double dcount = 0.0;
    bool sawArg = args && args->getSize(ctx) > 0;
    if (sawArg) {
        const proto::ProtoObject* a = args->getAt(ctx, 0);
        if (a && a != PROTO_NONE) {
            if (a->isInteger(ctx))       dcount = static_cast<double>(a->asLong(ctx));
            else if (a->isDouble(ctx) || a->isFloat(ctx)) dcount = a->asDouble(ctx);
            else if (a == PROTO_TRUE)    dcount = 1.0;
            else if (a == PROTO_FALSE)   dcount = 0.0;
            else if (a->isString(ctx)) {
                const proto::ProtoObject* num = jsToNumber(ctx, a);
                if (num && (num->isInteger(ctx))) dcount = static_cast<double>(num->asLong(ctx));
                else if (num && (num->isDouble(ctx) || num->isFloat(ctx))) dcount = num->asDouble(ctx);
            }
        }
    }
    // ToInteger: NaN → 0, truncate toward zero.
    if (std::isnan(dcount)) dcount = 0.0;
    if (dcount < 0.0 || std::isinf(dcount)) {
        signalNativeException(makeNativeError(ctx, "RangeError",
            "Invalid count value"));
        return PROTO_NONE;
    }
    long long count = static_cast<long long>(dcount);
    if (count == 0 || s.empty()) return ctx->fromUTF8String("");
    // Cap real allocation; legitimate uses fall well below this.
    constexpr size_t kRepeatCap = 1u << 24; // 16 MiB worth of repeats
    if (s.size() && static_cast<size_t>(count) > kRepeatCap / s.size()) {
        signalNativeException(makeNativeError(ctx, "RangeError",
            "Invalid string length"));
        return PROTO_NONE;
    }
    std::string result;
    result.reserve(s.size() * static_cast<size_t>(count));
    for (long long i = 0; i < count; i++) result += s;
    return ctx->fromUTF8String(result.c_str());
}

const proto::ProtoObject* stringLocaleCompare(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!requireStringThis(ctx, self)) return PROTO_NONE;
    std::string a = objToStr(ctx, self);
    std::string b = getStrArg(ctx, args, 0);
    int cmp = a.compare(b);
    return ctx->fromInteger(cmp < 0 ? -1LL : cmp > 0 ? 1LL : 0LL);
}

// ECMA-262 §22.1.3.30 String.prototype.toLocaleString — without ICU
// we return the same String value. The wrapper ensures
// `"abc".toLocaleString()` doesn't throw and matches the expected
// shape (length === 0, name === "toLocaleString").
const proto::ProtoObject* stringToLocaleString(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{
    if (!requireStringThis(ctx, self)) return PROTO_NONE;
    if (self->isString(ctx)) return self;
    // String wrapper: unwrap.
    const proto::ProtoString* pvKey = JSSymbols::primitiveValue(ctx);
    if (pvKey) {
        const proto::ProtoObject* pv = self->getAttribute(ctx, pvKey, false);
        if (pv && pv != PROTO_NONE && pv->isString(ctx)) return pv;
    }
    // Fall back to ToString.
    std::string s = objToStr(ctx, self);
    return ctx->fromUTF8String(s.c_str());
}

// ECMA-262 §22.1.3.32 WhiteSpace + LineTerminator: covers ASCII
// space/tab/newline/CR/formfeed/vtab AND the higher Unicode
// whitespace classes including NBSP (U+00A0), BOM (U+FEFF),
// IDEOGRAPHIC SPACE (U+3000), and U+2028 / U+2029. Returns the
// width consumed when the position points to a whitespace code
// point; 0 otherwise.
static size_t jsWhitespaceWidth(const std::string& s, size_t pos) {
    if (pos >= s.size()) return 0;
    unsigned char c = static_cast<unsigned char>(s[pos]);
    if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v') return 1;
    if (pos + 1 < s.size() && c == 0xC2 &&
        static_cast<unsigned char>(s[pos + 1]) == 0xA0) return 2; // NBSP
    if (pos + 2 < s.size()) {
        unsigned char b1 = static_cast<unsigned char>(s[pos + 1]);
        unsigned char b2 = static_cast<unsigned char>(s[pos + 2]);
        if (c == 0xE1 && b1 == 0x9A && b2 == 0x80) return 3; // U+1680
        if (c == 0xE2 && b1 == 0x80 && (b2 >= 0x80 && b2 <= 0x8A)) return 3; // U+2000-200A
        if (c == 0xE2 && b1 == 0x80 && (b2 == 0xA8 || b2 == 0xA9 || b2 == 0xAF)) return 3; // U+2028/2029/202F
        if (c == 0xE2 && b1 == 0x81 && b2 == 0x9F) return 3; // U+205F
        if (c == 0xE3 && b1 == 0x80 && b2 == 0x80) return 3; // U+3000
        if (c == 0xEF && b1 == 0xBB && b2 == 0xBF) return 3; // U+FEFF (BOM)
    }
    return 0;
}

// Trim backwards: find the start of any whitespace code point
// ending at `hi`. Returns the new `hi` after consuming one
// whitespace code point if present.
static size_t trimOneCharBackwards(const std::string& s, size_t hi) {
    if (hi == 0) return 0;
    // ASCII fast path
    unsigned char last = static_cast<unsigned char>(s[hi - 1]);
    if (last == ' ' || last == '\t' || last == '\n' || last == '\r' || last == '\f' || last == '\v')
        return hi - 1;
    // 2-byte / 3-byte UTF-8: scan back up to 3 bytes.
    for (size_t w = 2; w <= 3 && hi >= w; ++w) {
        size_t consumed = jsWhitespaceWidth(s, hi - w);
        if (consumed == w) return hi - w;
    }
    return hi; // no trim
}

const proto::ProtoObject* stringTrim(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{
    if (!requireStringThis(ctx, self)) return PROTO_NONE;
    std::string s = objToStr(ctx, self);
    size_t lo = 0, hi = s.size();
    while (lo < hi) {
        size_t w = jsWhitespaceWidth(s, lo);
        if (w == 0) break;
        lo += w;
    }
    while (hi > lo) {
        size_t newHi = trimOneCharBackwards(s, hi);
        if (newHi == hi) break;
        hi = newHi;
    }
    return ctx->fromUTF8String(s.substr(lo, hi - lo).c_str());
}

const proto::ProtoObject* stringTrimStart(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{
    if (!requireStringThis(ctx, self)) return PROTO_NONE;
    std::string s = objToStr(ctx, self);
    size_t lo = 0;
    while (lo < s.size()) {
        size_t w = jsWhitespaceWidth(s, lo);
        if (w == 0) break;
        lo += w;
    }
    return ctx->fromUTF8String(s.substr(lo).c_str());
}

const proto::ProtoObject* stringTrimEnd(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{
    if (!requireStringThis(ctx, self)) return PROTO_NONE;
    std::string s = objToStr(ctx, self);
    size_t hi = s.size();
    while (hi > 0) {
        size_t newHi = trimOneCharBackwards(s, hi);
        if (newHi == hi) break;
        hi = newHi;
    }
    return ctx->fromUTF8String(s.substr(0, hi).c_str());
}

const proto::ProtoObject* stringStartsWith(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!requireStringThis(ctx, self)) return PROTO_NONE;
    // Spec §22.1.3.22 step 3: if searchString is a RegExp, throw
    // TypeError. The regex must be rejected before ToString coerces
    // it into "/pattern/" — silent acceptance breaks the rule that
    // .startsWith/.endsWith/.includes do not interpret regex syntax.
    if (args && args->getSize(ctx) > 0 && isRegExp(ctx, args->getAt(ctx, 0))) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "First argument to String.prototype.startsWith must not be a regular expression"));
        return PROTO_NONE;
    }
    std::string s    = objToStr(ctx, self);
    std::string srch = getStrArgWithUndef(ctx, args, 0);
    auto su16 = utf8ToUTF16(s);
    auto se16 = utf8ToUTF16(srch);
    long long pos = getIntArg(ctx, args, 1, 0);
    if (pos < 0) pos = 0;
    if (static_cast<size_t>(pos) + se16.size() > su16.size()) return PROTO_FALSE;
    for (size_t i = 0; i < se16.size(); i++)
        if (su16[static_cast<size_t>(pos) + i] != se16[i]) return PROTO_FALSE;
    return PROTO_TRUE;
}

const proto::ProtoObject* stringEndsWith(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!requireStringThis(ctx, self)) return PROTO_NONE;
    if (args && args->getSize(ctx) > 0 && isRegExp(ctx, args->getAt(ctx, 0))) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "First argument to String.prototype.endsWith must not be a regular expression"));
        return PROTO_NONE;
    }
    std::string s    = objToStr(ctx, self);
    std::string srch = getStrArgWithUndef(ctx, args, 0);
    auto su16 = utf8ToUTF16(s);
    auto se16 = utf8ToUTF16(srch);
    long long endPos = static_cast<long long>(su16.size());
    if (args && args->getSize(ctx) > 1) {
        const proto::ProtoObject* ep = args->getAt(ctx, 1);
        if (ep && ep != PROTO_NONE) {
            endPos = getIntArg(ctx, args, 1, endPos);
            endPos = std::max(0LL, std::min(endPos, static_cast<long long>(su16.size())));
        }
    }
    if (static_cast<long long>(se16.size()) > endPos) return PROTO_FALSE;
    long long start = endPos - static_cast<long long>(se16.size());
    for (size_t i = 0; i < se16.size(); i++)
        if (su16[static_cast<size_t>(start) + i] != se16[i]) return PROTO_FALSE;
    return PROTO_TRUE;
}

const proto::ProtoObject* stringIncludes(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!requireStringThis(ctx, self)) return PROTO_NONE;
    if (args && args->getSize(ctx) > 0 && isRegExp(ctx, args->getAt(ctx, 0))) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "First argument to String.prototype.includes must not be a regular expression"));
        return PROTO_NONE;
    }
    std::string s    = objToStr(ctx, self);
    std::string srch = getStrArgWithUndef(ctx, args, 0);
    auto su16 = utf8ToUTF16(s);
    auto se16 = utf8ToUTF16(srch);
    long long fromIdx = getIntArg(ctx, args, 1, 0);
    if (fromIdx < 0) fromIdx = 0;
    if (se16.empty()) return PROTO_TRUE;
    if (static_cast<size_t>(fromIdx) + se16.size() > su16.size()) return PROTO_FALSE;
    for (size_t i = static_cast<size_t>(fromIdx);
         i + se16.size() <= su16.size(); i++) {
        if (std::equal(se16.begin(), se16.end(), su16.begin() + i))
            return PROTO_TRUE;
    }
    return PROTO_FALSE;
}

const proto::ProtoObject* stringIsWellFormed(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*,
    const proto::ProtoSparseList*)
{
    if (!requireStringThis(ctx, self)) return PROTO_NONE;
    std::string s = objToStr(ctx, self);
    auto u16 = utf8ToUTF16(s);
    for (size_t i = 0; i < u16.size(); i++) {
        uint16_t h = u16[i];
        if (h >= 0xD800 && h <= 0xDBFF) {
            if (i + 1 < u16.size() && u16[i+1] >= 0xDC00 && u16[i+1] <= 0xDFFF) {
                i++; // Valid surrogate pair
            } else {
                return PROTO_FALSE; // Unpaired high surrogate
            }
        } else if (h >= 0xDC00 && h <= 0xDFFF) {
            return PROTO_FALSE; // Unpaired low surrogate
        }
    }
    return PROTO_TRUE;
}

// ECMA-262 §22.1.3.13/14 — StringPad. Apply ToLength to maxLength;
// fall back to the receiver's length when fillString is "". Reject
// excessive allocations (V8/Node throw RangeError when the requested
// length is large enough that the allocation would fail).
static const proto::ProtoObject* stringPadCommon(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ProtoList* args, bool atStart)
{
    if (!requireStringThis(ctx, self)) return PROTO_NONE;
    std::string s = objToStr(ctx, self);
    auto su16 = utf8ToUTF16(s);
    // ToLength on the target. NaN → 0. +Infinity is rejected because
    // the resulting allocation would always fail; matches V8/JSC.
    double dTarget = 0.0;
    if (args && args->getSize(ctx) > 0) {
        const proto::ProtoObject* a = args->getAt(ctx, 0);
        if (a && a != PROTO_NONE) {
            const proto::ProtoObject* num = jsToNumber(ctx, a);
            if (num) {
                if (num->isInteger(ctx)) dTarget = static_cast<double>(num->asLong(ctx));
                else if (num->isDouble(ctx) || num->isFloat(ctx)) dTarget = num->asDouble(ctx);
            }
        }
    }
    if (std::isnan(dTarget)) dTarget = 0.0;
    if (std::isinf(dTarget) && dTarget > 0) {
        signalNativeException(makeNativeError(ctx, "RangeError",
            "Invalid string length"));
        return PROTO_NONE;
    }
    if (dTarget < 0) dTarget = 0;
    long long targetLen = static_cast<long long>(dTarget);
    if (targetLen <= static_cast<long long>(su16.size()))
        return ctx->fromUTF8String(s.c_str());
    std::string padStr = " ";
    if (args && args->getSize(ctx) > 1) {
        const proto::ProtoObject* pa = args->getAt(ctx, 1);
        if (pa && pa != PROTO_NONE) padStr = objToStr(ctx, pa);
    }
    auto padU16 = utf8ToUTF16(padStr);
    if (padU16.empty()) return ctx->fromUTF8String(s.c_str());
    long long needed = targetLen - static_cast<long long>(su16.size());
    // Cap on the output to avoid runaway allocations; legitimate
    // formatting fits well below.
    constexpr long long kPadCap = (1LL << 24); // ~16M code units
    if (needed > kPadCap) {
        signalNativeException(makeNativeError(ctx, "RangeError",
            "Invalid string length"));
        return PROTO_NONE;
    }
    if (atStart) {
        std::vector<uint16_t> result;
        result.reserve(static_cast<size_t>(targetLen));
        for (long long i = 0; i < needed; i++)
            result.push_back(padU16[static_cast<size_t>(i) % padU16.size()]);
        result.insert(result.end(), su16.begin(), su16.end());
        return ctx->fromUTF8String(utf16ToUTF8(result).c_str());
    }
    std::vector<uint16_t> result(su16);
    result.reserve(static_cast<size_t>(targetLen));
    for (long long i = 0; i < needed; i++)
        result.push_back(padU16[static_cast<size_t>(i) % padU16.size()]);
    return ctx->fromUTF8String(utf16ToUTF8(result).c_str());
}

const proto::ProtoObject* stringPadStart(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    return stringPadCommon(ctx, self, args, /*atStart=*/true);
}

const proto::ProtoObject* stringPadEnd(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    return stringPadCommon(ctx, self, args, /*atStart=*/false);
}

static std::string applyStringReplacement(const std::string& s, const std::string& pat, const std::string& rep, size_t pos) {
    std::string result;
    for (size_t i = 0; i < rep.size(); i++) {
        if (rep[i] == '$' && i + 1 < rep.size()) {
            char next = rep[i + 1];
            if (next == '$') {
                result += '$';
                i++;
            } else if (next == '&') {
                result += pat;
                i++;
            } else if (next == '`') {
                result += s.substr(0, pos);
                i++;
            } else if (next == '\'') {
                result += s.substr(pos + pat.size());
                i++;
            } else if (std::isdigit(next)) {
                result += '$';
            } else {
                result += '$';
            }
        } else {
            result += rep[i];
        }
    }
    return result;
}

/** Helper to detect if an object is a RegExp by looking for the Symbol.match surrogate. */
static bool isRegExp(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    if (!obj || obj == PROTO_NONE || obj->isString(ctx) || obj->isInteger(ctx) || obj->isDouble(ctx)) return false;
    const proto::ProtoString* matchKey = JSSymbols::symbolMatch(ctx);
    if (!matchKey) return false;
    const proto::ProtoObject* m = obj->getAttribute(ctx, matchKey, true);
    return m && m != PROTO_NONE;
}

const proto::ProtoObject* stringMatch(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink* parent, const proto::ProtoList* args,
    const proto::ProtoSparseList* sparse)
{
    if (!requireStringThis(ctx, self)) return PROTO_NONE;
    if (!args || args->getSize(ctx) == 0) {
        // Spec: match(undefined) wraps via RegExpCreate(undefined) which
        // becomes /(?:)/, matching the empty string at position 0.
        const proto::ProtoObject* arr = createNewArray(ctx, nullptr);
        const proto::ProtoString* isArrKey = JSSymbols::isArray(ctx);
        if (isArrKey) arr = arr->setAttribute(ctx, isArrKey, PROTO_TRUE);
        const proto::ProtoList* els = ctx->newList();
        els = els->appendLast(ctx, ctx->fromUTF8String(""));
        setArrayElements(ctx, arr, els);
        const proto::ProtoString* lenKey = JSSymbols::length(ctx);
        if (lenKey) arr = arr->setAttribute(ctx, lenKey, ctx->fromInteger(1LL));
        return arr;
    }
    const proto::ProtoObject* pattern = args->getAt(ctx, 0);
    if (isRegExp(ctx, pattern)) {
        const proto::ProtoString* matchKey = JSSymbols::symbolMatch(ctx);
        const proto::ProtoObject* matchFn = pattern->getAttribute(ctx, matchKey, true);
        if (matchFn && matchFn != PROTO_NONE) {
            // Call pattern[Symbol.match](self)
            const proto::ProtoList* newArgs = ctx->newList();
            newArgs = newArgs->appendLast(ctx, self);
            return pattern->call(ctx, nullptr, matchKey, pattern, newArgs, nullptr);
        }
    }
    // Non-regex pattern: spec wraps via RegExpCreate(pattern). For the
    // string case the result is the literal pattern matched at the
    // first occurrence — array of [match] with .index and .input.
    // Without regex flags (no /g), only the first match is returned.
    std::string s   = objToStr(ctx, self);
    std::string pat;
    if (pattern && pattern != PROTO_NONE) {
        if (pattern == getNullSentinel())            pat = "null";
        else if (pattern == getUndefinedSentinel())  pat = "";
        else                                          pat = objToStr(ctx, pattern);
    }
    const proto::ProtoObject* result = createNewArray(ctx, nullptr);
    const proto::ProtoString* isArrKey = JSSymbols::isArray(ctx);
    if (isArrKey) result = result->setAttribute(ctx, isArrKey, PROTO_TRUE);
    const proto::ProtoString* lenKey = JSSymbols::length(ctx);
    if (pat.empty()) {
        const proto::ProtoList* els = ctx->newList();
        els = els->appendLast(ctx, ctx->fromUTF8String(""));
        setArrayElements(ctx, result, els);
        if (lenKey) result = result->setAttribute(ctx, lenKey, ctx->fromInteger(1LL));
        return result;
    }
    size_t pos = s.find(pat);
    if (pos == std::string::npos) {
        // Spec: when there's no match and no /g flag, return null.
        return getNullSentinel();
    }
    const proto::ProtoList* els = ctx->newList();
    els = els->appendLast(ctx, ctx->fromUTF8String(pat.c_str()));
    setArrayElements(ctx, result, els);
    if (lenKey) result = result->setAttribute(ctx, lenKey, ctx->fromInteger(1LL));
    // Attach .index and .input per spec.
    {
        const proto::ProtoObject* ixObj = ctx->fromUTF8String("index");
        const proto::ProtoString* ixK = ixObj ? ixObj->asString(ctx) : nullptr;
        if (ixK) result = result->setAttribute(ctx, ixK,
            ctx->fromInteger(static_cast<long long>(pos)));
        const proto::ProtoObject* inObj = ctx->fromUTF8String("input");
        const proto::ProtoString* inK = inObj ? inObj->asString(ctx) : nullptr;
        if (inK) result = result->setAttribute(ctx, inK, ctx->fromUTF8String(s.c_str()));
    }
    return result;
}

const proto::ProtoObject* stringSearch(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink* parent, const proto::ProtoList* args,
    const proto::ProtoSparseList* sparse)
{
    if (!requireStringThis(ctx, self)) return PROTO_NONE;
    if (!args || args->getSize(ctx) == 0) return ctx->fromInteger(0);
    const proto::ProtoObject* pattern = args->getAt(ctx, 0);
    if (isRegExp(ctx, pattern)) {
        const proto::ProtoString* searchKey = JSSymbols::symbolSearch(ctx);
        const proto::ProtoObject* searchFn = pattern->getAttribute(ctx, searchKey, true);
        if (searchFn && searchFn != PROTO_NONE) {
            // Call pattern[Symbol.search](self)
            const proto::ProtoList* newArgs = ctx->newList();
            newArgs = newArgs->appendLast(ctx, self);
            return pattern->call(ctx, nullptr, searchKey, pattern, newArgs, nullptr);
        }
    }
    // Spec §22.1.3.20: when pattern isn't a regex object it's still
    // wrapped via RegExpCreate(pattern, undefined). The cheapest
    // equivalent for a string pattern is std::string::find. Pre-fix
    // we returned -1 unconditionally for non-regex patterns, so
    // `"abc".search("b")` was -1 instead of 1.
    const proto::ProtoObject* patStr = pattern;
    if (patStr && patStr != PROTO_NONE && !patStr->isString(ctx)) {
        // Allow null/undefined to fall through as "undefined" search.
        if (patStr == getNullSentinel())             patStr = ctx->fromUTF8String("null");
        else if (patStr == getUndefinedSentinel())   patStr = nullptr;
        else if (patStr->isInteger(ctx))             patStr = ctx->fromUTF8String(std::to_string(patStr->asLong(ctx)).c_str());
    }
    if (!patStr) return ctx->fromInteger(0LL); // empty match at position 0
    std::string s   = objToStr(ctx, self);
    std::string pat = objToStr(ctx, patStr);
    if (pat.empty()) return ctx->fromInteger(0LL);
    size_t pos = s.find(pat);
    return ctx->fromInteger(pos == std::string::npos
        ? -1LL : static_cast<long long>(pos));
}

/** replace(pattern, replacement) — handles string patterns only.
 *  Regex patterns return PROTO_NONE to preserve vacuous-pass behaviour. */
const proto::ProtoObject* stringReplace(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!requireStringThis(ctx, self)) return PROTO_NONE;
    if (!args || args->getSize(ctx) < 2) return self;
    const proto::ProtoObject* pattern = args->getAt(ctx, 0);
    if (!pattern || pattern == PROTO_NONE) return ctx->fromUTF8String(objToStr(ctx, self).c_str());

    if (isRegExp(ctx, pattern)) {
        const proto::ProtoString* replaceKey = JSSymbols::symbolReplace(ctx);
        const proto::ProtoObject* replaceFn = pattern->getAttribute(ctx, replaceKey, true);
        if (replaceFn && replaceFn != PROTO_NONE) {
            // Call pattern[Symbol.replace](self, replacement)
            const proto::ProtoList* newArgs = ctx->newList();
            newArgs = newArgs->appendLast(ctx, self);
            newArgs = newArgs->appendLast(ctx, args->getAt(ctx, 1));
            return pattern->call(ctx, nullptr, replaceKey, pattern, newArgs, nullptr);
        }
    }

    // Per ECMA-262 §22.1.3.18 step 5: ToString(searchValue) — non-string
    // non-regex patterns coerce, so .replace(undefined, …) searches
    // for the literal string 'undefined', .replace(null, …) for 'null'.
    // Pre-fix any non-string returned PROTO_NONE, which surfaced as
    // 'undefined' in user code (the whole result, not a no-match).
    std::string s   = objToStr(ctx, self);
    std::string pat = objToStr(ctx, pattern);

    const proto::ProtoObject* repObj = args->getAt(ctx, 1);
    // Spec §22.1.3.18 step 7: if replacement is callable, call it with
    // (match, offset, string) and use the result. Pre-fix non-string
    // replacements (including functions) returned PROTO_NONE.
    auto isCallable = [&](const proto::ProtoObject* fn) -> bool {
        if (!fn || fn == PROTO_NONE) return false;
        if (fn->isMethod(ctx)) return true;
        const proto::ProtoString* bcKey = JSSymbols::bytecodeId(ctx);
        if (bcKey && fn->getAttribute(ctx, bcKey, false) != PROTO_NONE) return true;
        const proto::ProtoString* nfKey = JSSymbols::nativeFn(ctx);
        if (nfKey && fn->getAttribute(ctx, nfKey, false) != PROTO_NONE) return true;
        return false;
    };
    bool repCallable = isCallable(repObj);
    if (!repCallable && repObj && !repObj->isString(ctx)
        && !repObj->isInteger(ctx) && !repObj->isDouble(ctx)) {
        // Coerce primitives via objToStr (covers null/boolean/etc.).
    }

    auto callReplacement = [&](const std::string& matched, size_t offset) -> std::string {
        const proto::ProtoList* callArgs = ctx->newList();
        callArgs = callArgs->appendLast(ctx, ctx->fromUTF8String(matched.c_str()));
        callArgs = callArgs->appendLast(ctx, ctx->fromInteger(static_cast<long long>(offset)));
        callArgs = callArgs->appendLast(ctx, ctx->fromUTF8String(s.c_str()));
        const proto::ProtoObject* res = callJSFunction(ctx, repObj, PROTO_NONE, callArgs);
        return objToStr(ctx, res);
    };

    if (repCallable) {
        if (pat.empty()) {
            // Empty pattern: replace at position 0 with cb("", 0, s).
            return ctx->fromUTF8String((callReplacement("", 0) + s).c_str());
        }
        size_t pos = s.find(pat);
        if (pos == std::string::npos) return ctx->fromUTF8String(s.c_str());
        return ctx->fromUTF8String(
            (s.substr(0, pos) + callReplacement(pat, pos) + s.substr(pos + pat.size())).c_str());
    }

    std::string rep = objToStr(ctx, repObj);
    if (pat.empty()) return ctx->fromUTF8String((applyStringReplacement(s, pat, rep, 0) + s).c_str());
    size_t pos = s.find(pat);
    if (pos == std::string::npos) return ctx->fromUTF8String(s.c_str());
    return ctx->fromUTF8String(
        (s.substr(0, pos) + applyStringReplacement(s, pat, rep, pos) + s.substr(pos + pat.size())).c_str());
}

/** replaceAll(pattern, replacement) — string patterns only. */
const proto::ProtoObject* stringReplaceAll(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!requireStringThis(ctx, self)) return PROTO_NONE;
    if (!args || args->getSize(ctx) < 2) return self;
    const proto::ProtoObject* pattern = args->getAt(ctx, 0);
    if (!pattern || pattern == PROTO_NONE) return ctx->fromUTF8String(objToStr(ctx, self).c_str());
    if (!pattern->isString(ctx)) return PROTO_NONE;

    std::string s   = objToStr(ctx, self);
    std::string pat = objToStr(ctx, pattern);

    const proto::ProtoObject* repObj = args->getAt(ctx, 1);
    // Spec §22.1.3.20 step 8: callable replacement gets (match, offset, string).
    auto isCallable = [&](const proto::ProtoObject* fn) -> bool {
        if (!fn || fn == PROTO_NONE) return false;
        if (fn->isMethod(ctx)) return true;
        const proto::ProtoString* bcKey = JSSymbols::bytecodeId(ctx);
        if (bcKey && fn->getAttribute(ctx, bcKey, false) != PROTO_NONE) return true;
        const proto::ProtoString* nfKey = JSSymbols::nativeFn(ctx);
        if (nfKey && fn->getAttribute(ctx, nfKey, false) != PROTO_NONE) return true;
        return false;
    };
    bool repCallable = isCallable(repObj);
    auto callReplacement = [&](const std::string& matched, size_t offset) -> std::string {
        const proto::ProtoList* callArgs = ctx->newList();
        callArgs = callArgs->appendLast(ctx, ctx->fromUTF8String(matched.c_str()));
        callArgs = callArgs->appendLast(ctx, ctx->fromInteger(static_cast<long long>(offset)));
        callArgs = callArgs->appendLast(ctx, ctx->fromUTF8String(s.c_str()));
        const proto::ProtoObject* res = callJSFunction(ctx, repObj, PROTO_NONE, callArgs);
        return objToStr(ctx, res);
    };

    if (pat.empty()) {
        std::string result;
        if (repCallable) {
            result += callReplacement("", 0);
            for (size_t i = 0; i < s.size(); i++) {
                result += s[i];
                result += callReplacement("", i + 1);
            }
        } else {
            std::string rep = objToStr(ctx, repObj);
            result += applyStringReplacement(s, pat, rep, 0);
            for (size_t i = 0; i < s.size(); i++) {
                result += s[i];
                result += applyStringReplacement(s, pat, rep, i + 1);
            }
        }
        return ctx->fromUTF8String(result.c_str());
    }

    std::string result;
    size_t lastPos = 0;
    size_t pos = s.find(pat, lastPos);
    while (pos != std::string::npos) {
        result += s.substr(lastPos, pos - lastPos);
        if (repCallable) {
            result += callReplacement(pat, pos);
        } else {
            std::string rep = objToStr(ctx, repObj);
            result += applyStringReplacement(s, pat, rep, pos);
        }
        lastPos = pos + pat.size();
        pos = s.find(pat, lastPos);
    }
    result += s.substr(lastPos);
    return ctx->fromUTF8String(result.c_str());
}

const proto::ProtoObject* stringMatchAll(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!requireStringThis(ctx, self)) return PROTO_NONE;
    return PROTO_NONE;
}

/** split(separator, limit) — string separator only; regex falls through to PROTO_NONE */
const proto::ProtoObject* stringSplit(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!requireStringThis(ctx, self)) return PROTO_NONE;
    std::string s = objToStr(ctx, self);
    // Result array — use createNewArray so that [] prototype methods (join, forEach, etc.) are inherited.
    const proto::ProtoObject* result = createNewArray(ctx, nullptr);
    const proto::ProtoString* lenKey = JSSymbols::length(ctx);
    // Native __elements__ storage so OP_get_array_el / .map / .filter /
    // JSON.stringify see entries (pre-fix split wrote indexed-attribute
    // only, producing an empty-looking array via __elements__).
    proto::ProtoContext::CriticalSection splitCs(ctx);
    const proto::ProtoList* els = ctx->newList();

    // Determine limit. Spec §22.1.3.21 step 8-9: limit = ToUint32(limit)
    // (so undefined → 2^32-1, false → 0, true → 1, NaN → 0, '0' → 0, etc.).
    unsigned long long limit = static_cast<unsigned long long>(std::numeric_limits<uint32_t>::max());
    if (args && args->getSize(ctx) > 1) {
        const proto::ProtoObject* lim = args->getAt(ctx, 1);
        if (lim && lim != PROTO_NONE && lim != getUndefinedSentinel()) {
            const proto::ProtoObject* n = jsToNumber(ctx, lim);
            if (hasCallException()) return PROTO_NONE;
            double d = 0.0;
            if (n && n != PROTO_NONE) {
                if (n->isInteger(ctx)) d = static_cast<double>(n->asLong(ctx));
                else if (n->isDouble(ctx) || n->isFloat(ctx)) d = n->asDouble(ctx);
            }
            // ToUint32: NaN/Inf -> 0, otherwise mod 2^32.
            if (std::isnan(d) || std::isinf(d)) limit = 0;
            else {
                double trunc = std::trunc(d);
                if (trunc < 0) trunc += 4294967296.0;
                trunc = std::fmod(trunc, 4294967296.0);
                if (trunc < 0) trunc += 4294967296.0;
                limit = static_cast<unsigned long long>(trunc);
            }
        }
    }

    if (limit == 0) {
        protojs::setArrayElements(ctx, result, els);
        if (lenKey) result = result->setAttribute(ctx, lenKey, ctx->fromInteger(0));
        return result;
    }

    // No separator argument (or undefined): return array with entire string
    if (!args || args->getSize(ctx) == 0) {
        els = els->appendLast(ctx, ctx->fromUTF8String(s.c_str()));
        protojs::setArrayElements(ctx, result, els);
        if (lenKey) result = result->setAttribute(ctx, lenKey, ctx->fromInteger(1));
        return result;
    }

    const proto::ProtoObject* sepArg = args->getAt(ctx, 0);
    // Undefined separator: return array with entire string
    if (!sepArg || sepArg == PROTO_NONE) {
        els = els->appendLast(ctx, ctx->fromUTF8String(s.c_str()));
        protojs::setArrayElements(ctx, result, els);
        if (lenKey) result = result->setAttribute(ctx, lenKey, ctx->fromInteger(1));
        return result;
    }

    if (isRegExp(ctx, sepArg)) {
        const proto::ProtoString* splitKey = JSSymbols::symbolSplit(ctx);
        const proto::ProtoObject* splitFn = sepArg->getAttribute(ctx, splitKey, true);
        if (splitFn && splitFn != PROTO_NONE) {
            // Call pattern[Symbol.split](self, limit)
            const proto::ProtoList* newArgs = ctx->newList();
            newArgs = newArgs->appendLast(ctx, self);
            if (args->getSize(ctx) > 1) {
                newArgs = newArgs->appendLast(ctx, args->getAt(ctx, 1));
            }
            return sepArg->call(ctx, nullptr, splitKey, sepArg, newArgs, nullptr);
        }
    }

    // Non-regexp separator: coerce to string via ToString (ECMAScript step 8).
    // objToStr handles null→"null", numbers, booleans, etc.
    std::string sep = objToStr(ctx, sepArg);
    auto su16 = utf8ToUTF16(s);
    auto se16 = utf8ToUTF16(sep);

    unsigned long long count = 0;

    // Empty separator: split into individual UTF-16 code units (JS char-by-char)
    if (se16.empty()) {
        for (size_t i = 0; i < su16.size() && count < limit; i++) {
            std::string ch = utf16ToUTF8(su16, i, i + 1);
            els = els->appendLast(ctx, ctx->fromUTF8String(ch.c_str()));
            count++;
        }
        protojs::setArrayElements(ctx, result, els);
        if (lenKey) result = result->setAttribute(ctx, lenKey, ctx->fromInteger(static_cast<long long>(count)));
        return result;
    }

    // General case: find sep occurrences
    size_t pos = 0;
    while (pos <= su16.size() && count < limit) {
        // Search for sep starting at pos
        size_t found = su16.size(); // default: no match → take rest of string
        for (size_t i = pos; i + se16.size() <= su16.size(); i++) {
            if (std::equal(se16.begin(), se16.end(), su16.begin() + i)) {
                found = i;
                break;
            }
        }
        // Slice [pos, found) as a segment
        std::string segment = utf16ToUTF8(su16, pos, found);
        els = els->appendLast(ctx, ctx->fromUTF8String(segment.c_str()));
        count++;
        if (found == su16.size()) break; // no more separators
        pos = found + se16.size();
    }

    protojs::setArrayElements(ctx, result, els);
    if (lenKey) result = result->setAttribute(ctx, lenKey, ctx->fromInteger(static_cast<long long>(count)));
    return result;
}

/** normalize(form) — NFC/NFD/NFKC/NFKD. Without ICU we only support NFC (identity for ASCII). */
const proto::ProtoObject* stringNormalize(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!requireStringThis(ctx, self)) return PROTO_NONE;
    // ECMA-262 §22.1.3.14 step 4: form must be one of "NFC", "NFD",
    // "NFKC", "NFKD", or undefined (defaults to "NFC"). Anything else
    // throws RangeError.
    if (args && args->getSize(ctx) > 0) {
        const proto::ProtoObject* formArg = args->getAt(ctx, 0);
        if (formArg && formArg != PROTO_NONE && formArg != getUndefinedSentinel()) {
            std::string form = objToStr(ctx, formArg);
            if (form != "NFC" && form != "NFD" && form != "NFKC" && form != "NFKD") {
                signalNativeException(makeNativeError(ctx, "RangeError",
                    "Invalid normalization form"));
                return PROTO_NONE;
            }
        }
    }
    // Without ICU: return string as-is (identity for NFC on ASCII/Latin-1).
    std::string s = objToStr(ctx, self);
    return ctx->fromUTF8String(s.c_str());
}

// ---------------------------------------------------------------------------
// String static methods (registered on the String constructor object)
// ---------------------------------------------------------------------------

/** String.fromCharCode(...codes) */
const proto::ProtoObject* stringFromCharCode(
    proto::ProtoContext* ctx, const proto::ProtoObject* /*self*/,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    std::vector<uint16_t> units;
    unsigned long argc = args ? static_cast<unsigned long>(args->getSize(ctx)) : 0;
    for (unsigned long i = 0; i < argc; i++) {
        const proto::ProtoObject* a = args->getAt(ctx, static_cast<int>(i));
        long long code = 0;
        if (a && a != PROTO_NONE) {
            if (a->isInteger(ctx)) code = a->asLong(ctx);
            else if (a->isDouble(ctx)) code = static_cast<long long>(a->asDouble(ctx));
        }
        units.push_back(static_cast<uint16_t>(code & 0xFFFF));
    }
    return ctx->fromUTF8String(utf16ToUTF8(units).c_str());
}

/** String.fromCodePoint(...codePoints) */
const proto::ProtoObject* stringFromCodePoint(
    proto::ProtoContext* ctx, const proto::ProtoObject* /*self*/,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    std::string result;
    unsigned long argc = args ? static_cast<unsigned long>(args->getSize(ctx)) : 0;
    for (unsigned long i = 0; i < argc; i++) {
        const proto::ProtoObject* a = args->getAt(ctx, static_cast<int>(i));
        // ECMA-262 §22.1.2.2 step 5: each code point must be a
        // non-negative integer < 0x110000. Non-integer doubles,
        // negative values, NaN, Infinity all throw RangeError.
        double dcp = 0.0;
        if (a && a != PROTO_NONE) {
            if (a->isInteger(ctx)) dcp = static_cast<double>(a->asLong(ctx));
            else if (a->isDouble(ctx) || a->isFloat(ctx)) dcp = a->asDouble(ctx);
            else {
                const proto::ProtoObject* num = jsToNumber(ctx, a);
                if (num) {
                    if (num->isInteger(ctx)) dcp = static_cast<double>(num->asLong(ctx));
                    else if (num->isDouble(ctx) || num->isFloat(ctx)) dcp = num->asDouble(ctx);
                }
            }
        }
        if (std::isnan(dcp) || std::isinf(dcp) || dcp < 0 || dcp > 0x10FFFF
            || dcp != std::trunc(dcp)) {
            signalNativeException(makeNativeError(ctx, "RangeError",
                "Invalid code point"));
            return PROTO_NONE;
        }
        uint32_t cp = static_cast<uint32_t>(dcp);
        std::vector<uint16_t> tmp;
        if (cp < 0x10000) {
            tmp.push_back(static_cast<uint16_t>(cp));
        } else {
            cp -= 0x10000;
            tmp.push_back(static_cast<uint16_t>(0xD800 + (cp >> 10)));
            tmp.push_back(static_cast<uint16_t>(0xDC00 + (cp & 0x3FF)));
        }
        result += utf16ToUTF8(tmp);
    }
    return ctx->fromUTF8String(result.c_str());
}

// Sentinel key used to mark that the prototype has been built.
static const char kBuiltSentinel[] = "__string_proto_built__";

// String.raw(template, ...substitutions)
// Template tag that returns the raw string form without processing escape sequences.
// template.raw[i] contains the raw source text of each string segment.
const proto::ProtoObject* stringRaw(
    proto::ProtoContext* ctx, const proto::ProtoObject* /*self*/,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!ctx || !args || args->getSize(ctx) < 1) return ctx->fromUTF8String("");
    const proto::ProtoObject* tpl = args->getAt(ctx, 0);
    if (!tpl || tpl == PROTO_NONE) return ctx->fromUTF8String("");

    // Get template.raw
    const proto::ProtoObject* rawKeyObj = ctx->fromUTF8String("raw");
    const proto::ProtoString* rawKey = rawKeyObj ? rawKeyObj->asString(ctx) : nullptr;
    const proto::ProtoObject* rawArr = rawKey
        ? tpl->getAttribute(ctx, rawKey, true) : PROTO_NONE;
    if (!rawArr || rawArr == PROTO_NONE) return ctx->fromUTF8String("");

    // Get raw.length
    const proto::ProtoString* lenKey = JSSymbols::length(ctx);
    long long rawLen = 0;
    if (lenKey) {
        const proto::ProtoObject* lv = rawArr->getAttribute(ctx, lenKey, true);
        if (lv && lv != PROTO_NONE && lv->isInteger(ctx))
            rawLen = lv->asLong(ctx);
        else if (lv && lv != PROTO_NONE && lv->isDouble(ctx))
            rawLen = static_cast<long long>(lv->asDouble(ctx));
    }

    // Build result: raw[0] + subs[0] + raw[1] + subs[1] + ... + raw[n-1]
    std::string result;
    for (long long i = 0; i < rawLen; i++) {
        // Append raw[i]
        const proto::ProtoString* idxKey = JSSymbols::indexKey(ctx, static_cast<uint32_t>(i));
        if (idxKey) {
            const proto::ProtoObject* seg = rawArr->getAttribute(ctx, idxKey, true);
            if (seg && seg != PROTO_NONE) {
                std::string sv;
                if (seg->isString(ctx) && seg->asString(ctx))
                    seg->asString(ctx)->toUTF8String(ctx, sv);
                else
                    sv = objToStr(ctx, seg);
                result += sv;
            }
        }
        // Append substitution args[i+1] if it exists
        if (i + 1 < (long long)args->getSize(ctx)) {
            const proto::ProtoObject* sub = args->getAt(ctx, static_cast<int>(i + 1));
            result += objToStr(ctx, sub);
        }
    }
    return ctx->fromUTF8String(result.c_str());
}

// String iterator next() — returns {value: char, done: bool}.
// The iterator object holds __str__ (the string) and __idx__ (current
// UTF-16 code-unit index).  Advancing reads one Unicode codepoint
// (surrogate pair coalesced) and bumps __idx__.
static const proto::ProtoObject* stringIteratorNext(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*)
{
    auto makeResult = [&](const proto::ProtoObject* val, bool done) -> const proto::ProtoObject* {
        const proto::ProtoObject* r = ctx->newObject(true);
        const proto::ProtoString* vk = JSSymbols::value(ctx);
        const proto::ProtoString* dk = JSSymbols::done(ctx);
        if (vk) r = r->setAttribute(ctx, vk, val ? val : PROTO_NONE);
        if (dk) r = r->setAttribute(ctx, dk, done ? PROTO_TRUE : PROTO_FALSE);
        return r;
    };
    if (!self || self == PROTO_NONE) return makeResult(PROTO_NONE, true);
    const proto::ProtoObject* strKo = ctx->fromUTF8String("__str__");
    const proto::ProtoString* strKey = strKo ? strKo->asString(ctx) : nullptr;
    const proto::ProtoObject* idxKo = ctx->fromUTF8String("__idx__");
    const proto::ProtoString* idxKey = idxKo ? idxKo->asString(ctx) : nullptr;
    if (!strKey || !idxKey) return makeResult(PROTO_NONE, true);
    const proto::ProtoObject* sObj = self->getAttribute(ctx, strKey, false);
    const proto::ProtoObject* iObj = self->getAttribute(ctx, idxKey, false);
    if (!sObj || !sObj->isString(ctx)) return makeResult(PROTO_NONE, true);
    long long idx = (iObj && iObj->isInteger(ctx)) ? iObj->asLong(ctx) : 0;
    std::string utf8;
    sObj->asString(ctx)->toUTF8String(ctx, utf8);
    // Walk to byte offset for idx (UTF-8 byte sequence count).
    long long byteOffset = 0;
    long long cps = 0;
    while (byteOffset < (long long)utf8.size() && cps < idx) {
        unsigned char c = static_cast<unsigned char>(utf8[byteOffset]);
        size_t step = (c < 0x80) ? 1 : (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : 4;
        byteOffset += step;
        cps++;
    }
    if (byteOffset >= (long long)utf8.size())
        return makeResult(PROTO_NONE, true);
    unsigned char c = static_cast<unsigned char>(utf8[byteOffset]);
    size_t step = (c < 0x80) ? 1 : (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : 4;
    std::string single = utf8.substr(byteOffset, step);
    const proto::ProtoObject* charObj = ctx->fromUTF8String(single.c_str());
    // Advance the iterator's idx in-place (mutable object).
    self->setAttribute(ctx, idxKey, ctx->fromInteger(idx + 1));
    return makeResult(charObj, false);
}

// String[Symbol.iterator]() — returns a fresh iterator over the
// string's codepoints.  Receiver is the string primitive (this).
static const proto::ProtoObject* stringSymbolIterator(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*)
{
    const proto::ProtoObject* iter = ctx->newObject(true);
    if (!iter) return PROTO_NONE;
    const proto::ProtoObject* strKo = ctx->fromUTF8String("__str__");
    const proto::ProtoString* strKey = strKo ? strKo->asString(ctx) : nullptr;
    const proto::ProtoObject* idxKo = ctx->fromUTF8String("__idx__");
    const proto::ProtoString* idxKey = idxKo ? idxKo->asString(ctx) : nullptr;
    if (strKey) iter = iter->setAttribute(ctx, strKey, self ? self : PROTO_NONE);
    if (idxKey) iter = iter->setAttribute(ctx, idxKey, ctx->fromInteger(0));

    // .next method on the iterator.
    const proto::ProtoString* nextKey = JSSymbols::next(ctx);
    if (nextKey) {
        const proto::ProtoObject* wrapper = ctx->newObject(true);
        const proto::ProtoString* nfKey = JSSymbols::nativeFn(ctx);
        const proto::ProtoObject* rawM = ctx->fromMethod(nullptr, stringIteratorNext);
        if (wrapper && nfKey && rawM) {
            wrapper = wrapper->setAttribute(ctx, nfKey, rawM);
            iter = iter->setAttribute(ctx, nextKey, wrapper);
        }
    }
    return iter;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------------

void BuildStringPrototype(proto::ProtoSpace* space, proto::ProtoContext* ctx,
                          const proto::ProtoObject* objectProto) {
    if (!space || !ctx || !objectProto) return;

    // Inherit from the existing protoCore string prototype if present, so we
    // don't lose built-in protoCore string methods (split, trim, etc.).
    const proto::ProtoObject* baseProto =
        space->stringPrototype
            ? reinterpret_cast<const proto::ProtoObject*>(space->stringPrototype)
            : objectProto;

    // Idempotency guard: don't rebuild if we already added our sentinel.
    const proto::ProtoString* sentinelKey =
        JSSymbols::stringProtoBuild(ctx);
    if (sentinelKey) {
        const proto::ProtoObject* chk = baseProto->getAttribute(ctx, sentinelKey, false);
        if (chk && chk != PROTO_NONE) return; // already built
    }

    // Must be mutable so JS-level assignments (String.prototype.x = y) modify
    // the object in-place, keeping space->stringPrototype consistent and
    // ensuring attribute lookups on primitive strings find newly added properties.
    const proto::ProtoObject* sp = baseProto->newChild(ctx, true);
    proto::ProtoObject* mp = const_cast<proto::ProtoObject*>(sp);

    // Helper lambda to register one method with correct .length and .name descriptors.
    // Mirrors wrapNativeFunction but without requiring globalRoot, which is unavailable
    // at space-init time. Each method is wrapped in a plain object carrying:
    //   __native_fn__  — the raw callable METHOD cell
    //   length         — arity with descriptor {writable:false, enumerable:false, configurable:true}
    //   name           — method name with the same non-writable, configurable descriptor
    auto reg = [&](const char* name, proto::ProtoMethod fn, long long length) {
        const proto::ProtoString* key = ctx->fromUTF8String(name)->asString(ctx);
        if (!key) return;

        // Build a wrapper object carrying __native_fn__, length, name.
        const proto::ProtoObject* wrapper = ctx->newObject(true);
        if (!wrapper) return;

        // __native_fn__ — marks this as a callable native wrapper.
        // Pass nullptr as receiver (not mp) so the method cell is receiver-agnostic;
        // `this` is resolved at call time from the call site, matching wrapNativeFunction.
        const proto::ProtoString* nfKey = JSSymbols::nativeFn(ctx);
        const proto::ProtoObject* rawMethod = ctx->fromMethod(nullptr, fn);
        if (nfKey && rawMethod) wrapper = wrapper->setAttribute(ctx, nfKey, rawMethod);

        // length: {writable:false, enumerable:false, configurable:true} → 0x2
        const proto::ProtoString* lenKey = JSSymbols::length(ctx);
        if (lenKey) {
            wrapper = wrapper->setAttribute(ctx, lenKey, ctx->fromInteger(length));
            const proto::ProtoObject* pdlko = ctx->fromUTF8String("__pd_length__");
            const proto::ProtoString* pdlk = pdlko ? pdlko->asString(ctx) : nullptr;
            if (pdlk) wrapper = wrapper->setAttribute(ctx, pdlk, ctx->fromInteger(0x2));
        }

        // name: {writable:false, enumerable:false, configurable:true} → 0x2
        const proto::ProtoString* nmKey = JSSymbols::name(ctx);
        if (nmKey) {
            wrapper = wrapper->setAttribute(ctx, nmKey, ctx->fromUTF8String(name ? name : ""));
            const proto::ProtoObject* pdnko = ctx->fromUTF8String("__pd_name__");
            const proto::ProtoString* pdnk = pdnko ? pdnko->asString(ctx) : nullptr;
            if (pdnk) wrapper = wrapper->setAttribute(ctx, pdnk, ctx->fromInteger(0x2));
        }

        sp = sp->setAttribute(ctx, key, wrapper);
    };

    reg("valueOf",           stringValueOf,       0);
    reg("toString",          stringToString,      0);
    reg("charAt",            stringCharAt,        1);
    reg("charCodeAt",        stringCharCodeAt,    1);
    reg("codePointAt",       stringCodePointAt,   1);
    reg("at",                stringAt,            1);
    reg("concat",            stringConcat,        1);
    reg("indexOf",           stringIndexOf,       1);
    reg("lastIndexOf",       stringLastIndexOf,   1);
    reg("slice",             stringSlice,         2);
    reg("substring",         stringSubstring,     2);
    reg("substr",            stringSubstr,        2);
    reg("toLowerCase",       stringToLowerCase,   0);
    reg("toUpperCase",       stringToUpperCase,   0);
    reg("toLocaleLowerCase", stringToLowerCase,   0);
    reg("toLocaleUpperCase", stringToUpperCase,   0);
    reg("repeat",            stringRepeat,        1);
    reg("localeCompare",     stringLocaleCompare, 1);
    reg("toLocaleString",    stringToLocaleString, 0);
    reg("trim",              stringTrim,          0);
    reg("trimStart",         stringTrimStart,     0);
    reg("trimLeft",          stringTrimStart,     0);
    reg("trimEnd",           stringTrimEnd,       0);
    reg("trimRight",         stringTrimEnd,       0);
    reg("startsWith",        stringStartsWith,    1);
    reg("endsWith",          stringEndsWith,      1);
    reg("includes",          stringIncludes,      1);
    reg("padStart",          stringPadStart,      1);
    reg("padEnd",            stringPadEnd,        1);
    reg("match",             stringMatch,         1);
    reg("search",            stringSearch,        1);
    reg("replace",           stringReplace,       2);
    reg("replaceAll",        stringReplaceAll,    2);
    reg("split",             stringSplit,         2);
    reg("normalize",         stringNormalize,     0);
    reg("isWellFormed",      stringIsWellFormed,  0);
    reg("matchAll",          stringMatchAll,      1);

    // String[Symbol.iterator] — yields each Unicode codepoint (UTF-16
    // code unit pair handled correctly for BMP-only strings).
    // The iterator is returned as a fresh object carrying __str__ and
    // __idx__ markers plus a .next method that advances the index.
    reg("stringIterNext",    stringIteratorNext,  0);
    {
        const proto::ProtoObject* siko = ctx->fromUTF8String("Symbol.iterator");
        const proto::ProtoString* sik = siko ? siko->asString(ctx) : nullptr;
        if (sik) {
            const proto::ProtoObject* wrapper = ctx->newObject(true);
            const proto::ProtoString* nfKey = JSSymbols::nativeFn(ctx);
            const proto::ProtoObject* rawM = ctx->fromMethod(nullptr, stringSymbolIterator);
            if (wrapper && nfKey && rawM) {
                wrapper = wrapper->setAttribute(ctx, nfKey, rawM);
                sp = sp->setAttribute(ctx, sik, wrapper);
            }
        }
    }

    // Mark as built.
    if (sentinelKey) sp = sp->setAttribute(ctx, sentinelKey, PROTO_TRUE);

    space->stringPrototype = const_cast<proto::ProtoObject*>(sp);
}

void ReinstallStringPrototypeMethods(proto::ProtoContext* ctx) {
    if (!ctx || !ctx->space || !ctx->space->stringPrototype) return;
    // Only reinstall if methodPrototype is now available.
    if (!ctx->space->methodPrototype) return;

    const proto::ProtoObject* sp =
        reinterpret_cast<const proto::ProtoObject*>(ctx->space->stringPrototype);
    // Idempotency: check for a reinstall sentinel.
    const proto::ProtoObject* sentinelObj = ctx->fromUTF8String("__string_methods_reinstalled__");
    const proto::ProtoString* sentinelKey = sentinelObj ? sentinelObj->asString(ctx) : nullptr;
    if (sentinelKey) {
        const proto::ProtoObject* chk = sp->getAttribute(ctx, sentinelKey, false);
        if (chk && chk != PROTO_NONE) return; // Already done.
    }

    struct Entry { const char* name; proto::ProtoMethod fn; int argc; };
    static const Entry kMethods[] = {
        { "valueOf",           stringValueOf,       0 },
        { "toString",          stringToString,      0 },
        { "charAt",            stringCharAt,        1 },
        { "charCodeAt",        stringCharCodeAt,    1 },
        { "codePointAt",       stringCodePointAt,   1 },
        { "at",                stringAt,            1 },
        { "concat",            stringConcat,        1 },
        { "indexOf",           stringIndexOf,       1 },
        { "lastIndexOf",       stringLastIndexOf,   1 },
        { "slice",             stringSlice,         2 },
        { "substring",         stringSubstring,     2 },
        { "substr",            stringSubstr,        2 },
        { "toLowerCase",       stringToLowerCase,   0 },
        { "toUpperCase",       stringToUpperCase,   0 },
        { "toLocaleLowerCase", stringToLowerCase,   0 },
        { "toLocaleUpperCase", stringToUpperCase,   0 },
        { "repeat",            stringRepeat,        1 },
        { "localeCompare",     stringLocaleCompare, 1 },
        { "toLocaleString",    stringToLocaleString, 0 },
        { "trim",              stringTrim,          0 },
        { "trimStart",         stringTrimStart,     0 },
        { "trimLeft",          stringTrimStart,     0 },
        { "trimEnd",           stringTrimEnd,       0 },
        { "trimRight",         stringTrimEnd,       0 },
        { "startsWith",        stringStartsWith,    1 },
        { "endsWith",          stringEndsWith,      1 },
        { "includes",          stringIncludes,      1 },
        { "padStart",          stringPadStart,      1 },
        { "padEnd",            stringPadEnd,        1 },
        { "match",             stringMatch,         1 },
        { "search",            stringSearch,        1 },
        { "replace",           stringReplace,       2 },
        { "replaceAll",        stringReplaceAll,    2 },
        { "split",             stringSplit,         2 },
        { "normalize",         stringNormalize,     0 },
        { "isWellFormed",      stringIsWellFormed,  0 },
        { "matchAll",          stringMatchAll,      1 },
        { nullptr, nullptr, 0 }
    };

    for (int i = 0; kMethods[i].name; i++) {
        sp = installNonEnumerableMethod(ctx, sp, kMethods[i].name,
                                        kMethods[i].fn, kMethods[i].argc);
    }

    // Set reinstall sentinel.
    if (sentinelKey) sp = sp->setAttribute(ctx, sentinelKey, PROTO_TRUE);

    // §22.1.3 String.prototype.length === 0 per spec — required by
    // verifyProperty fixtures and by code that probes the empty
    // String.prototype.length default.
    const proto::ProtoString* lenKey = JSSymbols::length(ctx);
    if (lenKey) sp = sp->setAttribute(ctx, lenKey, ctx->fromInteger(0LL));

    ctx->space->stringPrototype = const_cast<proto::ProtoObject*>(sp);
}

void ensureStringConstructor(proto::ProtoContext* ctx,
                              const proto::ProtoObject** globalRoot) {
    if (!ctx || !globalRoot || !*globalRoot) return;
    const proto::ProtoString* keyString = JSSymbols::String(ctx);
    if (!keyString) return;

    // Only register once.
    const proto::ProtoObject* existing =
        (*globalRoot)->getAttribute(ctx, keyString, false);
    if (existing && existing != PROTO_NONE) return;

    // Reinstall String.prototype methods with .call/.apply/.bind support now
    // that methodPrototype is available (set by ensureFunctionPrototype).
    ReinstallStringPrototypeMethods(ctx);

    const proto::ProtoObject* ctorParent = nullptr;
    if (ctx->space && ctx->space->methodPrototype)
        ctorParent = ctx->space->methodPrototype;
    const proto::ProtoObject* ctor = ctorParent
        ? ctorParent->newChild(ctx, true)
        : ctx->newObject(true);
    if (!ctor) return;
    proto::ProtoObject* mCtor = const_cast<proto::ProtoObject*>(ctor);

    auto regStatic = [&](const char* name, proto::ProtoMethod fn, long long length) {
        const proto::ProtoString* key = ctx->fromUTF8String(name)->asString(ctx);
        if (key) {
            const proto::ProtoObject* mObj = wrapNativeFunction(ctx, fn, name, length, globalRoot);
            if (mObj && mObj != PROTO_NONE)
                ctor = ctor->setAttribute(ctx, key, mObj);
        }
    };

    regStatic("fromCharCode",  stringFromCharCode,  1);
    regStatic("fromCodePoint", stringFromCodePoint, 1);
    regStatic("raw",           stringRaw,           1);

    // Set String.prototype.constructor = String (required by ECMAScript).
    // Descriptor per §22.1.3.1: {writable:true, enumerable:false,
    // configurable:true} → bits 0x3. Without the sidecar the default
    // is fully enumerable, surfacing `constructor` in for-in over any
    // string-wrapper and breaking Object.keys() / dynamic key routing
    // off a String instance.
    if (ctx->space && ctx->space->stringPrototype) {
        const proto::ProtoObject* sp =
            reinterpret_cast<const proto::ProtoObject*>(ctx->space->stringPrototype);
        const proto::ProtoString* ctorKey2 = JSSymbols::constructor(ctx);
        if (ctorKey2) {
            sp = sp->setAttribute(ctx, ctorKey2, ctor);
            const proto::ProtoObject* pdo = ctx->fromUTF8String("__pd_constructor__");
            const proto::ProtoString* pdk = pdo ? pdo->asString(ctx) : nullptr;
            if (pdk) sp = sp->setAttribute(ctx, pdk, ctx->fromInteger(0x3LL));
            ctx->space->stringPrototype = const_cast<proto::ProtoObject*>(sp);
        }
    }

    // name property
    const proto::ProtoString* nameKey = JSSymbols::name(ctx);
    if (nameKey) ctor = ctor->setAttribute(ctx, nameKey, ctx->fromUTF8String("String"));
    // String.length === 1 per §22.1.1.
    const proto::ProtoString* lenKey2 = JSSymbols::length(ctx);
    if (lenKey2) {
        ctor = ctor->setAttribute(ctx, lenKey2, ctx->fromInteger(1LL));
        const proto::ProtoObject* pdlo = ctx->fromUTF8String("__pd_length__");
        const proto::ProtoString* pdlk = pdlo ? pdlo->asString(ctx) : nullptr;
        if (pdlk) ctor = ctor->setAttribute(ctx, pdlk, ctx->fromInteger(0x2LL));
    }

    const proto::ProtoString* protoKey = JSSymbols::prototype(ctx);
    if (protoKey && ctx->space && ctx->space->stringPrototype) {
        ctor = ctor->setAttribute(ctx, protoKey, reinterpret_cast<const proto::ProtoObject*>(ctx->space->stringPrototype));
    }

    // Explicitly mark as a constructor for OP_call_constructor.
    const proto::ProtoString* isCtorKey = ctx->fromUTF8String("__is_constructor__")->asString(ctx);
    if (isCtorKey) ctor = ctor->setAttribute(ctx, isCtorKey, PROTO_TRUE);

    // Mark as the String constructor so OP_call can invoke it as a conversion function.
    const proto::ProtoString* markerKey = JSSymbols::stringCtor(ctx);
    if (markerKey) ctor = ctor->setAttribute(ctx, markerKey, PROTO_TRUE);

    *globalRoot = (*globalRoot)->setAttribute(ctx, keyString, ctor);
}

} // namespace protojs
