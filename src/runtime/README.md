# ProtoCore Runtime (Option B: Reuse Parser)

This directory implements the **Reuse Parser, Full protoCore Runtime** path: QuickJS is used only to parse and compile JavaScript to bytecode; execution is done by a protoCore-native interpreter.

## Absolute rule: no std::vector for execution state

**Local variables and the operand stack must not use `std::vector`.** They are not considered by the garbage collector. The only valid storage for locals and stack is **ProtoContext** (via `closureLocals` and, where applicable, automatic locals). All slot and stack reads/writes in the interpreter go through ProtoContext so the GC can trace every reference.

## Flow

1. **Compile only:** `ProtoCompileOnly.cpp` calls `JS_Eval(..., JS_EVAL_FLAG_COMPILE_ONLY)` and obtains the top-level `JSFunctionBytecode*` via `protojs_get_function_bytecode()` (implemented in `deps/quickjs/quickjs.c`).
2. **Load:** `ProtoBytecodeLoader.cpp` converts the bytecode into a `ProtoBytecodeModule`: constant pool → `ProtoObject*`, nested functions → placeholder objects with `__bytecode_id__`, atom resolution deferred to the interpreter.
3. **Run:** `ProtoInterpreter.cpp` executes the bytecode using **only** ProtoContext: the operand stack is a `ProtoList` stored in `closureLocals` under a reserved key, and local/argument slots are entries in `closureLocals` keyed by slot index. Opcodes are dispatched in terms of `ProtoContext` and `ProtoObject` (get/put field, call, return, etc.).

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
- **Locals, arguments and lexical environment**: `get/put/set_loc*`, `get/put/set_arg*`, `get/put/set_var_ref*`, plus the `_check` variants used for TDZ and lexical checks. Locals can be implemented as **automatic variables** (by index, discarded on return) or as a **ProtoSparseList** keyed by interned variable name for closure support; the dictionary may be immutable (snapshot semantics) or mutable (closures see latest state). See ARCHITECTURE.md § 1.3a.
- **Properties and arrays**: `get/put_field*`, `define_field`, and array access opcodes (`get/put_array_el*`) mapped to `ProtoObject` attributes and interned `ProtoString` keys.
- **Control flow**: unconditional and conditional jumps (`goto*`, `if_true*`, `if_false*`) implemented in terms of JS-style truthiness (`toBool`).
- **Calls**: bytecode function calls and `ProtoMethod` calls are routed through `runBytecode` and protoCore’s `call` model, with a proper `this` binding and argument list. **Phase 6** adds `OP_call_method`, `OP_tail_call_method`, and `OP_call_constructor`. **Phase 7** adds comparison, logical, `typeof`, `instanceof`, `in`, and `delete` operators.

## Phase 4 (wire compile → load → run)

Phase 4 is complete: the eval entry point uses the protoCore path when `setUseProtoEval(true)` is set. The CLI supports `--proto-eval` and `PROTOJS_USE_PROTO_EVAL=1`; the Test262 runner supports `TEST262_USE_PROTO_EVAL=1` or config `use_proto_eval: true`. See `ARCHITECTURE.md` § 1.4.

## Phase 5 (legacy path context + conformance)

Phase 5 completes the Option B runtime behaviour and sets the stage for conformance work:

- **Single path:** All script execution uses compile → load → run; there is no legacy `JS_Eval` path. `ExecutionEngine::getProtoContext(ctx)` is used only by the debugger (and similar) to obtain the current ProtoContext from the wrapper.
- **Stack and locals in ProtoContext only:** All interpreter state (operand stack and local/argument slots) is stored in `ProtoContext::closureLocals`; no `std::vector` is used so the GC sees every reference (see § "Absolute rule" above).
- **Next (Phase 6 / conformance):** Run Test262 on the protoCore path (`TEST262_USE_PROTO_EVAL=1`), document pass/fail by category, and fix missing opcodes or built-ins to improve conformance. Optionally make the protoCore path the default for the CLI.

## Phase 6 (Test262 conformance on protoCore path)

Phase 6 focuses on **conformance of the protoCore interpreter** (Option B path):

- **Run Test262 on protoCore:** Use the same runner (`tests/test262/runner/test262_runner.js`) with `TEST262_USE_PROTO_EVAL=1` or `"use_proto_eval": true` in `tests/test262/config/test262_paths.json`. The runner passes `PROTOJS_USE_PROTO_EVAL=1` to the protojs process so every test runs via compile → load → run (no QuickJS interpreter). The default config expects the Test262 repo **at the same level as protoJS** (e.g. `proyectos/protoJS` and `proyectos/test262`); override with `TEST262_ROOT` if needed.
- **Document results:** Update `CONFORMANCE_JS.md` § "Phase 6 (protoCore path)" with pass/fail/timeout counts per category from the JSON snapshots in `tests/test262/reports/`.
- **Fix gaps:** Address missing opcodes, built-in methods, or coercion in the ProtoInterpreter and TypeBridge so more tests pass; re-run and update the report.
- The protoCore path is the only path; the CLI uses it by default.

## Phase 8 (directed tests and documentation)

Phase 8 completes the Phase 3 interpreter cycle with **targeted tests** and **documented coverage**:

- **Directed smoke test:** Run `node tests/test262/runner/proto_eval_smoke.js` from the protoJS root. This script invokes protojs with `PROTOJS_USE_PROTO_EVAL=1` and a short list of expressions (arithmetic, `typeof`, comparison, `Array.isArray`) and asserts exit code 0. Use it to quickly verify the protoCore path after interpreter changes.
- **Test262 mini-suites:** Use the same runner with `TEST262_USE_PROTO_EVAL=1` and config patterns (e.g. `built-ins/Array/isArray`) to run selected Test262 categories. Snapshot results go to `tests/test262/reports/`; update `CONFORMANCE_JS.md` § "Phase 6 (protoCore path)" with pass/fail/timeout counts.
- **Coverage notes:** Phase 6 (calls/constructors) and Phase 7 (operators) are implemented in the interpreter; coverage is documented in this README and in `ARCHITECTURE.md` § 1.4.

## Phase 9 (refactor and documentation)

Phase 9 completes the Phase 3 cycle with **cleanup and documentation**:

- **Unknown opcodes:** The interpreter no longer silently returns on an unimplemented opcode. The `default` case in the opcode switch logs the unsupported opcode (hex) and byte offset to stderr and then returns, so missing opcodes are visible during development and Test262 runs.
- **Documentation:** `ARCHITECTURE.md` § 1.4 states that the protoCore interpreter covers the opcode set needed for the main execution path and uses no QuickJS abstraction beyond the compile frontend. `CONFORMANCE_JS.md` states that Test262 on the protoCore path is supported and tracks results in the Phase 6 table and reports.

## Enabling

The CLI sets `setUseProtoEval(true)` by default. `eval()` always uses compile → load → run and converts the result to `JSValue` only at the boundary.

From the CLI, you can enable the protoCore path by either:

- Passing `--proto-eval`, or
- Setting `PROTOJS_USE_PROTO_EVAL=1` in the environment.

For Test262: set `TEST262_USE_PROTO_EVAL=1` or `"use_proto_eval": true` in `tests/test262/config/test262_paths.json` so the runner invokes protojs with the protoCore path.

## Bridges

**QuickJSArrayBridge** is stubbed (no-ops). **ExecutionEngine** provides only `getProtoContext(ctx)`. **TypeBridge** and **GCBridge** are used at the boundary (global object, script result, host function bridge). See `ARCHITECTURE.md` § 1.4.
