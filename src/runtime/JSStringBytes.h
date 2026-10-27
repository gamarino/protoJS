#pragma once

// Length-aware construction of protoCore strings from UTF-8 bytes.
//
// A JavaScript string may contain U+0000. ProtoContext::fromUTF8String and
// ProtoString::createSymbol take a zero-terminated C string and stop at the
// first NUL byte, so "a\0b" built through them became "a". These helpers take
// the byte count explicitly. A buffer without a NUL byte (nearly every string)
// takes the same constructor as before, so its result is unchanged; one that
// carries a NUL is built with ProtoString::fromUTF8Buffer, which honours the
// length, and is interned with ProtoString::intern when a symbol is needed.

#include <protoCore.h>

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

namespace protojs {

// The string whose UTF-8 encoding is bytes[0, len). bytes[len] must be a NUL
// (a std::string's data(), or a QuickJS C string).
inline const proto::ProtoObject* jsStringFromUTF8(proto::ProtoContext* ctx,
                                                   const char* bytes, std::size_t len) {
    if (!bytes || len == 0) return ctx->fromUTF8String("");
    if (std::memchr(bytes, 0, len) == nullptr) return ctx->fromUTF8String(bytes);
    std::uint8_t remainder[4];
    std::uint8_t remainderCount = 0;
    const proto::ProtoString* s = proto::ProtoString::fromUTF8Buffer(
        ctx, reinterpret_cast<const std::uint8_t*>(bytes), len, nullptr, 0,
        remainder, &remainderCount);
    return s ? s->asObject(ctx) : ctx->fromUTF8String("");
}

inline const proto::ProtoObject* jsStringFromUTF8(proto::ProtoContext* ctx,
                                                   const std::string& utf8) {
    return jsStringFromUTF8(ctx, utf8.c_str(), utf8.size());
}

// The interned symbol whose UTF-8 encoding is bytes[0, len); bytes[len] must
// be a NUL.
inline const proto::ProtoString* jsSymbolFromUTF8(proto::ProtoContext* ctx,
                                                   const char* bytes, std::size_t len) {
    if (!bytes) return proto::ProtoString::createSymbol(ctx, "");
    if (std::memchr(bytes, 0, len) == nullptr)
        return proto::ProtoString::createSymbol(ctx, bytes);
    const proto::ProtoObject* o = jsStringFromUTF8(ctx, bytes, len);
    const proto::ProtoString* s = o ? o->asString(ctx) : nullptr;
    return s ? s->intern(ctx) : proto::ProtoString::createSymbol(ctx, "");
}

inline const proto::ProtoString* jsSymbolFromUTF8(proto::ProtoContext* ctx,
                                                   const std::string& utf8) {
    return jsSymbolFromUTF8(ctx, utf8.c_str(), utf8.size());
}

}  // namespace protojs
