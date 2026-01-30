#include "CommonJSLoader.h"
#include "ModuleResolver.h"
#include "ModuleCache.h"
#include "../JSContext.h"
#include "../native/DynamicLibraryLoader.h"
#include <fstream>
#include <sstream>
#include <mutex>

namespace protojs {

std::map<std::string, JSValue> CommonJSLoader::moduleCache;
std::mutex CommonJSLoader::cacheMutex;

void CommonJSLoader::init(JSContext* ctx) {
    JSValue requireFunc = JS_NewCFunction(ctx, requireImpl, "require", 1);
    
    // Add require.resolve
    JS_SetPropertyStr(ctx, requireFunc, "resolve", 
                     JS_NewCFunction(ctx, requireResolveImpl, "resolve", 1));
    
    // Add require.cache
    JSValue cacheObj = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, requireFunc, "cache", cacheObj);
    
    JSValue global_obj = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global_obj, "require", requireFunc);
    JS_FreeValue(ctx, global_obj);
}

JSValue CommonJSLoader::require(
    const std::string& specifier,
    const std::string& fromPath,
    JSContext* ctx
) {
    // Resolve module
    ResolveResult resolved = ModuleResolver::resolve(specifier, fromPath, ctx);
    if (resolved.filePath.empty()) {
        return JS_ThrowTypeError(ctx, ("Cannot find module: " + specifier).c_str());
    }
    
    std::string cacheKey = resolved.filePath;
    
    // Check cache
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        auto it = moduleCache.find(cacheKey);
        if (it != moduleCache.end()) {
            return JS_DupValue(ctx, it->second);
        }
    }
    
    // Native module: load shared library and run init
    if (resolved.type == ModuleType::Native) {
        LoadedModule* loaded = DynamicLibraryLoader::load(resolved.filePath);
        if (!loaded) {
            return JS_ThrowTypeError(ctx, ("Cannot load native module: " + resolved.filePath).c_str());
        }
        JSContextWrapper* wrapper = static_cast<JSContextWrapper*>(JS_GetContextOpaque(ctx));
        proto::ProtoContext* pContext = wrapper ? wrapper->getProtoContext() : nullptr;
        if (!pContext) {
            DynamicLibraryLoader::unload(loaded);
            return JS_ThrowTypeError(ctx, "Native module load: ProtoContext not available");
        }
        JSValue moduleObj = createModuleObject(resolved.filePath, ctx);
        JSValue exports = DynamicLibraryLoader::initializeModule(loaded, ctx, pContext, moduleObj);
        JS_FreeValue(ctx, moduleObj);
        if (JS_IsException(exports)) {
            DynamicLibraryLoader::unload(loaded);
            return exports;
        }
        // Keep library loaded (no unload); cache exports
        {
            std::lock_guard<std::mutex> lock(cacheMutex);
            moduleCache[cacheKey] = JS_DupValue(ctx, exports);
        }
        return exports;
    }
    
    // JavaScript module: read source and evaluate
    std::ifstream file(resolved.filePath);
    if (!file.is_open()) {
        return JS_ThrowTypeError(ctx, ("Cannot open module: " + resolved.filePath).c_str());
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();
    
    // Create module object
    JSValue moduleObj = createModuleObject(resolved.filePath, ctx);
    
    // Cache module immediately (for circular dependency handling)
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        moduleCache[cacheKey] = JS_DupValue(ctx, moduleObj);
    }
    
    // Wrap and execute module
    JSValue wrapped = wrapModule(source, resolved.filePath, ctx);
    if (JS_IsException(wrapped)) {
        // Remove from cache on error
        std::lock_guard<std::mutex> lock(cacheMutex);
        moduleCache.erase(cacheKey);
        return wrapped;
    }
    
    // Get module.exports
    JSValue exports = JS_GetPropertyStr(ctx, moduleObj, "exports");
    
    // Update cache with final exports
    {
        std::lock_guard<std::mutex> lock(cacheMutex);
        JS_FreeValueRT(JS_GetRuntime(ctx), moduleCache[cacheKey]);
        moduleCache[cacheKey] = JS_DupValue(ctx, exports);
    }
    
    JS_FreeValue(ctx, wrapped);
    JS_FreeValue(ctx, moduleObj);
    
    return exports;
}

JSValue CommonJSLoader::createModuleObject(
    const std::string& filePath,
    JSContext* ctx
) {
    JSValue moduleObj = JS_NewObject(ctx);
    JSValue exportsObj = JS_NewObject(ctx);
    
    JS_SetPropertyStr(ctx, moduleObj, "exports", exportsObj);
    JS_SetPropertyStr(ctx, moduleObj, "id", JS_NewString(ctx, filePath.c_str()));
    JS_SetPropertyStr(ctx, moduleObj, "filename", JS_NewString(ctx, filePath.c_str()));
    
    // __dirname and __filename will be set in wrapper
    JS_SetPropertyStr(ctx, moduleObj, "loaded", JS_NewBool(ctx, false));
    JS_SetPropertyStr(ctx, moduleObj, "children", JS_NewArray(ctx));
    JS_SetPropertyStr(ctx, moduleObj, "parent", JS_NULL);
    
    JS_FreeValue(ctx, exportsObj);
    return moduleObj;
}

