# Memory safety: two open problems and proposals

Status: **approved 2026-10-11** and implemented the same day.

- Section 1: the proposal as written -- per-space `PinnedBuiltin` prototypes, a
  per-space `BehaviorRegistry` with a locked insert, read-copy-update reads and
  `forgetSpace` cleanup, and the two tests.
- Section 2: option **(b), dictionary mode in protoJS**. protoCore is not
  changed.

Each section ends with what was built and measured ("Implemented"). The
findings below are kept as they were written on 2026-10-04, when nothing here
was implemented; both items had been held back from that day's memory-safety
pass as design work (the fixes made in that pass -- RegExp capture array,
bytecode loader, exceptions across contexts, typed array coercion, the native
stack guard, the pinned `"undefined"` string -- are in CHANGELOG.md and
docs/GC_BRIDGING.md).

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

### Implemented (2026-10-11)

- **Prototypes.** `Array.prototype`, `%TypedArray%.prototype` and the eleven
  concrete typed-array prototypes, `ArrayBuffer.prototype`, `Map.prototype`
  and `Set.prototype` are `PinnedBuiltin` entries, one per space, published
  with `replace()` by their installers.
- **Hot readers.** `PinnedBuiltin::get(ctx, memo)` reads through a
  `thread_local` memo holding (space, generation, object): a hit is one
  acquire load and two compares. The memo is refreshed when the thread's
  space changes, or when any `PinnedBuiltin` publishes, replaces or forgets an
  entry (a process-wide generation counter), and only a pinned object is
  memoised. `createNewArray`, the typed-array constructors and `of`/`from`/
  `map`/`slice`/`subarray`, `createArrayBuffer`, `new Map`/`new Set` read
  this way.
- **`JSON.parse` arrays.** Found on the way: `JSContextWrapper::
  getJSArrayPrototype` returned the empty placeholder that
  `BootstrapJSPrototypes` builds, because `ensureArrayPrototype` runs while
  no wrapper is current and could not update it, so `JSON.parse('[1]').join`
  was undefined on every thread. It now returns the space's populated
  `Array.prototype`.
- **GC bridge.** `GCBridge` keeps one mapping table per space, allocated in
  and rooted by a root set of that space, and `cleanup` destroys it with the
  context. The process-wide table was rebuilt by every space's thread and
  rooted only in the first space's root set. (The unrooted `rootAnchor`
  object, which pinned nothing, is gone.)
- **`BehaviorRegistry`.** One immutable table per space in a fixed array of 64
  slots (an overflow map under the mutex beyond that); `registerBehavior`
  takes a mutex and publishes a copy (read-copy-update); `resolve()` finds its
  space's slot through a `thread_local` hint and reads the table with one
  acquire load. `BehaviorRegistry::forgetSpace` -- called by
  `~JSContextWrapper` next to `PinnedBuiltin::forgetSpace` -- frees the
  space's tables and bumps the epoch that empties every thread's per-object
  cache. The typed-array behaviours are eleven static objects shared by every
  space, so a cached behaviour pointer is never dangling.
- **Not changed:** `Metrics::getDefaultBuckets` (`src/monitoring/Metrics.cpp`)
  also caches a list in a function-local static, but nothing calls `Metrics`.
- **Tests.** `js/workers/prototypes_after_worker` checks the prototype and the
  element behaviour of every kind of object the main thread makes (arrays from
  every built-in that makes one, `JSON.parse` arrays, all eleven typed arrays,
  `ArrayBuffer`s, `Map`, `Set`, iterators) before a worker starts, while it
  runs, after it exits and after a collection: 249 failed checks at
  `f12e5a19`, 0 after. `js/workers/workers_churn_small_heap` starts and stops
  workers while the main thread allocates under a 300,000-cell ceiling (about
  40 collections on the main thread): 2,643 failed checks before, 0 after;
  it also runs clean under AddressSanitizer (see CHANGELOG).
- **Measured** (`perf stat -r 3`, an array-heavy loop of literals, `map`,
  `filter`, `concat`, `slice` and `Array.of`, 20,000 iterations): 12.150 G
  instructions before and after; wall time within noise.

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

### Decision (2026-10-11)

Option (b), dictionary mode in protoJS; protoCore is not changed.

