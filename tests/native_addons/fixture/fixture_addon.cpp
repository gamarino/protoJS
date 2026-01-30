/**
 * Fixture native addon for resolution-order tests.
 * Exports type: 'native' so test_resolution.js can verify native was loaded first.
 */
#include "native/NativeModuleABI.h"
#include "quickjs.h"
#include "headers/protoCore.h"

namespace protojs {

static int init_impl(JSContext* ctx, proto::ProtoContext* pContext, JSValue moduleObject) {
    (void)pContext;
    JSValue exports = JS_GetPropertyStr(ctx, moduleObject, "exports");
    if (JS_IsException(exports)) return -1;
    JS_SetPropertyStr(ctx, exports, "type", JS_NewString(ctx, "native"));
    return 0;
}

extern "C" {

ProtoJSNativeModuleInfo protojs_native_module_info(
    PROTOJS_ABI_VERSION,
    "fixture_addon",
    "1.0.0",
    init_impl,
    nullptr
);

} // extern "C"
} // namespace protojs
