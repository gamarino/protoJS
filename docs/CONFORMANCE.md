# protoJS — embedder conformance

The normative rule table is `protoCore/docs/EMBEDDER-CONFORMANCE.md`.

- Static ratchet: `conformance-allow.txt` — **exits 1 on purpose: 14 uncovered errors**
- Runtime adaptor: **NOT WRITTEN IN P4.** See "What is not covered".
- Run the static half:
  `python3 ../protoCore/scripts/conformance/check_static.py --repo .`

## Regression gate — 2026-09-26, against protoCore 2.5.0 (`df8406a3`)

`build_release` rebuilt **from clean** with **no `-j`** and re-run
**sequentially**, per this machine's constraints. protoCore 2.5.0 keeps
`SOVERSION 3`, so a stale protoJS would have linked against the new library
without complaint — which is exactly why the rebuild is from clean.

| Gate | Result |
|---|---|
| `ctest --test-dir build_release < /dev/null` | **34/34** |
| test262 `built-ins/{Object,Reflect,Proxy}`, `TEST262_CONCURRENCY=1` | **3619 passed** of 3875, 256 failed (1 syntax, 255 semantics), 0 timeout, 0 skipped — **exactly the recorded baseline** |

`ldd build_release/protojs` resolves `libprotoCore.so.3` to
`../protoCore/build_release/libprotoCore.so.3` (protoCore 2.5.0), never the
root-owned 1.0.0 at `/usr/local/lib`. **No regression from 2.4.0 to 2.5.0**: the
only changes under `core/` between the two tags are a `getenv`-gated report at
`ProtoSpace` teardown and the new `MutableCycles.cpp` translation unit, with the
`Thread.cpp` and `ProtoMPSCQueue.cpp` changes comment-only.

## Regression gate — 2026-09-25, against protoCore 2.3.0

Rebuilt from clean with **no `-j`** and re-run **sequentially**, per this
machine's constraints:

| Gate | Result |
|---|---|
| `ctest --test-dir build_p4 < /dev/null` | **34/34** |
| test262 `built-ins/{Object,Reflect,Proxy}`, `TEST262_CONCURRENCY=1` | **3619 passed**, 256 failed — exactly the recorded baseline |

`ldd` confirms `libprotoCore.so.3` from the workspace build tree, never
`/usr/local`. So protoCore's `ProtoThread::join` fix causes **no protoJS
regression**, which is the thing that had to be checked: protoJS reaches
`ProtoThread::join` at `src/ProtoCoreNativeBindings.cpp:196`, from a CPU-pool
thread, through a `ProtoContext` fabricated on the stack.

## What is not covered

**protoJS has no `proto::conformance::Host` yet**, so none of the twelve runtime
cases has run against it: rules 1, 2, 2b, 3, 4-runtime, 5, 8, 9b, 9c and 11 are
**UNVERIFIED by this suite** — not passed. The Catch2 adapter exists and the
target design is settled (a separate `protojs_conformance` executable linking
`protojs_core` only, so a conformance run never touches test262); what is missing
is the `Host` itself, and two facts make it more work than protoScala's or
protoPython's: protoJS has no unit test that evaluates a source string, and the
module-init list it would need is `static` inside `src/main.cpp`, which is not
part of `protojs_core`.

## Static check — the phase's largest finding

**14 uncovered errors, 217 justified warnings, 2 informational.** The allowlist
exits 1 deliberately: these are real defects, and a ratchet must never be used to
launder one.

### Rule 7 — five finalizers that do what the contract forbids absolutely

A finalizer runs **on the single GC thread, inside the sweep**. The contract is
that it *"never allocates cells, never publishes to a shared structure with
compare-and-swap, never loops over protoCore data and never dereferences other
`ProtoObject*`"*, and *"must also not block"*
(`protoCore/docs/GarbageCollector.md` §7).

