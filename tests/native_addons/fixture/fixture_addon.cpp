/**
 * Fixture native addon (ABI v2) for the resolution-order test.
 * Exports type: 'native', so test_resolution.js can verify that the native
 * addon was preferred over the sibling fixture.js.
 */
#include "native/NativeModuleABI.h"
#include "headers/protoCore.h"

namespace protojs {

namespace {

int init_impl(proto::ProtoContext* ctx, const proto::ProtoObject* module) {
    return protojs_set_export(ctx, module, "type", ctx->fromUTF8String("native"));
}

} // namespace

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
