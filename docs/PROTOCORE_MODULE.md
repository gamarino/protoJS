# The `protoCore` Global

The `protoCore` global exposes protoCore functionality that has no direct equivalent in standard JavaScript. Scripts run on the protoCore interpreter against the protoCore-native global object; on that object, `protoCore` is created by `src/ProtoCoreNativeBindings.cpp` and currently provides a single function, `runInThread`.

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

## Collections and mutability helpers (not reachable from scripts)

`src/modules/ProtoCoreModule.cpp` implements a larger `protoCore` object against the QuickJS C API:

- constructors `Set` (`add`, `has`, `remove`, `size`), `Multiset` (`add`, `count`, `remove`, `size`) and `SparseList` (`set`, `get`, `has`, `size`);
- functions `Tuple`, `ImmutableObject`, `MutableObject`, `isImmutable`, `makeImmutable` and `makeMutable`.

`protojs` installs that object only on the QuickJS-side global object, which scripts running on the protoCore interpreter do not see. As a result, `protoCore.Set`, `protoCore.Tuple` and the other names above are `undefined` in scripts; `tests/integration/collections/protoCore_collections.js` checks for each one before using it. Moving these bindings to the protoCore-native global is step 3 of [MIGRATION_QUICKJS_TO_PROTOCORE.md](MIGRATION_QUICKJS_TO_PROTOCORE.md).

## Module discovery

protoCore also provides a unified module discovery system (resolution chain, `ProviderRegistry`, `ProtoSpace::getImportModule`). `require()` consults it for bare specifiers. See [MODULE_DISCOVERY_PROTOCORE.md](MODULE_DISCOVERY_PROTOCORE.md).