| Finalizer | What it does that is forbidden |
|---|---|
| `freeServerState`, `src/modules/http/HTTPModule.cpp:120` | `thread.join()` **and** `ProtoRootSet::remove()`; also calls `getRootSet()`, which can *create* a root set |
| `freeClientRequestState`, `HTTPModule.cpp:731` | `ProtoRootSet::remove()` |
| `freeServerState`, `src/modules/net/NetModule.cpp:213` | a join via `teardownServer` + `ProtoRootSet::remove()` |
| `freeSocketState`, `NetModule.cpp:224` | a join via `teardownSocket` + `ProtoRootSet::remove()` |
| `freeWorkerState`, `src/modules/worker_threads/WorkerThreadsModule.cpp:357` | joins a thread that owns **its own `ProtoSpace`**, `ProtoRootSet::remove()`, **and** `workerWrapper.reset()` — which runs a whole `~JSContextWrapper` from inside a GC callback: `destroyRootSet`, a `std::recursive_mutex`, two thread-pool shutdowns with their own joins, and the destruction of a `ProtoSpace` |

`ProtoRootSet::remove` is separately forbidden because it takes the set's internal
mutex. A blocking wait here **stalls collection for the whole space**, and with a
heap limit configured it stalls every mutator waiting for reclamation behind it.

**Severity: high.** A concrete escalation path exists today: a `WorkerThread` GC'd
while its worker is running makes the GC thread join that worker, which is
executing JavaScript in its own space and may itself be waiting for memory.

**What makes this a finding rather than an oversight:** the *same* teardown
helpers are correctly bracketed in `UnmanagedScope` when called from JavaScript
(`NetModule.cpp:401`, `:649`, `HTTPModule.cpp:549`) and not when called from the
finalizer. Someone knew; the finalizer path was missed.

A sixth site, `src/GCBridge.cpp:676`, is a null finalizer whose comment says the
pointer is not owned. It is probably legitimate and is left uncovered because
nobody has answered it as a C7 question. A seventh, `finalizeJSValue`
(`GCBridge.cpp:27-35`), is the cleanest of the set — no join, no lock, no root-set
mutation — but `JS_FreeValue` can run QuickJS's own finalizer chain, and QuickJS
class finalizers in this tree do touch protoCore. Whether that reentrancy is safe
is not protoCore's to decide: **C7, unanswered.**

### Rule 2 — six unbracketed blocking joins

`src/ThreadPoolExecutor.cpp:43` and `:64` are reached from `~JSContextWrapper` via
`CPUThreadPool::shutdown()` / `IOThreadPool::shutdown()`
(`src/JSContext.cpp:199-201`) on the main thread, which **is** registered from
`ProtoSpace` construction, with no `UnmanagedScope`. The `condition.wait` at
`ThreadPoolExecutor.cpp:36-38` is unbracketed on the same path, and it waits for
pool work that may itself be a `ProtoThread::join`
(`ProtoCoreNativeBindings.cpp:196`) — a shutdown-order hazard between the two.
The other four are the joins inside the finalizers above.

**protoCore's 2.3 fix does not help any of these**: they are raw
`std::thread::join`, which the kernel cannot see. That is exactly why the rule
stayed in the suite after the kernel took over `ProtoThread::join`.

### Rule 4 — 197 warnings, and the reason none of them is an error

protoJS has by far the largest uninterned-key surface in the family: **1,401
`fromUTF8String` occurrences against 114 `createSymbol`**, and a defensible
**596** of them are key-shaped (the result reaches an attribute call as the *name*;
bounds 596–628, method recorded in the P4 report). Every one the checker flags
reaches `setAttribute` or `getAttribute`, which **auto-intern** and **fall back to
a content lookup** respectively — so all of them are **correct** and pay a
`SymbolTable::intern` or a content hash over the rope per call. A performance
finding, not a miss.

What would make one an **error** is a single uninterned key reaching
`getOwnAttributeDirect`, which has no STRING branch and no content fallback and
returns `nullptr` — also its value for "absent" — so the miss is silent. **The
checker found none.** That is the most reassuring result in this file, and it is
also why the severity split by destination (§D11) was worth the trouble: a check
that treated all 596 as errors would have buried the one distinction that matters.

**Two things to look at anyway**, both from reading rather than from the checker:

