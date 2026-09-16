# protoCore Runtime (`src/runtime`)

This directory contains the protoJS execution pipeline: QuickJS parses and compiles JavaScript to bytecode, and a protoCore-native interpreter executes that bytecode. QuickJS does not execute script code, with two exceptions handled in `src/JSContext.cpp`:

- **ES modules** (`--input-type=module`) are evaluated by the QuickJS module evaluator.
- **Compile failures:** if the protoCore compile step fails, `JSContextWrapper::eval` retries with QuickJS `JS_Eval` unless `PROTOJS_NO_FALLBACK=1` is set.

## Pipeline

1. **Compile.** `ProtoCompileOnly.cpp` calls `JS_Eval(..., JS_EVAL_FLAG_COMPILE_ONLY)` (`compileToBytecode`, `compileToBytecodeWithFlags`) and extracts the top-level `JSFunctionBytecode*` with `protojs_get_function_bytecode()`, which is added to the vendored `deps/quickjs/quickjs.c`.
2. **Specialise.** `BytecodeSpecialiser.cpp` rewrites common opcode sequences into fused opcodes. The loader applies it to each function's bytecode. The mode comes from `PROTOJS_SPECIALISER`: `compact` (default), `nop` or `off`.
3. **Load.** `ProtoBytecodeLoader.cpp` (`loadBytecode(ctx, bytecode, pContext, out)`) builds a `ProtoBytecodeModule`: a copy of the bytecode, the constant pool converted to `ProtoObject*`, nested functions as objects carrying `__bytecode_id__`, and function metadata. All atoms are resolved to protoCore strings at load time (`preResolveAllAtoms`), so the interpreter does not call QuickJS.
4. **Run.** `ProtoInterpreter.cpp` executes the module with `runBytecode(pContext, module, thisObj, args, pGlobalRoot, outException)`, dispatching opcodes directly on `ProtoContext` and `ProtoObject`. `pGlobalRoot` points at the protoCore-native global, so writes to globals replace the root and later reads see them.

## Execution state and the garbage collector

Every `ProtoObject*` that the interpreter holds must be visible to protoCore's garbage collector.

- A frame's arguments, local variables, closure variables and operand stack live in the frame's `ProtoContext` automatic locals, a flat array that protoCore scans as a GC root. The layout is `[args][locals][closure vars][operand stack][reserved]` (see the comment above `InterpFrame` in `ProtoInterpreter.cpp`).
- The per-thread `InterpFrame` records keep only integer indices (stack base, top and capacity) and the frame's `ProtoContext*`; they hold no object references.
- Do not keep `ProtoObject*` values in C++ containers (`std::vector`, lambda captures, ...) across points where the GC can run. For objects that must outlive a C++ call boundary, follow [docs/GC_BRIDGING.md](../../docs/GC_BRIDGING.md).

## Components

| File | Role |
|------|------|
| `QuickJSBytecodeExport.h` | C declarations for reading a compile-only function's bytecode fields (buffer, length, argument count, constant pool, ...). |
| `ProtoCompileOnly.h/.cpp` | `compileToBytecode` / `compileToBytecodeWithFlags`: opaque bytecode pointer, or `nullptr` on a parse or compile error. |
| `BytecodeSpecialiser.h/.cpp` | Post-compile opcode fusion, selected by `PROTOJS_SPECIALISER`. |
| `ProtoBytecodeModule.h` | The loaded module: bytecode copy, `protoCpool`, nested functions, atom cache, function metadata. |
| `ProtoBytecodeLoader.cpp` | `loadBytecode`: fills a `ProtoBytecodeModule`. |
| `QuickJSOpcodeEnum.h` | Opcode enumeration matching QuickJS, used for dispatch. |
| `ProtoInterpreter.h/.cpp` | `runBytecode` and the helpers that call JavaScript functions from native code, including from event-loop callbacks. |
| `GeneratorFrame.h` | Definitions shared by `runBytecode` and the generator protocol (for example the catch-frame record). |
| `BehaviorRegistry.h/.cpp` | Polymorphic object behaviours; the default delegates to protoCore `getAttribute` / `setAttribute`. |

## Built-ins and host modules

The standard built-ins (`src/*Prototype.cpp`, `src/JSONBuiltin.cpp`, `src/MathBuiltin.cpp`, ...) and the host modules (`src/modules/`) are native protoCore functions (`ProtoMethod`s) installed on the protoCore-native global. The interpreter calls them directly.

The native global starts as an object holding only `Infinity`, `NaN` and `undefined` (`JSContextWrapper::getNativeGlobal`); only bindings that register on it explicitly are visible to scripts. Bindings that still install on the QuickJS-side global are listed in [docs/MIGRATION_QUICKJS_TO_PROTOCORE.md](../../docs/MIGRATION_QUICKJS_TO_PROTOCORE.md).

When the interpreter reaches an opcode it does not implement, it prints `[ProtoInterpreter] unsupported opcode 0x.. at byte offset N` to stderr and returns from the current function.

## Remaining QuickJS boundaries

- **`QuickJSArrayBridge.cpp`** — no-op stubs for hooks that `quickjs.c` still references.
- **`ExecutionEngine`** — initialized only to provide `getProtoContext(ctx)`.
- **`TypeBridge` / `GCBridge`** — used where QuickJS values cross into protoCore, for example constant-pool conversion during loading and the exports returned by JavaScript file modules. Neither `require()` of a built-in name nor a native addon goes through the bridge any more: built-ins are read from the protoCore-native global, and ABI v2 addons build protoCore objects themselves.

## Threads and protoCore contexts

- `ProtoContext`s are never shared between threads. A thread's entry function creates its first `ProtoContext` with a null caller context and uses it for all work on that thread (see `cpuChunkThreadEntry` in `src/ProtoCoreNativeBindings.cpp`).
- `protoCore.runInThread` starts protoCore threads with `ProtoSpace::newThread`; they run in the script's `ProtoSpace`.
- A `worker_threads` `Worker` runs on a `std::thread` with its own `JSContextWrapper`, and therefore its own `ProtoSpace` (`src/modules/worker_threads/WorkerThreadsModule.cpp`). Messages between the main runtime and a worker are serialized.
- C++ worker threads (the I/O thread pool, the `http` accept loop) do not run script code; they hand results to the script thread with `EventLoop::enqueueCallback`.
- protoJS includes only protoCore's public header, `headers/protoCore.h`.

## Test262 on this path

Test262 runs use `tests/test262/runner/test262_runner.js`; see [docs/TEST262_STATUS.md](../../docs/TEST262_STATUS.md) for the latest full-suite result and [tests/README.md](../../tests/README.md) for instructions. `node tests/test262/runner/proto_eval_smoke.js` runs six short expressions as a quick check after interpreter changes.
