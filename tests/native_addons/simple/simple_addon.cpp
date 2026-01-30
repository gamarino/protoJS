/**
 * Minimal protoJS native addon for testing require() of shared libraries.
 * Exports: version (number), sum (function).
 */
#include "native/NativeModuleABI.h"
#include "quickjs.h"
#include "headers/protoCore.h"
#include <cstdlib>

namespace protojs {

static JSValue sum_impl(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_NewInt32(ctx, 0);
    int32_t a = 0, b = 0;
    JS_ToInt32(ctx, &a, argv[0]);
    JS_ToInt32(ctx, &b, argv[1]);
    return JS_NewInt32(ctx, a + b);
}

static int init_impl(JSContext* ctx, proto::ProtoContext* pContext, JSValue moduleObject) {
    (void)pContext;
    JSValue exports = JS_GetPropertyStr(ctx, moduleObject, "exports");
    if (JS_IsException(exports)) return -1;
    JS_SetPropertyStr(ctx, exports, "version", JS_NewInt32(ctx, 1));
    JS_SetPropertyStr(ctx, exports, "sum", JS_NewCFunction(ctx, sum_impl, "sum", 2));
    return 0;
}

extern "C" {

ProtoJSNativeModuleInfo protojs_native_module_info(
    PROTOJS_ABI_VERSION,
    "simple_addon",
    "1.0.0",
    init_impl,
    nullptr
);

} // extern "C"
} // namespace protojs
