#include "URLModule.h"
#include <string>
#include <sstream>

namespace protojs {

static JSClassID url_class_id;

void URLModule::init(JSContext* ctx) {
    JS_NewClassID(&url_class_id);
    JSClassDef classDef = {"URL", nullptr};
    JS_NewClass(JS_GetRuntime(ctx), url_class_id, &classDef);
    
    JSValue proto = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, proto, "toString", JS_NewCFunction(ctx, URLToString, "toString", 0));
    JS_SetClassProto(ctx, url_class_id, proto);
    
    JSValue ctor = JS_NewCFunction2(ctx, URLConstructor, "URL", 1, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, ctor, proto);
    
    JSValue urlModule = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, urlModule, "URL", ctor);
    
    JSValue global_obj = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global_obj, "url", urlModule);
    JS_FreeValue(ctx, global_obj);
}

JSValue URLModule::URLConstructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "URL constructor expects a URL string");
    const char* urlStr = JS_ToCString(ctx, argv[0]);
    if (!urlStr) return JS_EXCEPTION;
    
    JSValue obj = JS_NewObjectClass(ctx, url_class_id);
    JS_SetPropertyStr(ctx, obj, "href", JS_NewString(ctx, urlStr));
    JS_SetPropertyStr(ctx, obj, "protocol", JS_NewString(ctx, "http:"));
    JS_SetPropertyStr(ctx, obj, "hostname", JS_NewString(ctx, "localhost"));
    JS_SetPropertyStr(ctx, obj, "pathname", JS_NewString(ctx, "/"));
    
    JS_FreeCString(ctx, urlStr);
    return obj;
}

JSValue URLModule::URLToString(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    JSValue href = JS_GetPropertyStr(ctx, this_val, "href");
    if (JS_IsException(href)) return href;
    const char* str = JS_ToCString(ctx, href);
    JSValue result = JS_NewString(ctx, str ? str : "");
    if (str) JS_FreeCString(ctx, str);
    JS_FreeValue(ctx, href);
    return result;
}

} // namespace protojs