- `src/runtime/ProtoInterpreter.cpp:354` (`ensureInterned`) keeps a
  `static thread_local std::unordered_map` **keyed by raw `ProtoString*`**, and the
  file contains two mutually contradictory comments about it: `:392-398` argues the
  pointer key is safe, and `:405-426` records that the same cache was **removed**
  because *"when the GC freed a rope cell and the arena reused the slot for a fresh
  rope with different content, a hash collision returned the stale symbol — keys
  ended up installed under the wrong slot, manifested as `obj['k17'] = 17`
  becoming `obj['k_other'] = 17'"*. The live code keeps the cache. One of those two
  comments is wrong and it matters which.
- The `"__pd_<Name>__"` family builds a property-descriptor key by string
  concatenation and interns it **per call**, at 18 sites in that one file.

### Rule 6 — a dead TDZ check

`src/runtime/ProtoInterpreter.cpp:8438` states the convention as *"nullptr=absent,
PROTO_NONE=undefined"*. protoCore's is the other way round: `getAttribute` returns
`PROTO_NONE` for absent and `nullptr` only for invalid input. The consequence is
three lines below, at `:8445`:

```cpp
rawVal = liveGlobal->getAttribute(pContext, key, false);
    /* TDZ check: absent key for a lexical variable means uninitialized. */
    if (isLexical && !rawVal) { ... ReferenceError ... }
```

`PROTO_NONE` is `321UL` and therefore truthy, so `!rawVal` is true only for
invalid input: **the TDZ branch is dead, and a lexical variable read before
initialization does not raise `ReferenceError` through this path.** Reported as a
`warn` by the checker — the boolean-test shape has a false-positive rate that
cannot be bounded from the text — and confirmed by reading. **Severity: medium
(a missing ReferenceError, i.e. wrong behaviour rather than corruption), unproven:
no failing test262 case is attached, and attaching one is the first step.**

## Rule 11 — protoJS is conforming in a shape the naive rule would fail

Recorded here so the next reviewer does not "fix" it. protoJS has **ten** live
thread kinds, and they fall into the three conforming shapes rule 11 accepts:

- **Registered**, via `space->newThread`: `src/Deferred.cpp:421`,
  `src/ProtoCoreNativeBindings.cpp:170`.
- **HoldsNothing**: the CPU and IO pools, the HTTP and net accept loops, the
  socket read loop, and the CDP debugger thread, all of which hand work back
  through `EventLoop::enqueueCallback`.
- **OwnSpace**: `worker_threads` workers, each constructing its own
  `JSContextWrapper` and therefore its own `ProtoSpace`
  (`src/JSContext.h:234`).

Two corrections to the P4 plan's reading, both verified: `src/modules/ProtoCoreModule.cpp`
is **dead code** — absent from the `protojs_core` source list — so the `newThread`
and `ProtoThread::join` it contains are not in the binary and the plan's citation
of it as a registered-thread site is wrong. And `src/Deferred.cpp:293` constructs
a second `JSContextWrapper` — a whole second `ProtoSpace` plus two thread pools —
**on a registered ProtoThread of the first space**, so that kind is `Registered`
and `OwnSpace` at once. The rule-11 case would have to be told which, and that is
a question for whoever writes the `Host`.

## Judgement items

**C3, C5 and C7 are unanswered.** C5 has an easy answer waiting: protoJS's
user-visible sequence is an `Array` backed by `__elements__` → `ProtoList`, and the
compiled runtime constructs **zero** `ProtoTuple`s (the only `newTupleFromList` is
in the dead file), so rule 5 is not protoJS's risk. C7 must answer for
`GCBridge.cpp:676` and for the QuickJS reentrancy above. C3 is the one that needs
`gc.host_stress` under ASan.

## Informational

- protoJS never calls `setHeapLimits`, installs no `outOfMemoryCallback` and never
  calls `heapLimitCheckpoint`, so no collection starts by itself and rule 8 is
  unreachable as configured.
- `safepoint()` is called at **exactly one** site — inside the `DISPATCH()` macro
  at `src/runtime/ProtoInterpreter.cpp:6712`, every 1,024 dispatches. Nothing
  outside the interpreter polls: no module, pool or thread body does. That is
  sufficient for rule 1 only while every allocating path runs through the
  interpreter loop.
- `CONFORMANCE_JS.md:72` documents a *different* ten-pattern test262 built-ins set
  (9,400 tests, 71.9%) as protoJS's conformance baseline, while the figure this
  phase was given is the three-pattern 3619. The two coexist and the discrepancy
  should be settled so one number is authoritative.
