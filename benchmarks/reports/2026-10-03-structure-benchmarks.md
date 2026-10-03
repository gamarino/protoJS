# Structure benchmarks: protoJS Deferred vs Node.js worker_threads (2026-10-03)

## What is compared, and what is not

protoJS's `Deferred` runs functions in parallel on threads of one protoCore
space, reading the caller's objects in place. Node.js runs JavaScript in
parallel in `worker_threads`, each with its own heap; the idiomatic way to
give a worker data is `postMessage`, which makes a structured clone of it.
The benchmarks here measure that difference on what it matters for: several
tasks analysing one large structure of ordinary objects (records, indexes,
document trees, dictionaries, object graphs, a CAD model).

**Numeric loops are excluded as headline figures.** A loop over integers in
protoJS against Node measures V8's optimising JIT against an interpreter: Node
wins by two orders of magnitude, and the number says nothing about sharing
data between threads. The `cpu_primes` figures of
`2026-10-02-parallel-deferred.md` are kept only as an internal check that
Deferred scales on independent CPU work.

Method (details in `benchmarks/structures/README.md`): every task returns a
checksum, and the runner stops unless every runtime and mode computes the
same checksum for every task (Node's sequential run is the reference). Each
configuration is a separate process that builds the data once, then repeats
the timed phase 3 times; tables show the median. N = 1, 2, 4, 6, 12.

Machine: AMD Ryzen 5 5500U (6 cores, 12 threads), 62 GB, Ubuntu 24.04,
protoCore 2.8.0 (installed), Node.js v22.17.0, protoJS at the commit
recorded below. The machine is the maintainer's desktop: an editor and a
browser used one to two cores during the runs (load average 6-9), so
parallel results at N = 6 and 12 are pessimistic for both runtimes, and
single numbers can move by 20 %; compare within a table, not across tables.

## Results

protoJS at 954389362. Building the shared data once (main thread), protoJS /
Node: records 1.32 s / 44 ms, join 0.51 s / 20 ms, doctree 0.80 s / 37 ms,
wordfreq 1.10 s / 23 ms, graph 0.60 s / 33 ms. Scale 1 everywhere (25,000
orders, 12,000 orders and 4,000 products, 40 document trees, 6,000 lines,
20,000 graph nodes): sizes that keep protoJS with 12 tasks under the 4 GB
cap.

### records

| N | protoJS seq | protoJS Deferred | speedup | GC | RSS | Node seq | Node workers | speedup | post / clone / compute | RSS |
|--:|--:|--:|--:|--:|--:|--:|--:|--:|--|--:|
| 1 | 1572 ms | 1665 ms | 0.94 | 0 | 2240 MB | 11 ms | 162 ms | 0.07 | 56 / 146 / 17 ms | 160 MB |
| 2 | 3195 ms | 2536 ms | 1.26 | 1 | 2535 MB | 20 ms | 233 ms | 0.09 | 111 / 167 / 21 ms | 241 MB |
| 4 | 8862 ms | 4097 ms | 2.16 | 3 | 2573 MB | 45 ms | 519 ms | 0.09 | 323 / 278 / 44 ms | 400 MB |
| 6 | 14549 ms | 5124 ms | 2.84 | 5 | 2573 MB | 66 ms | 763 ms | 0.09 | 594 / 287 / 56 ms | 560 MB |
| 12 | 28025 ms | 11835 ms | 2.37 | 10 | 2664 MB | 156 ms | 1015 ms | 0.15 | 885 / 236 / 27 ms | 1031 MB |

### join

| N | protoJS seq | protoJS Deferred | speedup | GC | RSS | Node seq | Node workers | speedup | post / clone / compute | RSS |
|--:|--:|--:|--:|--:|--:|--:|--:|--:|--|--:|
| 1 | 1665 ms | 2276 ms | 0.73 | 0 | 1492 MB | 28 ms | 86 ms | 0.33 | 19 / 54 / 31 ms | 91 MB |
| 2 | 4413 ms | 2919 ms | 1.51 | 1 | 2578 MB | 97 ms | 149 ms | 0.65 | 49 / 92 / 56 ms | 135 MB |
| 4 | 8898 ms | 3726 ms | 2.39 | 2 | 2643 MB | 211 ms | 237 ms | 0.89 | 137 / 112 / 80 ms | 220 MB |
| 6 | 14365 ms | 5131 ms | 2.80 | 3 | 2659 MB | 152 ms | 309 ms | 0.49 | 182 / 125 / 85 ms | 306 MB |
| 12 | 27996 ms | 11305 ms | 2.48 | 8 | 2896 MB | 302 ms | 623 ms | 0.48 | 445 / 172 / 119 ms | 548 MB |

### doctree

| N | protoJS seq | protoJS Deferred | speedup | GC | RSS | Node seq | Node workers | speedup | post / clone / compute | RSS |
|--:|--:|--:|--:|--:|--:|--:|--:|--:|--|--:|
| 1 | 1618 ms | 1494 ms | 1.08 | 0 | 1092 MB | 26 ms | 51 ms | 0.51 | 10 / 24 / 27 ms | 78 MB |
| 2 | 2699 ms | 1580 ms | 1.71 | 0 | 2097 MB | 13 ms | 64 ms | 0.20 | 16 / 26 / 35 ms | 115 MB |
| 4 | 5363 ms | 1882 ms | 2.85 | 1 | 2544 MB | 29 ms | 91 ms | 0.32 | 37 / 28 / 36 ms | 184 MB |
| 6 | 8935 ms | 3073 ms | 2.91 | 2 | 2576 MB | 43 ms | 122 ms | 0.35 | 56 / 30 / 36 ms | 252 MB |
| 12 | 20819 ms | 9183 ms | 2.27 | 6 | 2671 MB | 71 ms | 197 ms | 0.36 | 136 / 40 / 50 ms | 445 MB |

### wordfreq

| N | protoJS seq | protoJS Deferred | speedup | GC | RSS | Node seq | Node workers | speedup | post / clone / compute | RSS |
|--:|--:|--:|--:|--:|--:|--:|--:|--:|--|--:|
| 1 | 1168 ms | 1239 ms | 0.94 | 0 | 1144 MB | 19 ms | 24 ms | 0.79 | 0 / 1 / 23 ms | 59 MB |
| 2 | 2466 ms | 1258 ms | 1.96 | 0 | 2133 MB | 42 ms | 23 ms | 1.83 | 1 / 1 / 21 ms | 78 MB |
| 4 | 4685 ms | 1728 ms | 2.71 | 1 | 2562 MB | 74 ms | 36 ms | 2.06 | 3 / 2 / 32 ms | 118 MB |
| 6 | 6765 ms | 2208 ms | 3.06 | 2 | 2534 MB | 108 ms | 44 ms | 2.45 | 7 / 5 / 41 ms | 158 MB |
| 12 | 14213 ms | 4985 ms | 2.85 | 5 | 2579 MB | 228 ms | 65 ms | 3.51 | 10 / 6 / 56 ms | 275 MB |

### graph

| N | protoJS seq | protoJS Deferred | speedup | GC | RSS | Node seq | Node workers | speedup | post / clone / compute | RSS |
|--:|--:|--:|--:|--:|--:|--:|--:|--:|--|--:|
| 1 | 3164 ms | 3279 ms | 0.96 | 1 | 2488 MB | 30 ms | fails (RangeError: Maximum call stack size exceeded) | - | - | - |
| 2 | 6823 ms | 4528 ms | 1.51 | 2 | 2543 MB | 61 ms | fails (RangeError: Maximum call stack size exceeded) | - | - | - |
| 4 | 13448 ms | 4223 ms | 3.18 | 4 | 2612 MB | 89 ms | fails (RangeError: Maximum call stack size exceeded) | - | - | - |
| 6 | 14872 ms | 6885 ms | 2.16 | 7 | 2621 MB | 83 ms | fails (RangeError: Maximum call stack size exceeded) | - | - | - |
| 12 | 37700 ms | 19849 ms | 1.90 | 17 | 2766 MB | 264 ms | fails (RangeError: Maximum call stack size exceeded) | - | - | - |

Columns: *protoJS seq* runs tasks 0..N-1 one after another on the main
thread; *Deferred* runs them as N Deferreds; *speedup* is seq / parallel for
the same runtime. *GC* is the number of collections in the protoJS process
(three repetitions). *Node workers* is the end-to-end time from the first
`postMessage` to the last result; *post / clone / compute* split it: the time
the main thread spends in `postMessage` (serialization, sequential across
workers), the longest delay until a worker received its copy (serialization,
transfer and deserialization), and the longest compute time. RSS is the peak
resident set of the whole process.

protoJS runs under `PROTOCORE_HEAP_LIMIT_CELLS=40000000` (2.5 GB of cells):
protoCore collects only as the heap approaches the ceiling, so the ceiling,
not the live data, sets protoJS's resident set, and a lower ceiling buys a
smaller resident set with more collections. The sweep that chose it (N = 12,
one repetition, commit decd79efe):

| Workload | 10M cells (640 MB) | 20M cells (1.3 GB) | 40M cells (2.5 GB) |
|----------|-------------------:|-------------------:|-------------------:|
| records | 19.9 s, 20 GC, 0.7 GB | 11.2 s, 9 GC, 1.3 GB | 8.2 s, 4 GC, 2.6 GB |
| join | out of memory | out of memory | 7.1 s, 2 GC, 2.8 GB |
| doctree | 15.3 s, 13 GC, 0.7 GB | 8.4 s, 5 GC, 1.3 GB | 6.3 s, 2 GC, 2.6 GB |
| wordfreq | 6.9 s, 8 GC, 0.9 GB | 4.6 s, 3 GC, 1.4 GB | 3.6 s, 1 GC, 2.5 GB |
| graph | 63.6 s, 54 GC, 0.9 GB | 24.8 s, 15 GC, 1.4 GB | 12.3 s, 6 GC, 2.7 GB |

(`join` at N = 12 holds twelve derived-record arrays and their sorts at once;
1.3 GB of cells is below that live set.) The full runs repeat the phase three
times in one process; from the second repetition the heap starts at its
ceiling, collections are more frequent, and the median at N = 12 is up to
twice the single-run time of the sweep.

## Map and Set on protoCore's ProtoMap (before / after)

The dictionary-heavy workloads were re-measured when Map and Set moved from
hand-rolled hash buckets in four hidden attributes to protoCore's `ProtoMap`
hashed helper with one state record (f48e48565, `src/HashedCollection.*`).
Two sequential tasks per run, the two builds interleaved, median of three:

| Workload | before (decd79efe) | after (f48e48565) |
|----------|-------------------:|------------------:|
| records (group-by: Maps keyed by country, customer, tag) | 5,234 ms | 4,821 ms (-8 %) |
| join (Map index of 4,000 products, Set of suppliers) | 5,280 ms | 3,629 ms (-31 %) |
| wordfreq (Map of word counts) | 2,374 ms | 2,239 ms (-6 %) |

Cells per operation at 50,000 string keys: `Map.set` of a new key 79.6 ->
44.1, of an existing key 20.6 -> 22.6, `Map.delete` 32.4 -> 19.2,
`Set.add` 61.4 -> 42.8, `Map.get` and `Set.has` 1.0 (unchanged).

## CAD model (multi-gigabyte)

`benchmarks/structures/cad/`: 200,000 box-shaped parts (1.6 M vertices,
2.4 M edges, 1.2 M faces, each an object referring to others) in a 585-node
assembly tree, with materials, layers and metadata. Four analyses: bounding
boxes per assembly (0), area and mass by material (1), reference integrity
(2: a Set per part, a Map per face), spatial bucketing (3); task k runs
analysis k mod 4 with parameter k. One repetition (the protoJS run takes 80
minutes). All completed variants agree on every checksum.

| Variant | Completes | Build | Sequential, one task of each analysis (0 / 1 / 2 / 3) | N=1 | N=2 | N=4 | N=6 | N=12 | Peak RSS |
|---------|-----------|------:|------------------------------------------------------|----:|----:|----:|----:|-----:|---------:|
| protoJS, N Deferreds | yes | 330 s | 26.3 / 61.4 / 578 / 82.1 s | 29.9 s | 86.8 s | 706 s | 823 s | 2,090 s | 11.7 GB |
| protoJS, sequential (sum of the task times) | | | | 26.3 s | 87.6 s | 748 s | 836 s | 2,244 s | |
| Node, default heap | yes | 12.6 s | 0.18 / 0.41 / 0.82 / 0.43 s | - | - | - | - | - | 0.74 GB |
| Node, 16 GB heap | yes | 13.8 s | 0.18 / 0.34 / 0.70 / 0.44 s | - | - | - | - | - | 0.74 GB |
| Node, workers + postMessage | yes | 12-19 s | (same) | 22.5 s | 35.7 s | 48.7 s | 71.8 s | 126.7 s | 2.0 / 2.9 / 4.8 / 6.4 / 9.2 GB |
| Node, SharedArrayBuffer columns | yes | 17.2 s (+ encoding) | 0.06 / 0.18 / 0.44 / 0.32 s | 0.06 s | 0.18 s | 0.49 s | 0.50 s | 0.69 s | 0.94 GB |

Node workers, per N: main-thread `postMessage` 7.2 / 20.8 / 36.7 / 52.0 /
111.9 s; worker receive delay 22.4 / 26.2 / 25.6 / 28.8 / 30.0 s; compute
0.10 / 0.21 / 0.55 / 0.59 / 0.67 s. protoJS speedup over its own sequential
time: 0.88 / 1.01 / 1.06 / 1.02 / 1.07.

- **protoJS**: the model's live data is 4.7 GB (73.4 M cells after the build;
  Node holds the same model in 0.74 GB). Built once in 330 s; the N tasks read
  it in place. Ceiling 160 M cells (10.2 GB) under a 14 GB cap; peak RSS
  11.7 GB, 115 collections.
