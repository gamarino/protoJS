# Memory per object (2026-10-02)

The question: protoJS used about 5-6 KB of resident memory per object (500,000
objects: 2.5 GB, against about 130 MB in Node.js). Where does it go, and how
much of it is live data?

Machine: AMD Ryzen 5 5500U (6 cores, 12 threads), 62 GB, Ubuntu 24.04,
installed protoCore 2.8.0, Node.js v22.17.0. "Before" is master at 01e20a80a
(after the integer canonicalisation, before this work); "after" is
11a5dace2 plus the heap-ceiling change.

## Summary

- **The 5-6 KB is mostly garbage, not live data.** Under a tight heap ceiling
  the same 100,000-object programs complete with **4 to 11 cells (256-704
  bytes) of live data per object** for flat objects. The rest is short-lived
  garbage that protoCore's collector reclaims only as the heap approaches its
  ceiling, so the resident set tracks the ceiling, not the live set.
- **Where the garbage comes from:** every write to a mutable object publishes
  a new immutable snapshot into protoCore's process-wide mutable table: about
  12 cells (768 bytes) per write, half for the object's attribute tree and half
  for the table's path copy. An object literal with five fields made five such
  writes (77.5 cells); a constructor with five `this.x = v` made five more
  expensive ones (148 cells); an array literal made four plus its list.
- **Fixed in protoJS** (no protoCore change): literals built immutable and
  published once; arrays the same; per-write string allocations removed from
  `OP_put_field`; `Map.set`/`Set.add` were O(n) per insertion and are now
  O(log n); integral numbers are no longer boxed doubles (task 2,
  `CHANGELOG.md`). Allocation per object dropped 40-60 % for literals, and
  the smallest workable heap for the five-field-plus-string-and-double object
  went from 12.5 to 9.0 cells per object.
- **Not fixable in protoJS without changing protoCore's model:** the
  per-write publication cost (immutable snapshot + table path copy), the
  O(log n) path copy of every array append, and the collection policy (a
  cycle runs only near the ceiling). These are measured below, with
  proposals for the maintainer; protoCore was not changed.

## Method

