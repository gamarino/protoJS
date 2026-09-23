#include "CommonJSLoader.h"
#include "ModuleResolver.h"
#include "ModuleCache.h"
#include "../GCBridge.h"
#include "../JSContext.h"
#include "../TypeBridge.h"
#include "../JSSymbols.h"
#include "../FunctionPrototype.h"
#include "../runtime/ProtoCompileOnly.h"
#include "../runtime/ProtoBytecodeModule.h"
#include "../runtime/ProtoInterpreter.h"
#include "../native/DynamicLibraryLoader.h"
#include "protoCore.h"
#include <string>
#include <fstream>
#include <sstream>
#include <mutex>
#include <iostream>

namespace protojs {

namespace {

// Built-in module names that `require()` resolves to the object installed on
// the protoCore-native global.
//
// This is a deliberate allowlist. The previous implementation read ANY
// property of the (QuickJS) global object, so `require('console')`,
// `require('JSON')`, `require('memory')`, `require('profiler')` and
// `require('debugger')` would all have "resolved", and any npm package with
// one of those names would have been shadowed by a host global.
const char* const kBuiltinModules[] = {
    "child_process", "cluster", "crypto", "dgram", "dns", "events", "fs",
    "http", "net", "path", "process", "stream", "url", "util",
    "worker_threads",
};

bool isBuiltinModuleName(const std::string& name) {
    for (const char* n : kBuiltinModules) {
        if (name == n) return true;
    }
    return false;
}

// Read a name from the protoCore-native global, building the key exactly as
// ProtoNativeModule::registerOnGlobal does, so the lookup matches the
// registration.
const proto::ProtoObject* nativeGlobalLookup(proto::ProtoContext* pCtx,
                                              const char* name) {
    JSContextWrapper* wrapper = JSContextWrapper::current();
    if (!wrapper || !pCtx) return nullptr;
    const proto::ProtoObject* g = wrapper->getNativeGlobal();
    if (!g) return nullptr;
    const proto::ProtoObject* nameObj = pCtx->fromUTF8String(name);
    const proto::ProtoString* key = nameObj ? nameObj->asString(pCtx) : nullptr;
    if (!key) return nullptr;
    const proto::ProtoObject* v = g->getAttribute(pCtx, key, false);
    if (!v || v == PROTO_NONE) return nullptr;
    return v;
}

// `require('buffer')` yields `{ Buffer }`, as in Node, where the constructor
// is a property of the module rather than the module itself.  The object is
// memoised under a private attribute of the native global — which is a GC
// root — so repeated calls return the same object.
const proto::ProtoObject* bufferModuleObject(proto::ProtoContext* pCtx) {
    JSContextWrapper* wrapper = JSContextWrapper::current();
    if (!wrapper || !pCtx) return nullptr;
    const proto::ProtoObject* g = wrapper->getNativeGlobal();
    if (!g) return nullptr;

    const proto::ProtoObject* cacheNameObj =
        pCtx->fromUTF8String("__pjs_buffer_module__");
    const proto::ProtoString* cacheKey =
        cacheNameObj ? cacheNameObj->asString(pCtx) : nullptr;
    if (cacheKey) {
        const proto::ProtoObject* cached = g->getAttribute(pCtx, cacheKey, false);
        if (cached && cached != PROTO_NONE) return cached;
    }

    const proto::ProtoObject* buf = nativeGlobalLookup(pCtx, "Buffer");
    if (!buf) return nullptr;
    const proto::ProtoObject* mod = pCtx->newObject(/*mutable=*/true);
    if (!mod) return nullptr;
    const proto::ProtoObject* bufNameObj = pCtx->fromUTF8String("Buffer");
    const proto::ProtoString* bufKey =
        bufNameObj ? bufNameObj->asString(pCtx) : nullptr;
    if (bufKey) mod->setAttribute(pCtx, bufKey, buf);
    // The global is mutable, so this publishes in place.
    if (cacheKey) g->setAttribute(pCtx, cacheKey, mod);
    return mod;
}

// Resolve a bare built-in specifier natively.  Returns nullptr when the name
// is not a built-in, so the caller falls through to UMD / file resolution.
const proto::ProtoObject* resolveBuiltinModule(proto::ProtoContext* pCtx,
                                                const std::string& specifier) {
    std::string bare = specifier;
    if (bare.rfind("node:", 0) == 0) bare = bare.substr(5);
    if (bare.empty()) return nullptr;
    if (bare == "buffer") return bufferModuleObject(pCtx);
    if (!isBuiltinModuleName(bare)) return nullptr;
    return nativeGlobalLookup(pCtx, bare.c_str());
}

// Raise a JavaScript exception from a native method.  Adds Node's
// `code: 'MODULE_NOT_FOUND'` to resolution failures.
void throwModuleError(proto::ProtoContext* pCtx, const char* type,
                       const std::string& message) {
    const proto::ProtoObject* err = makeNativeError(pCtx, type, message.c_str());
    if (err && message.rfind("Cannot find module", 0) == 0) {
        const proto::ProtoObject* codeNameObj = pCtx->fromUTF8String("code");
        const proto::ProtoString* codeKey =
            codeNameObj ? codeNameObj->asString(pCtx) : nullptr;
        if (codeKey)
            err = err->setAttribute(pCtx, codeKey,
                                     pCtx->fromUTF8String("MODULE_NOT_FOUND"));
    }
    signalNativeException(err);
}

// ---- Native addons (ABI v2) --------------------------------------------

void setAttr(proto::ProtoContext* pCtx, const proto::ProtoObject* obj,
              const char* name, const proto::ProtoObject* value) {
    if (!obj) return;
    const proto::ProtoObject* n = pCtx->fromUTF8String(name);
    const proto::ProtoString* k = n ? n->asString(pCtx) : nullptr;
    if (k) obj->setAttribute(pCtx, k, value ? value : PROTO_NONE);
}

const proto::ProtoObject* getAttr(proto::ProtoContext* pCtx,
                                   const proto::ProtoObject* obj,
                                   const char* name) {
    if (!obj || obj == PROTO_NONE) return nullptr;
    const proto::ProtoObject* n = pCtx->fromUTF8String(name);
    const proto::ProtoString* k = n ? n->asString(pCtx) : nullptr;
    if (!k) return nullptr;
    const proto::ProtoObject* v = obj->getAttribute(pCtx, k, false);
    return (v && v != PROTO_NONE) ? v : nullptr;
}

// `require.cache`, reachable from the native global and therefore GC-rooted.
// Addon module records live here, which is what keeps them alive.
const proto::ProtoObject* requireCacheObject(proto::ProtoContext* pCtx) {
    JSContextWrapper* wrapper = JSContextWrapper::current();
    if (!wrapper) return nullptr;
    const proto::ProtoObject* g = wrapper->getNativeGlobal();
    if (!g) return nullptr;
    const proto::ProtoString* rk = JSSymbols::require(pCtx);
    if (!rk) return nullptr;
    const proto::ProtoObject* req = g->getAttribute(pCtx, rk, false);
    if (!req || req == PROTO_NONE) return nullptr;
    return getAttr(pCtx, req, "cache");
}

// Load a native addon and return its exports.
//
// Under ABI v1 the addon built its exports with the QuickJS C API and the
// loader handed back a JSValue, which `requireProtoMethod` converted with
// TypeBridge::fromJS.  That conversion turns a QuickJS function into an empty
// object (TypeBridge.cpp:189-198), so every exported function was unusable:
// `typeof m.sum` was "object" and calling it threw.  Under v2 the addon builds
// protoCore objects directly and they are returned unconverted.
const proto::ProtoObject* loadNativeAddon(proto::ProtoContext* pCtx,
                                           const std::string& filePath) {
    const proto::ProtoObject* cache = requireCacheObject(pCtx);
    const proto::ProtoObject* keyObj = pCtx->fromUTF8String(filePath.c_str());
    const proto::ProtoString* cacheKey = keyObj ? keyObj->asString(pCtx) : nullptr;
    if (cache && cacheKey) {
        const proto::ProtoObject* cached = cache->getAttribute(pCtx, cacheKey, false);
        if (cached && cached != PROTO_NONE) {
            const proto::ProtoObject* ex = getAttr(pCtx, cached, "exports");
            if (ex) return ex;
        }
    }

    LoadedModule* loaded = DynamicLibraryLoader::load(filePath);
    if (!loaded) {
        // load() has already explained the reason (missing symbol, ABI v1, ...).
        throwModuleError(pCtx, "Error",
                          "Cannot load native module '" + filePath + "'");
        return PROTO_NONE;
    }

    const proto::ProtoObject* exportsObj = pCtx->newObject(/*mutable=*/true);
    const proto::ProtoObject* moduleObj = pCtx->newObject(/*mutable=*/true);
    if (!exportsObj || !moduleObj) {
        DynamicLibraryLoader::unload(loaded);
        return PROTO_NONE;
    }
    setAttr(pCtx, moduleObj, "exports", exportsObj);
    setAttr(pCtx, moduleObj, "id", pCtx->fromUTF8String(filePath.c_str()));
    setAttr(pCtx, moduleObj, "filename", pCtx->fromUTF8String(filePath.c_str()));
    setAttr(pCtx, moduleObj, "loaded", PROTO_FALSE);
    setAttr(pCtx, moduleObj, "parent", PROTO_NONE);

    const proto::ProtoObject* exports =
        DynamicLibraryLoader::initializeModule(loaded, pCtx, moduleObj);

    if (!exports) {
        // The library stays loaded on purpose: its ProtoMethod pointers may
        // already be reachable.  Only report the failure.
        if (!hasCallException()) {
            throwModuleError(pCtx, "Error",
                              "Native module '" + filePath + "' failed to initialise");
        }
        return PROTO_NONE;
    }

    setAttr(pCtx, moduleObj, "loaded", PROTO_TRUE);
    if (cache && cacheKey) cache->setAttribute(pCtx, cacheKey, moduleObj);
    return exports;
}

// Drain a pending QuickJS exception and re-raise it as a native one.  Without
// this the exception stayed on the QuickJS context, `require()` returned
// PROTO_NONE, and the script silently saw `undefined`.
void rethrowQuickJSException(proto::ProtoContext* pCtx, JSContext* ctx,
                              const std::string& fallbackMessage) {
    std::string message;
    JSValue exc = JS_GetException(ctx);
    if (!JS_IsNull(exc) && !JS_IsUndefined(exc)) {
        JSValue msgVal = JS_GetPropertyStr(ctx, exc, "message");
        const char* m = (!JS_IsUndefined(msgVal) && !JS_IsException(msgVal))
                        ? JS_ToCString(ctx, msgVal) : nullptr;
        if (m) { message = m; JS_FreeCString(ctx, m); }
        JS_FreeValue(ctx, msgVal);
        if (message.empty()) {
            const char* s = JS_ToCString(ctx, exc);
            if (s) { message = s; JS_FreeCString(ctx, s); }
        }
    }
    JS_FreeValue(ctx, exc);
    if (message.empty()) message = fallbackMessage;
    throwModuleError(pCtx, "Error", message);
}

}  // namespace

// ProtoMethod wrapper for `require.resolve` — same dispatch as the
// QuickJS-side requireResolveImpl, exposed on the protoCore-native
// global.  Returns the resolved file path as a ProtoString.
static const proto::ProtoObject* requireResolveProtoMethod(
        proto::ProtoContext* pCtx,
        const proto::ProtoObject* /*self*/,
        const proto::ParentLink*,
        const proto::ProtoList* args,
        const proto::ProtoSparseList*) {
    if (!pCtx) return PROTO_NONE;
    if (!args || args->getSize(pCtx) == 0) {
        throwModuleError(pCtx, "TypeError",
                          "require.resolve expects a module specifier");
        return PROTO_NONE;
    }
    const proto::ProtoObject* a = args->getAt(pCtx, 0);
    if (!a || !a->isString(pCtx)) {
        throwModuleError(pCtx, "TypeError",
                          "require.resolve specifier must be a string");
        return PROTO_NONE;
    }
    std::string specifier;
    a->asString(pCtx)->toUTF8String(pCtx, specifier);

    // A built-in resolves to its own name, as in Node.
    if (resolveBuiltinModule(pCtx, specifier)) {
        std::string bare = specifier;
        if (bare.rfind("node:", 0) == 0) bare = bare.substr(5);
        return pCtx->fromUTF8String(bare.c_str());
    }

    JSContextWrapper* wrapper = JSContextWrapper::current();
    if (!wrapper) return PROTO_NONE;
    std::string fromPath = pCtx->currentFileName ? pCtx->currentFileName : ".";
    JSContext* ctx = wrapper->getJSContext();
    JSValue r = CommonJSLoader::requireResolve(specifier, fromPath, ctx);
    if (JS_IsException(r)) {
        JS_FreeValue(ctx, r);
        rethrowQuickJSException(pCtx, ctx,
                                 "Cannot find module '" + specifier + "'");
        return PROTO_NONE;
    }
    const char* s = JS_ToCString(ctx, r);
    const proto::ProtoObject* out = pCtx->fromUTF8String(s ? s : "");
    if (s) JS_FreeCString(ctx, s);
    JS_FreeValue(ctx, r);
    return out;
}

const proto::ProtoObject* CommonJSLoader::init(
        proto::ProtoContext* pCtx,
        const proto::ProtoObject* globalObj) {
    if (!pCtx || !globalObj) return globalObj;

    // wrapNativeFunction returns a callable ProtoObject (carries
    // `__native_fn__`) that supports attribute attachment — necessary
    // for require.resolve / require.cache.  A bare fromMethod()
    // tagged pointer does not.
    const proto::ProtoObject* requireFn =
        wrapNativeFunction(pCtx, requireProtoMethod, "require",
                            /*length=*/1, /*globalRoot=*/nullptr);
    if (!requireFn) return globalObj;

    const proto::ProtoString* resolveKey =
        pCtx->fromUTF8String("resolve")->asString(pCtx);
    if (resolveKey) {
        const proto::ProtoObject* resolveFn =
            wrapNativeFunction(pCtx, requireResolveProtoMethod, "resolve",
                                /*length=*/1, /*globalRoot=*/nullptr);
        if (resolveFn) requireFn = requireFn->setAttribute(pCtx, resolveKey, resolveFn);
    }
    const proto::ProtoString* cacheKey =
        pCtx->fromUTF8String("cache")->asString(pCtx);
    if (cacheKey) {
        requireFn = requireFn->setAttribute(pCtx, cacheKey,
            pCtx->newObject(/*mutable=*/true));
    }

    const proto::ProtoString* requireKey = JSSymbols::require(pCtx);
    if (!requireKey) return globalObj;
    return globalObj->setAttribute(pCtx, requireKey, requireFn);
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
        return JS_ThrowTypeError(ctx, "%s", ("Cannot find module '" + specifier + "'").c_str());
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
    
    // Native addons are handled by the native entry point
    // (requireProtoMethod -> loadNativeAddon), which returns protoCore objects
    // directly.  Reaching here means a caller used the JSValue path, which
    // cannot represent an ABI v2 addon's exports.
    if (resolved.type == ModuleType::Native) {
        return JS_ThrowTypeError(ctx, "%s",
            ("Native addon '" + resolved.filePath +
             "' must be loaded through require() on the protoCore path").c_str());
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
        const proto::ProtoString* exportsKey = JSSymbols::exports(wrapper->getProtoContext());
        const proto::ProtoObject* exportsProto = moduleProto->getAttribute(wrapper->getProtoContext(), exportsKey, true);

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
        return JS_ThrowTypeError(ctx, "%s", ("Cannot find module '" + specifier + "'").c_str());
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

// Execute a JavaScript file module and return its `module.exports`.
//
// Pre-fix this went through CommonJSLoader::executeModule, which ran the
// wrapper against a TypeBridge copy of the QuickJS global, kept the
// ProtoBytecodeModule in a stack local that died on return, and dropped any
// exception -- so `require('./x.js')` returned an object with no exports.
//
// Everything here stays in protoCore objects: the module record, the exports
// object and the five arguments are all ProtoObjects, so the functions a module
// exports are real interpreter closures rather than TypeBridge copies.
static const proto::ProtoObject* executeFileModuleNative(
        proto::ProtoContext* pCtx,
        JSContextWrapper* wrapper,
        const std::string& filePath) {
    if (!pCtx || !wrapper) return PROTO_NONE;

    const proto::ProtoObject* cache = requireCacheObject(pCtx);
    const proto::ProtoObject* keyObj = pCtx->fromUTF8String(filePath.c_str());
    const proto::ProtoString* cacheKey = keyObj ? keyObj->asString(pCtx) : nullptr;

    // A second require returns the very same exports object, as in Node.
    if (cache && cacheKey) {
        const proto::ProtoObject* cached = cache->getAttribute(pCtx, cacheKey, false);
        if (cached && cached != PROTO_NONE) {
            const proto::ProtoObject* ex = getAttr(pCtx, cached, "exports");
            if (ex) return ex;
        }
    }

    std::ifstream file(filePath);
    if (!file.is_open()) {
        throwModuleError(pCtx, "Error", "Cannot open module '" + filePath + "'");
        return PROTO_NONE;
    }
    std::stringstream sourceBuffer;
    sourceBuffer << file.rdbuf();

    // The CommonJS wrapper: the module body becomes a function whose parameters
    // are the five names a module may use.
    std::string wrapped =
        "(function(exports, require, module, __filename, __dirname) {\n";
    wrapped += sourceBuffer.str();
    wrapped += "\n});";

    // Build the module record natively and publish it BEFORE running the body,
    // so that a require cycle sees the partially-filled exports object and
    // terminates instead of recursing.  require.cache hangs off the native
    // global, so the record is GC-rooted for free.
    const proto::ProtoObject* exportsObj = pCtx->newObject(/*mutable=*/true);
    const proto::ProtoObject* moduleObj = pCtx->newObject(/*mutable=*/true);
    if (!exportsObj || !moduleObj) return PROTO_NONE;
    setAttr(pCtx, moduleObj, "exports", exportsObj);
    setAttr(pCtx, moduleObj, "id", pCtx->fromUTF8String(filePath.c_str()));
    setAttr(pCtx, moduleObj, "filename", pCtx->fromUTF8String(filePath.c_str()));
    setAttr(pCtx, moduleObj, "loaded", PROTO_FALSE);
    setAttr(pCtx, moduleObj, "parent", PROTO_NONE);
    if (cache && cacheKey) cache->setAttribute(pCtx, cacheKey, moduleObj);

    // `require('./sibling.js')` inside the module must resolve against THIS
    // file rather than against the entry script.  evalIsolatedToProto copies
    // currentFileName from the wrapper's root context, while the frame
    // callJSFunction builds for the module body inherits it from pCtx, so both
    // have to carry the module path: with only the first set, a nested require
    // resolves against the entry script's directory.
    const std::string pathStorage = filePath;
    char* pathPtr = const_cast<char*>(pathStorage.c_str());
    proto::ProtoContext* rootCtx = wrapper->getProtoContext();
    struct FileNameScope {
        proto::ProtoContext* outer;
        char* outerPrev;
        proto::ProtoContext* inner;
        char* innerPrev;
        ~FileNameScope() {
            if (outer) outer->currentFileName = outerPrev;
            if (inner) inner->currentFileName = innerPrev;
        }
    } fileNameScope{rootCtx, rootCtx ? rootCtx->currentFileName : nullptr,
                    pCtx, pCtx->currentFileName};
    if (rootCtx) rootCtx->currentFileName = pathPtr;
    pCtx->currentFileName = pathPtr;

    // Compile and run the wrapper.  evalIsolatedToProto keeps the bytecode
    // module alive for the wrapper's lifetime, pins its metadata, runs on the
    // native global, and returns the wrapper closure stamped with
    // __closure_module__.
    const proto::ProtoObject* wrapperFn =
        wrapper->evalIsolatedToProto(wrapped, filePath);
    if (!wrapperFn || wrapperFn == PROTO_NONE) {
        if (cache && cacheKey) cache->setAttribute(pCtx, cacheKey, PROTO_NONE);
        // A compile failure has already signalled a precise SyntaxError; report
        // a generic error only when nothing is pending, so that the precise
        // message is never overwritten.
        if (!hasCallException()) {
            throwModuleError(pCtx, "Error",
                              "Failed to load module '" + filePath + "'");
        }
        return PROTO_NONE;
    }

    // The module's `require` is the native one from the global, so that
    // `require('path') === path` holds inside a module too.
    const proto::ProtoObject* g = wrapper->getNativeGlobal();
    const proto::ProtoString* requireKey = JSSymbols::require(pCtx);
    const proto::ProtoObject* requireFn =
        (g && requireKey) ? g->getAttribute(pCtx, requireKey, false) : nullptr;

    const std::string dirName = ModuleResolver::getDirectory(filePath);
    const proto::ProtoList* argsList = pCtx->newList();
    if (!argsList) return PROTO_NONE;
    argsList = argsList->appendLast(pCtx, exportsObj);
    argsList = argsList->appendLast(pCtx, requireFn ? requireFn : PROTO_NONE);
    argsList = argsList->appendLast(pCtx, moduleObj);
    argsList = argsList->appendLast(pCtx, pCtx->fromUTF8String(filePath.c_str()));
    argsList = argsList->appendLast(pCtx, pCtx->fromUTF8String(dirName.c_str()));

    callJSFunction(pCtx, wrapperFn, PROTO_NONE, argsList);

    if (hasCallException()) {
        // Leave the exception pending: the interpreter promotes it once this
        // native method returns.  A module that threw is not kept -- as in
        // Node, the next require runs the body again rather than serving a
        // half-built exports object.
        if (cache && cacheKey) cache->setAttribute(pCtx, cacheKey, PROTO_NONE);
        return PROTO_NONE;
    }

    setAttr(pCtx, moduleObj, "loaded", PROTO_TRUE);
    // Re-read: the body may have replaced it with `module.exports = ...`.
    const proto::ProtoObject* finalExports = getAttr(pCtx, moduleObj, "exports");
    return finalExports ? finalExports : PROTO_NONE;
}

const proto::ProtoObject* CommonJSLoader::requireProtoMethod(
    proto::ProtoContext* pCtx,
    const proto::ProtoObject* self,
    const proto::ParentLink* parent,
    const proto::ProtoList* args,
    const proto::ProtoSparseList* locals
) {
    if (!pCtx) return PROTO_NONE;

    if (!args || args->getSize(pCtx) == 0) {
        throwModuleError(pCtx, "TypeError", "require expects a module specifier");
        return PROTO_NONE;
    }

    const proto::ProtoObject* specifierProto = args->getAt(pCtx, 0);
    if (!specifierProto || !specifierProto->isString(pCtx)) {
        throwModuleError(pCtx, "TypeError", "require specifier must be a string");
        return PROTO_NONE;
    }

    std::string specifier;
    specifierProto->asString(pCtx)->toUTF8String(pCtx, specifier);

    // Built-in names resolve natively, before any JSValue is built, so that
    // `require('fs') === fs`.  The old code read them from the QuickJS global
    // object, where the standard modules are not registered: the lookup always
    // missed, fell through to file resolution and ended in a dropped
    // exception, so the script silently received `undefined`.
    if (const proto::ProtoObject* builtin = resolveBuiltinModule(pCtx, specifier))
        return builtin;

    JSContextWrapper* wrapper = JSContextWrapper::current();
    if (!wrapper) return PROTO_NONE;
    JSContext* ctx = wrapper->getJSContext();
    if (!ctx) return PROTO_NONE;

    // Get fromPath from current file name in ProtoContext
    std::string fromPath = ".";
    if (pCtx->currentFileName) {
        fromPath = pCtx->currentFileName;
    }

    // protoCore's module discovery keeps its documented precedence over file
    // resolution for bare specifiers: built-in names, then discovery, then
    // files (MODULE_DISCOVERY_PROTOCORE.md).  Routing every resolvable file to
    // the native path below would otherwise have moved discovery behind file
    // resolution.  Answering it here also drops the last TypeBridge round trip
    // on this path -- the JSValue route converted the exports toJS and then
    // back fromJS, which rebuilds a function as an empty object.
    if (isBareSpecifier(specifier)) {
        if (proto::ProtoSpace* space = wrapper->getProtoSpace()) {
            const proto::ProtoObject* umd =
                space->getImportModule(pCtx, specifier.c_str(), "exports");
            if (umd && umd != PROTO_NONE) {
                if (const proto::ProtoObject* ex = getAttr(pCtx, umd, "exports"))
                    return ex;
            }
        }
    }

    ResolveResult resolved = ModuleResolver::resolve(specifier, fromPath, ctx);

    // Native addons are loaded natively: under ABI v2 the addon builds
    // protoCore objects, so its exports are returned without conversion and
    // its functions stay callable.
    if (!resolved.filePath.empty() &&
        (resolved.type == ModuleType::Native ||
         ModuleResolver::isNativeExtension(resolved.filePath))) {
        return loadNativeAddon(pCtx, resolved.filePath);
    }

    // JavaScript file modules run natively for the same reason: the exports
    // stay protoCore objects, so the functions they carry remain callable.
    // Pre-fix this fell through to the JSValue path below, whose exports
    // round-tripped through TypeBridge and came back empty.
    if (!resolved.filePath.empty()) {
        return executeFileModuleNative(pCtx, wrapper, resolved.filePath);
    }

    // Only a specifier that resolved to no file reaches the JSValue path now:
    // protoCore's module discovery may still serve it, and otherwise it throws.

    JSValue result = require(specifier, fromPath, ctx);
    if (JS_IsException(result)) {
        JS_FreeValue(ctx, result);
        rethrowQuickJSException(pCtx, ctx,
                                 "Cannot find module '" + specifier + "'");
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