- **Node, default heap (4.05 GB)**: the model fits (0.74 GB), so raising the
  heap (`--max-old-space-size=16384`) changes nothing. Sequential only.
- **Node, worker_threads + postMessage**: every worker receives a clone of
  the whole model. The main thread serializes it once per worker (7 s each:
  112 s of `postMessage` at N = 12) and each worker spends 22-30 s
  deserializing; compute is under 0.7 s. N = 12 takes 127 s and 9.2 GB.
- **Node, SharedArrayBuffer columns**: the model re-encoded as typed-array
  columns over SharedArrayBuffers (`cad/soa.js`), which workers share without
  copying. It is the fastest variant (N = 12 in 0.69 s), at the cost of a
  redesign: an encoder and a second implementation of every analysis over
  integer indexes and CSR offset arrays (156 lines against the 98 lines of
  the object-model analyses), no strings, no object references, and every
  future analysis written twice or only for the columns.

## Conclusions

- **Node is much faster per task, on every workload.** One task takes 11-30
  ms in Node and 1.2-3.2 s in protoJS (records 1,572 ms against 11 ms):
  60 to 140 times. V8 compiles these loops and keeps objects in compact
  hidden-class layouts; protoJS interprets bytecode over protoCore objects,
  where every write publishes an immutable snapshot. No protoJS
  configuration here, at any N, is faster than Node running the same tasks
  one after another on one thread.