### Implemented (2026-10-11)

**Which keys.** A key goes to an object's property dictionary
(`src/runtime/PropertyDictionary.h`) when all of these hold:

- it is a heap string: not an interned symbol, and not an inline string
  (ASCII of up to six bytes lives in the pointer and costs no table entry);
- it is not program vocabulary: no bytecode atom of any loaded code (every
  identifier, property name and string literal of every script, module, `eval`
  and `new Function`) and no protoJS built-in name (`JSSymbols`) has that
  spelling, and it is not an internal `__name__`, `@@sym#n` or `Symbol.`
  spelling. Vocabulary is interned anyway, so storing it as a named attribute
  costs nothing new, and code reads it by name;
- neither the object nor any of its prototypes (JS `[[Prototype]]` overrides
  included) has it as a named attribute -- looked up by content, which interns
  nothing -- so an inherited accessor or read-only property keeps its meaning;
- the object accepts new keys: an ordinary extensible object, not an array,
  typed array, function, proxy, `String` wrapper or the global object.

**N.** The proposal said "after N dynamic keys". N = 0: the first dynamic key
puts the object in dictionary mode. Measured (below): a dictionary entry costs
little more than a named attribute even for an object with one dynamic key,
while any N > 0 would intern the first N keys of every object for good -- one
key per object (a session map, a request record keyed by id) would grow
without bound, which is the case the change exists for.

**Storage.** One hidden attribute (`__props_dict__`) holds an immutable record
`[entries, index, next, size]`, the layout of `Map` (`src/HashedCollection.h`):
`entries` maps an insertion slot to `[key, value]`, `index` is a `ProtoMap`
from key to slot driven by protoCore's `hashedPut` / `hashedGet` /
`hashedRemove` with the SameValueZero `KeySemantics` (strings by content).
Keys are ordinary GC-managed strings held by the record. Every change publishes
a new record with `setAttributeIfEqual` and retries on a lost race, so
concurrent writers on several Deferred threads never lose a key. An immutable
object under construction (a literal, a parsed JSON object) gets the record
with one `setAttribute`.

**Invariant.** A key lives in one place on an object: its named attributes or
its dictionary. Dictionary entries are plain data properties (writable,
enumerable, configurable, except as `Object.freeze` / `seal` /
`preventExtensions` say for the whole object). Giving a dictionary key an
accessor or a non-default descriptor moves it to the named attributes first
(`dict::evict`, which keeps the integrity level in the `__pd_` sidecar) --
that interns that key, an explicit act on one key rather than data flowing
through a loop.

**Paths routed.**

| Path | Change |
|---|---|
| `obj[k]` read (`OP_get_array_el`, `_el2`, `_el3`) | key not interned (`propertyKey`); own dictionary key shadows the chain; inherited dictionary keys after a named miss |
| `obj.k` read (`OP_get_field`, `_field2`) | `resolveFieldOOP`: dictionaries of the receiver and prototypes after a named miss; for a key that may be both (see "Shadowing"), the nearest dictionary entry first |
| `obj[k] = v`, `obj.k = v` (`resolvePutFieldOOP`) | updates a dictionary key, or stores a new dynamic key; non-extensible / frozen rules |
| index keys of 7+ digits on ordinary objects | read and written as heap strings (`dict::indexKeyNoIntern`), dictionary-backed |
| `{[k]: v}` (`OP_define_array_el`), `{k: v}` after it (`OP_define_field`) | CreateDataProperty into the dictionary |
| spread / rest (`OP_copy_data_properties`) | source dictionary copied; exclusion list consults dictionaries |
| `k in o`, `delete o[k]` | dictionary-aware; `delete` no longer interns the key or its sidecars when they are absent |
| `for-in` | each level's dictionary keys after its named keys |
| `Object.keys/values/entries/getOwnPropertyNames/getOwnPropertyDescriptors` (`collectOwnKeys`) | dictionary keys after the named ones, integer indices first |
| `hasOwnProperty`, `Object.hasOwn`, `propertyIsEnumerable`, `getOwnPropertyDescriptor` | dictionary keys are own, enumerable data properties; descriptor sidecar names are no longer interned on lookup |
| `Object.defineProperty` / `defineProperties` / `__defineGetter__` | a default data descriptor on a new dynamic key goes to the dictionary; any other descriptor evicts the key first |
| `Object.assign`, `Object.fromEntries`, `Object.groupBy` | read and write dictionaries |
| `Object.freeze/seal/preventExtensions/isFrozen/isSealed` | integrity bits cover dictionary keys; `isFrozen` of a non-frozen object with dictionary keys is false |
| `Reflect.get/set/has/deleteProperty/ownKeys/defineProperty/getOwnPropertyDescriptor` | dictionary-aware; `Reflect.set` with a Proxy receiver now defines on the proxy (its `defineProperty` trap or target); `Reflect.deleteProperty` now removes the key instead of storing `PROTO_NONE` |
| Proxy without traps (`defaultGet`, `set`, `has`, `deleteProperty`, own descriptor, `ownKeys` invariants) | dictionary-aware |
| `JSON.parse` | new `TypeBridge::fromJSON`: string values are not interned, dynamic keys go to the dictionary, objects are built immutable and published once, and no `GCBridge` mapping is registered (it kept every parsed object and its QuickJS original for the life of the context) |
| `JSON.stringify`, the reviver of `JSON.parse` | dictionary keys serialised and revived; the reviver now deletes a key it maps to `undefined`; `JSON.stringify` no longer drops keys that start with one `_` |
| `worker_threads` messages, HTTP header objects, `Iterator.zipKeyed`, the C++ `console` fallback printer | dictionary-aware |
| `structuredClone` | not provided by protoJS |