- **Allocated cells per operation**: `process.memoryUsage().heapUsed` (protoCore
  heap minus the space's free list, in 64-byte cells) before and after 50,000
  iterations, with `PROTOCORE_HEAP_LIMIT_CELLS=100000000` so that no
  collection runs in between. This counts everything allocated, live or not.
- **Live cells per object**: the smallest `PROTOCORE_HEAP_LIMIT_CELLS` under
  which a script that builds and keeps N objects still completes (binary
  search, 2.5 % resolution; above it the run ends with "out of memory", exit
  3). It bounds the live set from above: the heap must also hold the garbage
  made between two collections.
- **protoCore primitives**: a C++ program against libprotoCore 2.8.0 that
  counts `heapSize - freeCellsCount` around 20,000 repetitions, with 0 and
  100,000 other mutable objects in the table.

## protoCore primitives (cells per operation)

| Operation | 0 other mutables | 100,000 other mutables |
|-----------|-----------------:|-----------------------:|
| `newObject(true)` (mutable, no attributes yet) | 2.0 | 2.0 |
| immutable object, 5 `setAttribute` | 29.9 | 29.9 |
| mutable object, 5 `setAttribute` | 59.8 | 80.3 |
| immutable, 5 `setAttribute`, then `clone(true)` | 31.1 | 31.1 |
| one write to an existing mutable attribute | 9.0 | 11.9 |

A write to a mutable object costs the immutable path copy of its attribute
tree plus a path copy in the mutable table's shard, which grows with the
number of mutable objects in the process (the JavaScript runtime itself holds
tens of thousands). `clone(true)` turns a finished immutable object into a
mutable one for about one cell: building first and publishing once is the
cheap order.

## Allocated cells per JavaScript operation

50,000 iterations each; the result of each iteration is discarded.

| Operation | Before | After |
|-----------|-------:|------:|
| `o = {}` | 3.1 | 3.1 |
| `o = {a: i}` | 14.7 | 5.9 |
| `o = {id, name, qty, price, flag}` (5 fields) | 77.5 | 32.1 |
| `o = {}; o.id = i; ...` (5 assignments) | 143.0 | 100.1 |
| `new P(i)`, constructor with 5 `this.x = ...` | 147.9 | 111.6 |
| `[i, 1, 2, ..., 9]` | 94.0 | 51.9 |
| `a[i] = i` on `new Array(N)` | 31.6 | 30.6 |
| `a.push(i)` | 30.6 | 29.7 |
| `o.x = i` (existing key) | 14.1 | 12.0 |
| `m.set(i, i)` on a `Map` (1,000 keys / 4,000 keys / 50,000 keys) | 1,081 / 4,088 / out of memory | 68 / 68 / 78 (44 after the ProtoMap storage of 2026-10-03) |
| `s.add(k)` on a `Set` (4,000 keys) | 2,081 | 68 |
| a call `f(i)`, a closure call, `o.x` read | 0 | 0 |

What changed:

- **Object literals** (`markObjectLiterals`, `src/runtime/BytecodeSpecialiser.cpp`):
  a load-time pass finds each `OP_object` whose fields are defined before
  anything else can see the object, using the stack level before each
  instruction (exported from QuickJS's `compute_stack_size`). The object is
  created immutable, the fields extend it by structural sharing, and the last
  field makes it mutable with one `clone(true)`. `PROTOJS_LITERAL_BUILD=off`
  restores the old behaviour.
- **Arrays** (`OP_array_from`, `createNewArray`): the array marker, the
  elements list, `length` and its descriptor are set on an immutable object,
  then one clone.
- **Writes of new keys** (`OP_put_field`): the accessor sidecar names were
  built as fresh strings on every write of a key the object did not have yet,
  and a `std::string` was built on every write for the `length` check; both
  are gone (per-thread symbol caches, a pointer compare).
- **Map and Set**: a new key missed the single-entry hash slot and fell back
  to a scan of every key; each insertion also scanned the order list for the
  next slot. Both were O(n) per insertion in time and allocation (1 GB for
  4,000 `Map.set`; 50,000 exhausted 4 GB). Hash buckets now hold every slot
  with the hash, and the next slot is kept in a hidden attribute.

Remaining per-object costs, each a protoCore-level property:

- **Assignments after creation** (`this.x = v` in a constructor, `o.k = v`):
  about 19 cells per new key, 12 per existing key. The object is visible to
  other code, and potentially to other threads, from the moment `new` creates
  it, so each write must be published.
- **Array appends**: the elements list is a persistent AVL list, so each
  `push` path-copies about log2(n) nodes (16-17 at 50,000) plus one publication.

## Live cells per object (smallest heap that completes)

100,000 objects built in a loop into `new Array(100000)` and kept.

| Object | Before (cells/object) | After (cells/object) |
|--------|----------------------:|---------------------:|
| `{}` | 4 | 4 |
| `{id, name, qty, price, flag}` | 11 | 8 |
| `{id, name: "n" + (i % 100), x: i * 0.5, y: i, tag: "t"}` | 40 | 9 |
| `new P(i)` (5 fields by assignment) | 11 | 11 |
| `[i, 1, ..., 9]` | 20 | 17 |
| nested order: customer, address, one item, one tag | 54 | 38 |

With `push` instead of indexed stores, the third object needs 12.5 cells per
object before and 9.0 after (`tests/cli/objects_bounded_memory.py`, which now
runs 200,000 of them under 11 cells each; the previous build ends with "out of
memory" there).

An estimate of those 9 cells (not measured cell by cell): the mutable handle
and its snapshot (2), the attribute tree of 5 entries (about 3-5 nodes), the
double `i * 0.5` (1, a boxed cell), the string `"n" + (i % 100)` (1-2), and the
object's share of the array's elements list and of the mutable table (about 1).
Node.js stores the same object in about 80-120 bytes.

## Resident set for N objects

The same object (`{id, name: "n" + (i % 100), x: i * 0.5, y: i, tag: "t"}`,
`push`ed), peak RSS from `/usr/bin/time`. "Default ceiling" is 640 MB of cells
before and 75 % of the available memory after (here, under a 12 GB cgroup
limit: 9 GB).

| N | Node.js | protoJS before, default ceiling | protoJS after, default ceiling | protoJS after, `PROTOCORE_HEAP_LIMIT_CELLS` = 20 x N |
|--:|--------:|--------------------------------:|-------------------------------:|------------------------------------------------------:|
| 100,000 | 55 MB, 0.13 s | 636 MB, 2.1 s | 680 MB, 1.7 s | 147 MB, 2.4 s |
| 500,000 | 124 MB, 1.3 s | 717 MB, 28.2 s | 3.6 GB, 8.8 s | 718 MB, 12.7 s |
| 1,000,000 | 192 MB, 0.5 s | out of memory after 131 s (778 MB) | 7.6 GB, 15.7 s | 1.36 GB, 30.0 s |

Reading it:

- Before, the 640 MB ceiling kept the resident set low but made the
  collector run almost continuously once the live set approached it (28 s for
  500,000 objects), and 1,000,000 objects did not fit at all.
- After, with the default ceiling at 75 % of the memory, nothing is collected
  until the heap is large: fast, but the resident set grows towards the
  ceiling (7.6 GB for 1,000,000 objects whose live data is about 0.6 GB).
- With an explicit ceiling at about twice the live set, the resident set stays
  close to it (1.36 GB for 1,000,000 objects), at the cost of more collections.
- Live data alone is still 5-7 times Node's: about 576 bytes per object here
  against Node's 100-200.

The policy that would give both (collect according to the live set, keep the
ceiling only as a safety cap) is the adaptive heap controller planned in
protoCore; `configureHeap` in `src/JSContext.cpp` is the one place that will
adopt it.

## Proposals for protoCore (not implemented)

These would change or extend protoCore's model and are left to the
maintainer:

1. **Adaptive collection trigger** (planned): start a cycle based on the live
   set after the last cycle (for example when the heap reaches twice it), not
   only near the hard ceiling. This is the largest factor in resident memory.
2. **A cheaper mutable write.** A write costs the snapshot's path copy plus a
   path copy in the shard of the global mutable table (9-12 cells with the
   runtime's mutables). A per-object "owned until shared" state, in which a
   mutable not yet reachable from another thread updates its table entry in
   place, would remove most of it; it changes the model (mutable = atomic
   reference to an immutable snapshot published in the table), so it is only
   a proposal.
3. **A vector type for arrays.** Appending to a persistent AVL list
   path-copies log2(n) nodes. A new immutable type with a chunked tail (as in
   RRB vectors or Clojure's persistent vector: appends touch one 32-wide leaf
   and a short path) would make `push` and indexed stores O(1) amortised in
   allocation. Extending protoCore by a new type keeps the existing model.
4. **Boxed doubles** cost one cell each; an embedded (NaN-boxed or tagged)
   representation for doubles that fit would remove them, but changes the
   pointer encoding.

## Tests

- `tests/cli/objects_bounded_memory.py` (ctest `cli/objects-bounded-memory`):
  200,000 objects under `PROTOCORE_HEAP_LIMIT_CELLS` = 11 cells each, verified
  aggregate, peak resident set below 400 MB.
- `tests/integration/collections/map_set_scaling.js`: Map/Set semantics and
  120,000 insertions under a 60 s limit.
- `tests/integration/basic/object_literal_build.js`: literal forms the pass
  rewrites and those it leaves alone.
