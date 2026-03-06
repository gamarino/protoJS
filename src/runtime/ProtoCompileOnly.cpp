#include "ProtoCompileOnly.h"
#include "QuickJSBytecodeExport.h"
#include "quickjs.h"

namespace protojs {

void* compileToBytecode(JSContext* ctx, const char* source, size_t sourceLen,
                       const char* filename) {
    return compileToBytecodeWithFlags(ctx, source, sourceLen, filename, JS_EVAL_TYPE_GLOBAL);
}

void* compileToBytecodeWithFlags(JSContext* ctx, const char* source, size_t sourceLen,
                                 const char* filename, int evalFlags) {
    if (!ctx || !source || !filename) return nullptr;
    int flags = (evalFlags & ~JS_EVAL_FLAG_COMPILE_ONLY) | JS_EVAL_FLAG_COMPILE_ONLY;
    JSValue funVal = JS_Eval(ctx, source, sourceLen, filename, flags);
    if (JS_IsException(funVal)) return nullptr;
    void* bytecode = protojs_get_function_bytecode(ctx, &funVal);
    JS_FreeValue(ctx, funVal);
    return bytecode;
}

} // namespace protojs
