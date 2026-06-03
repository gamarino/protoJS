# Changelog

All notable changes to protoJS are documented in this file.

## [Unreleased]

### Fixed (test262 spec conformance push, round 3 — 2026-06-02)

Continuation of the prior conformance push. 30 more commits targeting
concrete ECMA-262 gaps surfaced by deeper test262 traversal. Each
commit fixes one root cause and remains under the "purity > performance"
constraint.

**Numerical/string coercion:**
- `String.prototype.toString` throws TypeError on non-string receivers
  per §22.1.3.27 / §22.1.3.32 (was returning "[object Object]").
- `Object.create` throws TypeError on non-Object/non-null prototype
  arg per §20.1.2.2 step 1 (was returning `{}` for `undefined`,
  primitives, etc.).
- `Object.defineProperty` raises TypeError for missing target arg
  (was silent PROTO_NONE return).
- `Object.hasOwn` raises TypeError on null/undefined target per
  §20.1.2.13 step 1.
- `Array.prototype.sort` rejects non-callable comparefn with TypeError
  per §23.1.3.30 step 1 (was silently using the string default).
- `Array.from(src, mapFn, ...)` rejects non-callable mapFn with
  TypeError per §23.1.2.1 step 2.
- `JSON.parse` throws SyntaxError on malformed input per §25.5.1
  step 3 (was returning `null`).

**ToIntegerOrInfinity coverage extended:**
- `Array.prototype.includes` / `at` / `fill` / `copyWithin` /
  `lastIndexOf` now apply ToIntegerOrInfinity to their index
  arguments. NaN → 0, +Infinity past-end returns false / out-of-range,
  -Infinity clamps to 0. (Companion to the indexOf / slice / splice /
  flat fixes from the prior round.)
- `Number.prototype.toString` applies ToIntegerOrInfinity on radix
  (string "16" coerces correctly; NaN → 0 → RangeError).
- `ToNumber` trims the full Unicode WhiteSpace + LineTerminator set
  (NBSP, BOM, U+2028/2029, U+3000, etc.) — not just ASCII.

**Spec details inside built-ins:**
- `Number.prototype.toString` emits fractional digits for non-base-10
  radixes (`(0.5).toString(2) === "0.1"`).
- `String.prototype.normalize` raises RangeError for forms outside
  {NFC, NFD, NFKC, NFKD}.
- `String.prototype.trim / trimStart / trimEnd` strip the full
  Unicode WhiteSpace set (NBSP, BOM, U+2028/2029, U+3000, etc.).
- `String.fromCodePoint` raises RangeError for NaN, Infinity, negative,
  > 0x10FFFF, or non-integer code points per §22.1.2.2 step 5.
- `parseInt` only auto-detects the "0x" prefix when the radix
  argument is unspecified or 16 — explicit radix 10/8/2 etc. parses
  "0" and stops at 'x'.
- `Array.from` clamps NaN / negative array-like lengths to 0
  (was hanging on negative length via uint wrap-around).

**Built-in shape (name / length / descriptor):**
- `Function.prototype.length === 0`, `Function.prototype.name === ""`
  per §20.2.3.
- `String.prototype.toLocaleString` implemented (identity without
  ICU) — `"abc".toLocaleString()` no longer throws.
- `Boolean.prototype.{toString, valueOf}` re-installed with name /
  length attributes (was empty strings).
- `Object.prototype.{hasOwnProperty, isPrototypeOf,
  propertyIsEnumerable}` carry .length === 1 (was 0).
- `Promise.length === 1` per §27.2.3.
- `Object.create.length === 2` per §20.1.2.2.
- `Error.prototype[@@toStringTag] === "Error"` (now
  `Object.prototype.toString.call(new TypeError())` returns
  `"[object Error]"`).
- `Date.parse` and `Date.UTC` implemented per §21.4.3.2 /
  §21.4.3.4 (ISO 8601 fragments / UTC time component to ms).

**Reflect + Promise:**
- `Object.create` mutable ctor: the constructor backref / prototype
  round-trip now preserves identity so `Object.prototype.constructor
  === Object` holds (was failing after the constructor-backref
  series due to immutable-ctor splitting).
- `JSON.stringify` honours the replacer-array form (filters object
  keys per §25.5.2 step 4) and invokes `toJSON` before serialising
  per step 3.

**Misc:**
- `Function.prototype.bind` raises TypeError on non-callable receiver
  per §20.2.3.2 step 1.
- `Object.assign` skips non-enumerable own properties per §20.1.2.1
  step 4.c.ii.1.

### Fixed (test262 spec conformance push, rounds 1+2 — 2026-06-02)

A ~80-commit sprint dedicated to closing concrete ECMA-262 conformance
gaps surfaced by test262. Each commit fixes one root cause and
preserves the protoJS architectural rule that purity wins over
performance until the community justifies otherwise. Documented by
category — see `git log c7f7dc9d..` for the full per-commit detail.

