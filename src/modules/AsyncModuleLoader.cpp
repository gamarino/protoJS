#include "AsyncModuleLoader.h"
#include "ESModuleLoader.h"
#include "../Deferred.h"
namespace protojs {
JSValue AsyncModuleLoader::dynamicImport(const std::string& specifier, JSContext* ctx) {
    // Create promise capability with resolve/reject functions.
    JSValue resolving_funcs[2];
    JSValue promise = JS_NewPromiseCapability(ctx, resolving_funcs);
    if (JS_IsException(promise)) return promise;

    // Resolve the module relative to the current working directory.
    std::string fromPath = ".";
    JSValue result = ESModuleLoader::loadModule(specifier, fromPath, ctx);

    if (JS_IsException(result)) {
        // On load failure, reject the promise with the pending exception.
        JSValue exc = JS_GetException(ctx);
        JSValue rejectResult = JS_Call(ctx, resolving_funcs[1], JS_UNDEFINED, 1, &exc);
        JS_FreeValue(ctx, rejectResult);
        JS_FreeValue(ctx, exc);
    } else {
        // On success, resolve with the module namespace object.
        JSValue resolveResult = JS_Call(ctx, resolving_funcs[0], JS_UNDEFINED, 1, &result);
        JS_FreeValue(ctx, resolveResult);
        JS_FreeValue(ctx, result);
    }

    JS_FreeValue(ctx, resolving_funcs[0]);
    JS_FreeValue(ctx, resolving_funcs[1]);
    return promise;
}
JSValue AsyncModuleLoader::evaluateAsyncModule(ESModuleRecord* record, JSContext* ctx) {
    // Handle top-level await
    // Simplified - would properly handle async evaluation
    ESModuleLoader::evaluateModule(record, ctx);
    return JS_UNDEFINED;
}
} // namespace protojs
