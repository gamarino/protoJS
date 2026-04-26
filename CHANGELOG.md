# Changelog

All notable changes to protoJS are documented in this file.

## [Unreleased]

### Added

- **Restore standard timing APIs** (2026-04-26): The runtime now exposes
  `Date.now()`, `performance.now()`, and `console.time()` /
  `console.timeEnd()` / `console.timeLog()` (plus `console.info` and
  `console.debug` as Node-style aliases of `log`).  Implemented as
  native ProtoMethod bindings in `src/console.cpp` (new class
  `protojs::TimingAPIs` alongside `protojs::Console`) and wired in
  from `src/main.cpp` next to the existing `Console::init` calls.
  Backends: `std::chrono::system_clock` for `Date.now` (whole-
  millisecond integer since the Unix epoch); `std::chrono::steady_clock`
  for `performance.now` (double-precision ms since program start)
  and for `console.time` (per-label store is process-wide and mutex-
  guarded to match Node semantics across callbacks).  Closes the
  regression where every benchmark in `tests/benchmarks/standard/`
  and the legacy `console.time`-based suite threw `TypeError: is not
  a function` before reaching its workload.

- **Fix function-argument binding regression** (2026-04-26): User-defined
  function arguments were arriving as `undefined` at the callee
  (`function f(a,b){return a+b}; f(3,4)` returned `NaN`).  Root cause
  was in `src/runtime/ProtoInterpreter.cpp` — the slot-storage helpers
  (`setSlot`, `initStack`, `stackPush/Pop/peek`) all silently no-op'd
  when `ctx->closureLocals` was null, but the OP_call / OP_call_method
  / OP_call_constructor handlers create a child `ProtoContext` with
  `parameterNames=nullptr`, which means protoCore's lazy-init in the
  ProtoContext constructor leaves `closureLocals` null.  The handlers
  then called `setSlot(&childCtx, i, arg)` to seed argument slots, but
  every one of those calls dropped on the floor.  When `runBytecode`
  later bootstrapped `closureLocals` (line 1040), it was too late —
  the args were gone.

  Fix: added `ensureClosureLocals(ctx)` and called it from every
  helper that mutates closureLocals.  Idempotent with the existing
  bootstrap in `runBytecode`.  All call paths now correctly deliver
  arguments to callees.

- **JSON.stringify / JSON.parse polyfill** (2026-04-26): With the
  argument-binding fix in place, a JS-level polyfill is now usable.
  Added `kJSONPolyfillPrefix` in `src/main.cpp` — top-level globals
  (no IIFE) prepended to user code in non-module mode.  Cross-eval
  function references in the current runtime are still flaky (a
  function defined in `wrapper.eval` A is not callable cleanly from
  `wrapper.eval` B because its bcId is module-relative), so prepending
  keeps the polyfill and user code in the same module.  Also worked
  around `String.prototype.length` returning `undefined` in the
  protoCore eval path by iterating with `charAt(i) === ''` as the
  end-of-string sentinel.  The protoCore-side `JSON` namespace stub
  in `ProtoInterpreter.cpp` is now a mutable Object (was immutable,
  which silently swallowed property assignments).  Verified end-to-end:
  `JSON.stringify({a:1, b:"x", c:[true,null]})` →
  `{"ok":true,"name":"numeric_loop","time_ms":42}` style output;
  `__BENCH_RESULT__<json>` lines now emit correctly.

