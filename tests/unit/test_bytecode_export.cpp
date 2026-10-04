// The bytecode loader reads variable names through the protojs_bytecode_*
// exports patched into deps/quickjs/quickjs.c. A variable can have no name:
// the hidden argument that holds a destructured parameter, `function f([a])`,
// is JS_ATOM_NULL. JS_AtomToCString(JS_ATOM_NULL) does not return a
// NUL-terminated string, and the loader's std::string(name) read past the end
// of a QuickJS heap block (AddressSanitizer: heap-buffer-overflow in
// loadBytecodeRecursive). The exports must report an unnamed variable as NULL,
// which the loader maps to the empty name.

#include <catch2/catch_all.hpp>

#include "quickjs.h"
#include "../../src/runtime/QuickJSBytecodeExport.h"

#include <cstring>
#include <string>

namespace {

struct QuickJSFixture {
    JSRuntime* rt = JS_NewRuntime();
    JSContext* ctx = JS_NewContext(rt);
    ~QuickJSFixture() {
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
    }
};

// The name of variable `idx` of the first function in the script's constant
// pool, or "<null>" for an unnamed one.
std::string firstFunctionVarName(QuickJSFixture& f, const char* source, uint16_t idx,
                                 uint16_t* argCount) {
    JSValue script = JS_Eval(f.ctx, source, std::strlen(source), "<test>",
                             JS_EVAL_TYPE_GLOBAL | JS_EVAL_FLAG_COMPILE_ONLY);
    REQUIRE(!JS_IsException(script));
    void* top = protojs_get_function_bytecode(f.ctx, &script);
    REQUIRE(top != nullptr);
    REQUIRE(protojs_bytecode_cpool_count(top) >= 1);
    const JSValue* cpool = static_cast<const JSValue*>(protojs_bytecode_cpool(top));
    void* fn = protojs_get_function_bytecode(f.ctx, &cpool[0]);
    REQUIRE(fn != nullptr);
    *argCount = protojs_bytecode_arg_count(fn);
    const char* name = protojs_bytecode_var_name(f.ctx, fn, idx);
    std::string out = name ? std::string(name) : std::string("<null>");
    if (name) JS_FreeCString(f.ctx, name);
    JS_FreeValue(f.ctx, script);
    return out;
}

} // namespace

TEST_CASE("The hidden argument of a destructured parameter has no name",
          "[BytecodeExport]") {
    QuickJSFixture f;
    uint16_t argCount = 0;
    CHECK(firstFunctionVarName(f, "function f([a], b) { return a + b; }", 0, &argCount)
          == "<null>");
    CHECK(argCount == 2);
    CHECK(firstFunctionVarName(f, "function f([a], b) { return a + b; }", 1, &argCount)
          == "b");
    CHECK(firstFunctionVarName(f, "function f({x}) { return x; }", 0, &argCount)
          == "<null>");
}

TEST_CASE("Named parameters keep their names", "[BytecodeExport]") {
    QuickJSFixture f;
    uint16_t argCount = 0;
    CHECK(firstFunctionVarName(f, "function f(alpha, beta) {}", 0, &argCount) == "alpha");
    CHECK(firstFunctionVarName(f, "function f(alpha, beta) {}", 1, &argCount) == "beta");
    CHECK(argCount == 2);
}
