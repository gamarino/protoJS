# protoJS — GC Root Discipline Audit

## The Invariant

This audit verifies that protoJS respects the invariant formalized in
protoCore's [`STW_ELIMINATION_RESEARCH.md`](../../../protoCore/docs/STW_ELIMINATION_RESEARCH.md)
Section 11:

> Before any call into protoCore code that may allocate, every
> transient `ProtoObject*` must be reachable from a root the GC sees
> (a `ProtoContext` local, a `ProtoRootSet` pin, an attribute of an
> already-rooted object, or a NULL-context perpetual allocation).

The protoCore GC does **not** scan:
- C++ stack variables (locals, registers)
- C++ static storage
- Function arguments held only in C++ stack
- `std::vector<ProtoObject*>` on the C++ stack

If a `ProtoObject*` is held only in one of these locations across a
call that may trigger GC, the referenced Cell can be freed and the
next dereference is use-after-free.

## protoJS has TWO legitimate pinning mechanisms

A key insight that makes auditing protoJS different from protoPython
and protoST: protoJS embeds QuickJS, which has its own reference-counted
JSValue type. The GCBridge mediates between QuickJS reference counts
and protoCore Cell liveness. This produces **two** pinning paths:

### Path A — Raw `ProtoObject*` pinned via `ProtoRootSet`

For native code that holds raw `ProtoObject*` values (no JSValue
wrapper), the canonical mechanism is the wrapper's root set:

```cpp
proto::ProtoRootSet* rs = wrapper->getRootSet();
auto handle = rs->add(transientObj);
// ... call into protoCore code that may allocate ...
const proto::ProtoObject* obj = rs->resolve(handle);
rs->remove(handle);
```

Used heavily in `src/modules/net/`, `src/modules/http/`,
`src/modules/fs/`, `src/modules/worker_threads/`, and the `ProtoDeferred`
infrastructure.

### Path B — JSValue handles pinned via QuickJS refcount + GCBridge

When a `ProtoObject*` is wrapped in a `JSValue`, the JSValue is
reference-counted by QuickJS via `JS_DupValue` / `JS_FreeValue`.
**GCBridge** (`src/GCBridge.cpp`) maintains the bidirectional mapping
between JSValues and their backing protoCore Cells. While the JSValue
is alive in QuickJS (refcount > 0), GCBridge keeps the Cell pinned.

```cpp
JSValue cb = JS_DupValue(ctx, argv[0]);  // refcount++ keeps Cell pinned
task->thenCallback = cb;
// ... arbitrary delay, GC may run multiple cycles ...
JS_Call(ctx, task->thenCallback, ...);   // safe — Cell still pinned
JS_FreeValue(ctx, cb);                   // refcount-- releases pin
```

This is the standard QuickJS C-extension pattern, augmented by GCBridge
to extend it into protoCore. **No explicit `ProtoRootSet` operation is
needed** for values held as JSValues with positive refcount.

### When does each path apply?

| You hold | Use |
|---|---|
| Raw `ProtoObject*` from `ctx->newList()`, `ctx->fromUTF8String(...)`, etc. | Path A — `wrapper->getRootSet()->add(...)` |
| `JSValue` from QuickJS API (`JS_NewString`, `JS_NewObject`, `argv[i]`, etc.) | Path B — `JS_DupValue` keeps it alive |
| `ProtoObject*` that you also convert to JSValue via GCBridge | Either works; Path B is the more idiomatic for QuickJS-bridged code |

