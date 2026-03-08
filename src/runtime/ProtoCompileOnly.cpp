#include "ProtoCompileOnly.h"
#include "QuickJSBytecodeExport.h"
#include "quickjs.h"

namespace protojs {

void* compileToBytecode(JSContext* ctx, const char* source, size_t sourceLen,
                       const char* filename, void* outException) {
    return compileToBytecodeWithFlags(ctx, source, sourceLen, filename, JS_EVAL_TYPE_GLOBAL,
                                      outException);
}

void* compileToBytecodeWithFlags(JSContext* ctx, const char* source, size_t sourceLen,
                                 const char* filename, int evalFlags, void* outException) {
    if (!ctx || !source || !filename) return nullptr;
    int flags = (evalFlags & ~JS_EVAL_FLAG_COMPILE_ONLY) | JS_EVAL_FLAG_COMPILE_ONLY;
    JSValue funVal = JS_Eval(ctx, source, sourceLen, filename, flags);
    if (JS_IsException(funVal)) {
        /* Leave exception in context for caller to retrieve with JS_GetException(ctx). */
        (void)outException;
        return nullptr;
    }
    void* bytecode = protojs_get_function_bytecode(ctx, &funVal);
    if (!bytecode) {
        JS_FreeValue(ctx, funVal);
        return nullptr;
    }
    /* Do NOT call JS_FreeValue here: freeing the JS_TAG_FUNCTION_BYTECODE JSValue
     * would immediately deallocate the JSFunctionBytecode struct, making the
     * returned bytecode pointer dangling.  The JSValue is intentionally kept alive
     * (ref-count > 0) until the runtime shuts down, which happens right after the
     * single eval in our execution model. */
    return bytecode;
}

} // namespace protojs
