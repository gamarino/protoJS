// protoCore's unified exception model, from the embedder's side
// (docs/EXCEPTIONS.md).
//
//  - A JavaScript error that no JavaScript handler catches leaves the runtime
//    as the proto::ProtoException carrying the JavaScript error object, never
//    as a string or a C++ standard exception.
//  - protoCore's own errors, raised outside any interpreter frame, are built by
//    protoJS's error factory as the JavaScript error classes of the current
//    wrapper's realm.

#include <catch2/catch_all.hpp>

#include <exception>
#include <memory>
#include <string>

#include "../../src/JSContext.h"
#include "../../src/runtime/ProtoBytecodeModule.h"
#include "../../src/runtime/ProtoInterpreter.h"

using namespace protojs;

namespace {

const proto::ProtoObject* global(JSContextWrapper& w, proto::ProtoContext* ctx, const char* name) {
    return w.getGlobalObject(ctx)->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, name));
}

// `<name>.prototype` of the wrapper's realm.
const proto::ProtoObject* classPrototype(JSContextWrapper& w, proto::ProtoContext* ctx,
                                         const char* name) {
    const proto::ProtoObject* ctor = global(w, ctx, name);
    REQUIRE(ctor);
    REQUIRE(ctor != PROTO_NONE);
    return ctor->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "prototype"));
}

}  // namespace

TEST_CASE("An unhandled JavaScript error leaves the runtime as a ProtoException",
          "[exceptions]") {
    auto wrapper = std::make_unique<JSContextWrapper>();
    JSContextWrapper::CurrentScope scope(wrapper.get());
    JSValue v = wrapper->eval(
        "globalThis.fail = function () { throw new TypeError('escapes'); };", "escape.js");
    JS_FreeValue(wrapper->getJSContext(), v);

    proto::ProtoContext* ctx = wrapper->getProtoContext();
    const proto::ProtoObject* fail = global(*wrapper, ctx, "fail");
    REQUIRE(fail);
    REQUIRE(fail != PROTO_NONE);

    bool caught = false;
    try {
        (void) callJSFunctionFromAsync(
            ctx, fail, PROTO_NONE, ctx->newList(),
            static_cast<const ProtoBytecodeModule*>(wrapper->getRootModule()),
            wrapper->getNativeGlobalRootPtr());
    } catch (const proto::ProtoException& e) {
        caught = true;
        CHECK(e.isInstanceOf(ctx, classPrototype(*wrapper, ctx, "TypeError")));
        const proto::ProtoObject* m =
            e.value()->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "message"));
        REQUIRE(m);
        REQUIRE(m->isString(ctx));
        CHECK(m->asString(ctx)->toStdString(ctx) == "escapes");
    } catch (const std::exception& e) {
        FAIL(std::string("left as a C++ standard exception: ") + e.what());
    }
    CHECK(caught);
}

TEST_CASE("protoCore's errors are built as JavaScript errors by the factory",
          "[exceptions]") {
    auto wrapper = std::make_unique<JSContextWrapper>();
    JSContextWrapper::CurrentScope scope(wrapper.get());
    // The realm's global object is built by the first evaluation.
    JSValue v = wrapper->eval("0", "realm.js");
    JS_FreeValue(wrapper->getJSContext(), v);
    proto::ProtoContext* ctx = wrapper->getProtoContext();
    REQUIRE(wrapper->getProtoSpace()->getErrorFactory() != nullptr);

    // Outside any interpreter frame: the classes come from the current wrapper.
    bool divisionCaught = false;
    try {
        (void) ctx->fromInteger(1)->divide(ctx, ctx->fromInteger(0));
    } catch (const proto::ProtoException& e) {
        divisionCaught = true;
        CHECK(e.isInstanceOf(ctx, classPrototype(*wrapper, ctx, "RangeError")));
    }
    CHECK(divisionCaught);

    bool parseCaught = false;
    try {
        (void) ctx->fromString("ten", 10);
    } catch (const proto::ProtoException& e) {
        parseCaught = true;
        CHECK(e.isInstanceOf(ctx, classPrototype(*wrapper, ctx, "SyntaxError")));
    }
    CHECK(parseCaught);
}
