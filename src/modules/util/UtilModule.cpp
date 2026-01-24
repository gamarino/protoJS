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
    JS_SetPropertyStr(ctx, utilModule, "types", types);
    
    JSValue global_obj = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global_obj, "util", utilModule);
    JS_FreeValue(ctx, global_obj);
}

JSValue UtilModule::promisify(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1 || !JS_IsFunction(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "promisify expects a function");
    }
    
    // Create a Promise-returning wrapper for a callback-based function
    JSValue originalFunc = JS_DupValue(ctx, argv[0]);
    
    JSValue promisified = JS_NewCFunction(ctx, [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
        // Get original function from closure (simplified)
        // Create Deferred that calls original function with callback
        // Return Promise
        
        // Simplified implementation - would properly wrap callback
        return JS_UNDEFINED;
    }, "promisified", 1);
    
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

} // namespace protojs
