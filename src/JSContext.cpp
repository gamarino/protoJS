#include "JSContext.h"
#include <iostream>

namespace protojs {

JSContextWrapper::JSContextWrapper() : pSpace() {
    rt = JS_NewRuntime();
    ctx = JS_NewContext(rt);
    
    // Initialize protoCore root context
    pContext = pSpace.rootContext;
    
    // TODO: Register global objects like 'console', 'Deferred', etc.
}

JSContextWrapper::~JSContextWrapper() {
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
}

JSValue JSContextWrapper::eval(const std::string& code, const std::string& filename) {
    JSValue val = JS_Eval(ctx, code.c_str(), code.length(), filename.c_str(), JS_EVAL_TYPE_GLOBAL);
    
    if (JS_IsException(val)) {
        JSValue exception = JS_GetException(ctx);
        const char* str = JS_ToCString(ctx, exception);
        if (str) {
            std::cerr << "Exception in " << filename << ": " << str << std::endl;
            JS_FreeCString(ctx, str);
        }
        JS_FreeValue(ctx, exception);
    }
    
    return val;
}

} // namespace protojs
