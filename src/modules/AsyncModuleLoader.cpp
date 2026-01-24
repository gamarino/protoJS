#include "AsyncModuleLoader.h"
#include "ESModuleLoader.h"
#include "../Deferred.h"
namespace protojs {
JSValue AsyncModuleLoader::dynamicImport(const std::string& specifier, JSContext* ctx) {
    // Get current module path (simplified)
    std::string fromPath = ".";
    JSValue result = ESModuleLoader::loadModule(specifier, fromPath, ctx);
    if (JS_IsException(result)) return result;
    
    // Create promise capability with resolve/reject functions
    JSValue resolving_funcs[2];
    JSValue promise = JS_NewPromiseCapability(ctx, resolving_funcs);
    
    JSValue args[] = { result };
    JSValue resolveResult = JS_Call(ctx, resolving_funcs[0], JS_UNDEFINED, 1, args);
    JS_FreeValue(ctx, resolveResult);
    JS_FreeValue(ctx, resolving_funcs[0]);
    JS_FreeValue(ctx, resolving_funcs[1]);
    JS_FreeValue(ctx, result);
    return promise;
}
JSValue AsyncModuleLoader::evaluateAsyncModule(ESModuleRecord* record, JSContext* ctx) {
    // Handle top-level await
    // Simplified - would properly handle async evaluation
    ESModuleLoader::evaluateModule(record, ctx);
    return JS_UNDEFINED;
}
} // namespace protojs