- **protoJS's Deferreds share the data and scale; Node's idiomatic workers
  pay for copies.** With the data shared in place, protoJS runs 4 tasks
  2.2-3.2 times and 6 tasks 2.2-3.1 times faster than one after another, on
  6 cores with other programs running. Node's workers must receive the data
  by structured clone, serialized on the main thread one worker at a time:
  for records the copy costs 12-40 times the computation, so 12 workers are
  6.5 times *slower* than Node's own sequential run, and join and doctree
  stay below 1 at every N. Where the data is cheap to clone (wordfreq, an
  array of strings) Node's workers scale like protoJS's (3.5 at N = 12).
- **Some structures cannot be sent at all.** The graph (nodes holding
  references to other nodes) makes `postMessage` overflow V8's stack. In
  Node it has to be redesigned into index arrays before it can be processed
  in parallel; protoJS's Deferreds read it as it is (3.2 times faster at
  N = 4).
- **Memory goes opposite ways.** Node's resident set grows with N, one copy
  per worker (records: 160 MB at N = 1, 1.0 GB at N = 12; CAD: 2.0 GB to 9.2
  GB). protoJS's does not grow with N, but it is large to begin with: its
  objects take 5-7 times Node's space, and protoCore collects only near the
  heap ceiling, so the resident set sits at the ceiling chosen (2.5 GB here;
  0.7-0.9 GB with a 640 MB ceiling and more collections, table above).
