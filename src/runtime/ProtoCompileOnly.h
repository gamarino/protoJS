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
 * outException: if non-null and compile fails, receives the exception value (caller must free).
 */
void* compileToBytecode(struct JSContext* ctx, const char* source, size_t sourceLen,
                        const char* filename, void* outException = nullptr);

/**
 * Compile source with custom eval flags (e.g. JS_EVAL_TYPE_MODULE for ES modules).
 * Same as compileToBytecode but uses (flags | JS_EVAL_FLAG_COMPILE_ONLY).
 */
void* compileToBytecodeWithFlags(struct JSContext* ctx, const char* source, size_t sourceLen,
                                 const char* filename, int evalFlags, void* outException = nullptr);

} // namespace protojs

#endif /* PROTOJS_PROTO_COMPILE_ONLY_H */