**Constructor backref + descriptor (§17, §19.x.4.1):**
`prototype.constructor` now points at the live constructor object on
every built-in prototype (Boolean, Number, Object, Function, Promise,
Map, Set, all 11 TypedArray kinds, ArrayBuffer, DataView, Error +
each subtype) with the non-enumerable `__pd_constructor__ = 0x3`
descriptor. Function.prototype was switched to mutable so the
recursive backref doesn't split it into two identities; the same
lesson applies to every other built-in prototype and is captured in
`memory/feedback_protojs_proto_constructor_backref.md`.

**Type coercion via ToObject / ToNumber / ToString / ToPrimitive:**
- `Object.getOwnPropertyDescriptor`, `getOwnPropertyNames`,
  `getOwnPropertyDescriptors` (new), `getOwnPropertySymbols` (stub),
  `Object.fromEntries`, `Array.from`, every `Array.prototype.*`
  iterator, and all four `Reflect.has/get/set/ownKeys` now throw
  TypeError on null / undefined / primitive targets per spec §28.1.x
  step 1 / §20.1.2.x step 1. `arrayThrowIfNullUndefined` now
  recognises the undefined sentinel uniformly.
- `ToNumber` distinguishes null (→ +0) from undefined (→ NaN).
  `globalIsNaN` / `globalIsFinite` route through `jsToNumber` so
  objects coerce via valueOf/toString.
- `parseInt` / `parseFloat` apply ToString step (custom toString /
  valueOf) on object arguments. `decodeURI` /
  `decodeURIComponent` raise URIError on malformed escapes.
- `Number` constructor delegates to `jsToNumber`. `String()` no-arg
  returns `""` (was "undefined"). `objToStr` returns "undefined"
  for the undefined sentinel and now calls user-defined `toString`
  via callJSFunction for `'a'.concat([1,2])` style coercion.
- `Array.prototype.indexOf` / `lastIndexOf` / `slice` / `splice` /
  `flat` apply ToIntegerOrInfinity to their index arguments
  (`Infinity` / `-Infinity` / `NaN` handled per spec).
- Spec-compliant number formatting: `ToString(Number)` and console
  number printing both use shortest round-trip; `toExponential` /
  `toPrecision` emit single-digit exponents, handle NaN/Infinity,
  throw RangeError, and serialise `-0` without sign;
  `toFixed(-0)` returns `"0.00"`.

**Built-in shape (name / length / descriptor):**
- Every Math method, JSON.parse / stringify, Date.now,
  parseInt / parseFloat / isNaN / isFinite / encodeURI /
  encodeURIComponent / decodeURI / decodeURIComponent, global
  built-in constructors (Boolean / Number / String / Object /
  Error + subtypes) now carry the spec-mandated `.name` and
  `.length` with the §17 descriptor (`writable:false`,
  `enumerable:false`, `configurable:true` → 0x2).
  Global functions also carry the §17 wrapper descriptor (0x3) on
  the globalRoot binding, so `for (k in globalThis)` no longer
  emits them.
- `Math` is now mutable so `delete Math.sqrt` and `Math.foo = 1`
  persist as the spec allows.
- `Symbol.toStringTag` set on JSON and Promise.prototype so
  `Object.prototype.toString.call(...)` returns `[object JSON]` /
  `[object Promise]`.
- `Error.prototype.message === ""` per §20.5.5.3.
- `String.prototype.length === 0` per §22.1.3.

**Spec details fixed in built-ins:**
- `Array(N)` validates length per §22.1.1.2 (RangeError on
  non-integer / NaN / Infinity / >= 2^32 / negative).
- `Array.prototype.with` throws RangeError on out-of-range index.
- `Array.prototype.toString` invokes the receiver's own `join`
  (now respects user overrides).
- `Array.prototype.toLocaleString` implemented.
- `Array.prototype.join` treats null / undefined elements as `""`.
- `Object.prototype.toLocaleString` delegates to ToString for typed
  primitives (was aliased to `objectToString`, which gave
  `[object Number]` for `(1).toLocaleString()`).
- `Object.assign` honours non-enumerable own properties (skips them
  per §20.1.2.1 step 4.c.ii.1).
- `Math.pow(1, NaN) === NaN`, `Math.pow(±1, ±Infinity) === NaN` per
  §21.3.2.24. `Math.round` preserves `-0`. `Math.clz32` applies
  ToUint32 (NaN / ±Infinity / 0 / 2^32 → 0). `Math.hypot(NaN,
  Infinity) === Infinity` (Infinity dominates NaN).
- `JSON.stringify` indent argument per §25.5.2 step 6; undefined
  array elements become `null`, undefined object members are
  dropped, callable values serialise as `null` (arrays) or are
  dropped (objects).
