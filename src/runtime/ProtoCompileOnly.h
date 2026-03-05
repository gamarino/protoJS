#ifndef PROTOJS_PROTO_COMPILE_ONLY_H
#define PROTOJS_PROTO_COMPILE_ONLY_H

#include <cstddef>

/**
 * Compile-only path: use QuickJS to parse and compile JS to bytecode
 * without executing. Returns an opaque bytecode pointer for the loader.
 */

struct JSContext;

namespace protojs {

/**
 * Compile source to bytecode. Uses JS_Eval with JS_EVAL_FLAG_COMPILE_ONLY.
 * Returns opaque bytecode pointer (JSFunctionBytecode*) or nullptr on parse/compile error.
 * ctx: QuickJS context (must stay alive until loader has finished using the bytecode).
 * On exception, ctx has the exception set; caller may need to clear it.
 */
void* compileToBytecode(struct JSContext* ctx, const char* source, size_t sourceLen,
                        const char* filename);

} // namespace protojs

#endif /* PROTOJS_PROTO_COMPILE_ONLY_H */