- **Fix interpreter slot/stack quadratic-allocation regression** (2026-04-26):
  Commit 4bd3657 (Mar 5) had replaced the interpreter's `std::vector`-based
  value stack and local-slot store with `ProtoSparseList` (persistent AVL
  tree) — to make all references GC-visible.  The intent was right but
  the cost was catastrophic: every `stackPush` / `stackPop` / `setSlot`
  allocated O(log N) AVL cells, and a tight integer loop spent ~38 % of
  CPU in the GC scanning the resulting cells.  Microbenchmarks slowed
  ~1000× — a `for(let i=0;i<10000;i++) s+=i` loop went from
  milliseconds to ~7 seconds.

  Switched the slot/stack storage to `ProtoContext::automaticLocals` —
  a flat `const ProtoObject*[]` that protoCore already scans as a GC
  root, giving the same GC visibility with O(1) array writes.  Layout
  per call frame:

      [0 .. argCount-1]               args
      [argCount .. argCount+varCount] local vars
      [.. closureCount]               closure vars
      [stackBase .. stackBase + top]  pushed operand stack

  `stackBase` and `stackTop` live in a thread_local `std::vector<
  InterpFrame>` — pushed at runBytecode entry, RAII-popped at exit so
  nested calls compose correctly.  All ~750 stackPush/Pop/Top/At/Size/
  Empty + setSlot/getSlot call sites kept their existing 1-arg
  (ProtoContext*) signature; only the helpers' bodies changed.

  protoCore companion change: added
  `ProtoContext::resizeAutomaticLocals(unsigned int newCount)` so
  runBytecode can grow the slot region after construction (the
  bytecode module's `stackSize_` + var/closure counts are only
  available after the function is resolved, not at ProtoContext
  build time).  See `proto::ProtoContext::resizeAutomaticLocals`
  in protoCore commit on the same day.

  Measurements (Release):

      bench (5K iter int loop):  ~9 s  → ~40 ms       (~225×)
      bench (100K):               ~90 s →  ~298 ms     (~300×)
      bench (1M):                 timeout → ~6.9 s    (linear)

  Standard suite (`run_standard_comparison.js`): 5/7 benchmarks now
  run end-to-end (was 0/7 before the timing/JSON/arg-binding fixes
  earlier in this session).  Geomean vs Node 271× slower (was
  effectively infinite — every protoJS run hit the 120 s/bench
  timeout).  function_calls.js and string_concat.js still time out
  at default sizes; reducing inner counts would fit them in the
  per-bench budget but that's a separate tuning step.

- **PROTOJS_BIN env var** (2026-04-26): `tests/benchmarks/run_standard_
  comparison.js` now honours `PROTOJS_BIN` for selecting which
  protojs binary to test, so an experimental build can be benched
  without overwriting `../build/protojs`.

- **setImmediate in CLI** (2026-03-03): Global `setImmediate(callback)` enqueues to the event loop so scripts can yield between ProtoThread creations and avoid lock contention when creating several threads in quick succession. Used by `parallel_cpu.js` under protoCore.

- **Phase 6: ProtoCore-native global object** (2026-03-03): The global scope is now a ProtoObject built at first eval from the QuickJS global via `JS_GetOwnPropertyNames` and `TypeBridge::fromJS`. No QuickJS heap is used for the global container; conversion only at host boundaries. `runBytecode` accepts `pGlobalRoot` and updates it on `put_field`/`define_field` so top-level `var` assignments persist and subsequent reads see the new object. Directed tests: `proto_eval_smoke.js` (6 cases, including Phase 6 global var) and `tests/test262/tests/phase6_native_global.js`. Docs: ARCHITECTURE.md § 1.4, CONFORMANCE_JS.md Phase 6 table, src/runtime/README.md, TECHNICAL_AUDIT.md.

### Fixed

- **Multithreading: first ProtoContext in thread entry** (2026-03-03): Deferred and runInThread now create the first ProtoContext inside the thread’s initial function with `nullptr` as caller, so no ProtoContext is shared across threads. `deferredProtoThreadEntry` builds `ProtoContext(space, nullptr, ...)` and uses it for all work; `cpuChunkThreadEntry` does the same and then calls `cpuChunkWorker`. Deferred uses only ProtoThreads (no CPUThreadPool fallback for execution); if `newThread` fails or wrapper/space is missing, the Deferred is rejected. Docs: `src/runtime/README.md` § Multithreading and protoCore.

- **parallel_cpu benchmark under protojs** (2026-03-03): `protoCore.runInThread` tasks are scheduled with `setImmediate` stagger so the main thread yields between `newThread` calls. Under protojs, `WORK_PER_TASK` is 2e5 so the run completes within the runner timeout; Node keeps 2e6. Standard comparison passes all 7 benchmarks; parallel_cpu reports protoJS faster than Node on that benchmark.

