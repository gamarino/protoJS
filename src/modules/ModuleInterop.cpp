#include "ModuleInterop.h"

namespace protojs {

JSValue ModuleInterop::cjsToESM(JSValue cjsModule, JSContext* ctx) {
    // Get module.exports
    JSValue exports = JS_GetPropertyStr(ctx, cjsModule, "exports");
    if (JS_IsException(exports)) {
        return exports;
    }
    
    // Create namespace object
    JSValue namespaceObj = JS_NewObject(ctx);
    
    // Check if exports is an object with properties
    if (JS_IsObject(exports)) {
        // Copy all properties to namespace
        JSPropertyEnum* props;
        uint32_t propCount;
        if (JS_GetOwnPropertyNames(ctx, &props, &propCount, exports, 
                                  JS_GPN_STRING_MASK | JS_GPN_ENUM_ONLY) == 0) {
            for (uint32_t i = 0; i < propCount; i++) {
                JSValue propVal = JS_GetProperty(ctx, exports, props[i].atom);
                const char* propName = JS_AtomToCString(ctx, props[i].atom);
                if (propName) {
                    JS_SetPropertyStr(ctx, namespaceObj, propName, propVal);
                    JS_FreeCString(ctx, propName);
                }
                JS_FreeValue(ctx, propVal);
                JS_FreeAtom(ctx, props[i].atom);
            }
            js_free(ctx, props);
        }
        
        // Also set as default export
        JS_SetPropertyStr(ctx, namespaceObj, "default", JS_DupValue(ctx, exports));
    } else {
        // Primitive value - only default export
        JS_SetPropertyStr(ctx, namespaceObj, "default", exports);
    }
    
    // Mark as ES module
    JS_DefinePropertyValueStr(ctx, namespaceObj, "__esModule", 
                             JS_NewBool(ctx, true), JS_PROP_CONFIGURABLE);
    
    JS_FreeValue(ctx, exports);
    return namespaceObj;
}

JSValue ModuleInterop::esmToCJS(JSValue esmNamespace, JSContext* ctx) {
    // Get default export
    JSValue defaultExport = JS_GetPropertyStr(ctx, esmNamespace, "default");
    if (JS_IsException(defaultExport)) {
        return defaultExport;
    }
    
    // If default is not undefined, return it
    if (!JS_IsUndefined(defaultExport)) {
        return defaultExport;
    }
    
    // Otherwise, return the namespace object itself
    return JS_DupValue(ctx, esmNamespace);
}

bool ModuleInterop::isCommonJSModule(JSValue val, JSContext* ctx) {
    if (!JS_IsObject(val)) {
        return false;
    }
    
    // Check for module.exports pattern
    JSValue exports = JS_GetPropertyStr(ctx, val, "exports");
    bool hasExports = !JS_IsException(exports) && !JS_IsUndefined(exports);
    JS_FreeValue(ctx, exports);
    
    return hasExports;
}

bool ModuleInterop::isESModuleNamespace(JSValue val, JSContext* ctx) {
    if (!JS_IsObject(val)) {
        return false;
    }
    
    // Check for __esModule flag
    JSValue esModule = JS_GetPropertyStr(ctx, val, "__esModule");
    bool isESM = !JS_IsException(esModule) && JS_ToBool(ctx, esModule);
    JS_FreeValue(ctx, esModule);
    
    return isESM;
}

} // namespace protojs