The two paths are complementary, not redundant. Mixing them
inconsistently is what produces false-positive audit findings (Path B
sites flagged as "missing Path A pin" — see Section "Verified clean
sites" below).

## Audit Status by Module

This is a survey, not an exhaustive line-by-line audit. The goal is to
characterize the discipline used across the protoJS native layer.

### ✅ Confirmed clean — Path A discipline

| Module | Mechanism | Notes |
|---|---|---|
| `ProtoDeferred.cpp` | `PinnedInvocation { Handle cb, Handle val }` + helpers `pinInvocation()` / `takeInvocation()` | De-facto formalization of the pin/unpin pattern. Used consistently in `drainQueue`, `deferredThen`, constructor. |
| `EventLoopBindings.cpp:82-91` | Inline `rs->add(callback)` → `callJSFunctionFromAsync()` → `rs->remove(pin)` | Pattern correct. |
| `src/modules/net/NetModule.cpp` | `state->socketPin = rs->add(sock)` at state construction; `state->serverPin` similarly | All async paths capture `(wrapper, pin)` and resolve via `rs->resolve(pin)`. |
| `src/modules/http/HTTPModule.cpp` | `state->listenerPin = rs->add(listener)` at server construction | Same pattern as net. |
| `src/modules/fs/FSModule.cpp` | `pin = rs->add(deferred)` → enqueue → `rs->resolve(pin)` → `rs->remove(pin)` | Clean async I/O lifecycle. |
| `src/modules/worker_threads/WorkerThreadsModule.cpp` | `state->workerPin = rs->add(...)` for worker objects | Worker lifecycle correctly pinned. |

### ✅ Confirmed clean — Path B discipline (initially flagged as suspect)

| Site | Initial flag | Verification |
|---|---|---|
| `Deferred.cpp:298` (`enqueueReject` lambda capturing `[t, errorMsg]`) | "JSValues `t->catchCallback` not pinned by RootSet" | **False positive.** `t->catchCallback` is a JSValue obtained via `JS_DupValue` (see `deferredCatch` at line 227). QuickJS keeps refcount > 0; GCBridge keeps the backing Cell pinned. JSON serialization through `t->serializedResult` (a C++ `uint8_t*` heap buffer) carries no ProtoObject* across the boundary. |
| `Deferred.cpp:314` (`enqueueResolve` lambda capturing `[t]`) | Same | Same verification — `t->thenCallback` is QuickJS-pinned via `JS_DupValue`. |
| `Deferred.cpp:426, 442` (error-path enqueue lambdas) | Same | Same — captures `shared_ptr<DeferredTask>` whose JSValue members are QuickJS-pinned. |

**Lesson learned**: when auditing protoJS, always check whether the
held value is a raw `ProtoObject*` (needs Path A) or a `JSValue`
(uses Path B automatically). The two are easy to confuse from the
audit perspective; the runtime distinguishes them correctly.

### 🟡 Unsurveyed — likely clean but not verified

These modules have not been audited line-by-line. Spot checks suggest
they follow the established patterns, but a thorough sweep has not
been performed.

- `src/modules/crypto/` — uses Path B (JSValues) almost exclusively
- `src/modules/stream/` — uses Path B
- `src/modules/util/` — small, mostly Path B
- `src/modules/url/` — Path B
- `src/modules/events/EventsModule.cpp` — uses Path A for listener lists, needs verification
- `src/modules/path/PathModule.cpp` — Path B
- `src/modules/cluster/ClusterModule.cpp` — Path A for IPC state, needs verification

### 🔴 Known concerns

None currently. The audit found no clear violations of either Path A
or Path B discipline. Earlier flags against `Deferred.cpp` were
false positives stemming from confusion between the two paths.

## Patterns to apply when adding new native code

### Adding a JS-callable native function

```cpp
const proto::ProtoObject* myNativeFunc(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    // self and elements of args are already pinned by the dispatcher
    // (invokeEEMethod or callJSFunction).  No action needed.

    // If you create a transient ProtoObject* and need to hold it
    // across a call that may allocate, pin it:
    const proto::ProtoObject* tmp = ctx->newList();
    JSContextWrapper* w =
        static_cast<JSContextWrapper*>(JS_GetContextOpaque(...));
    auto pin = w->getRootSet()->add(tmp);

    // ... allocating call(s) ...

    tmp = w->getRootSet()->resolve(pin);
    w->getRootSet()->remove(pin);
    return tmp;
}
```

For the common case of a one-shot newList → setAttribute → return,
no pin is needed because nothing happens between newList and return
that triggers GC outside the user's control.

### Adding an EventLoop async callback

```cpp
auto rs = wrapper->getRootSet();
auto cbPin  = rs->add(callbackObj);
auto valPin = rs->add(valueObj);

EventLoop::getInstance().enqueueCallback([wrapper, cbPin, valPin]() {
    auto* rs = wrapper->getRootSet();
    const proto::ProtoObject* cb  = rs->resolve(cbPin);
    const proto::ProtoObject* val = rs->resolve(valPin);
    rs->remove(cbPin);
    rs->remove(valPin);
    // dispatch...
});
```

This is the documented pattern in `CLAUDE.md` § "protoCore GC Bridging
Rules" / Mechanism B.

### Adding code that holds JSValues across async work

Use `JS_DupValue` to acquire a strong reference, store the JSValue in a
heap-allocated structure (`shared_ptr<MyTask>`, or similar), and call
`JS_FreeValue` when done. The protoCore Cell backing the JSValue is
pinned automatically by GCBridge for the duration of the QuickJS
refcount. This is Path B and does not need explicit RootSet operations.

## Open Follow-ups

Not addressed in this audit:

1. **Sweep of the 🟡 unsurveyed modules above.** Each is small and the
   pattern is well-defined; a focused 1-2 hour sweep per module should
   close them out.

2. **A RAII helper analogous to protoPython's `TransientPin`** would
   make Path A code more concise and less error-prone. Currently each
   site manages `add` / `remove` manually. A helper like:

   ```cpp
   struct RSPin {
       proto::ProtoRootSet* rs;
       proto::ProtoRootSet::Handle h;
       RSPin(JSContextWrapper* w, const proto::ProtoObject* obj)
         : rs(w->getRootSet()), h(rs->add(obj)) {}
       ~RSPin() { rs->remove(h); }
       const proto::ProtoObject* resolve() const { return rs->resolve(h); }
   };
   ```

   Not urgent — the current discipline works — but would reduce
   boilerplate. Defer until a refactor naturally surfaces the need.

3. **Automated detection.** A grep pattern for the violation shape
   `const proto::ProtoObject\* [a-z]+ = ctx->[a-zA-Z]+\(.+\);` followed
   within 10 lines by another `->setAttribute\|->newChild\|->getAttribute`
   would catch most candidates. Out of scope here.

## Cross-Reference

- protoCore [`docs/STW_ELIMINATION_RESEARCH.md`](../../../protoCore/docs/STW_ELIMINATION_RESEARCH.md)
  § 11 — formalization of the invariant.
- protoCore [`docs/GarbageCollector.md`](../../../protoCore/docs/GarbageCollector.md) — links
  to the research note.
- protoPython [`tasks/audit/03-gc-roots.md`](../../../protoPython/tasks/audit/03-gc-roots.md) —
  analogous audit for the Python runtime.
- protoST [`tasks/audit/gc-roots.md`](../../../protoST/tasks/audit/gc-roots.md) —
  analogous audit for the Smalltalk runtime.
- protoJS [`CLAUDE.md`](../../CLAUDE.md) § "protoCore GC Bridging
  Rules" — operational rules for native authors.

## Status

| Date | Action | Result |
|---|---|---|
| 2026-05-26 | Initial audit, sweep informed by protoCore Section 11 | No violations found. Path A and Path B discipline both correct. Earlier flags against Deferred.cpp identified as false positives. |
