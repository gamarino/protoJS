// The value-kind tests of src/runtime/ValueKinds.h and PropertyDictionary.h,
// and the JavaScript type model of src/runtime/JsType.h, use protoCore's
// public API only, never its pointer tags. protoJS asks no "is this an object
// cell" question; the JavaScript type of every protoCore value is pinned here.

#include <catch2/catch_all.hpp>

#include "../../src/runtime/PropertyDictionary.h"
#include "../../src/runtime/ValueKinds.h"
#include "../../src/runtime/JsType.h"

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

    using protojs::JsType;
    using protojs::jsTypeOf;
    CHECK(jsTypeOf(ctx, nullptr) == JsType::Undefined);
    CHECK(jsTypeOf(ctx, PROTO_NONE) == JsType::Undefined);
    CHECK(jsTypeOf(ctx, PROTO_TRUE) == JsType::Boolean);
    CHECK(jsTypeOf(ctx, PROTO_FALSE) == JsType::Boolean);
    CHECK(jsTypeOf(ctx, ctx->fromLong(7)) == JsType::Number);
    CHECK(jsTypeOf(ctx, dbl) == JsType::Number);
    CHECK(jsTypeOf(ctx, big) == JsType::Number);
    CHECK(jsTypeOf(ctx, ctx->fromUTF8String("ab")) == JsType::String);
    CHECK(jsTypeOf(ctx, ctx->fromUTF8String("a heap string longer than six bytes")) ==
          JsType::String);
    // The well-known Symbols are carried as "Symbol.<name>" strings.
    CHECK(jsTypeOf(ctx, ctx->fromUTF8String("Symbol.iterator")) == JsType::Symbol);
    CHECK_FALSE(protojs::jsIsObject(ctx, ctx->fromUTF8String("Symbol.iterator")));
    // An object whose __data__ is a string is an Object: ProtoObject::isString
    // would read the attribute, the type model does not.
    const proto::ProtoObject* withData = ctx->newObject(true)->setAttribute(
        ctx, proto::ProtoString::fromUTF8(ctx, "__data__"), ctx->fromUTF8String("x"));
    for (const proto::ProtoObject* v : {parentless, child, withData, method, buffer,
                                        ctx->newList()->asObject(ctx)}) {
        CHECK(jsTypeOf(ctx, v) == JsType::Object);
        CHECK(protojs::jsIsObject(ctx, v));
    }
    for (const proto::ProtoObject* v : {dbl, big, ctx->fromLong(7), PROTO_NONE, PROTO_TRUE})
        CHECK_FALSE(protojs::jsIsObject(ctx, v));

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
