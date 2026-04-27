# Migration plan: QuickJS-side bindings → protoCore-native

## Why

protoJS uses QuickJS for **parsing and bytecode compilation**, but executes
that bytecode in `ProtoInterpreter` against a **protoCore-native global
object** (Phase 6, 2026-03).  Globals installed via `JS_SetPropertyStr`
on the QuickJS-side global object never appear on the protoCore-native
global, because `JSContextWrapper::getNativeGlobal()` returns a blank
ProtoObject child of `JS Object.prototype` — modules that want to be
visible to user code must register on that ProtoObject explicitly.

Currently:

  - **Migrated**: `console`, `Date.now`, `performance.now`,
    `console.time` / `timeEnd` / `timeLog` (Console + TimingAPIs init).
    These use `ProtoNativeModule::registerOnGlobal` and install
    ProtoMethod-bearing objects directly on the protoCore global —
    callable from the protoCore interpreter without going back to
    QuickJS.

  - **Not migrated**: every other module — `Deferred`, `protoCore.*`,
    `setImmediate`, `__filename`, `__dirname`, `__protojs__`, `require`,
    `fs`, `path`, `url`, `http`, `events`, `stream`, `util`, `crypto`,
    `Buffer`, `net`, `worker_threads`, `cluster`, `dgram`,
    `child_process`, `dns`, `process`, `io`, `profiler`, `memory`,
    `debugger`.  These install via `JS_SetPropertyStr` on the QuickJS
    global and are **invisible** to user code running through the
    protoCore interpreter.

This document is the migration plan.  No bridges (no JS_Call from inside
the interpreter, no `tryQuickJSGetProperty` fallback): every binding
that user code can see must be a real ProtoMethod / ProtoObject on the
protoCore-native global.

## Migration pattern

The reference is `Console::init` (`src/console.cpp`):

```cpp
void Console::init(proto::ProtoContext* ctx, const proto::ProtoObject*& globalObj) {
    static const NativeEntry entries[] = {
        {"log",     Console::log},
        {"error",   Console::error},
        // ... all methods ...
        NATIVE_MODULE_END
    };
    const proto::ProtoObject* consoleObj =
        ProtoNativeModule::buildModule(ctx, entries, /*N=*/8);
    if (!consoleObj) return;
    globalObj = ProtoNativeModule::registerOnGlobal(ctx, globalObj, "console", consoleObj);
}
```

Caller in `src/main.cpp`:

```cpp
const proto::ProtoObject* nativeGlobal = wrapper.getNativeGlobal();
Console::init(wrapper.getProtoContext(), nativeGlobal);
wrapper.updateNativeGlobal(nativeGlobal);
```

Each module follows this pattern.  The `NativeEntry::method` is a
plain C++ function with the `ProtoMethod` signature:

```cpp
const proto::ProtoObject* foo(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*);
```

The body works directly with protoCore primitives (no `JSContext*`,
no `JSValue`).  When a module needs a class — Deferred, Set, Multiset,
SparseList — the constructor returns a mutable ProtoObject whose
prototype holds the instance methods.

## Steps (in dependency order)

Each step is independent (compiles and tests pass on its own) and
gets its own commit with a complete explanation.  Steps 1–5 are
required for `parallel_cpu` to actually run in parallel; the rest
are scoped but deferred.

### Step 1 — `setImmediate`  ← smallest, sets the pattern

Single ProtoMethod that pushes a callable onto the event loop.  The
callable will be a protoCore object: bytecode function (handled by
`callJSFunction`) or a ProtoMethod (handled by direct `asMethod`
dispatch).  The existing `EventLoop::enqueueCallback` accepts an
`std::function<void()>` — we wrap the proto callable invocation in
that lambda.  No QuickJS involvement.

### Step 2 — `__filename`, `__dirname`, `__protojs__`

Three primitive attributes on the protoCore-native global:

```cpp
nativeGlobal = nativeGlobal->setAttribute(ctx,
    JSSymbols::filename(ctx), ctx->fromUTF8String(filename.c_str()));
// ... and __dirname, __protojs__ similarly
```

JSSymbols entries added for each.  Removes three `JS_SetPropertyStr`
sites in `main.cpp`.

### Step 3 — `protoCore` module (the runtime API)

