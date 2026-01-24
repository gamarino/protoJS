#include "AsyncModuleLoader.h"
#include "ESModuleLoader.h"
#include "../Deferred.h"
namespace protojs {
JSValue AsyncModuleLoader::dynamicImport(const std::string& specifier, JSContext* ctx) {
    // Get current module path (simplified)
    std::string fromPath = ".";
    JSValue result = ESModuleLoader::loadModule(specifier, fromPath, ctx);
    if (JS_IsException(result)) return result;
    // Wrap in Promise
    JSValue promise = JS_NewPromiseCapability(ctx);
    JSValue resolve = JS_GetPropertyStr(ctx, promise, "resolve");
    JSValue args[] = { result };
    JSValue resolveResult = JS_Call(ctx, resolve, JS_UNDEFINED, 1, args);
    JS_FreeValue(ctx, resolveResult);
    JS_FreeValue(ctx, resolve);
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
