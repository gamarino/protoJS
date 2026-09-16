# The `protoCore` Global

The `protoCore` global exposes protoCore functionality that has no direct equivalent in standard JavaScript. Scripts run on the protoCore interpreter against the protoCore-native global object; on that object, `protoCore` is created by `src/ProtoCoreNativeBindings.cpp` and provides the collections (`Set`, `Multiset`, `SparseList`, `Tuple`), the mutability helpers (`ImmutableObject`, `MutableObject`, `isImmutable`, `makeImmutable`, `makeMutable`) and `runInThread`.

## Native multithreading: `runInThread`

```javascript
const d = protoCore.runInThread('cpuChunk', [2000000]);
d.then((sum) => {
    console.log("sum of generator states:", sum);
});
```

`protoCore.runInThread(workerName, args)`:

- **`workerName`** — the name of a native worker registered in `src/ProtoCoreNativeBindings.cpp`. The only registered worker is `cpuChunk`. If the name is not a string or is not registered, the call returns `undefined`.
- **`args`** — optional array whose elements are passed to the worker as protoCore objects, without serialization.
- **Result** — a [`Deferred`](DEFERRED_USAGE.md). The worker runs on a new protoCore thread (`ProtoSpace::newThread`) in the same `ProtoSpace` as the script. When the thread finishes, the Deferred is fulfilled with the worker's result on the script's event loop. If the thread cannot be created, the Deferred is rejected with the message `runInThread: failed to create thread`.

**`cpuChunk`** takes one argument, an iteration count `n`. It runs `n` steps of a 32-bit linear congruential generator and fulfils the Deferred with the integer sum of the generated states. The loop is data-dependent; the `parallel_cpu` benchmark in `tests/benchmarks/standard/` uses it to measure parallel CPU work.

Several calls can run at the same time, one protoCore thread each:

```javascript
let done = 0;
for (let i = 0; i < 4; i++) {
    protoCore.runInThread('cpuChunk', [2000000]).then((sum) => {
        done++;
        console.log("task", i, "finished:", sum);
    });
}
```

`protojs` keeps the process alive while these Deferreds are pending (see [DEFERRED_USAGE.md](DEFERRED_USAGE.md#process-lifetime)).

## Collections

`Set`, `Multiset` and `SparseList` are **constructors**: call them with `new`, or they throw `TypeError: Constructor <name> requires 'new'`. Each instance keeps its persistent protoCore collection in a private attribute and republishes the derived collection with a compare-and-swap on every mutation, so two threads mutating the same instance cannot lose an update. The mutators return the instance, so calls chain.

**`size()` is a method, not a property.** `set.size` is the function itself; `set.size()` is the count.

| Constructor | Methods |
|---|---|
| `new protoCore.Set(array?)` | `add(value)`, `has(value)`, `remove(value)`, `size()` |
| `new protoCore.Multiset(array?)` | `add(value)`, `count(value)`, `remove(value)`, `size()` |
| `new protoCore.SparseList()` | `set(index, value)`, `get(index)`, `has(index)`, `size()` |

```javascript
const set = new protoCore.Set([1, 2, 3, 3, 4]);
set.size();          // 4 — duplicates collapse
set.add(5).has(5);   // true

const counts = new protoCore.Multiset([1, 1, 2]);
counts.count(1);     // 2

const sparse = new protoCore.SparseList();
sparse.set(1000000, "far");
sparse.size();       // 1
sparse.get(7);       // undefined — an unset index reads as undefined
```

- The optional array argument seeds a `Set` or `Multiset`; anything that is not an array throws a `TypeError`.
- Elements are keyed by `ProtoObject::getHash`, so equal values built at runtime match: a `Set` containing `"ab"` reports `has("a" + "b") === true`.
- Calling a method on a receiver that is not an instance throws `TypeError: Invalid <name> object`.

## `Tuple`

`protoCore.Tuple(array)` builds the elements in protoCore list storage and returns them as a regular JavaScript `Array` (`Array.isArray` is `true`). Immutability is not enforced. A non-array argument throws a `TypeError`.

## Mutability helpers

`ImmutableObject(obj)` and `makeImmutable(obj)` return `obj.clone(immutable)`; `MutableObject(obj)` and `makeMutable(obj)` return `obj.clone(mutable)`. All four return a new object, never the original, and throw a `TypeError` for a primitive or missing argument.

**Former limitation, fixed in protoCore e43fa2e4.** `ProtoObject::clone()` used to copy an object's birth-time state rather than its current state, so the returned object came back **empty**: `protoCore.ImmutableObject({a: 1}).a` was `undefined`. The QuickJS-side module appeared to copy the properties only because it converted the argument JavaScript → protoCore → JavaScript through `TypeBridge`, which rebuilt the object along the way; the native binding has no such round trip. `clone()` now copies the object's current state, so all four helpers return a copy that carries the source's own attributes, and `protoCore.ImmutableObject({a: 1}).a` is `1`. `tests/integration/collections/protoCore_collections.js` asserts the copy. (protoCore also now exposes `processOwnAttributes`, which walks an object's own attributes as name/value pairs, so the attribute-name enumeration this note used to call for is available to embedders as well.)

Writes to the result of `ImmutableObject` are **not** rejected; immutability is not enforced at the JavaScript level.

`isImmutable(value)` is a **placeholder**: protoCore's public API exposes no mutability query, so primitives report `true` and every object reports `false`. This matches the behaviour of the QuickJS-side module it replaced. Reporting real mutability would need a public `isMutable` on `ProtoObject` in protoCore.

## Module discovery

protoCore also provides a unified module discovery system (resolution chain, `ProviderRegistry`, `ProtoSpace::getImportModule`). `require()` consults it for bare specifiers. See [MODULE_DISCOVERY_PROTOCORE.md](MODULE_DISCOVERY_PROTOCORE.md).
