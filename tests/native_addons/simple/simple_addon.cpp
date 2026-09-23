/**
 * Minimal protoJS native addon (ABI v2) used by the require() tests.
 *
 * Exports:
 *   version : number
 *   sum     : function(a, b) -> a + b
 *   boom    : function() -> throws a TypeError through protojs_throw
 *
 * Note that no QuickJS header is involved: an addon works with protoCore
 * objects, and its functions are called directly by the interpreter.
 */
#include "native/NativeModuleABI.h"
#include "protoCore.h"

namespace protojs {

namespace {

// Read a numeric argument as a double, whatever numeric form it arrived in.
bool numericArg(proto::ProtoContext* ctx, const proto::ProtoObject* v, double& out) {
    if (!v || v == PROTO_NONE) return false;
    if (v->isInteger(ctx)) { out = static_cast<double>(v->asLong(ctx)); return true; }
    if (v->isDouble(ctx) || v->isFloat(ctx)) { out = v->asDouble(ctx); return true; }
    return false;
}

const proto::ProtoObject* sum_impl(proto::ProtoContext* ctx,
                                    const proto::ProtoObject* /*self*/,
                                    const proto::ParentLink*,
                                    const proto::ProtoList* args,
                                    const proto::ProtoSparseList*) {
    if (!args || args->getSize(ctx) < 2) {
        protojs_throw(ctx, "TypeError", "sum expects two numbers");
        return PROTO_NONE;
    }
    double a = 0, b = 0;
    if (!numericArg(ctx, args->getAt(ctx, 0), a) ||
        !numericArg(ctx, args->getAt(ctx, 1), b)) {
        protojs_throw(ctx, "TypeError", "sum expects two numbers");
        return PROTO_NONE;
    }
    const double total = a + b;
    const long long asInt = static_cast<long long>(total);
    if (static_cast<double>(asInt) == total) return ctx->fromInteger(asInt);
    return ctx->fromDouble(total);
}

// Used by the tests to check that an addon can raise a catchable exception.
const proto::ProtoObject* boom_impl(proto::ProtoContext* ctx,
                                     const proto::ProtoObject* /*self*/,
                                     const proto::ParentLink*,
                                     const proto::ProtoList*,
                                     const proto::ProtoSparseList*) {
    protojs_throw(ctx, "TypeError", "addon refused");
    return PROTO_NONE;
}

int init_impl(proto::ProtoContext* ctx, const proto::ProtoObject* module) {
    if (protojs_set_export(ctx, module, "version", ctx->fromInteger(1)) != 0) return -1;
    if (protojs_set_export(ctx, module, "sum",
                            protojs_make_function(ctx, sum_impl, "sum", 2)) != 0) return -1;
    if (protojs_set_export(ctx, module, "boom",
                            protojs_make_function(ctx, boom_impl, "boom", 0)) != 0) return -1;
    return 0;
}

} // namespace

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
