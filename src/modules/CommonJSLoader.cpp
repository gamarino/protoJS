#include "CommonJSLoader.h"
#include "ModuleResolver.h"
#include "ModuleCache.h"
#include "../JSContext.h"
#include "../TypeBridge.h"
#include "../ProtoJSStringCache.h"
#include "../runtime/ProtoCompileOnly.h"
#include "../runtime/ProtoBytecodeModule.h"
#include "../runtime/ProtoInterpreter.h"
#include "../native/DynamicLibraryLoader.h"
#include "headers/protoCore.h"
#include <string>
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

static bool isBareSpecifier(const std::string& specifier) {
    if (specifier.empty()) return false;
    if (specifier[0] == '.') {
        if (specifier.size() >= 2 && specifier[1] == '.') return false;
        return false;
    }
    if (specifier[0] == '/') return false;
    return true;
}

JSValue CommonJSLoader::require(
    const std::string& specifier,
    const std::string& fromPath,
    JSContext* ctx
) {
    // Unified Module Discovery (protoCore): try ProtoSpace::getImportModule first for bare specifiers
    if (isBareSpecifier(specifier)) {
        JSContextWrapper* wrapper = static_cast<JSContextWrapper*>(JS_GetContextOpaque(ctx));
        proto::ProtoSpace* space = wrapper ? wrapper->getProtoSpace() : nullptr;
        proto::ProtoContext* pContext = wrapper ? wrapper->getProtoContext() : nullptr;
        if (space && pContext) {
            const std::string umdCacheKey = "umd:" + specifier;
            {
                std::lock_guard<std::mutex> lock(cacheMutex);
                auto it = moduleCache.find(umdCacheKey);
                if (it != moduleCache.end()) {
                    return JS_DupValue(ctx, it->second);
                }
            }
            const proto::ProtoObject* umdWrapper = space->getImportModule(pContext, specifier.c_str(), "exports");
            if (umdWrapper && umdWrapper != PROTO_NONE) {
                const proto::ProtoString* exportsName = proto::ProtoString::fromUTF8String(pContext, "exports");
                if (exportsName) {
                    const proto::ProtoObject* exportsObj = umdWrapper->getAttribute(pContext, exportsName);
                    if (exportsObj && exportsObj != PROTO_NONE) {
                        JSValue jsv = TypeBridge::toJS(ctx, exportsObj, pContext);
                        if (!JS_IsException(jsv)) {
                            std::lock_guard<std::mutex> lock(cacheMutex);
                            moduleCache[umdCacheKey] = JS_DupValue(ctx, jsv);
                            return jsv;
                        }
                        JS_FreeValue(ctx, jsv);
                    }
                }
            }
        }
    }

    // Built-in modules: resolve from global object (Node.js-style require('fs'), require('path'), etc.)
    if (isBareSpecifier(specifier)) {
        JSValue global_obj = JS_GetGlobalObject(ctx);
        JSValue builtin = JS_UNDEFINED;
        if (specifier == "buffer") {
            builtin = JS_GetPropertyStr(ctx, global_obj, "Buffer");
            if (!JS_IsUndefined(builtin)) {
                JSValue exports = JS_NewObject(ctx);
                JS_SetPropertyStr(ctx, exports, "Buffer", builtin);
                JS_FreeValue(ctx, builtin);
                builtin = exports;
            }
        } else {
            builtin = JS_GetPropertyStr(ctx, global_obj, specifier.c_str());
        }
        JS_FreeValue(ctx, global_obj);
        if (!JS_IsUndefined(builtin)) {
            std::string cacheKey = "builtin:" + specifier;
            {
                std::lock_guard<std::mutex> lock(cacheMutex);
                auto it = moduleCache.find(cacheKey);
                if (it != moduleCache.end()) {
                    JS_FreeValue(ctx, builtin);
                    return JS_DupValue(ctx, it->second);
                }
                moduleCache[cacheKey] = JS_DupValue(ctx, builtin);
            }
            return builtin;
        }
    }

    // Resolve module (file-based)
    ResolveResult resolved = ModuleResolver::resolve(specifier, fromPath, ctx);
    if (resolved.filePath.empty()) {
        return JS_ThrowTypeError(ctx, "%s", ("Cannot find module: " + specifier).c_str());
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
            return JS_ThrowTypeError(ctx, "%s", ("Cannot load native module: " + resolved.filePath).c_str());
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
        return JS_ThrowTypeError(ctx, "%s", ("Cannot open module: " + resolved.filePath).c_str());
    }
    
    std::stringstream buffer;
    buffer << file.rdbuf();
    std::string source = buffer.str();
    
    // Create module object
    JSValue moduleObj = createModuleObject(resolved.filePath, ctx);
    
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
        auto it = moduleCache.find(cacheKey);
        if (it != moduleCache.end()) {
            JS_FreeValueRT(JS_GetRuntime(ctx), it->second);
        }
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
    
    return moduleObj;
}

