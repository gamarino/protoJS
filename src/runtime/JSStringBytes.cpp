// Length-aware construction of protoCore strings from UTF-8 bytes
// (JSStringBytes.h).

#include "JSStringBytes.h"

#include <cstdint>
#include <cstring>

namespace protojs {

const proto::ProtoObject* jsStringFromUTF8(proto::ProtoContext* ctx,
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
    if (std::memchr(bytes, 0, len) == nullptr)
        return proto::ProtoString::createSymbol(ctx, bytes);
    const proto::ProtoObject* o = jsStringFromUTF8(ctx, bytes, len);
    const proto::ProtoString* s = o ? o->asString(ctx) : nullptr;
    return s ? detail::internString(ctx, s, 0) : proto::ProtoString::createSymbol(ctx, "");
}

}  // namespace protojs
