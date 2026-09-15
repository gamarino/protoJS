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
