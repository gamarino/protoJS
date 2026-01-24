#include "console.h"
#include <iostream>

namespace protojs {

static void printValue(JSContext* ctx, JSValueConst val, std::ostream& out) {
    if (JS_IsString(val)) {
        const char* str = JS_ToCString(ctx, val);
        if (str) {
            out << str;
            JS_FreeCString(ctx, str);
        }
    } else if (JS_IsNumber(val)) {
        double d;
        JS_ToFloat64(ctx, &d, val);
        out << d;
    } else if (JS_IsBool(val)) {
        out << (JS_ToBool(ctx, val) ? "true" : "false");
    } else if (JS_IsNull(val)) {
        out << "null";
    } else if (JS_IsUndefined(val)) {
        out << "undefined";
    } else if (JS_IsObject(val)) {
        const char* str = JS_ToCString(ctx, val);
        if (str) {
            out << str;
            JS_FreeCString(ctx, str);
        } else {
            out << "[object Object]";
        }
    } else {
        const char* str = JS_ToCString(ctx, val);
        if (str) {
            out << str;
            JS_FreeCString(ctx, str);
        }
    }
}

JSValue Console::log(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    for (int i = 0; i < argc; i++) {
        if (i > 0) std::cout << " ";
        printValue(ctx, argv[i], std::cout);
    }
    std::cout << std::endl;
    return JS_UNDEFINED;
}

JSValue Console::error(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    for (int i = 0; i < argc; i++) {
        if (i > 0) std::cerr << " ";
        printValue(ctx, argv[i], std::cerr);
    }
    std::cerr << std::endl;
    return JS_UNDEFINED;
}

JSValue Console::warn(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    std::cerr << "Warning: ";
    for (int i = 0; i < argc; i++) {
        if (i > 0) std::cerr << " ";
        printValue(ctx, argv[i], std::cerr);
    }
    std::cerr << std::endl;
    return JS_UNDEFINED;
}

void Console::init(JSContext* ctx) {
    JSValue console = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, console, "log", JS_NewCFunction(ctx, log, "log", 1));
    JS_SetPropertyStr(ctx, console, "error", JS_NewCFunction(ctx, error, "error", 1));
    JS_SetPropertyStr(ctx, console, "warn", JS_NewCFunction(ctx, warn, "warn", 1));
    
    JSValue global_obj = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global_obj, "console", console);
    JS_FreeValue(ctx, global_obj);
}

} // namespace protojs
