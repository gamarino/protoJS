# Migration: QuickJS-side bindings → protoCore-native global

## Why

protoJS uses QuickJS for **parsing and bytecode compilation** and executes that bytecode in `ProtoInterpreter` against a **protoCore-native global object**. That global starts as an object holding only `Infinity`, `NaN` and `undefined` (`JSContextWrapper::getNativeGlobal` in `src/JSContext.cpp`). Anything installed with `JS_SetPropertyStr` on the QuickJS-side global object is invisible to scripts running on the protoCore interpreter; a binding is visible only if it registers on the protoCore-native global.

The rule for this migration: no bridges. The interpreter does not call back into QuickJS and does not fall back to QuickJS property lookups. Every binding that scripts can see must be a real `ProtoMethod` / `ProtoObject` on the protoCore-native global.

## Status

**Registered on the protoCore-native global** (see `src/main.cpp`):

| Binding | Source |
|---------|--------|
| `console`, `print`, `performance`, date helpers | `src/console.cpp` |
| `JSON` | `src/JSONBuiltin.cpp` |
| `setImmediate` | `src/EventLoopBindings.cpp` |
| `Deferred` | `src/ProtoDeferred.cpp` |
| `protoCore.runInThread` | `src/ProtoCoreNativeBindings.cpp` |
| `__filename`, `__dirname`, `__protojs__` | `installScriptGlobals` in `src/main.cpp` |
| `io`, `process`, `require` | `src/modules/IOModule.cpp`, `src/modules/ProcessModule.cpp`, `src/modules/CommonJSLoader.cpp` |
| `path`, `fs`, `url`, `http`, `events`, `stream`, `util`, `crypto`, `Buffer`, `net`, `worker_threads`, `cluster`, `dgram`, `child_process`, `dns` | `src/modules/` |
| `profiler`, `memory`, `debugger` | `src/profiling/`, `src/memory/`, `src/debugging/` |

**Still installed on the QuickJS-side global** (called from `src/main.cpp` with `wrapper.getJSContext()`):

| Binding | Source | Situation |
|---------|--------|-----------|
| `protoCore.Set`, `Multiset`, `SparseList`, `Tuple`, `ImmutableObject`, `MutableObject`, `isImmutable`, `makeImmutable`, `makeMutable` | `src/modules/ProtoCoreModule.cpp` | Not reachable from scripts; see step 1 below. |
| QuickJS-side `Deferred` | `src/Deferred.cpp` | Superseded by `src/ProtoDeferred.cpp`; see step 3. |

**Other remaining QuickJS dependencies in the runtime surface:**

- `CommonJSLoader::require` resolves bare built-in names by reading the QuickJS-side global, where the standard modules are no longer registered (see step 2).
- The native addon ABI (`src/native/NativeModuleABI.h`) passes a QuickJS `JSContext*` and `JSValue` to addons; exports are converted with `TypeBridge::fromJS`.
- ES modules (`--input-type=module`) are evaluated by the QuickJS module evaluator.

QuickJS remains a build dependency in any case: it provides the parser and compiler (`JS_Eval` with `JS_EVAL_FLAG_COMPILE_ONLY`), atoms and class IDs.

## Migration pattern

The reference is `Console::init` (`src/console.cpp`):

```cpp
void Console::init(proto::ProtoContext* ctx, const proto::ProtoObject*& globalObj) {
    static const NativeEntry entries[] = {
        {"log",     Console::log},
        {"error",   Console::error},
        // ... one entry per method ...
        NATIVE_MODULE_END
    };
    const proto::ProtoObject* consoleObj =
        ProtoNativeModule::buildModule(ctx, entries, /*number of entries*/ 19);
    if (!consoleObj) return;
    globalObj = ProtoNativeModule::registerOnGlobal(ctx, globalObj, "console", consoleObj);
}
```

Modules migrated later return the updated global instead of taking it by reference, and `src/main.cpp` stores it back:

```cpp
const proto::ProtoObject* nativeGlobal = wrapper.getNativeGlobal();
nativeGlobal = protojs::PathModule::init(wrapper.getProtoContext(), nativeGlobal);
wrapper.updateNativeGlobal(nativeGlobal);
```

Each `NativeEntry` method is a plain C++ function with the `ProtoMethod` signature:

```cpp
const proto::ProtoObject* foo(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*);
```

The body works directly with protoCore primitives — no `JSContext*`, no `JSValue`.

### Recipe

1. **Header** — drop `#include "quickjs.h"` and declare
   `static const proto::ProtoObject* init(proto::ProtoContext*, const proto::ProtoObject*);`
2. **Implementation** — replace each
   `JSValue f(JSContext*, JSValueConst, int, JSValueConst*)`
   with a `ProtoMethod` and rewrite the body against `ctx->fromUTF8String` / `fromInteger`, `isString` / `asString` / `toUTF8String`, `setAttribute` / `getAttribute`, and so on.
3. **Class instances** — keep instance state in attributes of a mutable `ProtoObject` whose parent is a prototype holding the methods, instead of `JS_SetOpaque` C++ structs. Native resources that must live outside the object graph go in an external pointer (`ctx->fromExternalPointer`), as in `worker_threads`.
4. **Constructors** — create the constructor with `wrapNativeFunction(ctx, ctorMethod, name, length, nullptr)` and attach its `prototype`.
5. **Registration** — `ProtoNativeModule::buildModule(ctx, entries, N)`, then `ProtoNativeModule::registerOnGlobal(ctx, globalObj, "name", mod)`; return the updated global.
6. **`src/main.cpp`** — replace the `Module::init(wrapper.getJSContext())` call with the three-line pattern above.
7. **GC safety** — any `ProtoObject*` captured by a C++ lambda for the event loop or a thread pool must be pinned; see [GC_BRIDGING.md](GC_BRIDGING.md).

## Remaining steps

### Step 1 — `protoCore` collections and mutability helpers

Port the constructors and functions of `src/modules/ProtoCoreModule.cpp` to `ProtoMethod`s and add them to the `protoCore` object built in `src/ProtoCoreNativeBindings.cpp`. `Set`, `Multiset` and `SparseList` become constructors whose instances are mutable `ProtoObject`s parented to a prototype holding the methods. Then remove the `ProtoCoreModule::init(wrapper.getJSContext())` call from `src/main.cpp`.

Done when `tests/integration/collections/protoCore_collections.js` prints no "not available" lines.

### Step 2 — `require()` of built-in module names

`CommonJSLoader::require` looks bare names up on the QuickJS-side global. Look them up on the protoCore-native global instead (`JSContextWrapper::getNativeGlobal()`), keeping the special case that wraps `Buffer` as `{ Buffer }` for `require('buffer')`.

Done when `require('fs') === fs` holds in a script.

### Step 3 — remove the QuickJS-side `Deferred`

`src/Deferred.cpp` / `src/Deferred.h` are superseded by `src/ProtoDeferred.cpp`. Remove the `Deferred::init(wrapper.getJSContext(), &wrapper)` calls and the `Deferred::getActiveDeferredCount()` check from `src/main.cpp`, then delete the files and their entry in `CMakeLists.txt`.

### Step 4 — module loaders

`src/modules/AsyncModuleLoader.cpp` is referenced by no other source file, and `ESModuleLoader` is used only by it; module-mode code uses the QuickJS loader hooks in `src/JSContext.cpp`. Confirm that neither loader is needed and remove them.

## Testing

Each step ends with:

- `ctest --test-dir build` passing.
- The relevant integration script under `tests/integration/` behaving as described in the step.
- `node tests/benchmarks/run_standard_comparison.js` to check for performance regressions.
