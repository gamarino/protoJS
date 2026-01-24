#include "PathModule.h"
#include <filesystem>
#include <string>
#include <vector>

namespace protojs {
namespace fs = std::filesystem;

void PathModule::init(JSContext* ctx) {
    JSValue pathModule = JS_NewObject(ctx);
    
    JS_SetPropertyStr(ctx, pathModule, "join", JS_NewCFunction(ctx, join, "join", 1));
    JS_SetPropertyStr(ctx, pathModule, "resolve", JS_NewCFunction(ctx, resolve, "resolve", 1));
    JS_SetPropertyStr(ctx, pathModule, "normalize", JS_NewCFunction(ctx, normalize, "normalize", 1));
    JS_SetPropertyStr(ctx, pathModule, "dirname", JS_NewCFunction(ctx, dirname, "dirname", 1));
    JS_SetPropertyStr(ctx, pathModule, "basename", JS_NewCFunction(ctx, basename, "basename", 1));
    JS_SetPropertyStr(ctx, pathModule, "extname", JS_NewCFunction(ctx, extname, "extname", 1));
    JS_SetPropertyStr(ctx, pathModule, "isAbsolute", JS_NewCFunction(ctx, isAbsolute, "isAbsolute", 1));
    JS_SetPropertyStr(ctx, pathModule, "relative", JS_NewCFunction(ctx, relative, "relative", 2));
    
    JSValue global_obj = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global_obj, "path", pathModule);
    JS_FreeValue(ctx, global_obj);
}

JSValue PathModule::join(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    std::vector<std::string> parts;
    for (int i = 0; i < argc; i++) {
        const char* part = JS_ToCString(ctx, argv[i]);
        if (part) {
            parts.push_back(part);
            JS_FreeCString(ctx, part);
        }
    }
    
    if (parts.empty()) {
        return JS_NewString(ctx, ".");
    }
    
    fs::path result;
    for (const auto& part : parts) {
        if (!part.empty()) {
            result /= part;
        }
    }
    
    std::string resultStr = result.string();
    return JS_NewString(ctx, resultStr.c_str());
}

JSValue PathModule::resolve(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    fs::path result = fs::current_path();
    
    for (int i = 0; i < argc; i++) {
        const char* part = JS_ToCString(ctx, argv[i]);
        if (part) {
            result /= part;
            JS_FreeCString(ctx, part);
        }
    }
    
    try {
        result = fs::canonical(result);
    } catch (...) {
        // If canonical fails, use absolute
        result = fs::absolute(result);
    }
    
    return JS_NewString(ctx, result.string().c_str());
}

JSValue PathModule::normalize(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "normalize expects a path");
    }
    
    const char* pathStr = JS_ToCString(ctx, argv[0]);
    if (!pathStr) {
        return JS_EXCEPTION;
    }
    
    fs::path p(pathStr);
    JS_FreeCString(ctx, pathStr);
    
    // Normalize the path
    std::string normalized = p.lexically_normal().string();
    return JS_NewString(ctx, normalized.c_str());
}

JSValue PathModule::dirname(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "dirname expects a path");
    }
    
    const char* pathStr = JS_ToCString(ctx, argv[0]);
    if (!pathStr) {
        return JS_EXCEPTION;
    }
    
    fs::path p(pathStr);
    JS_FreeCString(ctx, pathStr);
    
    return JS_NewString(ctx, p.parent_path().string().c_str());
}

JSValue PathModule::basename(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "basename expects a path");
    }
    
    const char* pathStr = JS_ToCString(ctx, argv[0]);
    if (!pathStr) {
        return JS_EXCEPTION;
    }
    
    fs::path p(pathStr);
    JS_FreeCString(ctx, pathStr);
    
    std::string basename = p.filename().string();
    return JS_NewString(ctx, basename.c_str());
}

JSValue PathModule::extname(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "extname expects a path");
    }
    
    const char* pathStr = JS_ToCString(ctx, argv[0]);
    if (!pathStr) {
        return JS_EXCEPTION;
    }
    
    fs::path p(pathStr);
    JS_FreeCString(ctx, pathStr);
    
    std::string ext = p.extension().string();
    return JS_NewString(ctx, ext.c_str());
}

JSValue PathModule::isAbsolute(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) {
        return JS_ThrowTypeError(ctx, "isAbsolute expects a path");
    }
    
    const char* pathStr = JS_ToCString(ctx, argv[0]);
    if (!pathStr) {
        return JS_EXCEPTION;
    }
    
    fs::path p(pathStr);
    JS_FreeCString(ctx, pathStr);
    
    bool absolute = p.is_absolute();
    return JS_NewBool(ctx, absolute);
}

JSValue PathModule::relative(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 2) {
        return JS_ThrowTypeError(ctx, "relative expects two paths");
    }
    
    const char* fromStr = JS_ToCString(ctx, argv[0]);
    const char* toStr = JS_ToCString(ctx, argv[1]);
    if (!fromStr || !toStr) {
        if (fromStr) JS_FreeCString(ctx, fromStr);
        if (toStr) JS_FreeCString(ctx, toStr);
        return JS_EXCEPTION;
    }
    
    fs::path from(fromStr);
    fs::path to(toStr);
    JS_FreeCString(ctx, fromStr);
    JS_FreeCString(ctx, toStr);
    
    try {
        fs::path rel = fs::relative(to, from);
        return JS_NewString(ctx, rel.string().c_str());
    } catch (...) {
        return JS_NewString(ctx, "");
    }
}

} // namespace protojs
