/**
 * Geometry addon (ABI v2) for the module-exception test
 * (tests/integration/native_addons/test_module_exceptions.js).
 *
 * protoCore's example module of foreign error types
 * (protoCore/examples/exceptions/geometry_module) is compiled into this addon.
 * Loading the addon registers the module's UMD provider ("example_geometry")
 * and puts "provider:example_geometry" first in the space's resolution chain,
 * so that a script then imports the module the way it imports any UMD module:
 *
 *     require('<path>/geometry_addon');           // installs the provider
 *     const geometry = require('example/geometry');
 *
 * The addon is the smallest host for the provider: protoJS has no other way to
 * register a ModuleProvider from a script, and a test-only executable would
 * not run the interpreter the way `protojs` does.
 */
#include "native/NativeModuleABI.h"
#include "protoCore.h"
#include "GeometryModule.h"

#include <mutex>

namespace protojs {

namespace {

int init_impl(proto::ProtoContext* ctx, const proto::ProtoObject* module) {
    static std::once_flag registered;
    std::call_once(registered, [] { example::registerGeometryProvider(); });

    proto::ProtoSpace* space = ctx->space;
    if (!space) return -1;
    const char* entry = "provider:example_geometry";
    // The provider first, then the chain the space already had.
    const proto::ProtoList* chain = ctx->newList()->appendLast(ctx, ctx->fromUTF8String(entry));
    const proto::ProtoObject* current = space->getResolutionChain();
    const proto::ProtoList* old = (current && current != PROTO_NONE && current->asList(ctx))
        ? current->asList(ctx) : nullptr;
    if (old) {
        for (proto::proto_ulong i = 0; i < old->getSize(ctx); ++i) {
            const proto::ProtoObject* e = old->getAt(ctx, static_cast<int>(i));
            if (e && e->isString(ctx) && e->asString(ctx)->toStdString(ctx) == entry) continue;
            chain = chain->appendLast(ctx, e);
        }
    }
    space->setResolutionChain(chain->asObject(ctx));
    return protojs_set_export(ctx, module, "provider", ctx->fromUTF8String("example_geometry"));
}

}  // namespace

extern "C" {

PROTOJS_ADDON_EXPORT ProtoJSNativeModuleInfo protojs_native_module_info(
    PROTOJS_ABI_VERSION,
    "geometry_addon",
    "1.0.0",
    init_impl,
    nullptr
);

}  // extern "C"
}  // namespace protojs
