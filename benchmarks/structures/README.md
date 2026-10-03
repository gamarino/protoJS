# Structure benchmarks: shared data, parallel tasks

These benchmarks compare protoJS and Node.js on what protoJS's `Deferred` is
for: several tasks working at once on one large structure of ordinary
JavaScript objects. They are not numeric loops: comparing protoJS with Node
on integer arithmetic measures V8's JIT against an interpreter, and says
nothing about sharing data between threads (the `primes` numbers in
`benchmarks/reports/2026-10-02-parallel-deferred.md` are kept only as an
internal scaling check).

The results and their reading are in
`benchmarks/reports/2026-10-03-structure-benchmarks.md`.

## Workloads (`workloads/`)

Each workload module exports `build(scale)`, which builds the shared data
deterministically, and `task(data, k)`, which returns `{k, checksum, ...}`;
task `k` differs from the others only in a parameter, so every task does
the same amount of work. The same module runs in both runtimes.

| Workload | Data (scale 1) | A task |
|----------|----------------|--------|
| `records` | 25,000 orders: nested customer and address, 1-5 items, tags, status | group-by with Maps (revenue per country and per customer, counts per status, tag counts per country), filters, sorts with comparators, string keys |
| `join` | 12,000 orders with 1-4 lines, 4,000 products | builds a Map index, joins every line, derives records, sorts them, deduplicates suppliers with a Set, summarises |
| `doctree` | 40 document trees, depth 5, mixed node kinds and value arrays | rewrites every node into a new normalised tree and hashes it |
| `wordfreq` | 6,000 lines of 6-19 words with case and punctuation | split, toLowerCase, Map counting, sort by count then word |
| `graph` | 20,000 nodes, 22,000 random edges as arrays of node references | four breadth-first searches and connected components |

Results never depend on object key order (protoJS reports keys in
protoCore's attribute order, a documented deviation): results are arrays,
and checksums are FNV-1a hashes over them (`lib/common.js`).

## How a run works

- `protojs_bench.js` / `node_bench.js` (environment: `WORKLOAD`, `MODE`,
  `N`, `REPS`, `SCALE`) build the data once, then repeat the timed phase
  `REPS` times:
  - `MODE=seq`: tasks 0..N-1 one after another on the main thread;
  - `MODE=par`, protoJS: N `Deferred`s; each reads the main thread's data
    directly, nothing is copied;
  - `MODE=par`, Node.js: N `worker_threads` (started before the timing),
    each sent the data with `postMessage` -- a structured clone per worker,
    Node's idiomatic way -- and posting its result back
    (`node_worker.js`). Reported besides the end-to-end time: the time the
    main thread spends in `postMessage` (serialization), the longest
    send-to-receive delay seen by a worker (serialization, transfer,
    deserialization) and the longest compute time.
- Each run prints one JSON line with `ok`, the times, the checksums and the
  peak resident set (`process.resourceUsage().maxRSS`); protoJS adds its
  heap ceiling and collector cycle count (`protoCore.gcStats()`).
- `run.py` runs every workload for N = 1, 2, 4, 6, 12, both modes, both
  runtimes, one process per configuration, and stops at the first failure
  or at a checksum that differs from Node's sequential reference.

protoJS runs with an explicit heap ceiling (`--heap-cells`, default 40M
cells = 2.5 GB) under a 4 GB systemd memory cap: protoCore collects only as
the heap nears its ceiling, so the ceiling decides both the resident set and
the number of collections, and the default (75 % of memory) would not be
reproducible across machines. See the report for the sweep that chose it.

```
python3 benchmarks/structures/run.py --protojs build/protojs --out results.jsonl
```

## CAD model (`cad/`)

A multi-gigabyte model: an assembly tree, parts as box solids with their own
vertices, edges and faces (objects referring to each other), materials,
layers and metadata (`cad/model.js`), and four analyses -- bounding boxes per
assembly, area and mass by material, reference integrity, spatial bucketing.
`cad/run.py` runs protoJS (model built once, N Deferreds) and Node.js with the
default heap, a raised heap, worker_threads receiving cloned copies, and the
model re-encoded as SharedArrayBuffer columns (`cad/soa.js`, a second
implementation of every task), each under a memory cap, and checks that every
completed variant agrees on every task's checksum.