- **Beyond 6 tasks protoJS stops scaling, and collection is why.** At
  N = 12 the speedup falls back to 1.9-2.9, and in the CAD model, where every
  collection marks 4.7 GB, to 1.07: protoCore's collector is a single thread
  that stops all mutators, and twelve allocating tasks trigger it often
  (CAD: 115 cycles). The shared data itself is read without contention.
- **The CAD model shows both limits at scale.** Node's object model is
  0.74 GB and fits its default heap; sending it to 12 workers takes 127 s
  and 9.2 GB, almost all of it copying. Re-encoded as SharedArrayBuffer
  columns, Node is fastest of all (0.69 s for 12 tasks), at the price of a
  second implementation of the model and of every analysis. protoJS keeps
  the object model and shares it with no copy and no redesign, but needs
  4.7 GB for it and is three orders of magnitude slower per task (the
  integrity analysis, built on many small Sets and Maps, takes 578 s against
  0.8 s).

Where protoJS stands: its model (share the objects you have, no copy, no
second data layout) does what it promises: N tasks over one structure cost
one copy of the structure, and object graphs that Node cannot even send are
processed in parallel. Its per-task speed and its memory per object are the
gaps, and both are measured: the interpreter's cost per operation (a
mutable write publishes a snapshot; Map and Set mutations cost 20-45 cells)
and protoCore's single-threaded, ceiling-triggered collection. Numeric speed
is not the comparison to make; these are the numbers that decide whether
protoJS is useful for shared-structure work, and today it is useful where
the alternative is copying (or redesigning) a large object graph per
worker, not where single-thread speed matters.

