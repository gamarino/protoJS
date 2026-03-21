#include "StringPrototype.h"
#include "ArrayPrototype.h"
#include "ProtoJSStringCache.h"
#include "headers/protoCore.h"
#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>
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
    if (!obj || obj == PROTO_NONE) return "";
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
        char buf[64];
        snprintf(buf, sizeof(buf), "%.15g", d);
        return buf;
    }
    return "";
}

/** Extract a numeric argument.  NaN → 0; ±Infinity → LLONG extremes. */
static long long getIntArg(proto::ProtoContext* ctx, const proto::ProtoList* args,
                            unsigned idx, long long defaultVal) {
    if (!args || static_cast<unsigned long>(args->getSize(ctx)) <= idx) return defaultVal;
    const proto::ProtoObject* a = args->getAt(ctx, static_cast<int>(idx));
    if (!a || a == PROTO_NONE) return defaultVal;
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

const proto::ProtoObject* stringValueOf(
    proto::ProtoContext* /*ctx*/, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{
    return self;
}

const proto::ProtoObject* stringToString(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{
    if (self && self != PROTO_NONE && self->isString(ctx)) return self;
    std::string s = objToStr(ctx, self);
    return ctx->fromUTF8String(s.c_str());
}

const proto::ProtoObject* stringCharAt(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
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
    std::string s    = objToStr(ctx, self);
    std::string srch = getStrArg(ctx, args, 0);
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
    std::string s = objToStr(ctx, self);
    std::transform(s.begin(), s.end(), s.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return ctx->fromUTF8String(s.c_str());
}

const proto::ProtoObject* stringToUpperCase(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{
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
    std::string s = objToStr(ctx, self);
    long long count = getIntArg(ctx, args, 0, 0);
    if (count <= 0 || s.empty()) return ctx->fromUTF8String("");
    if (count > 65536) count = 65536; // guard against absurd allocations
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
    std::string a = objToStr(ctx, self);
    std::string b = getStrArg(ctx, args, 0);
    int cmp = a.compare(b);
    return ctx->fromInteger(cmp < 0 ? -1LL : cmp > 0 ? 1LL : 0LL);
}

const proto::ProtoObject* stringTrim(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{
    std::string s = objToStr(ctx, self);
    auto isWS = [](unsigned char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
    };
    size_t lo = 0, hi = s.size();
    while (lo < hi && isWS(static_cast<unsigned char>(s[lo]))) lo++;
    while (hi > lo && isWS(static_cast<unsigned char>(s[hi - 1]))) hi--;
    return ctx->fromUTF8String(s.substr(lo, hi - lo).c_str());
}

const proto::ProtoObject* stringTrimStart(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{
    std::string s = objToStr(ctx, self);
    auto isWS = [](unsigned char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
    };
    size_t lo = 0;
    while (lo < s.size() && isWS(static_cast<unsigned char>(s[lo]))) lo++;
    return ctx->fromUTF8String(s.substr(lo).c_str());
}

const proto::ProtoObject* stringTrimEnd(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{
    std::string s = objToStr(ctx, self);
    auto isWS = [](unsigned char c) {
        return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v';
    };
    size_t hi = s.size();
    while (hi > 0 && isWS(static_cast<unsigned char>(s[hi - 1]))) hi--;
    return ctx->fromUTF8String(s.substr(0, hi).c_str());
}

const proto::ProtoObject* stringStartsWith(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    std::string s    = objToStr(ctx, self);
    std::string srch = getStrArg(ctx, args, 0);
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
    std::string s    = objToStr(ctx, self);
    std::string srch = getStrArg(ctx, args, 0);
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
    std::string s    = objToStr(ctx, self);
    std::string srch = getStrArg(ctx, args, 0);
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

const proto::ProtoObject* stringPadStart(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    std::string s = objToStr(ctx, self);
    auto su16 = utf8ToUTF16(s);
    long long targetLen = getIntArg(ctx, args, 0, static_cast<long long>(su16.size()));
    if (targetLen <= static_cast<long long>(su16.size()))
        return ctx->fromUTF8String(s.c_str());
    std::string padStr = " ";
    if (args && args->getSize(ctx) > 1) {
        const proto::ProtoObject* pa = args->getAt(ctx, 1);
        if (pa && pa != PROTO_NONE) padStr = objToStr(ctx, pa);
    }
    auto padU16 = utf8ToUTF16(padStr);
    if (padU16.empty()) return ctx->fromUTF8String(s.c_str());
    std::vector<uint16_t> result;
    long long needed = targetLen - static_cast<long long>(su16.size());
    for (long long i = 0; i < needed; i++)
        result.push_back(padU16[static_cast<size_t>(i) % padU16.size()]);
    result.insert(result.end(), su16.begin(), su16.end());
    return ctx->fromUTF8String(utf16ToUTF8(result).c_str());
}

const proto::ProtoObject* stringPadEnd(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    std::string s = objToStr(ctx, self);
    auto su16 = utf8ToUTF16(s);
    long long targetLen = getIntArg(ctx, args, 0, static_cast<long long>(su16.size()));
    if (targetLen <= static_cast<long long>(su16.size()))
        return ctx->fromUTF8String(s.c_str());
    std::string padStr = " ";
    if (args && args->getSize(ctx) > 1) {
        const proto::ProtoObject* pa = args->getAt(ctx, 1);
        if (pa && pa != PROTO_NONE) padStr = objToStr(ctx, pa);
    }
    auto padU16 = utf8ToUTF16(padStr);
    if (padU16.empty()) return ctx->fromUTF8String(s.c_str());
    std::vector<uint16_t> result(su16);
    long long needed = targetLen - static_cast<long long>(su16.size());
    for (long long i = 0; i < needed; i++)
        result.push_back(padU16[static_cast<size_t>(i) % padU16.size()]);
    return ctx->fromUTF8String(utf16ToUTF8(result).c_str());
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

/** replace(pattern, replacement) — handles string patterns only.
 *  Regex patterns return PROTO_NONE to preserve vacuous-pass behaviour. */
const proto::ProtoObject* stringReplace(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!args || args->getSize(ctx) < 2) return self;
    const proto::ProtoObject* pattern = args->getAt(ctx, 0);
    if (!pattern || pattern == PROTO_NONE) return ctx->fromUTF8String(objToStr(ctx, self).c_str());
    // Regex / non-string pattern: defer (vacuous-pass preserved)
    if (!pattern->isString(ctx)) return PROTO_NONE;

    std::string s   = objToStr(ctx, self);
    std::string pat = objToStr(ctx, pattern);
    
    const proto::ProtoObject* repObj = args->getAt(ctx, 1);
    if (repObj && !repObj->isString(ctx) && !repObj->isInteger(ctx) && !repObj->isDouble(ctx)) {
        return PROTO_NONE;
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
    if (!args || args->getSize(ctx) < 2) return self;
    const proto::ProtoObject* pattern = args->getAt(ctx, 0);
    if (!pattern || pattern == PROTO_NONE) return ctx->fromUTF8String(objToStr(ctx, self).c_str());
    if (!pattern->isString(ctx)) return PROTO_NONE;

    std::string s   = objToStr(ctx, self);
    std::string pat = objToStr(ctx, pattern);
    
    const proto::ProtoObject* repObj = args->getAt(ctx, 1);
    if (repObj && !repObj->isString(ctx) && !repObj->isInteger(ctx) && !repObj->isDouble(ctx)) {
        return PROTO_NONE;
    }
    std::string rep = objToStr(ctx, repObj);

    if (pat.empty()) {
        std::string result;
        result += applyStringReplacement(s, pat, rep, 0);
        for (size_t i = 0; i < s.size(); i++) {
            result += s[i];
            result += applyStringReplacement(s, pat, rep, i + 1);
        }
        return ctx->fromUTF8String(result.c_str());
    }

    std::string result;
    size_t lastPos = 0;
    size_t pos = s.find(pat, lastPos);
    while (pos != std::string::npos) {
        result += s.substr(lastPos, pos - lastPos);
        result += applyStringReplacement(s, pat, rep, pos);
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
    return PROTO_NONE;
}

/** split(separator, limit) — string separator only; regex falls through to PROTO_NONE */
const proto::ProtoObject* stringSplit(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    std::string s = objToStr(ctx, self);
    // Result array — use createNewArray so that [] prototype methods (join, forEach, etc.) are inherited.
    const proto::ProtoObject* result = createNewArray(ctx, nullptr);
    const proto::ProtoString* lenKey = ProtoJSStringCache::getKey(ctx, "length");

    // Determine limit
    long long limit = std::numeric_limits<long long>::max();
    if (args && args->getSize(ctx) > 1) {
        const proto::ProtoObject* lim = args->getAt(ctx, 1);
        if (lim && lim != PROTO_NONE) {
            if (lim->isInteger(ctx)) limit = lim->asLong(ctx);
            else if (lim->isDouble(ctx)) limit = static_cast<long long>(lim->asDouble(ctx));
            if (limit < 0) limit = std::numeric_limits<long long>::max(); // treat negative as no limit
        }
    }

    if (limit == 0) {
        if (lenKey) result = result->setAttribute(ctx, lenKey, ctx->fromInteger(0));
        return result;
    }

    // No separator argument (or undefined): return array with entire string
    if (!args || args->getSize(ctx) == 0) {
        const proto::ProtoObject* elem = ctx->fromUTF8String(s.c_str());
        const proto::ProtoString* k0 = ProtoJSStringCache::getIndexKey(ctx, 0);
        if (k0) result = result->setAttribute(ctx, k0, elem);
        if (lenKey) result = result->setAttribute(ctx, lenKey, ctx->fromInteger(1));
        return result;
    }

    const proto::ProtoObject* sepArg = args->getAt(ctx, 0);
    // Undefined separator: return array with entire string
    if (!sepArg || sepArg == PROTO_NONE) {
        const proto::ProtoObject* elem = ctx->fromUTF8String(s.c_str());
        const proto::ProtoString* k0 = ProtoJSStringCache::getIndexKey(ctx, 0);
        if (k0) result = result->setAttribute(ctx, k0, elem);
        if (lenKey) result = result->setAttribute(ctx, lenKey, ctx->fromInteger(1));
        return result;
    }
    // Non-string separator (e.g., regex): return PROTO_NONE to preserve vacuous-pass
    if (!sepArg->isString(ctx)) return PROTO_NONE;

    std::string sep = objToStr(ctx, sepArg);
    auto su16 = utf8ToUTF16(s);
    auto se16 = utf8ToUTF16(sep);

    long long count = 0;

    // Empty separator: split into individual UTF-16 code units (JS char-by-char)
    if (se16.empty()) {
        for (size_t i = 0; i < su16.size() && count < limit; i++) {
            std::string ch = utf16ToUTF8(su16, i, i + 1);
            const proto::ProtoString* k = ProtoJSStringCache::getIndexKey(ctx, static_cast<uint32_t>(count));
            if (k) result = result->setAttribute(ctx, k, ctx->fromUTF8String(ch.c_str()));
            count++;
        }
        if (lenKey) result = result->setAttribute(ctx, lenKey, ctx->fromInteger(count));
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
        const proto::ProtoString* k = ProtoJSStringCache::getIndexKey(ctx, static_cast<uint32_t>(count));
        if (k) result = result->setAttribute(ctx, k, ctx->fromUTF8String(segment.c_str()));
        count++;
        if (found == su16.size()) break; // no more separators
        pos = found + se16.size();
    }

    if (lenKey) result = result->setAttribute(ctx, lenKey, ctx->fromInteger(count));
    return result;
}

/** normalize(form) — NFC/NFD/NFKC/NFKD. Without ICU we only support NFC (identity for ASCII). */
const proto::ProtoObject* stringNormalize(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* /*args*/,
    const proto::ProtoSparseList*)
{
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
        uint32_t cp = 0;
        if (a && a != PROTO_NONE) {
            if (a->isInteger(ctx)) cp = static_cast<uint32_t>(a->asLong(ctx));
            else if (a->isDouble(ctx)) cp = static_cast<uint32_t>(
                static_cast<long long>(a->asDouble(ctx)));
        }
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
        ProtoJSStringCache::getKey(ctx, kBuiltSentinel);
    if (sentinelKey) {
        const proto::ProtoObject* chk = baseProto->getAttribute(ctx, sentinelKey, false);
        if (chk && chk != PROTO_NONE) return; // already built
    }

    const proto::ProtoObject* sp = baseProto->newChild(ctx, false);
    proto::ProtoObject* mp = const_cast<proto::ProtoObject*>(sp);

    // Helper lambda to register one method with length and name.
    auto reg = [&](const char* name, proto::ProtoMethod fn, long long length) {
        const proto::ProtoString* key = ProtoJSStringCache::getKey(ctx, name);
        if (key) {
            const proto::ProtoObject* mObj = ctx->fromMethod(mp, fn);
            if (mObj && mObj != PROTO_NONE) {
                const proto::ProtoString* lenKey = ProtoJSStringCache::getKey(ctx, "length");
                const proto::ProtoString* nameKey = ProtoJSStringCache::getKey(ctx, "name");
                if (lenKey) mObj = mObj->setAttribute(ctx, lenKey, ctx->fromInteger(length));
                if (nameKey) mObj = mObj->setAttribute(ctx, nameKey, ctx->fromUTF8String(name));
            }
            sp = sp->setAttribute(ctx, key, mObj);
        }
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
    reg("replace",           stringReplace,       2);
    reg("replaceAll",        stringReplaceAll,    2);
    reg("split",             stringSplit,         2);
    reg("normalize",         stringNormalize,     0);
    reg("isWellFormed",      stringIsWellFormed,  0);
    reg("matchAll",          stringMatchAll,      1);

    // Mark as built.
    if (sentinelKey) sp = sp->setAttribute(ctx, sentinelKey, PROTO_TRUE);

    space->stringPrototype = const_cast<proto::ProtoObject*>(sp);
}

void ensureStringConstructor(proto::ProtoContext* ctx,
                              const proto::ProtoObject** globalRoot) {
    if (!ctx || !globalRoot || !*globalRoot) return;
    const proto::ProtoString* keyString = ProtoJSStringCache::getKey(ctx, "String");
    if (!keyString) return;

    // Only register once.
    const proto::ProtoObject* existing =
        (*globalRoot)->getAttribute(ctx, keyString, false);
    if (existing && existing != PROTO_NONE) return;

    const proto::ProtoObject* ctor = ctx->newObject(true);
    if (!ctor) return;
    proto::ProtoObject* mCtor = const_cast<proto::ProtoObject*>(ctor);

    auto regStatic = [&](const char* name, proto::ProtoMethod fn, long long length) {
        const proto::ProtoString* key = ProtoJSStringCache::getKey(ctx, name);
        if (key) {
            const proto::ProtoObject* mObj = ctx->fromMethod(mCtor, fn);
            if (mObj && mObj != PROTO_NONE) {
                const proto::ProtoString* lenKey = ProtoJSStringCache::getKey(ctx, "length");
                const proto::ProtoString* nameKey = ProtoJSStringCache::getKey(ctx, "name");
                if (lenKey) mObj = mObj->setAttribute(ctx, lenKey, ctx->fromInteger(length));
                if (nameKey) mObj = mObj->setAttribute(ctx, nameKey, ctx->fromUTF8String(name));
            }
            ctor = ctor->setAttribute(ctx, key, mObj);
        }
    };

    regStatic("fromCharCode",  stringFromCharCode,  1);
    regStatic("fromCodePoint", stringFromCodePoint, 1);

    // name property
    const proto::ProtoString* nameKey = ProtoJSStringCache::getKey(ctx, "name");
    if (nameKey) ctor = ctor->setAttribute(ctx, nameKey, ctx->fromUTF8String("String"));

    const proto::ProtoString* protoKey = ProtoJSStringCache::getKey(ctx, "prototype");
    if (protoKey && ctx->space && ctx->space->stringPrototype) {
        ctor = ctor->setAttribute(ctx, protoKey, reinterpret_cast<const proto::ProtoObject*>(ctx->space->stringPrototype));
    }

    *globalRoot = (*globalRoot)->setAttribute(ctx, keyString, ctor);
}

} // namespace protojs
