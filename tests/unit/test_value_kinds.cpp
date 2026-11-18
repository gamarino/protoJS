// The value-kind tests of src/runtime/ValueKinds.h and PropertyDictionary.h use
// protoCore's public API only, never its pointer tags. protoCore aliases
// several kinds' prototypes to objectPrototype (doubles, LargeIntegers,
// methods, byte buffers), so an object cell is not decided by getPrototype
// alone.

#include <catch2/catch_all.hpp>

#include "../../src/runtime/PropertyDictionary.h"
#include "../../src/runtime/ValueKinds.h"

namespace {
const proto::ProtoObject* nopMethod(proto::ProtoContext*, const proto::ProtoObject*,
                                    const proto::ParentLink*, const proto::ProtoArguments*,
                                    const proto::ProtoKeywordArguments*) {
    return PROTO_NONE;
}
}  // namespace

TEST_CASE("Value kind tests use protoCore's public API exactly", "[valuekinds]") {
    proto::ProtoSpace space;
    proto::ProtoContext* ctx = space.rootContext;
    const proto::ProtoObject* parentless = ctx->newObject(false);
    const proto::ProtoObject* child = parentless->newChild(ctx, false);
    const proto::ProtoObject* dbl = ctx->fromDouble(2.5);
    const proto::ProtoObject* big = ctx->fromString("123456789012345678901234567890", 10);
    const proto::ProtoObject* method =
        ctx->fromMethod(const_cast<proto::ProtoObject*>(parentless), nopMethod);
    const proto::ProtoObject* buffer = ctx->newBuffer(8);

    CHECK(protojs::isObjectCell(ctx, parentless));
    CHECK(protojs::isObjectCell(ctx, child));
    for (const proto::ProtoObject* v : {dbl, big, method, buffer, ctx->newList()->asObject(ctx),
                                        ctx->fromLong(7), PROTO_NONE, PROTO_TRUE})
        CHECK_FALSE(protojs::isObjectCell(ctx, v));

    CHECK(protojs::isDoubleValue(ctx, dbl));
    CHECK_FALSE(protojs::isDoubleValue(ctx, ctx->fromLong(7)));
    CHECK_FALSE(protojs::isDoubleValue(ctx, big));

    // Heap strings only: not inline strings, not symbols.
    const proto::ProtoString* heap =
        proto::ProtoString::fromUTF8(ctx, "a heap string longer than six bytes");
    CHECK(protojs::dict::isHeapStringKey(heap));
    CHECK_FALSE(protojs::dict::isHeapStringKey(proto::ProtoString::fromUTF8(ctx, "ab")));
    CHECK_FALSE(protojs::dict::isHeapStringKey(
        proto::ProtoString::createSymbol(ctx, "a symbol longer than six bytes")));
}
