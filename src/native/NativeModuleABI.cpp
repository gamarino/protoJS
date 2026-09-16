#include "NativeModuleABI.h"
#include "../FunctionPrototype.h"
#include "../JSSymbols.h"
#include "../runtime/ProtoInterpreter.h"

// Implementation of the ABI v2 helper functions that native addons call.
//
// These live in protojs_core and are exported from the `protojs` executable
// (linked with -rdynamic), so a dlopen'd addon resolves them from the host
// process without linking against anything.

namespace {

const proto::ProtoString* attrKey(proto::ProtoContext* context, const char* name) {
    if (!context || !name) return nullptr;
    const proto::ProtoObject* nameObj = context->fromUTF8String(name);
    return nameObj ? nameObj->asString(context) : nullptr;
}

} // namespace

extern "C" {

const proto::ProtoObject* protojs_make_function(proto::ProtoContext* context,
                                                protojs::ProtoJSNativeFunction fn,
                                                const char* name,
                                                int length) {
    if (!context || !fn) return nullptr;
    // ProtoJSNativeFunction and proto::ProtoMethod are the same signature.
    return protojs::wrapNativeFunction(context,
                                        reinterpret_cast<proto::ProtoMethod>(fn),
                                        name ? name : "anonymous",
                                        static_cast<long long>(length),
                                        /*globalRoot=*/nullptr);
}

const proto::ProtoObject* protojs_get_exports(proto::ProtoContext* context,
                                              const proto::ProtoObject* module) {
    if (!context || !module || module == PROTO_NONE) return nullptr;
    const proto::ProtoString* key = protojs::JSSymbols::exports(context);
    if (!key) return nullptr;
    const proto::ProtoObject* exports = module->getAttribute(context, key, false);
    return (exports && exports != PROTO_NONE) ? exports : nullptr;
}

int protojs_set_export(proto::ProtoContext* context,
                       const proto::ProtoObject* module,
                       const char* name,
                       const proto::ProtoObject* value) {
    if (!context || !name) return -1;
    const proto::ProtoObject* exports = protojs_get_exports(context, module);
    if (!exports) return -1;
    const proto::ProtoString* key = attrKey(context, name);
    if (!key) return -1;
    // The exports object is mutable, so this publishes in place.
    exports->setAttribute(context, key, value ? value : PROTO_NONE);
    return 0;
}

int protojs_set_exports_object(proto::ProtoContext* context,
                               const proto::ProtoObject* module,
                               const proto::ProtoObject* exports) {
    if (!context || !module || module == PROTO_NONE || !exports) return -1;
    const proto::ProtoString* key = protojs::JSSymbols::exports(context);
    if (!key) return -1;
    module->setAttribute(context, key, exports);
    return 0;
}

void protojs_throw(proto::ProtoContext* context,
                   const char* type,
                   const char* message) {
    if (!context) return;
    protojs::signalNativeException(
        protojs::makeNativeError(context,
                                  type ? type : "Error",
                                  message ? message : ""));
}

} // extern "C"
