# GC bridging rules for protoCore embedders

protoJS uses protoCore as its object model. protoCore's tracing garbage
collector only keeps an object alive if it can reach it from a root. Whenever a
`ProtoObject*` must outlive an allocation boundary that the GC cannot see —
typically because a C++ lambda registered with the event loop or a thread pool
has captured the pointer — the code must use one of the two mechanisms that
protoCore provides, described below.

Storing references on the JavaScript global object with `setAttribute` to keep
them alive is an anti-pattern. New code must not introduce such sites.

The full rationale is in protoCore's design document, section
[Keeping ProtoObjects alive across allocation boundaries the GC cannot see](https://github.com/numaes/protoCore/blob/master/DESIGN.md#keeping-protoobjects-alive-across-allocation-boundaries-the-gc-cannot-see).

## Choosing a mechanism

| Object lifetime | Mechanism | Examples |
|---|---|---|
| Process-perpetual (language vocabulary, prototypes, cached literals) | Allocation with a null `ProtoContext` | An attribute-name symbol, the `Function.prototype` object, the `__bytecode_id__` symbol |
| Bounded asynchronous lifetime (microseconds to seconds) | `ProtoRootSet`, obtained with `wrapper->getRootSet()` | A `d.then(cb)` callback held in a `setImmediate` or thread-pool continuation; the deferred returned by `protoCore.runInThread` |

The two mechanisms are complementary, not interchangeable:

- A null-context allocation has no release path; that is its purpose. Do not
  try to release one.
- Do not use `ProtoRootSet` for objects that are conceptually language
  vocabulary. Every pinned root is scanned on each GC cycle, so pinning
  objects that never die adds cost for no benefit.

## Mechanism A: perpetual allocation through a null `ProtoContext`

Pass `nullptr` as the `ProtoContext*` argument through the entire allocation
call chain. The cell is then allocated with `posix_memalign` directly: it is
never placed on a thread free list or in a context's young-generation chain,
and it lives for the rest of the process.

```cpp
// Single-shot strong symbol: createSymbol(...) already does this.
const proto::ProtoString* k =
    proto::ProtoString::createSymbol(ctx, "myAttribute");

// Manual perpetual allocation, only needed for a non-string cell:
auto* permanent = new(/*ctx=*/nullptr) MyCell(/*ctor args*/);
```

**Invariant:** every cell reachable from a perpetual root must itself be
perpetual. A perpetual cell that holds a reference to a normal GC-managed cell
is a use-after-free waiting to happen, because the GC sees no path to that
child. In practice this means threading a single `nullptr` through the whole
construction chain, as protoCore's symbol table does:
`ProtoStringImplementation::fromUTF8Bytes(nullptr, ...)` →
`buildAVL(nullptr, ...)` → `new(nullptr) ProtoStringImplementation(...)`.

protoCore already applies this mechanism in two places that protoJS relies on:

- `ProtoString::createSymbol(ctx, name)` interns the name in protoCore's
  `SymbolTable`, which allocates every symbol with a null context. Symbols are
  never collected.
- `ProtoObject::setAttribute(ctx, key, value)` with a heap string key (a string
  that is not already a symbol) interns the key through the same
  `SymbolTable::intern` path, so the stored key is perpetual as well.

Do not work around these paths; they are correct.

## Mechanism B: `ProtoRootSet` (transient pinning)

Use a root set for callback receivers, deferred values and in-flight worker
arguments: any object whose reachability from JavaScript can end before the
C++ continuation runs. Pin the object before handing the continuation off, and
release it inside the continuation.

`JSContextWrapper::getRootSet()` (declared in `src/JSContext.h`) returns a
single root set named `"protojs-async"`. It is created on first use through
`ProtoSpace::createRootSet` and destroyed by `~JSContextWrapper`. During its
stop-the-world marking phase the GC treats every object pinned in a registered
root set as a root.

```cpp
proto::ProtoRootSet* rs = wrapper->getRootSet();
auto cbHandle  = rs->add(callbackObj);
auto valHandle = rs->add(valueObj);

EventLoop::getInstance().enqueueCallback([wrapper, cbHandle, valHandle]() {
    auto* rs = wrapper->getRootSet();
    const proto::ProtoObject* cb  = rs->resolve(cbHandle);
    const proto::ProtoObject* val = rs->resolve(valHandle);
    rs->remove(cbHandle);
    rs->remove(valHandle);
    // dispatch...
});
```

The handle type, `proto::ProtoRootSet::Handle`, is a 64-bit integer that
encodes a slot index and a generation, so capturing it by value in a C++ lambda
is safe and cheap. `add`, `resolve` and `remove` may be called from any thread.
Outstanding pins are independent of each other. Each handle must be removed
exactly once; removing a stale handle whose slot has since been reused is a
silent no-op because the generation no longer matches, and `resolve` returns
`nullptr` for such a handle.

## Blocking calls on a registered protoCore thread

A registered protoCore thread that blocks without leaving the running set is still
counted in `runningThreads`, so the stop-the-world quorum
(`parkedThreads >= runningThreads`) can never be met, no collection cycle can start,
and every thread that then needs memory waits for a cycle that cannot begin. That is
a deadlock, not a slow shutdown; the mechanism was read off a live backtrace in
protoClojure.

protoCore >= 2.3 brackets its own `ProtoThread::join`, and its header says in as many
words that this covers `ProtoThread::join` only — a direct `std::thread::join`, a
`condition_variable::wait`, or a `future::get` on a thread the embedder registered is
still the embedder's obligation.

**Which protoJS threads are registered**, verified rather than assumed: protoJS never
calls any `registerThread`. A thread enters `runningThreads` either by constructing a
`ProtoSpace` — whose constructor adopts the constructing thread as that space's main
thread — or through `ProtoSpace::newThread`. So the JS main thread is registered, and
each `worker_threads` worker is registered **in its own space**, because it constructs
its own `JSContextWrapper`. The CPU and I/O pool workers, the `net`/`http` accept and
read loops and the GC thread are **not**: protoCore's `Thread.cpp` says explicitly
that a bare `std::thread` is not counted.

Use `ProtoContext::UnmanagedScope` where a context is in hand, and
`protojs::ThreadUnmanagedScope` (`src/ThreadProtoContext.h`) where one is not — it reads
the calling thread's registered context from a thread-local and is a no-op on an
unregistered thread, which is the correct behaviour there. It also takes an explicit
context, so a helper that blocks can carry its own guard and be called from both kinds
of site; `awaitServerTeardown` in `NetModule.cpp` is the pattern.

Keep the guard **next to** the call it protects. protoCore's static conformance rule
`blocking_join_unbracketed` looks for one within eight lines of the join, and a guard
further away than that is one a future reader will not connect to the call either.

Two substitutes that look reasonable and are both wrong:

- **`this->pContext` at a destruction site.** A worker's `JSContextWrapper` is
  destroyed by the main thread, and its context belongs to the worker's own space with
  the worker thread adopted as that space's main thread. Parking that context from the
  main thread increments `parkedThreads` for a thread that is actively running managed
  code — the inverse failure, where the collector reaches quorum and scans while a
  mutator mutates.
- **`JSContextWrapper::current()`.** It is only published during `eval()` and under
  `CurrentScope`, so it is null on exactly the teardown paths that block.

Bracket the **whole** blocking region, not just the syscall. `ThreadPoolExecutor::
shutdown` waits on a condition variable for the queue to drain and *then* joins every
worker; a guard around the join loop alone leaves the wait blocking just as long.

`tests/unit/test_blocking_regions.cpp` asserts the property directly by reading
`ProtoSpace::parkedThreads` from inside the blocking region. Removing the guard makes
it report `0 >= 1`.

## Finalizers

`Cell::finalize` runs on the GC thread during sweep, concurrently with the mutators.
protoCore's `docs/GarbageCollector.md` section 7 is the normative text; the short form
is that a finalizer **must not block, must not allocate cells, must not publish to a
shared structure, and must not dereference other `ProtoObject*`**. The
`ProtoExternalPointer` callbacks passed to `ProtoContext::fromExternalPointer` are
finalizers and follow the same contract.

In protoJS terms, a finalizer may close a file descriptor and flip an atomic flag. It
may **not**:

- `join()` an OS thread. It blocks, and a finalizer is not even proof the thread
  stopped, because sweep runs with the world going.
- call `ProtoRootSet::remove`. That takes the very mutex the collector holds during
  root collection, so it both blocks and publishes — and on the same space it is a
  self-deadlock risk.
- reset a `std::unique_ptr<JSContextWrapper>`. That runs a whole
  `~JSContextWrapper`: a root set destruction, `GCBridge::cleanup` (which iterates a
  `ProtoSparseList` and dereferences `ProtoObject*`s), the process-wide thread pool
  shutdown, and the destruction of a second `ProtoSpace`.

Where the work genuinely has to happen, move it to **where the owner relinquishes the
resource** — `serverClose`, `socketDestroy`, `workerTerminate` — which is what section
7 prescribes and what protoCore itself did for thread teardown, releasing in
`thread_main`'s tail rather than in `finalize`. For the case where the script never
relinquishes and the object is simply collected, use `src/GcOrphanQueue.h`: the
finalizer records the orphan with an intrusive push onto a lock-free stack — no
allocation, no lock, no protoCore call, no wait — and `EventLoop::processCallbacks`
releases it on a mutator thread. That is section 7's own Phase 5b shape.

**Three of protoJS's five finalizers cannot run mid-program at all**, and this is
worth knowing before designing around them: `net.Server`, `net.Socket` and
`worker_threads.Worker` each pin the very object that carries their
`ExternalPointer`, so the pin keeps the owner reachable, the `ExternalPointer` is never
swept, and the finalizer that would release the pin never runs. A self-sustaining
root. Their bodies execute only at space teardown. `http.Server` and
`http.ClientRequest` pin a callback rather than themselves, so those two are genuinely
collectable and their finalizers do run.

`tests/cli/finalizers_do_not_block.py` audits all five bodies, following calls one
whole call graph deep within each translation unit — which is what the original `net`
finalizers needed, since their `join` was inside `teardownServer` and not in the
finalizer body at all.

## Anti-patterns

New code must not use any of the following. Use the matching mechanism instead.

- `wrapper->getNativeGlobal()->setAttribute(ctx, "__some_pending_thing__", obj)`
  to pin asynchronous state. The GC does see the global object, but every
  asynchronous operation then pays for a compare-and-swap rebuild of the entire
  mutable-object snapshot, contends with every other writer, and leaks state
  across embedders. Use `getRootSet()->add(obj)`.
- Custom thread-local registries that duplicate `ProtoRootSet`. The wrapper
  already owns one; reuse it.
- "Pin until the end of the program" workarounds for values that belong in
  `createSymbol`. If the value is vocabulary, intern it.
- Conditional pinning, such as "pin only if a GC ran". There is no reliable way
  to detect that, and any race with the GC makes the code incorrect.

## Review checklist

Before merging code that captures a `ProtoObject*` in a C++ lambda passed to
`EventLoop::getInstance().enqueueCallback(...)` or to
`CPUThreadPool::getInstance().getExecutor().submit(...)`:

1. Identify every `ProtoObject*` in the lambda's capture list.
2. For each one, point to where it was pinned with `getRootSet()->add(...)`, or
   show that it is perpetual.
3. If neither applies, the lambda has a latent use-after-free. Fix it before
   merging.

And before merging any blocking call — `join`, `condition_variable::wait`,
`future::get`, a sleep, or a syscall that may not return promptly:

4. Decide whether the calling thread is registered (see above; a `JSContextWrapper`
   constructor or `ProtoSpace::newThread` is what registers one).
5. If it may be, bracket the whole blocking region with `ProtoContext::UnmanagedScope`
   or `protojs::ThreadUnmanagedScope`, next to the call. On an unregistered thread the
   guard costs nothing.
   Then run `python3 ../protoCore/scripts/conformance/check_static.py --repo .` and check
   that `blocking_join_unbracketed` is still at zero.
6. If the call is inside a finalizer, it does not belong there at all. See above.
