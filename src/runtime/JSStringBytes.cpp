// Length-aware construction of protoCore strings from UTF-8 bytes
// (JSStringBytes.h).

#include "JSStringBytes.h"

#include <cstdint>
#include <cstring>
#include <string>

namespace protojs {

namespace {

// True at a three-byte high surrogate (ED A0..AF xx) followed by a three-byte
// low surrogate (ED B0..BF xx).
inline bool surrogatePairAt(const unsigned char* p, std::size_t len, std::size_t i) {
    return i + 6 <= len && p[i] == 0xED && p[i + 1] >= 0xA0 && p[i + 1] <= 0xAF
        && p[i + 3] == 0xED && p[i + 4] >= 0xB0 && p[i + 4] <= 0xBF;
}

// The canonical form of generalized UTF-8 (JSStringUnits.h): each encoded
// surrogate pair becomes the four-byte encoding of its code point. Answers
// false, leaving `out` untouched, when the bytes hold no such pair.
bool combineSurrogatePairs(const char* bytes, std::size_t len, std::string& out) {
    const auto* p = reinterpret_cast<const unsigned char*>(bytes);
    const void* ed = std::memchr(bytes, 0xED, len);
    if (!ed) return false;
    std::size_t i = static_cast<std::size_t>(static_cast<const char*>(ed) - bytes);
    bool found = false;
    for (std::size_t k = i; k + 6 <= len; ++k) {
        if (surrogatePairAt(p, len, k)) { found = true; i = k; break; }
    }
    if (!found) return false;
    out.assign(bytes, i);
    out.reserve(len);
    while (i < len) {
        if (surrogatePairAt(p, len, i)) {
            const std::uint32_t hi = ((p[i] & 0x0Fu) << 12) | ((p[i + 1] & 0x3Fu) << 6) | (p[i + 2] & 0x3Fu);
            const std::uint32_t lo = ((p[i + 3] & 0x0Fu) << 12) | ((p[i + 4] & 0x3Fu) << 6) | (p[i + 5] & 0x3Fu);
            const std::uint32_t cp = 0x10000u + ((hi - 0xD800u) << 10) + (lo - 0xDC00u);
            out += static_cast<char>(0xF0 | (cp >> 18));
            out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
            out += static_cast<char>(0x80 | (cp & 0x3F));
            i += 6;
        } else {
            out += static_cast<char>(p[i++]);
        }
    }
    return true;
}

}  // namespace

const proto::ProtoObject* jsStringFromUTF8(proto::ProtoContext* ctx,
                                           const char* bytes, std::size_t len) {
    if (!bytes || len == 0) return ctx->fromUTF8String("");
    {
        std::string combined;
        if (combineSurrogatePairs(bytes, len, combined))
            return jsStringFromUTF8(ctx, combined.c_str(), combined.size());
    }
    if (std::memchr(bytes, 0, len) == nullptr) return ctx->fromUTF8String(bytes);
    std::uint8_t remainder[4];
    std::uint8_t remainderCount = 0;
    const proto::ProtoString* s = proto::ProtoString::fromUTF8Buffer(
        ctx, reinterpret_cast<const std::uint8_t*>(bytes), len, nullptr, 0,
        remainder, &remainderCount);
    return s ? s->asObject(ctx) : ctx->fromUTF8String("");
}

namespace detail {
// ProtoString::intern exists since protoCore 3.2.0. Before it (the 3.1.0 floor)
// the string is used as it is: attribute lookups compare a non-symbol string
// key by content.
template <typename S>
auto internString(proto::ProtoContext* ctx, const S* s, int) -> decltype(s->intern(ctx)) {
    return s->intern(ctx);
}
template <typename S>
const S* internString(proto::ProtoContext*, const S* s, long) {
    return s;
}
}  // namespace detail

const proto::ProtoString* jsSymbolFromUTF8(proto::ProtoContext* ctx,
                                           const char* bytes, std::size_t len) {
    if (!bytes) return proto::ProtoString::createSymbol(ctx, "");
    {
        std::string combined;
        if (combineSurrogatePairs(bytes, len, combined))
            return jsSymbolFromUTF8(ctx, combined.c_str(), combined.size());
    }
    if (std::memchr(bytes, 0, len) == nullptr)
        return proto::ProtoString::createSymbol(ctx, bytes);
    const proto::ProtoObject* o = jsStringFromUTF8(ctx, bytes, len);
    const proto::ProtoString* s = o ? o->asString(ctx) : nullptr;
    return s ? detail::internString(ctx, s, 0) : proto::ProtoString::createSymbol(ctx, "");
}

}  // namespace protojs