JSValue CommonJSLoader::requireResolve(
    const std::string& specifier,
    const std::string& fromPath,
    JSContext* ctx
) {
    ResolveResult resolved = ModuleResolver::resolve(specifier, fromPath, ctx);
    if (resolved.filePath.empty()) {
        return JS_ThrowTypeError(ctx, ("Cannot resolve module: " + specifier).c_str());
    }
    return JS_NewString(ctx, resolved.filePath.c_str());
}

JSValue CommonJSLoader::requireImpl(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "require expects a module specifier");
    }
    
    const char* specifier = JS_ToCString(ctx, argv[0]);
    if (!specifier) {
        return JS_EXCEPTION;
    }
    
    // Get calling module's path (from __filename)
    JSValue global_obj = JS_GetGlobalObject(ctx);
    JSValue filename = JS_GetPropertyStr(ctx, global_obj, "__filename");
    std::string fromPath = ".";
    if (!JS_IsUndefined(filename)) {
        const char* fn = JS_ToCString(ctx, filename);
        if (fn) {
            fromPath = fn;
            JS_FreeCString(ctx, fn);
        }
        JS_FreeValue(ctx, filename);
    }
    JS_FreeValue(ctx, global_obj);
    
    JSValue result = require(specifier, fromPath, ctx);
    JS_FreeCString(ctx, specifier);
    
    return result;
}

JSValue CommonJSLoader::requireResolveImpl(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "require.resolve expects a module specifier");
    }
    
    const char* specifier = JS_ToCString(ctx, argv[0]);
    if (!specifier) {
        return JS_EXCEPTION;
    }
    
    JSValue global_obj = JS_GetGlobalObject(ctx);
    JSValue filename = JS_GetPropertyStr(ctx, global_obj, "__filename");
    std::string fromPath = ".";
    if (!JS_IsUndefined(filename)) {
        const char* fn = JS_ToCString(ctx, filename);
        if (fn) {
            fromPath = fn;
            JS_FreeCString(ctx, fn);
        }
        JS_FreeValue(ctx, filename);
    }
    JS_FreeValue(ctx, global_obj);
    
    JSValue result = requireResolve(specifier, fromPath, ctx);
    JS_FreeCString(ctx, specifier);
    
    return result;
}

JSValue CommonJSLoader::wrapModule(
    const std::string& source,
    const std::string& filename,
    JSContext* ctx
) {
    // Create CommonJS wrapper
    std::string wrapped = "(function(exports, require, module, __filename, __dirname) {\n";
    wrapped += source;
    wrapped += "\n});";
    
    // Get directory
    std::string dirname = ModuleResolver::getDirectory(filename);
    
    // Get module object
    JSValue moduleObj = createModuleObject(filename, ctx);
    JSValue exportsObj = JS_GetPropertyStr(ctx, moduleObj, "exports");
    
    // Create require function for this module
    JSValue requireFunc = JS_NewCFunction(ctx, requireImpl, "require", 1);
    
    // Evaluate wrapped code
    JSValue func = JS_Eval(ctx, wrapped.c_str(), wrapped.length(), filename.c_str(), JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(func)) {
        JS_FreeValue(ctx, exportsObj);
        JS_FreeValue(ctx, moduleObj);
        JS_FreeValue(ctx, requireFunc);
        return func;
    }
    
    // Call wrapper function
    JSValue args[] = {
        exportsObj,
        requireFunc,
        moduleObj,
        JS_NewString(ctx, filename.c_str()),
        JS_NewString(ctx, dirname.c_str())
    };
    
    JSValue result = JS_Call(ctx, func, JS_UNDEFINED, 5, args);
    
    // Mark module as loaded
    JS_SetPropertyStr(ctx, moduleObj, "loaded", JS_NewBool(ctx, true));
    
    JS_FreeValue(ctx, func);
    JS_FreeValue(ctx, requireFunc);
    JS_FreeValue(ctx, args[3]); // __filename
    JS_FreeValue(ctx, args[4]); // __dirname
    JS_FreeValue(ctx, exportsObj);
    JS_FreeValue(ctx, moduleObj);
    
    if (JS_IsException(result)) {
        return result;
    }
    
    JS_FreeValue(ctx, result);
    return JS_UNDEFINED;
}

} // namespace protojs
