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

Migrated (22 modules total in the apr 2026 push):

| Module           | LOC | Notes |
|------------------|-----|-------|
| `process`        | 119 | ✅ argv as real Array; env from POSIX `environ` |
| `path`           | 183 | ✅ All eight string-path helpers |
| `util`           | 155 | ✅ `types.*` predicates + inspect/format |
| `events`         | 124 | ✅ `EventEmitter` class; state via `__listeners__` attribute |
| `url`            |  54 | ✅ `URL` class — establishes `__construct__` pattern |
| `dns`            | 239 | ✅ sync + async lookups; async pinned through wrapper root set |
| `Profiler`       | 102 | ✅ start/stop/getProfile + memory variants |
| `VisualProfiler` | 157 | ✅ exportProfile (Chrome DevTools JSON) + generateHTMLReport |
| `MemoryAnalyzer` | 272 | ✅ snapshots from protoCore's heapSize/freeCellsCount |
| `IntegratedDebugger` | 323 | ✅ CDP server + breakpoint / call-stack bookkeeping |
| `IOModule`       | 246 | ✅ readFile / writeFile / *Async (now real ProtoDeferred) |
| `CommonJSLoader` | 468 | ✅ `require()` + `require.resolve` / `require.cache` |
| `fs`             | 626 | ✅ Sync API + `fs.promises` returning real ProtoDeferreds |
| `stream`         | 336 | ✅ Readable / Writable / Duplex / Transform / PassThrough |
| `crypto`         | 540 | ✅ Hash class fully working via OpenSSL EVP; Cipher/Sign/Verify stubs preserved |
| `cluster`        | 295 | ✅ fork-based workers; per-worker state in attributes |
| `child_process`  | 348 | ✅ spawn / exec / execFile / fork |
| `dgram`          | 445 | ✅ UDP sockets via createSocket |
| `http`           | 527 | ✅ createServer/listen/close + IncomingMessage (getHeader) + ServerResponse (writeHead/write/end); accept loop on std::thread captured via ExternalPointer; request listener pinned in ProtoRootSet; activeServer counter keeps event loop alive while bound |
| `worker_threads` | 542 | ✅ Worker class via prototype + ExternalPointer-backed `WorkerState` (std::thread + owned JSContextWrapper); on/emit/postMessage/terminate; cross-wrapper messaging via in-module C++ JSON serialise/parse + ProtoRootSet pin for the Worker instance; events.EventEmitter mixin via __events__ delegate; isMainThread / parentPort / workerData global accessors |
| `Buffer`         | 746 | ✅ Each Buffer is a ProtoObject carrying `__byte_buffer__` (a `ProtoByteBuffer::asObject()` handle traced naturally by the GC) plus `__is_buffer__` marker; toString / slice / copy / fill / indexOf / includes on the prototype; from / alloc / concat / isBuffer on the constructor; supports utf8/hex/ascii/latin1/base64 |
| `net`            | 708 | ✅ Server / Socket via prototype + ExternalPointer-backed state; accept and read loops on std::thread captured in `ServerState` / `SocketState`; on/emit forwarders to events.EventEmitter delegate; cross-thread emit via EventLoop::enqueueCallback + callJSFunctionFromAsync; data events deliver real Buffer instances; activeCount feeds main.cpp's drain loop |
| (already done earlier) | — | console, TimingAPIs, EventLoopBindings, ProtoDeferred, ProtoCoreNativeBindings |

Migration complete — every user-facing module now lives on the
protoCore-native side.  No `JS_NewClassID`, `JS_SetOpaque`, or
QuickJS-side per-instance struct remains in the registered modules.
QuickJS continues to provide parsing / compilation only; the runtime
contract (objects, GC, threading, I/O) is owned end-to-end by
protoCore.

Bug fix bundled with this batch
  - `EventsModule::listenersKey` was caching the `__listeners__`
    symbol via `static thread_local` — but ProtoSpace owns one
    SymbolTable per wrapper, so a callback running on the main
    thread that emits on a worker-space EE would look up under the
    main-space symbol and miss every listener.  Fixed by always
    re-interning via `createSymbol(ctx, ...)` (sharded hash lookup,
    cost negligible).  No other module is affected since none of
    them traffic ProtoObjects across wrappers within a single
    thread the way worker_threads does.

Cleanup (deletion targets, no migration):
  - `src/Deferred.cpp` / `.h` — original QuickJS-side Deferred,
    superseded by `ProtoDeferred.cpp`.  Safe to delete once the
    `Deferred::init` calls in main.cpp are removed (they install on
    the QuickJS-side global only and are unreachable from the
    protoCore eval path).
  - `src/modules/AsyncModuleLoader.cpp` / ESModuleLoader — verify
    whether these are reachable; if not, drop.

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
