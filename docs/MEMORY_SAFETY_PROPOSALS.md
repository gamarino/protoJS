# Memory safety: two open problems and proposals

Status: findings and proposals for the maintainer, 2026-10-04. Nothing in this
document is implemented. Both items were on the memory-safety queue and were
held back as design work rather than local fixes. The fixes made in the same
pass (RegExp capture array, bytecode loader, exceptions across contexts, typed
array coercion, the native stack guard, the pinned `"undefined"` string) are in
CHANGELOG.md and docs/GC_BRIDGING.md.

## 1. Per-process statics that hold one space's cells

Every `worker_threads` Worker builds its own `JSContextWrapper` and its own
`proto::ProtoSpace`, and runs the built-in installers for its own global
(`ensureArrayPrototype`, `ensureArrayBufferConstructor`,
`ensureTypedArrayConstructors`, ... from `src/runtime/ProtoInterpreter.cpp`
around line 6224). Several of those installers publish the prototype they made
in a C++ `static` that is shared by the whole process and not keyed by space.
The last space to initialise wins.

### Reproduction

```js
// main.js
function probe(tag) {
  console.log(tag,
    Object.getPrototypeOf([1, 2].map(x => x)) === Array.prototype,
    Object.getPrototypeOf(Uint8Array.of(1)) === Uint8Array.prototype);
}
probe('before');
const w = new (require('worker_threads').Worker)('child.js'); // child: parentPort.postMessage(1)
w.on('message', () => {});
w.on('exit', () => probe('after-exit'));
```

protoJS at `26a284b0d` prints `before true true` and `after-exit false false`:
after a worker has run, arrays made by `map` and typed arrays made by
`Uint8Array.of` on the MAIN thread get the worker's prototypes -- objects of a
space that has since been destroyed.

### The statics

| Static | Where | Read by |
|---|---|---|
| `s_arrayProto` | `src/ArrayPrototype.cpp:103` (its comment still says "a single logical context per process") | `createNewArray` |
| `s_taBaseProto`, `s_taProtos[11]` | `src/TypedArrayPrototype.cpp:28-29` | `of`, `from`, `map`, `slice`, `subarray`, the constructors |
| `s_abProto` | `src/ArrayBufferPrototype.cpp:13` | `createArrayBuffer` |
| `s_mapPrototype`, `s_setPrototype` | `src/MapPrototype.cpp:23`, `src/SetPrototype.cpp:23` | Map and Set construction |
| `GCBridge::contextMappings`, `rootAnchor` | `src/GCBridge.h` | the GC bridge |
| `BehaviorRegistry::registry` | `src/runtime/BehaviorRegistry.cpp:127-141` | `resolve()` on every element access that consults a behaviour |

Consequences, in increasing order of severity:

- **Wrong identity.** `instanceof Array`, `Array.prototype` patches and
  `Object.getPrototypeOf` checks fail on the main thread once a worker has run.
- **Use after free.** `~JSContextWrapper` forgets only the `PinnedBuiltin`
  entries and the prototype overrides of its space. The statics above keep
  pointing into the destroyed space, whose cells are freed.
