#include "quickjs.h"
#include <iostream>

namespace protojs {

static JSValue js_console_log(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    for (int i = 0; i < argc; i++) {
        const char* str = JS_ToCString(ctx, argv[i]);
        if (str) {
            std::cout << (i > 0 ? " " : "") << str;
            JS_FreeCString(ctx, str);
        }
    }
    std::cout << std::endl;
    return JS_UNDEFINED;
}

void init_console(JSContext* ctx) {
    JSValue console = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, console, "log", JS_NewCFunction(ctx, js_console_log, "log", 1));
    
    JSValue global_obj = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global_obj, "console", console);
    JS_FreeValue(ctx, global_obj);
}

} // namespace protojs