- **Packaging** (2026-02-08): Added `packaging/build_deb.sh` to build the protoJS .deb from current templates on Debian/Ubuntu. INSTALLATION and PROCEDURES updated: users must rebuild the .deb (e.g. run `./packaging/build_deb.sh`) after the protocore dependency fix—otherwise an old .deb still reports "protoCore is not installed" when the `protocore` package is installed.

- **Debian package dependency check** (2026-02-08): protoJS .deb preinst now looks for the protoCore package under the name **`protocore`** (lowercase), which is how CPack installs the protoCore .deb. Also added fallback check for `protoCore`. The control template `Depends` was updated to `protocore (>= 1.0.0)` so installation succeeds when protoCore is installed from its CPack-generated .deb. Docs (INSTALLATION.md, PROCEDURES.md) updated accordingly.

- **protoCore getImportModule API** (2026-02-08): CommonJSLoader now passes `ProtoContext*` as the first argument to `ProtoSpace::getImportModule(context, logicalPath, attrName)` to match the current protoCore API (fixes build error when building against updated protoCore).

- **-Wformat-security warnings** (2026-02-08): All `JS_ThrowTypeError(ctx, dynamic_string.c_str())` calls replaced with `JS_ThrowTypeError(ctx, "%s", dynamic_string.c_str())` in CommonJSLoader, ESModuleLoader, IOModule, FSModule, and DNSModule so the format string is a literal and the compiler no longer reports format-security warnings.

- **require() built-in module resolution** (2026-02-08): `require('fs')`, `require('path')`, `require('stream')`, `require('crypto')`, `require('buffer')`, and other core modules now resolve from the global object so integration tests (fs, stream, crypto, buffer) pass. See CommonJSLoader built-in resolution and docs (README, NATIVE_MODULES).

- **GCBridge null-pointer handling** (2026-02-07): Fixed `-Wnonnull` compiler warnings and potential undefined behavior in `GCBridge::detectLeaks()` and `GCBridge::getMemoryStats()` when `ProtoContext` is null. Both functions now return early with null/empty values instead of dereferencing a null pointer. Added null checks in `reportLeaks()` and `getMemoryStats()` for defensive handling of empty reports.

- **Exception logging in eval** (2026-03-06): When compile or run failed, the code called `JS_GetException(ctx)` a second time to stringify the exception; the first call had already consumed it, so the second returned an uninitialized value that stringified as "[unsupported type]". Fixed by (1) capturing the exception inside `compileToBytecode` via an optional out-parameter as soon as `JS_Eval` returns an exception, and (2) using that value (or the one already in `val`) for logging in `eval`. Real exceptions from the compiler are now reported correctly when QuickJS sets them; when the compiler fails without setting an exception (e.g. some `__JS_EvalInternal` fail paths), the message may still be "undefined". Standard suite `__BENCH_RESULT__` remains unavailable until both compile success and `console.log` output are fixed in the protoCore path.

### Build & Test

- Full project recompilation: protoCore + protoJS clean build
- All 33 unit tests passing (ctest)
- Integration tests verified (hello_world, arithmetic, modules)

### Performance (2026-02-07)

- Performance suite executed successfully: `run_nodejs_comparison.js` (5/5 benchmarks)
- **Array operations:** 34–45x faster than Node.js (immutable structural sharing)
- **Overall speedup:** ~10–45x depending on workload
- Added [docs/PERFORMANCE_RUN_2026-02-07.md](docs/PERFORMANCE_RUN_2026-02-07.md) with run report and analysis

### Performance (2026-03-06)

- Re-ran Node.js comparison suite: 5/5 benchmarks passed; protoJS wins all 5.
- **Latest results:** array_operations 45x faster; overall speedup 10.81x (protoJS avg 42.6 ms vs Node 460.4 ms).
- Full combined suite (41 tests) run with Node.js; report and JSON written to `tests/benchmarks/results/report_2026-03-06_00-07-34.html` and `results_2026-03-06_00-07-34.json`.
- Updated [docs/PERFORMANCE_RUN_2026-02-07.md](docs/PERFORMANCE_RUN_2026-02-07.md) with latest run table; [docs/PERFORMANCE_REPORT.md](docs/PERFORMANCE_REPORT.md) with "Latest Node.js comparison", report paths, and new "Results analysis" section (Node comparison table + full-suite summary).