- **Collector blind spot while both spaces live.** A static is not a root, and
  protoCore's collector of space A does not see references held by space B
  (protoCore `docs/GLOBAL_MUTABLE_TABLE.md`, "If a thread of space B keeps a cell
  of space A ... A may free it").
- **`BehaviorRegistry`.** A process singleton, an `unordered_map` keyed by
  prototype address. Each worker inserts its eleven typed-array prototypes
  without a lock while other threads read the map in `resolve()` -- a data race
  on rehash -- and entries are never removed, so after a worker exits a new
  cell at a recycled address is taken for a typed-array prototype.

Not affected: `JSSymbols` and other `createSymbol` keys. protoCore's
`SymbolTable` is process-global and symbols are allocated with a null context,
so they are shared safely (the comment in
`src/modules/worker_threads/WorkerThreadsModule.cpp` that says each space has
its own symbol table is out of date).

### Proposal

1. Replace each prototype static with a `PinnedBuiltin`
   (`src/runtime/PinnedBuiltin.h`): one entry per space, pinned in that
   space's wrapper root set, dropped by `forgetSpace` when the wrapper is
   destroyed. That is the mechanism the iterator prototypes already use. Cost:
   `get()` is two acquire loads per probed slot instead of one load; the hot
   readers (`createNewArray`) can keep a `thread_local` copy refreshed when the
   space changes, as `t_cellMarker` does with `s_cellMarkerCache`.
2. Make `BehaviorRegistry` per space (or key it by `(space, prototype)`), take
   a lock on insert (readers can use a read-copy-update snapshot, since inserts
   happen only at start-up), and remove a space's entries in `forgetSpace`.
3. Add a test that runs a worker to completion and then checks the prototypes
   of every kind of object the main thread makes, and one that starts and
   stops workers under a small heap ceiling while the main thread allocates
   arrays, typed arrays, maps and sets.

Size: about eight statics and one registry, with the test. The risk is in the
hot path of `createNewArray`; measure `benchmarks/` before and after.

## 2. Computed property keys are interned for ever

`ProtoObject::setAttribute` interns every heap string key in protoCore's
`SymbolTable` (protoCore `core/ProtoObject.cpp` around line 1016; strings of
up to 6 bytes are inline and are not interned). Symbols are allocated with a
null context and are never collected (`docs/GC_BRIDGING.md`, Mechanism A).
protoCore's comment justifies this as "bounded by the program's vocabulary",
which holds for identifiers and fails for data used as keys.

Where protoJS turns runtime strings into attribute keys:

- `obj[k]` with a string `k` (`toPropertyKey` -> `coercePropNameToKey`,
  `src/ObjectPrototype.cpp`, called from the interpreter's element opcodes);
- `JSON.parse` (QuickJS parses, `TypeBridge::fromJS` sets the attributes);
- `Object.fromEntries`, `Object.assign`, `Reflect.set`, `defineProperty`;
- index keys that are attributes rather than dense elements
  (`JSSymbols::indexKey`, which caches the first 4,096 and interns the rest);
  only indices of seven or more digits are heap strings;
- the `__pd_<key>__` sidecar of every key given a non-default descriptor.

Cost per distinct key: one 64-byte string cell, one 64-byte leaf per 32 bytes
of content, AVL interior cells for long keys, and a malloc'd hash bucket --
roughly 150 to 200 bytes for keys of 7 to 32 bytes, never reclaimed. Nothing
bounds it. A server that keys objects by request id, session id or UUID grows
by that much per distinct id for its whole life.

Measured on this machine (Release build, protoCore 2.14.1): `o['key-a-' + i]
= i` on a fresh object for 50,000 distinct `i` took 4.3 s and grew RSS by
186 MiB; the same loop in Node.js took 70 ms. Part of that is ordinary garbage
the collector had not reclaimed (no ceiling was set), so the figure is an
upper bound on the interning cost, not a measurement of it.

protoCore has no weak or collectable interning: `proto_internal.h` and
`core/SymbolTable.cpp` rule it out explicitly, because the attribute tree
compares keys by pointer identity and the per-thread attribute cache is keyed
by the name pointer.

### Options (design level; each touches protoCore's model)

- **(a) A collectable key type.** Keys that are not vocabulary stay ordinary
  GC'd strings and are compared by content hash plus equality. This touches
  the attribute sparse-list comparison, the attribute tree's
  `processReferences` (which traces values, not keys, today), the per-thread
  attribute cache, and every lookup that relies on pointer identity. It is the
  general fix and the most invasive one.
- **(b) Dictionary mode in protoJS.** After N dynamic keys, an object moves
  them into a hidden `ProtoMap` keyed by content, with GC'd string keys; the
  named attributes keep the vocabulary. protoCore is not changed. protoJS must
  route `toPropertyKey`'s callers, `ownKeys` (enumeration order), the property
  descriptor sidecars and `in`/`delete` through the dictionary. This follows
  the rule "extend protoCore by a new type, do not change an existing model".
- **(c) A weak SymbolTable.** Sweep buckets whose symbol is unreachable. It
  conflicts with null-context allocation (which the collector does not see) and
  needs mark coordination across spaces, because the table is process-global.

Recommendation: (b). It keeps protoCore's model, confines the change to
protoJS, and covers the cases that grow without bound (JSON objects and
`obj[k]` maps), while ordinary objects with identifier keys stay on the fast
path. It needs the maintainer's decision before any work starts.
