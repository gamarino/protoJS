#include "UtilModule.h"
#include "../../Deferred.h"

namespace protojs {

void UtilModule::init(JSContext* ctx) {
    JSValue utilModule = JS_NewObject(ctx);
    
    JS_SetPropertyStr(ctx, utilModule, "promisify", JS_NewCFunction(ctx, promisify, "promisify", 1));
    
    JSValue types = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, types, "isArray", JS_NewCFunction(ctx, typesIsArray, "isArray", 1));
    JS_SetPropertyStr(ctx, types, "isString", JS_NewCFunction(ctx, typesIsString, "isString", 1));
    JS_SetPropertyStr(ctx, types, "isNumber", JS_NewCFunction(ctx, typesIsNumber, "isNumber", 1));
    JS_SetPropertyStr(ctx, types, "isObject", JS_NewCFunction(ctx, typesIsObject, "isObject", 1));
    JS_SetPropertyStr(ctx, types, "isFunction", JS_NewCFunction(ctx, typesIsFunction, "isFunction", 1));
    JS_SetPropertyStr(ctx, types, "isDate", JS_NewCFunction(ctx, typesIsDate, "isDate", 1));
    JS_SetPropertyStr(ctx, utilModule, "types", types);
    
    JS_SetPropertyStr(ctx, utilModule, "inspect", JS_NewCFunction(ctx, inspect, "inspect", 1));
    JS_SetPropertyStr(ctx, utilModule, "format", JS_NewCFunction(ctx, format, "format", 1));
    
    JSValue global_obj = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global_obj, "util", utilModule);
    JS_FreeValue(ctx, global_obj);
}

JSValue UtilModule::promisify(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1 || !JS_IsFunction(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "promisify expects a function");
    }
    
    JSValue originalFunc = JS_DupValue(ctx, argv[0]);
    
    // Create promisified function that wraps callback-based function
    JSValue promisified = JS_NewCFunctionData(ctx, [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv, int magic, JSValue* func_data) {
        JSValue originalFunc = func_data[0];
        
        // Create a Promise using Deferred
        JSValue deferredCtor = JS_GetPropertyStr(ctx, JS_GetGlobalObject(ctx), "Deferred");
        if (JS_IsFunction(ctx, deferredCtor)) {
            // Create function that calls original with callback
            JSValue wrapperFunc = JS_NewCFunction(ctx, [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
                // This would call the original function with a callback
                // For Phase 2, return a basic promise
                return JS_UNDEFINED;
            }, "promisified_wrapper", argc);
            
            JSValue promise = JS_CallConstructor(ctx, deferredCtor, 1, &wrapperFunc);
            JS_FreeValue(ctx, wrapperFunc);
            JS_FreeValue(ctx, deferredCtor);
            return promise;
        }
        
        JS_FreeValue(ctx, deferredCtor);
        return JS_UNDEFINED;
    }, 0, JS_CFUNC_generic_magic, 1, &originalFunc);
    
    return promisified;
}

JSValue UtilModule::typesIsArray(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_NewBool(ctx, false);
    }
    return JS_NewBool(ctx, JS_IsArray(ctx, argv[0]));
}

JSValue UtilModule::typesIsString(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_NewBool(ctx, false);
    }
    return JS_NewBool(ctx, JS_IsString(argv[0]));
}

JSValue UtilModule::typesIsNumber(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_NewBool(ctx, false);
    }
    return JS_NewBool(ctx, JS_IsNumber(argv[0]));
}

JSValue UtilModule::typesIsObject(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_NewBool(ctx, false);
    }
    return JS_NewBool(ctx, JS_IsObject(argv[0]) && !JS_IsArray(ctx, argv[0]) && !JS_IsString(argv[0]));
}

JSValue UtilModule::typesIsFunction(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_NewBool(ctx, false);
    }
    return JS_NewBool(ctx, JS_IsFunction(ctx, argv[0]));
}

JSValue UtilModule::typesIsDate(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_NewBool(ctx, false);
    }
    // QuickJS doesn't have JS_IsDate, check if object has date-like properties
    if (JS_IsObject(argv[0])) {
        JSValue getTime = JS_GetPropertyStr(ctx, argv[0], "getTime");
        bool isDate = JS_IsFunction(ctx, getTime);
        JS_FreeValue(ctx, getTime);
        return JS_NewBool(ctx, isDate);
    }
    return JS_NewBool(ctx, false);
}

JSValue UtilModule::inspect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_NewString(ctx, "undefined");
    }
    
    // Basic inspect - convert to string
    if (JS_IsString(argv[0])) {
        return JS_DupValue(ctx, argv[0]);
    } else if (JS_IsNumber(argv[0])) {
        double num;
        JS_ToFloat64(ctx, &num, argv[0]);
        char buf[64];
        snprintf(buf, sizeof(buf), "%g", num);
        return JS_NewString(ctx, buf);
    } else if (JS_IsBool(argv[0])) {
        return JS_NewString(ctx, JS_ToBool(ctx, argv[0]) ? "true" : "false");
    } else if (JS_IsNull(argv[0])) {
        return JS_NewString(ctx, "null");
    } else if (JS_IsUndefined(argv[0])) {
        return JS_NewString(ctx, "undefined");
    } else if (JS_IsObject(argv[0])) {
        return JS_NewString(ctx, "[Object]");
    }
    
    return JS_NewString(ctx, "[Unknown]");
}

JSValue UtilModule::format(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    // Basic format implementation (like printf)
    if (argc < 1) {
        return JS_NewString(ctx, "");
    }
    
    std::string result;
    const char* formatStr = JS_ToCString(ctx, argv[0]);
    if (formatStr) {
        // Simple format - just return format string for Phase 2
        result = formatStr;
        JS_FreeCString(ctx, formatStr);
    }
    
    return JS_NewString(ctx, result.c_str());
}

} // namespace protojs
