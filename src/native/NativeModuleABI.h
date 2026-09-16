#ifndef PROTOJS_NATIVEMODULEABI_H
#define PROTOJS_NATIVEMODULEABI_H

#include "headers/protoCore.h"

/**
 * @brief Native Module ABI for protoJS.
 *
 * Defines the Application Binary Interface for native addons (`.node`,
 * `.so` / `.dylib` / `.dll`, `.protojs`).
 *
 * ## ABI v2 — protoCore objects only
 *
 * v1 passed a `JSContext*` and `JSValue`s, so an addon built its exports with
 * the QuickJS C API. Scripts run on the protoCore interpreter, which never
 * sees those values: the exports were converted with `TypeBridge::fromJS`,
 * and a QuickJS function became an empty object, so exported functions were
 * not callable. v2 removes QuickJS from the addon interface entirely — an
 * addon works with `proto::ProtoObject`s, exactly like the runtime's own
 * native modules.
 *
 * v1 addons are rejected at load time with a message telling the author to
 * rebuild.
 */

#define PROTOJS_ABI_VERSION 2

namespace protojs {

/**
 * @brief Native function signature (identical to proto::ProtoMethod).
 *
 * Functions exported from native addons use this signature and are called
 * directly by the interpreter — no bridge, no conversion.
 */
typedef const proto::ProtoObject* (*ProtoJSNativeFunction)(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters
);

/**
 * @brief Native module initialization function.
 *
 * Called once when the addon is loaded.
 *
 * `module` is a mutable protoCore object with CommonJS shape
 * (`id`, `filename`, `exports`, `loaded`, `parent`); its `exports` attribute
 * starts as an empty mutable object. The init function registers everything
 * it exports on that object, most conveniently with `protojs_set_export`.
 *
 * @param context protoCore context
 * @param module  the module object whose `exports` is to be populated
 * @return 0 on success, non-zero on error
 */
typedef int (*ProtoJSNativeModuleInit)(
    proto::ProtoContext* context,
    const proto::ProtoObject* module
);

/**
 * @brief Native module cleanup function (optional).
 */
typedef void (*ProtoJSNativeModuleCleanup)(proto::ProtoContext* context);

/**
 * @brief Native module information structure.
 *
 * Every native addon must export this symbol with C linkage:
 *   extern "C" ProtoJSNativeModuleInfo protojs_native_module_info;
 */
struct ProtoJSNativeModuleInfo {
    int abiVersion;                     // Must match PROTOJS_ABI_VERSION
    const char* name;                   // Module name
    const char* version;                // Module version
    ProtoJSNativeModuleInit init;       // Initialization function
    ProtoJSNativeModuleCleanup cleanup; // Optional, may be nullptr

    ProtoJSNativeModuleInfo()
        : abiVersion(0), name(nullptr), version(nullptr),
          init(nullptr), cleanup(nullptr) {}
    ProtoJSNativeModuleInfo(int ver, const char* n, const char* v,
                            ProtoJSNativeModuleInit i,
                            ProtoJSNativeModuleCleanup c)
        : abiVersion(ver), name(n), version(v), init(i), cleanup(c) {}
};

} // namespace protojs

/**
 * Helpers available to addons.
 *
 * They are implemented in `protojs_core` and linked into the `protojs`
 * executable with `-rdynamic`, so a dlopen'd addon resolves them from the
 * host process; an addon links against nothing.
 */
extern "C" {

/**
 * Wrap a native function so that JavaScript can call it and so that it
 * carries `name` and `length`. Returns nullptr on failure.
 */
const proto::ProtoObject* protojs_make_function(proto::ProtoContext* context,
                                                protojs::ProtoJSNativeFunction fn,
                                                const char* name,
                                                int length);

/**
 * Set `module.exports.<name> = value`. Returns 0 on success.
 */
int protojs_set_export(proto::ProtoContext* context,
                       const proto::ProtoObject* module,
                       const char* name,
                       const proto::ProtoObject* value);

/**
 * Return `module.exports`, or nullptr when it cannot be read.
 */
const proto::ProtoObject* protojs_get_exports(proto::ProtoContext* context,
                                              const proto::ProtoObject* module);

/**
 * Replace `module.exports` wholesale, the equivalent of
 * `module.exports = value`. Returns 0 on success.
 */
int protojs_set_exports_object(proto::ProtoContext* context,
                               const proto::ProtoObject* module,
                               const proto::ProtoObject* exports);

/**
 * Raise a JavaScript exception from inside an addon function. The addon must
 * return PROTO_NONE immediately afterwards. `type` is an error constructor
 * name such as "TypeError" or "RangeError".
 */
void protojs_throw(proto::ProtoContext* context,
                   const char* type,
                   const char* message);

} // extern "C"

#endif // PROTOJS_NATIVEMODULEABI_H
