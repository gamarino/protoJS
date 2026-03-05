# ProtoCore Runtime (Option B: Reuse Parser)

This directory implements the **Reuse Parser, Full protoCore Runtime** path: QuickJS is used only to parse and compile JavaScript to bytecode; execution is done by a protoCore-native interpreter.

## Flow

1. **Compile only:** `ProtoCompileOnly.cpp` calls `JS_Eval(..., JS_EVAL_FLAG_COMPILE_ONLY)` and obtains the top-level `JSFunctionBytecode*` via `protojs_get_function_bytecode()` (implemented in `deps/quickjs/quickjs.c`).
2. **Load:** `ProtoBytecodeLoader.cpp` converts the bytecode into a `ProtoBytecodeModule`: constant pool → `ProtoObject*`, nested functions → placeholder objects with `__bytecode_id__`, atom resolution deferred to the interpreter.
3. **Run:** `ProtoInterpreter.cpp` executes the bytecode with a `ProtoObject*` stack and locals; opcodes are dispatched in terms of `ProtoContext` and `ProtoObject` (get/put field, call, return, etc.).

## Components

| File | Role |
|------|------|
| `QuickJSBytecodeExport.h` | Declares the C API to get bytecode from a compile-only function and to read its fields (buf, len, arg_count, cpool, etc.). |
| `ProtoCompileOnly.h/cpp` | `compileToBytecode(ctx, source, len, filename)` → opaque bytecode pointer. |
| `ProtoBytecodeModule.h` | Loaded module: copied bytecode buffer, `protoCpool`, `nestedFunctions`, atom cache and function metadata (arg/var/stack sizes). |
| `ProtoBytecodeLoader.cpp` | `loadBytecode(ctx, bytecode, pContext, out)` → fills `ProtoBytecodeModule`. |
| `QuickJSOpcodeEnum.h` | Opcode enum matching QuickJS for interpreter dispatch. |
| `ProtoInterpreter.h/cpp` | `runBytecode(pContext, module, thisObj, args, globalObj, jsContextForAtoms)` → result `ProtoObject*`. |

## Status (Phase 3)

The Phase 3 interpreter implements the primary QuickJS opcode groups needed for the current protoJS runtime:

- **Stack and constants**: full family of `push_*`, `dup/*`, `swap/*`, `rot/*`, plus constant pool and atom-based loads.
- **Locals, arguments and lexical environment**: `get/put/set_loc*`, `get/put/set_arg*`, `get/put/set_var_ref*`, plus the `_check` variants used for TDZ and lexical checks.
- **Properties and arrays**: `get/put_field*`, `define_field`, and array access opcodes (`get/put_array_el*`) mapped to `ProtoObject` attributes and interned `ProtoString` keys.
- **Control flow**: unconditional and conditional jumps (`goto*`, `if_true*`, `if_false*`) implemented in terms of JS-style truthiness (`toBool`).
- **Calls**: bytecode function calls and `ProtoMethod` calls are routed through `runBytecode` and protoCore’s `call` model, with a proper `this` binding and argument list.

## Enabling

Set `JSContextWrapper::setUseProtoEval(true)`. Then `eval()` uses compile → load → run and converts the result to `JSValue` only at the boundary. Default is the legacy path (`JS_Eval`).

## Bridges

On the protoCore path, **QuickJSArrayBridge** and **ExecutionEngine** are not used during execution (no QuickJS interpreter run). **TypeBridge** and **GCBridge** are used only at the boundary (global object and script result). See `ARCHITECTURE.md` § 1.4.