**Order.** Named string keys keep protoCore's attribute order (the documented
deviation); dictionary keys follow in insertion order; with dictionary keys
present, integer indices come first, ascending. An object whose keys all came
from data therefore enumerates as in Node.js. A data spelling that also occurs
as an identifier or string literal anywhere in the loaded code is vocabulary
and stays named, so it is listed among the named keys.

**Shadowing.** Own properties shadow inherited ones, also when the own
property is a dictionary key and a prototype gains a named property of the
same spelling AFTER the object stored the key: code loaded later (`new
Function`, a module; protoJS does not execute `eval`) names the key and assigns
it on a prototype, `Object.defineProperty` or a class gives a prototype an
accessor of that name, or `Object.setPrototypeOf` gives the object such a
prototype. (The first implementation documented this as a known limit: a
static read `o.k` returned the prototype's value or called its getter.)

A key can be both a dictionary key of one object and a named attribute of an
object above it only when its spelling was data first and a name later.
Spellings become names in three places: `noteVocabulary` (code loaded), `evict`
(a dictionary key given an accessor or descriptor) and computed method or
accessor names. Every spelling a dictionary takes sets two bits of a fixed
128 KiB Bloom filter; when a spelling becomes a name and the filter may hold
it, the spelling is recorded and a process-wide flag is raised
(`dict::mayBeDictionaryKey`). For such keys, and for every heap-string key
(a computed key that is not vocabulary), a lookup walks the chain and takes the
first dictionary entry it meets before any object that has the key as a named
attribute or that is a proxy (`dict::shadowingValue`, `findInChain`); the
named lookup decides otherwise. This applies to `obj.k` and `obj[k]` reads,
method calls, `Reflect.get`, a proxy's default `[[Get]]`, and writes:
`obj.k = v` updates the own entry and calls no inherited setter; a nearer
prototype's dictionary entry makes the write define an own property (refused
when that prototype is frozen), as OrdinarySet says; `Reflect.set` likewise.
Ownership tests, descriptors and enumeration already consulted the own
dictionary. Found on the way: `Object.hasOwn` on a proxy probed the proxy cell
and returned false for every property of the target; it now uses the
`getOwnPropertyDescriptor` trap or the target, as `hasOwnProperty` does.

Cost: for a name whose spelling was never data, one relaxed atomic load per
static read and write (`obj.k`, `obj.k()`, `obj.k = v`). Once the flag is
raised, a static access also looks the name up in a per-thread table of
interned names, emptied when the set of such spellings grows. The filter
saturates after about a million distinct dictionary spellings; spellings named
after that are all taken for "named after data" and pay the dictionary walk on
their lookups -- slower, still correct. Measured below ("Shadowing fix").