The module currently exposes:

  - `protoCore.Set(...)` / `Set.prototype.{add, has, remove, size}`
  - `protoCore.Multiset(...)` / methods
  - `protoCore.SparseList(...)` / methods
  - `protoCore.Tuple(...)`, `protoCore.Hash(...)`
  - `protoCore.runInThread(workerName, args)` ← critical for parallel_cpu
  - `protoCore.makeMutable(obj)`

All move to ProtoMethod implementations.  The Set / Multiset /
SparseList constructors build a mutable ProtoObject whose parent is a
prototype object holding the methods (mirrors the `console` pattern).
The thread-pool side of `runInThread` already runs natively
(`cpuChunkThreadEntry` is a ProtoMethod) — we only need to migrate the
JS-facing dispatcher.

### Step 4 — `Deferred` class

The largest single migration.  Deferred is a class with:

  - constructor `new Deferred(workerFn)` — creates a ProtoThread,
    queues the worker, returns an instance.
  - `instance.then(callback)` — registers a fulfilment callback.
  - `instance.catch(callback)` — registers a rejection callback.
  - The instance also carries internal state: pending / fulfilled /
    rejected, value, callback chains.

In QuickJS, the class uses `JS_NewClass` + `JS_NewCFunction2` with
`JS_CFUNC_constructor`.  In protoCore, we build a prototype object
with `then` / `catch` ProtoMethods, and the constructor returns a
mutable ProtoObject child of that prototype carrying the state in
private attributes (`__deferred_state__`, `__deferred_value__`,
`__deferred_then_cb__`, …).

Critically, the worker dispatch must use protoCore threads
(`ProtoSpace::newThread`) and the resolution must happen via the
event loop without re-entering QuickJS.  The existing
`Deferred::createPending` / `Deferred::resolveTaskFromNative`
need protoCore-side equivalents.

### Step 5 — Verify `parallel_cpu` runs in parallel

`tests/benchmarks/standard/parallel_cpu.js` branches on
`typeof protoCore.runInThread === 'function'` then
`typeof Deferred !== 'undefined'`.  After steps 3 and 4 both should
report `function` / a real constructor, the benchmark should pick
the protoCore path, and the JSON it emits should read
`"parallel": true` instead of the current `"parallel": false`.

Re-run the standard suite to update the geomean.  Update
`docs/PERFORMANCE_RUN_2026-04-26.md` with the new measurement and
remove the "parallel_cpu is not actually parallel" caveat.

### Step 6+ — Remaining modules (deferred)

Long tail; same pattern, larger surface.  Grouped roughly by user
visibility:

  Critical-for-CommonJS:
  - `require` — the module loader is the entry point for nearly all
    third-party `require('fs')` etc. usage.  Migrating `require`
    while keeping the existing modules QuickJS-side leaves a
    half-bridged state, so this one should come EARLY in step 6.

  Standard-library modules (each is `require('name')` plus a global):
  - path, url, http, events, stream, util, crypto, Buffer, net,
    worker_threads, cluster, dgram, child_process, dns, process,
    fs, io.

  Tooling globals (less load-bearing):
  - profiler, memory, debugger.

For each, the work is mechanical: replace `JS_NewCFunction` with
ProtoMethod, replace `JS_SetPropertyStr` with `setAttribute`, work
out the type-conversion replacements (which currently use
`TypeBridge::fromJS` / `TypeBridge::toJS` to round-trip through
QuickJS — those calls must turn into direct protoCore operations).

## What stays QuickJS-side

The QuickJS C API is still used for compilation: `JS_Eval` to parse
source and produce bytecode, `JS_GetClassID`, `JSAtom`, etc.  The
runtime never executes that bytecode through QuickJS — `ProtoInterpreter`
loads the QuickJS bytecode module and runs it natively.  So the
build dependency on QuickJS remains; only the **runtime-visible
binding surface** is migrating.

`TypeBridge` and `GCBridge` continue to exist for the QuickJS bytecode
loading path (constants from the constant pool, atoms, the
once-per-startup global object construction) but stop being used as
a runtime call/property bridge.

## Testing

Each step ends with:

  - `ctest --test-dir build` — 36/36 passing.
  - `tests/benchmarks/run_standard_comparison.js` — geomean check.
  - Targeted smoke tests for the migrated binding (e.g.,
    `setImmediate(fn); fn must be called`).

The session ends with `parallel_cpu` showing `"parallel": true`.
