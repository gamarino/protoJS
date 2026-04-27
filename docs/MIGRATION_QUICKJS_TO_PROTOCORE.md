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

### Step 6+ — Remaining modules

Migrated in the apr 2026 push (16 modules, ~3300 LOC of churn):

| Module           | LOC | Notes |
|------------------|-----|-------|
| `process`        | 119 | ✅ argv as real Array; env from POSIX `environ`; cwd/platform/arch/exit |
| `path`           | 183 | ✅ All eight string-path helpers (`join`/`resolve`/`normalize`/etc.) |
| `util`           | 155 | ✅ `types.*` predicates + inspect/format; `promisify` still a stub |
| `events`         | 124 | ✅ `EventEmitter` class; state via `__listeners__` attribute |
| `url`            |  54 | ✅ `URL` constructor + toString — establishes `__construct__` pattern |
| `dns`            | 239 | ✅ sync + async lookups; async pinned through wrapper root set |
| `Profiler`       | 102 | ✅ start/stop/getProfile + memory variants |
| `VisualProfiler` | 157 | ✅ exportProfile (Chrome DevTools JSON) + generateHTMLReport |
| `MemoryAnalyzer` | 272 | ✅ heapSnapshot now reads protoCore's heapSize/freeCellsCount |
| `IntegratedDebugger` | 323 | ✅ CDP server + breakpoint / call-stack bookkeeping |
| `IOModule`       | 246 | ✅ readFile / writeFile / *Async (now real ProtoDeferred) |
| `CommonJSLoader` | 468 | ✅ `require()` + `require.resolve` / `require.cache` on protoCore-native global |
| (already done earlier) | — | console, TimingAPIs, EventLoopBindings, ProtoDeferred, ProtoCoreNativeBindings |

Pending (each defines a JS class with private state — same six-step
recipe but more surface area):

| Module          | LOC | Notes |
|-----------------|-----|-------|
| `stream`        | 336 | Readable / Writable / Duplex / Transform classes |
| `cluster`       | 295 | Workers via `fork()`; depends on `events` (done) |
| `child_process` | 348 | spawn / exec; depends on `events` (done) |
| `dgram`         | 445 | UDP sockets — Socket class with `events` (done) integration |
| `http`          | 527 | Server / IncomingMessage / ClientRequest, depends on `net` |
| `worker_threads`| 530 | Worker class; spawns JSContextWrappers — already partially migrated for the EE setup |
| `crypto`        | 540 | Hash + Cipher classes; can use protoCore Symbol pattern for algorithm constants |
| `fs`            | 626 | All file IO; sync helpers can reuse `IOModule::{read,write}FileSync` |
| `net`           | 708 | Socket / Server classes |
| `Buffer`        | 746 | Array-like with byte storage; biggest single migration |

Cleanup (no migration, just deletion):
  - `src/Deferred.cpp` / `.h` — the original QuickJS-side Deferred,
    superseded by `ProtoDeferred.cpp`.  No JS-visible binding
    references it on the protoCore eval path; safe to delete once
    QuickJS-side init paths in main.cpp are gone (they still call
    `Deferred::init` but only because of leftover wiring; those
    sites should be the next target after one more module
    migration).

### Migration recipe

Each pending module follows the same six-step recipe:

  1. Header — drop `#include "quickjs.h"`, switch the public
     signature to
     `static const proto::ProtoObject* init(proto::ProtoContext*, const proto::ProtoObject*);`
  2. Implementation — replace each
     `JSValue f(JSContext*, JSValueConst, int, JSValueConst*)`
     with
     `const proto::ProtoObject* f(proto::ProtoContext*, const proto::ProtoObject*, const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)`
     and rewrite the body against `ctx->fromUTF8String / fromInteger`,
     `arg->isString / asString / toUTF8String`, `obj->setAttribute /
     getAttribute`, etc.
  3. Class instances (Buffer, fs.Stats, http.Server, …) — store
     state as ProtoObject attributes (mutable parent + named fields)
     instead of `JS_SetOpaque` C++ structs.  No finalizers needed —
     references stay reachable through the GC graph.
  4. Constructors — `wrapNativeFunction(ctx, ctorMethod, name, N,
     nullptr)`, then both `prototype` and `__construct__` attributes.
     OP_call_constructor's default-dispatch branch invokes
     `__construct__` with the pre-parented `newObj` as `self`.
  5. Build — `ProtoNativeModule::buildModule(ctx, entries, N)` then
     `ProtoNativeModule::registerOnGlobal(ctx, globalObj, "name", mod)`;
     return the updated `globalObj`.
  6. main.cpp — at every existing `Module::init(wrapper.getJSContext())`
     site, switch to:

       const proto::ProtoObject* nativeGlobal = wrapper.getNativeGlobal();
       nativeGlobal = Module::init(wrapper.getProtoContext(), nativeGlobal);
       wrapper.updateNativeGlobal(nativeGlobal);

For each, the work is mechanical and the diff size scales linearly
with the module's surface area; `process` / `path` / `events` /
`url` together took ~750 LOC of net diff and are the canonical
references.

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