**What still interns.** Named attributes (identifiers, vocabulary, keys of
arrays, functions, proxies and the global object), keys given accessors or
non-default descriptors and their `__pd_`/`__get_`/`__set_` sidecars, and the
per-instance key of every `Symbol()` value (`@@sym#<n>`, one perennial entry
per symbol created; found here, not changed).

**Measured** (Release builds, protoCore 2.14.5, this machine -- a notebook-class
Ryzen 5 5500U that other work shared, so differences under about 5 % are
noise; "before" is master `f12e5a19`, "after" this branch):

| Workload | Before | After |
|---|---|---|
| `cli/dynamic-keys-bounded-memory`: 500,000 distinct keys (`obj[k] = v`, `JSON.parse`, read back by `for-in` and `Object.keys`) under a 1,000,000-cell ceiling | timed out at 1,500 s; with 200,000 keys: 263 s, RSS +45.4 MB over the second 100,000 keys (476 bytes per key) -- **fails** | 12 s, RSS +0.5 MB from 100,000 to 500,000 keys -- passes |
| The 50,000-key loop of section 2 (`o['key-a-' + i] = i`, read back, `Object.keys`), no ceiling | 10.0-12.2 s, peak RSS 243 MB | 1.2-1.4 s, peak RSS 282 MB |
| Same loop, 2,000,000-cell ceiling | 8.7-9.3 s, 156 MB | 0.8 s, 174 MB |
| 200,000 small objects, one computed key each (`o['user-' + i] = i`), 1,000,000-cell ceiling | 114-117 s, RSS +98 MB | 2.2-2.4 s, RSS +54 MB (the heap up to its ceiling) |
| 400,000 reads of existing computed keys on 1,000 two-key objects | 0.54-0.57 s | 1.0-1.2 s |
| `benchmarks/structures`, one task, `REPS=3` median, 3 processes each: records / join / wordfreq | 1,620-1,918 / 1,489-1,691 / 1,046-1,215 ms | 1,626-1,659 / 1,580-1,603 / 1,064-1,072 ms; peak RSS +0.1 % |
| Array-heavy loop (`perf stat -r 3`) | 12.149 G instructions | 12.154 G instructions |

The cost is on reads of keys that are already in a dictionary: about twice the
time of a named-attribute read, because the key is compared by content (a
named attribute is found by the interned pointer through protoCore's
per-thread attribute cache). Writes of new keys are 10 to 50 times faster,
because they no longer insert into the symbol table. A large live dictionary
takes more heap than the same keys as named attributes (the 50,000-key object:
+11 to +16 % peak RSS), but that memory goes with the object, where the
interned names never went. N = 0 was chosen on these numbers: "N > 0" means
the first N keys of every object are interned as before, and the one-key
objects above show what that costs.

Array methods on array-likes (`pop`, `splice`, `shift`, `sort`, `at`, ... on
an ordinary object with an index of seven or more digits) read, test and
delete through the dictionary (`arrGet`, `arrSet`, `arrHasProperty`,
`arrHas`, `arrDeleteIndex` in `src/ArrayPrototype.cpp`); found on the way,
`Array.prototype.reverse` now moves holes as §23.1.3.26 says.

**Test262** (sequential, the CI corpus pin): `built-ins/{Object, JSON,
Reflect, Proxy, Array, TypedArray, TypedArrayConstructors, ArrayBuffer, Map,
Set}`, `language/statements/for-in`, `language/expressions/{object, delete,
in}` and `language/computed-property-names`, 11,556 tests: 9,536 pass before
and after, 0 pass->fail, 0 fail->pass. (Test262 writes its keys as literals,
which are vocabulary, so it barely reaches the dictionary; its key-order tests
are unaffected.)

**Tests.** `js/basic/dictionary_keys` (95 checks of every path above, with
the values Node.js gives; at `f12e5a19` five checks failed -- key order, the
Proxy receiver of `Reflect.set`, `Reflect.deleteProperty`, `Reflect.ownKeys`
-- before the script died on `JSON.parse('[...]').map`),
`cli/dynamic-keys-bounded-memory`
(fails before, as measured above), `js/deferred/dictionary_keys_shared`
(four Deferreds write 6,000 keys into one object; every key, value and
per-writer order survives), `js/workers/dictionary_keys_message`.