JSValue CommonJSLoader::requireResolve(
    const std::string& specifier,
    const std::string& fromPath,
    JSContext* ctx
) {
    ResolveResult resolved = ModuleResolver::resolve(specifier, fromPath, ctx);
    if (resolved.filePath.empty()) {
        return JS_ThrowTypeError(ctx, "%s", ("Cannot resolve module: " + specifier).c_str());
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
    // Create CommonJS wrapper: script that evaluates to the wrapper function
    std::string wrapped = "(function(exports, require, module, __filename, __dirname) {\n";
    wrapped += source;
    wrapped += "\n});";

    std::string dirname = ModuleResolver::getDirectory(filename);

    JSContextWrapper* wrapper = static_cast<JSContextWrapper*>(JS_GetContextOpaque(ctx));
    proto::ProtoContext* pContext = wrapper ? wrapper->getProtoContext() : nullptr;
    if (!pContext) {
        return JS_ThrowInternalError(ctx, "CommonJSLoader: no ProtoContext");
    }

    // Build arguments as JSValues for registration and conversion
    JSValue moduleObj = createModuleObject(filename, ctx);
    JSValue exportsObj = JS_GetPropertyStr(ctx, moduleObj, "exports");
    JSValue requireFunc = JS_NewCFunction(ctx, requireImpl, "require", 1);
    JSValue filenameVal = JS_NewString(ctx, filename.c_str());
    JSValue dirnameVal = JS_NewString(ctx, dirname.c_str());

    // Compile and load via protoCore path
    void* bytecode = protojs::compileToBytecode(ctx, wrapped.c_str(), wrapped.size(), filename.c_str());
    if (!bytecode) {
        JSValue ex = JS_GetException(ctx);
        JS_FreeValue(ctx, exportsObj);
        JS_FreeValue(ctx, moduleObj);
        JS_FreeValue(ctx, requireFunc);
        JS_FreeValue(ctx, filenameVal);
        JS_FreeValue(ctx, dirnameVal);
        return ex;
    }

    protojs::ProtoBytecodeModule module;
    proto::ProtoContext frameCtx(pContext->space, pContext, nullptr, nullptr, nullptr, nullptr);
    if (!protojs::loadBytecode(ctx, bytecode, &frameCtx, &module)) {
        JS_FreeValue(ctx, exportsObj);
        JS_FreeValue(ctx, moduleObj);
        JS_FreeValue(ctx, requireFunc);
        JS_FreeValue(ctx, filenameVal);
        JS_FreeValue(ctx, dirnameVal);
        return JS_EXCEPTION;
    }

    JSValue globalVal = JS_GetGlobalObject(ctx);
    const proto::ProtoObject* globalObj = TypeBridge::fromJS(ctx, globalVal, &frameCtx);
    JS_FreeValue(ctx, globalVal);

    // Run top-level script; result is the wrapper function
    const proto::ProtoObject* wrapperFunc = protojs::runBytecode(&frameCtx, &module, globalObj, nullptr, &globalObj, ctx);
    frameCtx.returnValue = wrapperFunc;

    if (!wrapperFunc || wrapperFunc == PROTO_NONE) {
        JS_FreeValue(ctx, exportsObj);
        JS_FreeValue(ctx, moduleObj);
        JS_FreeValue(ctx, requireFunc);
        JS_FreeValue(ctx, filenameVal);
        JS_FreeValue(ctx, dirnameVal);
        return JS_EXCEPTION;
    }

    // Convert the five arguments to ProtoObjects and call the wrapper function
    const proto::ProtoObject* exportsProto = TypeBridge::fromJS(ctx, exportsObj, &frameCtx);
    const proto::ProtoObject* requireProto = TypeBridge::fromJS(ctx, requireFunc, &frameCtx);
    const proto::ProtoObject* moduleProto = TypeBridge::fromJS(ctx, moduleObj, &frameCtx);
    const proto::ProtoObject* filenameProto = TypeBridge::fromJS(ctx, filenameVal, &frameCtx);
    const proto::ProtoObject* dirnameProto = TypeBridge::fromJS(ctx, dirnameVal, &frameCtx);

    const proto::ProtoList* argsList = pContext->newList();
    if (argsList) {
        argsList = argsList->appendLast(&frameCtx, exportsProto ? exportsProto : PROTO_NONE);
        argsList = argsList->appendLast(&frameCtx, requireProto ? requireProto : PROTO_NONE);
        argsList = argsList->appendLast(&frameCtx, moduleProto ? moduleProto : PROTO_NONE);
        argsList = argsList->appendLast(&frameCtx, filenameProto ? filenameProto : PROTO_NONE);
        argsList = argsList->appendLast(&frameCtx, dirnameProto ? dirnameProto : PROTO_NONE);
    }

    const proto::ProtoString* key = ProtoJSStringCache::getKey(&frameCtx, "__bytecode_id__");
    const proto::ProtoObject* idVal = wrapperFunc->getAttribute(&frameCtx, key, false);
    int bcId = (idVal && idVal != PROTO_NONE && idVal->isInteger(&frameCtx)) ? static_cast<int>(idVal->asLong(&frameCtx)) : -1;

    JSValue resultVal = JS_UNDEFINED;
    if (bcId >= 0 && static_cast<size_t>(bcId) < module.nestedFunctions.size() && argsList) {
        proto::ProtoContext childCtx(pContext->space, &frameCtx, nullptr, nullptr, nullptr, nullptr);
        for (int i = 0; i < 5; i++) {
            std::string s = std::to_string(i);
            const proto::ProtoObject* o = childCtx.fromUTF8String(s.c_str());
            const proto::ProtoString* ps = o ? o->asString(&childCtx) : nullptr;
            unsigned long slotK = ps ? static_cast<unsigned long>(ps->getHash(&childCtx)) : 0;
            const proto::ProtoObject* arg = argsList->getAt(&childCtx, i);
            if (childCtx.closureLocals)
                childCtx.closureLocals = childCtx.closureLocals->setAt(&childCtx, slotK, arg ? arg : PROTO_NONE);
        }
        const proto::ProtoObject* result = protojs::runBytecode(&childCtx, &module.nestedFunctions[static_cast<size_t>(bcId)], PROTO_NONE, argsList, &globalObj, ctx);
        childCtx.returnValue = result;
        resultVal = TypeBridge::toJS(ctx, result ? result : PROTO_NONE, &frameCtx);
    }

    JS_SetPropertyStr(ctx, moduleObj, "loaded", JS_NewBool(ctx, true));
    JS_FreeValue(ctx, exportsObj);
    JS_FreeValue(ctx, moduleObj);
    JS_FreeValue(ctx, requireFunc);
    JS_FreeValue(ctx, filenameVal);
    JS_FreeValue(ctx, dirnameVal);

    if (JS_IsException(resultVal)) {
        return resultVal;
    }
    JS_FreeValue(ctx, resultVal);
    return JS_UNDEFINED;
}

} // namespace protojs
