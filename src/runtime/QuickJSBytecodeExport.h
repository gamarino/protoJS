#ifndef PROTOJS_QUICKJS_BYTECODE_EXPORT_H
#define PROTOJS_QUICKJS_BYTECODE_EXPORT_H

/**
 * Opaque API to obtain QuickJS bytecode from a compile-only function value
 * and to read its fields without exposing QuickJS internal structs.
 * Used by the ProtoCore bytecode loader. All functions are implemented in
 * quickjs.c (protojs patches).
 */

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct JSContext;
typedef struct JSContext JSContext;

/**
 * Return the bytecode pointer from a function value obtained via
 * JS_Eval(..., JS_EVAL_FLAG_COMPILE_ONLY). value_ptr points to a JSValue.
 * Returns NULL if *value_ptr is not a bytecode function.
 */
void* protojs_get_function_bytecode(JSContext* ctx, const void* value_ptr);

/**
 * Accessors for bytecode (void* is JSFunctionBytecode*).
 * All take non-null bytecode pointer from protojs_get_function_bytecode.
 */
const uint8_t* protojs_bytecode_buf(void* bytecode);
int protojs_bytecode_len(void* bytecode);
uint16_t protojs_bytecode_arg_count(void* bytecode);
uint16_t protojs_bytecode_var_count(void* bytecode);
uint16_t protojs_bytecode_stack_size(void* bytecode);
int protojs_bytecode_cpool_count(void* bytecode);
/* Returns pointer to JSValue array; only valid while ctx/runtime alive. */
const void* protojs_bytecode_cpool(void* bytecode);
uint16_t protojs_bytecode_closure_var_count(void* bytecode);

/** Return variable name for closure var at index idx; caller must JS_FreeCString. */
const char* protojs_bytecode_closure_var_name(struct JSContext* ctx, void* bytecode, uint16_t idx);

/** Return 1 if closure var at idx is lexical (const/let), 0 otherwise. */
int protojs_bytecode_closure_var_is_lexical(void* bytecode, uint16_t idx);

/** Return the JSClosureTypeEnum value for closure var at idx.
 *  0=LOCAL (parent local var), 1=ARG (parent arg), 2=REF (parent closure var),
 *  3=GLOBAL_REF, 4=GLOBAL_DECL, 5=GLOBAL, 6=MODULE_DECL, 7=MODULE_IMPORT.
 *  Returns -1 on out-of-bounds. */
int protojs_bytecode_closure_var_type(void* bytecode, uint16_t idx);

/** Return var_idx for closure var at idx — the slot index into the parent function's
 *  arg array (type 1), local var array (type 0), or closure var array (type 2).
 *  Returns 0 on out-of-bounds. */
uint16_t protojs_bytecode_closure_var_idx(void* bytecode, uint16_t idx);

/** Return 1 if the function was compiled in strict mode, 0 otherwise. */
int protojs_bytecode_is_strict(void* bytecode);

/** Return the function's declared name as a C string, or NULL if anonymous.
 *  Caller must JS_FreeCString(ctx, result) when done. */
const char* protojs_bytecode_func_name(struct JSContext* ctx, void* bytecode);

/** Return 1 if the function is an arrow function (no own this/new.target binding), 0 otherwise. */
int protojs_bytecode_is_arrow(void* bytecode);

/** Return the function kind: 0=normal, 1=generator, 2=async, 3=async-generator. */
int protojs_bytecode_func_kind(void* bytecode);

#ifdef __cplusplus
}
#endif

#endif /* PROTOJS_QUICKJS_BYTECODE_EXPORT_H */
