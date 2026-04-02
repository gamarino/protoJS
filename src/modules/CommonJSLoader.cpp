#include "CommonJSLoader.h"
#include "ModuleResolver.h"
#include "ModuleCache.h"
#include "../GCBridge.h"
#include "../JSContext.h"
#include "../TypeBridge.h"
#include "../JSSymbols.h"
#include "../runtime/ProtoCompileOnly.h"
#include "../runtime/ProtoBytecodeModule.h"
#include "../runtime/ProtoInterpreter.h"
#include "../native/DynamicLibraryLoader.h"
#include "headers/protoCore.h"
#include <string>
#include <fstream>
#include <sstream>
#include <mutex>
#include <iostream>

namespace protojs {

void CommonJSLoader::init(JSContext* ctx) {
    std::cerr << "[CommonJSLoader] init" << std::endl;
    // For QuickJS path
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

    // For protoCore interpreter path
    JSContextWrapper* wrapper = static_cast<JSContextWrapper*>(JS_GetContextOpaque(ctx));
    if (wrapper) {
        proto::ProtoContext* pCtx = wrapper->getProtoContext();
        const proto::ProtoObject* nativeGlobal = wrapper->getNativeGlobal();
        if (pCtx && nativeGlobal) {
            const proto::ProtoString* requireKey = ProtoJSStringCache::getKey(pCtx, "require");
            const proto::ProtoObject* requireMethod = pCtx->fromMethod(const_cast<proto::ProtoObject*>(nativeGlobal), requireProtoMethod);
            const proto::ProtoObject* updatedGlobal = nativeGlobal->setAttribute(pCtx, requireKey, requireMethod);
            wrapper->updateNativeGlobal(updatedGlobal);
        }
    }
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
    std::cerr << "[CommonJSLoader] require: " << specifier << " from " << fromPath << std::endl << std::flush;
    JSContextWrapper* wrapper = static_cast<JSContextWrapper*>(JS_GetContextOpaque(ctx));
    if (!wrapper) {
        return JS_ThrowTypeError(ctx, "CommonJSLoader: JSContextWrapper not found");
    }

    // Unified Module Discovery (protoCore): try ProtoSpace::getImportModule first for bare specifiers
    if (isBareSpecifier(specifier)) {
        proto::ProtoSpace* space = wrapper->getProtoSpace();
        proto::ProtoContext* pContext = wrapper->getProtoContext();
        if (space && pContext) {
            const std::string umdCacheKey = "umd:" + specifier;
            {
                std::lock_guard<std::mutex> lock(wrapper->getCJSCacheMutex());
                auto& cache = wrapper->getCJSCache();
                auto it = cache.find(umdCacheKey);
                if (it != cache.end()) {
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
                            std::lock_guard<std::mutex> lock(wrapper->getCJSCacheMutex());
                            wrapper->getCJSCache()[umdCacheKey] = JS_DupValue(ctx, jsv);
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
                std::lock_guard<std::mutex> lock(wrapper->getCJSCacheMutex());
                auto& cache = wrapper->getCJSCache();
                auto it = cache.find(cacheKey);
                if (it != cache.end()) {
                    JS_FreeValue(ctx, builtin);
                    return JS_DupValue(ctx, it->second);
                }
                cache[cacheKey] = JS_DupValue(ctx, builtin);
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
        std::lock_guard<std::mutex> lock(wrapper->getCJSCacheMutex());
        auto& cache = wrapper->getCJSCache();
        auto it = cache.find(cacheKey);
        if (it != cache.end()) {
            return JS_DupValue(ctx, it->second);
        }
    }
    
    // Native module: load shared library and run init
    if (resolved.type == ModuleType::Native) {
        LoadedModule* loaded = DynamicLibraryLoader::load(resolved.filePath);
        if (!loaded) {
            return JS_ThrowTypeError(ctx, "%s", ("Cannot load native module: " + resolved.filePath).c_str());
        }
        proto::ProtoContext* pContext = wrapper->getProtoContext();
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
            std::lock_guard<std::mutex> lock(wrapper->getCJSCacheMutex());
            wrapper->getCJSCache()[cacheKey] = JS_DupValue(ctx, exports);
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
    JSValue exportsObj = JS_GetPropertyStr(ctx, moduleObj, "exports");
    JSValue requireFunc = JS_NewCFunction(ctx, requireImpl, "require", 1);
    JSValue filenameVal = JS_NewString(ctx, resolved.filePath.c_str());
    std::string dirname = ModuleResolver::getDirectory(resolved.filePath);
    JSValue dirnameVal = JS_NewString(ctx, dirname.c_str());

    // Execute module
    JSValue executionResult = executeModule(source, resolved.filePath, moduleObj, exportsObj, requireFunc, filenameVal, dirnameVal, ctx);
    
    JS_FreeValue(ctx, exportsObj);
    JS_FreeValue(ctx, requireFunc);
    JS_FreeValue(ctx, filenameVal);
    JS_FreeValue(ctx, dirnameVal);

    if (JS_IsException(executionResult)) {
        JS_FreeValue(ctx, moduleObj);
        // Remove from cache on error
        std::lock_guard<std::mutex> lock(wrapper->getCJSCacheMutex());
        wrapper->getCJSCache().erase(cacheKey);
        return executionResult;
    }
    JS_FreeValue(ctx, executionResult);
    
    // Get module.exports from the ProtoObject (it might have been reassigned in the interpreter)
    const proto::ProtoObject* moduleProto = GCBridge::getProtoObject(moduleObj, ctx);
    JSValue exports = JS_UNDEFINED;
    if (moduleProto) {
        const proto::ProtoString* exportsKey = ProtoJSStringCache::getKey(wrapper->getProtoContext(), "exports");
        const proto::ProtoObject* exportsProto = moduleProto->getAttribute(wrapper->getProtoContext(), exportsKey, true);
        std::cerr << "[CommonJSLoader] exportsProto=" << exportsProto 
                  << " (isCell=" << (exportsProto ? exportsProto->isCell(wrapper->getProtoContext()) : 0) << ")" << std::endl;
        exports = TypeBridge::toJS(ctx, exportsProto, wrapper->getProtoContext());
    } else {
        exports = JS_GetPropertyStr(ctx, moduleObj, "exports");
    }
    
    // Update cache with final exports
    {
        std::lock_guard<std::mutex> lock(wrapper->getCJSCacheMutex());
        auto& cache = wrapper->getCJSCache();
        auto it = cache.find(cacheKey);
        if (it != cache.end()) {
            JS_FreeValue(ctx, it->second);
        }
        cache[cacheKey] = JS_DupValue(ctx, exports);
    }
    
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
    std::cerr << "[CommonJSLoader] requireImpl called" << std::endl;
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

const proto::ProtoObject* CommonJSLoader::requireProtoMethod(
    proto::ProtoContext* pCtx,
    const proto::ProtoObject* self,
    const proto::ParentLink* parent,
    const proto::ProtoList* args,
    const proto::ProtoSparseList* locals
) {
    std::cerr << "[CommonJSLoader] requireProtoMethod called" << std::endl << std::flush;
    JSContextWrapper* wrapper = JSContextWrapper::current();
    if (!wrapper) return PROTO_NONE;
    JSContext* ctx = wrapper->getJSContext();
    if (!ctx) return PROTO_NONE;

    if (!args || args->getSize(pCtx) == 0) {
        JS_ThrowTypeError(ctx, "require expects a module specifier");
        return PROTO_NONE; // Exception is set on JSContext
    }

    const proto::ProtoObject* specifierProto = args->getAt(pCtx, 0);
    if (!specifierProto || !specifierProto->isString(pCtx)) {
        JS_ThrowTypeError(ctx, "require specifier must be a string");
        return PROTO_NONE;
    }

    std::string specifier;
    specifierProto->asString(pCtx)->toUTF8String(pCtx, specifier);
    
    // Get fromPath from current file name in ProtoContext
    std::string fromPath = ".";
    if (pCtx->currentFileName) {
        fromPath = pCtx->currentFileName;
    }

    JSValue result = require(specifier, fromPath, ctx);
    if (JS_IsException(result)) {
        // Exception already set, just return PROTO_NONE
        JS_FreeValue(ctx, result);
        return PROTO_NONE;
    }

    const proto::ProtoObject* resultProto = TypeBridge::fromJS(ctx, result, pCtx);
    JS_FreeValue(ctx, result);
    return resultProto;
}

JSValue CommonJSLoader::executeModule(
    const std::string& source,
    const std::string& filename,
    JSValue moduleObj,
    JSValue exportsObj,
    JSValue requireFunc,
    JSValue filenameVal,
    JSValue dirnameVal,
    JSContext* ctx
) {
    // Create CommonJS wrapper: script that evaluates to the wrapper function
    std::string wrapped = "(function(exports, require, module, __filename, __dirname) {\n";
    wrapped += source;
    wrapped += "\n});";

    JSContextWrapper* wrapper = static_cast<JSContextWrapper*>(JS_GetContextOpaque(ctx));
    proto::ProtoContext* pContext = wrapper ? wrapper->getProtoContext() : nullptr;
    if (!pContext) {
        return JS_ThrowInternalError(ctx, "CommonJSLoader: no ProtoContext");
    }

    // Compile and load via protoCore path
    void* bytecode = protojs::compileToBytecode(ctx, wrapped.c_str(), wrapped.size(), filename.c_str());
    if (!bytecode) {
        return JS_GetException(ctx);
    }

    protojs::ProtoBytecodeModule module;
    proto::ProtoContext frameCtx(pContext->space, pContext, nullptr, nullptr, nullptr, nullptr);
    if (!protojs::loadBytecode(ctx, bytecode, &frameCtx, &module)) {
        return JS_EXCEPTION;
    }

    JSValue globalVal = JS_GetGlobalObject(ctx);
    const proto::ProtoObject* globalObj = TypeBridge::fromJS(ctx, globalVal, &frameCtx);
    JS_FreeValue(ctx, globalVal);

    // Run top-level script; result is the wrapper function
    const proto::ProtoObject* wrapperFunc = protojs::runBytecode(&frameCtx, &module, globalObj, nullptr, &globalObj);
    frameCtx.returnValue = wrapperFunc;

    if (!wrapperFunc || wrapperFunc == PROTO_NONE) {
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

    const proto::ProtoString* key = JSSymbols::bytecodeId(&frameCtx);
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
        const proto::ProtoObject* result = protojs::runBytecode(&childCtx, &module.nestedFunctions[static_cast<size_t>(bcId)], PROTO_NONE, argsList, &globalObj);
        childCtx.returnValue = result;
        resultVal = TypeBridge::toJS(ctx, result ? result : PROTO_NONE, &frameCtx);
    }

    JS_SetPropertyStr(ctx, moduleObj, "loaded", JS_NewBool(ctx, true));

    if (JS_IsException(resultVal)) {
        return resultVal;
    }
    JS_FreeValue(ctx, resultVal);
    return JS_UNDEFINED;
}

} // namespace protojs
