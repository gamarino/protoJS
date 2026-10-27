// Converting a protoCore string to UTF-8 allocates no protoCore cell.
//
// TypeBridge::toJS (a ProtoString handed to QuickJS), ErrorHandler's error
// fields and Logger::log read a string's bytes for an external consumer.
// They used ProtoString::asList, which builds a new ProtoList of every
// character, one append per character: about 16 cells per character until
// the caller's young generation was submitted, about 1.6 million cells for a
// 100 KB string, only to read the characters back. They now copy the rope's
// leaves with ProtoString::toUTF8String.
//
// The counter read here, ProtoContext::allocatedCellsCount, only goes back to
// zero in ProtoContext::safepoint, which none of these conversions reach.

#include <catch2/catch_all.hpp>

#include <memory>
#include <string>

#include "../../src/ErrorHandler.h"
#include "../../src/JSContext.h"
#include "../../src/TypeBridge.h"

using namespace protojs;

namespace {

// 100,000 code points: ASCII words plus two-, three- and four-byte UTF-8
// sequences, so the conversion's encoding is checked along with its cost.
std::string sampleText() {
    std::string text;
    const std::string unit = "word \xC3\xB1\xE2\x82\xAC\xF0\x9F\x98\x80 ";  // "word ñ€😀 "
    while (text.size() < 200000) text += unit;
    return text;
}

// A generous bound: a conversion that allocates nothing per character stays
// far below it, while the per-character list it replaced needed millions.
constexpr proto::proto_ulong kMaxCells = 1000;

}  // namespace

TEST_CASE("TypeBridge::toJS converts a large string without allocating cells",
          "[string][cells]") {
    auto wrapper = std::make_unique<JSContextWrapper>();
    JSContextWrapper::CurrentScope scope(wrapper.get());
    proto::ProtoContext* ctx = wrapper->getProtoContext();
    JSContext* jsCtx = wrapper->getJSContext();

    const std::string text = sampleText();
    const proto::ProtoString* str = proto::ProtoString::fromStdString(ctx, text);
    REQUIRE(str);

    const proto::proto_ulong before = ctx->allocatedCellsCount;
    JSValue v = TypeBridge::toJS(jsCtx, str->asObject(ctx), ctx);
    const proto::proto_ulong after = ctx->allocatedCellsCount;

    REQUIRE(JS_IsString(v));
    const char* converted = JS_ToCString(jsCtx, v);
    REQUIRE(converted);
    CHECK(std::string(converted) == text);
    JS_FreeCString(jsCtx, converted);
    JS_FreeValue(jsCtx, v);

    REQUIRE(after >= before);
    INFO("cells allocated by the conversion: " << (after - before));
    CHECK(after - before < kMaxCells);
}

TEST_CASE("ErrorHandler builds an error from a large message without allocating cells",
          "[string][cells]") {
    auto wrapper = std::make_unique<JSContextWrapper>();
    JSContextWrapper::CurrentScope scope(wrapper.get());
    proto::ProtoContext* ctx = wrapper->getProtoContext();
    JSContext* jsCtx = wrapper->getJSContext();

    const std::string text = sampleText();
    const proto::ProtoString* message = proto::ProtoString::fromStdString(ctx, text);
    REQUIRE(message);

    const proto::proto_ulong before = ctx->allocatedCellsCount;
    JSValue error = ErrorHandler::createError(jsCtx, ctx, ErrorType::TypeError, message);
    const proto::proto_ulong after = ctx->allocatedCellsCount;

    JSValue m = JS_GetPropertyStr(jsCtx, error, "message");
    const char* converted = JS_ToCString(jsCtx, m);
    REQUIRE(converted);
    CHECK(std::string(converted) == text);
    JS_FreeCString(jsCtx, converted);
    JS_FreeValue(jsCtx, m);
    JS_FreeValue(jsCtx, error);

    REQUIRE(after >= before);
    INFO("cells allocated by the error: " << (after - before));
    CHECK(after - before < kMaxCells);
}
