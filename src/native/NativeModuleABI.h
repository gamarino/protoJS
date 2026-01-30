#ifndef PROTOJS_NATIVEMODULEABI_H
#define PROTOJS_NATIVEMODULEABI_H

#include "quickjs.h"
#include "headers/protoCore.h"

/**
 * @brief Native Module ABI for protoJS.
 * 
 * Defines the Application Binary Interface for native modules (.protojs files).
 * Based on protoCore's ProtoMethod signature and object model.
 */

#define PROTOJS_ABI_VERSION 1

namespace protojs {

/**
 * @brief Native module initialization function signature.
 * 
 * Called when module is loaded to initialize exports.
 * 
 * The moduleObject has CommonJS shape: { id, filename, exports, loaded, children, parent }.
 * The loader creates it with an empty "exports" object. The init function must register
 * all exported values on moduleObject.exports (e.g. via JS_SetPropertyStr(ctx, exports, "key", value)).
 * 
 * @param ctx QuickJS context
 * @param pContext protoCore context
 * @param moduleObject JavaScript module object (with "exports" property to populate)
 * @return 0 on success, non-zero on error
 */
typedef int (*ProtoJSNativeModuleInit)(
    JSContext* ctx,
    proto::ProtoContext* pContext,
    JSValue moduleObject
);

/**
 * @brief Native module cleanup function signature.
 * 
 * Called when module is unloaded.
 * 
 * @param ctx QuickJS context
 */
typedef void (*ProtoJSNativeModuleCleanup)(JSContext* ctx);

/**
 * @brief Native function signature (matches protoCore ProtoMethod).
 * 
 * Functions exported from native modules use this signature.
 */
typedef const proto::ProtoObject* (*ProtoJSNativeFunction)(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters
);

/**
 * @brief Exception information structure.
 */
struct ProtoJSException {
    const char* message;
    const char* type;
    int code;
    
    ProtoJSException() : message(nullptr), type(nullptr), code(0) {}
    ProtoJSException(const char* msg, const char* t, int c) 
        : message(msg), type(t), code(c) {}
};

/**
 * @brief Native module information structure.
 * 
 * Every native module must export this symbol:
 * extern "C" ProtoJSNativeModuleInfo protojs_native_module_info;
 */
struct ProtoJSNativeModuleInfo {
    int abiVersion;                    // Must match PROTOJS_ABI_VERSION
    const char* name;                  // Module name
    const char* version;               // Module version
    ProtoJSNativeModuleInit init;      // Initialization function
    ProtoJSNativeModuleCleanup cleanup; // Cleanup function (optional, can be nullptr)
    
    ProtoJSNativeModuleInfo() 
        : abiVersion(0), name(nullptr), version(nullptr), 
          init(nullptr), cleanup(nullptr) {}
    ProtoJSNativeModuleInfo(int ver, const char* n, const char* v, ProtoJSNativeModuleInit i, ProtoJSNativeModuleCleanup c)
        : abiVersion(ver), name(n), version(v), init(i), cleanup(c) {}
};

} // namespace protojs

#endif // PROTOJS_NATIVEMODULEABI_H