- `String.prototype.repeat` / `padStart` / `padEnd` apply ToLength
  with RangeError on `-1` / `+Infinity` / target > 16M code units.
- `String.prototype.replace` / `replaceAll` invoke callable
  replacements; `String.prototype.search` / `match` handle
  non-regex patterns; indexOf / startsWith / endsWith / includes
  coerce missing args to "undefined" per ToString rules.
- `Function.prototype.bind` throws TypeError on non-callable
  receivers per §20.2.3.2 step 1.

**Promise:**
- `makeSettledPromise` parents the result on `Promise.prototype` so
  `Promise.resolve / reject / all / allSettled / race / any`
  outputs satisfy `p instanceof Promise`.
- `collectIterable` reads the array's native `__elements__`
  storage; `Promise.all` / `allSettled` / `race` / `any` no longer
  collapse their input into an array of undefined values.

**Reflect:** added `deleteProperty`, `getPrototypeOf`,
`setPrototypeOf`, `isExtensible`, `preventExtensions` (§28.1.4 /
§28.1.5 / §28.1.8 / §28.1.10 / §28.1.11) — all routed through the
existing non-object TypeError guard.

**Numeric format:** `ToString(Number)` uses shortest round-trip
per §7.1.12.1 (`String(3.14)` → `"3.14"`, was
`"3.1400000000000001"`). `console.log` matches the same algorithm.
Exponent normalised to single-digit form.

**Other:**
- `Number.parseInt === parseInt` and `Number.parseFloat ===
  parseFloat` per §21.1.2.12 / §21.1.2.13 (was two distinct
  function objects). `patchNumberParseFns` re-binds Number.* to
  the global references after the global fns are installed.
- `Number.prototype` methods reinstalled in `ensureNumberConstructor`
  so they inherit Function.prototype's `.call/.apply/.bind` (build
  ran before `ensureFunctionPrototype` so the original wrappers
  had no parent).

### Fixed

- **Standard benchmark suite restored after silent regression**
  (2026-05-31):  Two regressions had broken the standard suite between
  2026-05-06 and now.  Together they explain why no comparable
  performance number could be produced for nearly a month.

  - **`printf("TRACE: ...")` in `DISPATCH()` macro** (commit `283a02a5`).
    Committed by snapshot `7b5d9ddd` on 2026-05-22 with the explicit
    note "in-progress... not separately reviewed" — the line emitted a
    trace to stdout on every bytecode dispatch.  The `__BENCH_RESULT__`
    regex never matched, every benchmark reported `Error: undefined`,
    and per-dispatch printf overhead was catastrophic but masked by the
    runner failure upstream.  One-line removal.
  - **`Date.now` undefined** (commit `b546a64f`).  `TimingAPIs::init`
    created `Date` via `ctx->fromMethod(...)` then attached `.now` via
    `setAttribute`.  Method objects do not retain attribute writes —
    the assignment silently dropped, leaving `Date.now` permanently
    undefined.  Every standard benchmark times its workload via
    `Date.now()` so every benchmark threw `TypeError`.  Switched to
    `newObject(true)` with matching `name`/`prototype` so the
    interpreter's stub-installer guard skips it; constructor behaviour
    (`new Date()`) intentionally not provided — no standard benchmark
    needs it and reintroducing it properly belongs to broader Date
    work.

  After the fixes the suite passes 14/14 vs Node and 14/14 vs QuickJS.
  Geomean ratio against the 2026-04-28 baseline is **0.249** — protoJS
  is ~75 % faster than that baseline across the six benchmarks present
  in both runs (P-JS-{0..7} cycle's actual landed effect, finally
  measurable).  See README.md § "Honest baseline — 2026-05-31" and
  `tests/benchmarks/results/comparison_2026-05-31.md` for the full
  comparison including the new QuickJS reference.

- **`tree_traversal` UAF stabilised** (2026-05-04): The benchmark built a
  16383-node binary tree of mutable objects (depth=14) and then summed the
  values; with `PROTOCORE_GC_REINCLUDE_SURVIVORS=ON` it crashed reproducibly
  during the sum pass with a use-after-free on a snapshot's `attributes`
  pointer (stale `ocValue->attributes` pointing into a recycled cell). Root
  cause was a stale-mark bug in protoCore's GC: mark would set the mark bit
  on cells reachable from a root but not in `segmentsToProcess`, sweep never
  cleared those bits, and the next cycle's mark skipped the entire subtree
  underneath any such cell. Fix landed in protoCore (pre-mark unmark pass);
  see `protoCore/docs/GarbageCollector.md` § "Phase 4a". With the fix the
  benchmark passes 10/10 and the rest of the standard suite (`object_property`,
  `object_write_only`, `object_read_only`, `json_transform`, `string_concat`,
  `string_processing`, `array_literal`, `control_flow`, `function_calls`,
  `numeric_loop`, `tree_traversal`) is 5/5 stable.

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