## Bugs found and fixed while building these benchmarks

All with tests; see `CHANGELOG.md`.

- **for-of** kept each loop's state at slot `0x10000 + pc`: every call of a
  function containing a for-of allocated and freed a 512 KB slot array, half
  the run time was system time (`brk`). Now three slots per loop.
- **Map / Set**: insertion O(n) (fixed earlier the same day), iteration and
  `forEach` quadratic; storage now on protoCore's `ProtoMap` hashed helper
  with SameValueZero semantics and one snapshot per mutation; BigInt keys
  were compared by identity.
- **Array.prototype.map / filter** wrote every element as a separate
  publication (100 / 60 cells per element; now 24 / 14); callback argument
  lists and array iterators were built field by field.
- **The event loop gave up 180 s after the main script** ("Event loop
  timeout reached"), cutting off long Deferreds (the first CAD run lost its
  results after 31 minutes) and listening servers. No limit now, as in Node.
- **Accessor-name caches** held uninterned strings: every prototype-chain
  probe searched the symbol table by content.
- Getters and setters defined in object literals and classes were not
  invoked through dot access, and `super` in object-literal methods failed
  (found by the object-literal test of the memory work).
- `process.argv` rejected the script's arguments and had no Array methods;
  `process.memoryUsage()` did not exist.

## Not fixed (documented)

- **Node's structured clone cannot send the `graph` workload**: nodes that
  refer to each other in long chains overflow V8's serializer stack
  (`RangeError: Maximum call stack size exceeded` in `postMessage`). Node
  needs the data redesigned (indexes instead of references) to parallelise
  it; protoJS's Deferreds read it as it is.
- **protoCore collects with one thread, stopping the mutators**: with 12
  Deferreds allocating, and a multi-gigabyte live set to mark on every
  cycle, collection dominates (CAD: 115 cycles marking 4.7 GB each). This,
  not contention on the shared data, is what limits protoJS's speedup at
  N = 12; protoCore's planned adaptive heap controller and a parallel
  marker are the levers (`2026-10-02-memory-per-object.md`).
- **Per-operation cost**: a write to a mutable object publishes a new
  snapshot (about 12 cells); Map/Set mutation costs 20-45 cells; constructing
  a Set from an 8-element array 424 cells. The CAD integrity analysis (a Set
  per part, a Map per face) takes 578 s in protoJS against 0.8 s in Node.
- The hard heap ceiling is exceeded by allocations made inside protoCore
  critical sections when the heap is at its ceiling (observed: 3.6 GB of
  heap under a 1.9 GB ceiling with four threads, before the Map iteration
  fix removed the allocation burst that caused it).

## Data

Raw results (JSON lines, one per configuration):
`benchmarks/reports/2026-10-03-structure-benchmarks.jsonl` (structure
workloads) and `2026-10-03-structure-benchmarks-cad.jsonl` (CAD).
