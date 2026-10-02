# Parallel `Deferred` — benchmarks (2026-10-02)

Branch `feature/parallel-deferred`, which restores parallel execution of
`new Deferred(fn)`: `fn` runs on a pool of protoCore threads of the script's own
space, sharing its objects without copying (see `docs/DEFERRED_USAGE.md`).
Compared with:

- **protoJS master** (`37a598873`), where `Deferred` runs its function on the
  event loop's next turn, on the main thread (the behaviour since `29bd97423`);
- **Node.js v22.17.0** with `worker_threads`, running the same algorithm on the
  same N, with the same verified result.

Scripts: `benchmarks/deferred/` (each prints one JSON line with what it
computed and whether it is right; `run.py` stops on any wrong, missing or
crashed run). Raw results: `2026-10-02-parallel-deferred.jsonl` next to this
file. Every figure below is the **median of 5 runs**, runs interleaved by
repetition, one process at a time.

## Machine and caveats

- AMD Ryzen 5 5500U: **6 cores / 12 hardware threads** (SMT), max 4.06 GHz,
  62 GB RAM, Linux 7.0, `powersave` governor with boost. Frequency depends on
  how many cores are busy and on temperature: one busy core runs faster than
  six, so speed-ups measured here understate per-core scaling a little, and
  anything past 6 threads shares cores with its SMT sibling.
- protoJS: Release build of the branch, protoCore 2.8.0 (installed, `/usr`).
  The Deferred pool has 12 threads (the default, `--cpu-threads`).
- Times are wall-clock milliseconds measured inside the script; peak memory is
  the maximum resident set size from `/usr/bin/time`.

## (a) CPU-bound: count primes below LIMIT, split into N tasks

Trial division; the range is cut into blocks of 1000 numbers dealt round-robin
to the tasks, so every task gets the same mix of cheap and expensive numbers.
N=0 is sequential on the main thread. LIMIT = 300,000 (25,997 primes).

| N | protoJS branch (ms) | speed-up vs N=0 | protoJS master (ms) | Node (ms) |
|---:|---:|---:|---:|---:|
| 0 (sequential) | 3438 | 1.00 | 3614 | 61 |
| 1 | 3807 | 0.90 | 3936 | 96 |
| 2 | 1942 | 1.77 | — | 68 |
| 4 | 1066 | 3.23 | 3840 | 59 |
| 6 | **819** | **4.20** | — | 64 |
| 8 | 929 | 3.70 | — | 76 |
| 12 | 903 | 3.81 | 3845 | 105 |

Peak memory: protoJS 25–27 MB at every N (threads share one heap); Node 32 MB
sequential, rising to 149 MB with 12 workers (one isolate each).

On master, N Deferreds take the sequential time or longer: they run one after
another on the main thread. On the branch the same script scales to 4.2× at 6
threads, the number of physical cores, and stays flat beyond (SMT siblings add
nothing to this integer loop).

At LIMIT = 300,000 Node finishes the whole job in about 60 ms, so worker
start-up (~30 ms per worker) dominates and parallelism does not pay. The same
script at **LIMIT = 2,000,000** (148,933 primes; protoJS 3 runs, sequential 1
run):

| N | protoJS branch (ms) | speed-up | Node (ms) | speed-up |
|---:|---:|---:|---:|---:|
| 0 | 48060 | 1.00 | 873 | 1.00 |
| 2 | — | — | 480 | 1.82 |
| 4 | — | — | 265 | 3.29 |
| 6 | 11523 | 4.17 | 206 | 4.24 |
| 12 | 13061 | 3.68 | 279 | 3.13 |

**Both runtimes scale the same way (≈4.2× on 6 cores); Node is ≈55× faster
per thread.** Node runs this loop as JIT-compiled machine code; protoJS
interprets bytecode and boxes through protoCore objects. Parallel Deferreds
recover a constant factor, not that gap.

## (b) Shared data: one large object graph, N tasks reading all of it

500,000 objects `{id, v, w}` built once on the main thread; each of N tasks
computes an aggregate over **all** of them (`sum(v·(t+1) + w)`, checked against
closed forms). This is weak scaling: N tasks do N times the work, so ideal
scaling is a flat line. N=0 computes 4 aggregates sequentially.

- protoJS: each Deferred reads the main thread's array directly.
- Node `clone`: `postMessage(data)` to each worker (structured clone per
  worker) — the idiomatic way to give a worker an object graph.
- Node `sab`: the two fields encoded into an `Int32Array` on a
  `SharedArrayBuffer` and shared — possible here only because the fields are
  small integers; the encoding time is included.

Task phase (hand-over to last result), ms:

| N | protoJS branch | protoJS master | Node clone | Node sab |
|---:|---:|---:|---:|---:|
| 0 (4 aggregates, sequential) | 3590 | 3547 | 8 (Node sequential) | — |
| 1 | 1126 | — | 764 | 48 |
| 2 | 1129 | — | 978 | 47 |
| 4 | 1233 | 4584 | 1412 | 52 |
| 6 | 1378 | — | 1822 | 81 |
| 8 | 1926 | — | 2245 | 119 |
| 12 | 2514 | — | 3086 | 113 |

End to end (build + tasks), ms: protoJS 4568 (N=1) … 4846 (N=6) … 6046 (N=12);
Node clone 833 … 1883 … 3147; Node sab 111 … 144 … 174. Building the 500,000
objects takes 3.4 s in protoJS and 63 ms in Node.

Peak memory, MB:

| N | protoJS branch | Node clone | Node sab |
|---:|---:|---:|---:|
| 1 | 2483 | 168 | 85 |
| 4 | 2483 | 362 | 114 |
| 12 | 2483 | 431 | 187 |

What this shows:

- **No copies, flat memory.** protoJS's footprint does not change with N: every
  Deferred reads the same objects. Node's structured clone costs time and
  memory per worker (0.76 s → 3.1 s, 168 → 431 MB from 1 to 12 workers).
- **Near-flat weak scaling up to the core count.** protoJS computes 6 full
  passes in 1.38 s against 0.9 s for one pass on the main thread (a 3.9×
  throughput gain); 12 passes take 2.5 s (4.3×).
- **protoJS beats idiomatic Node from N=4 up** in the task phase (1233 vs 1412
  ms at 4, 1378 vs 1822 at 6, 2514 vs 3086 at 12), because Node pays a
  serialisation per worker and protoJS pays none. It does **not** beat Node
  end to end: building the data in protoJS (3.4 s) costs more than Node's
  whole run.
- **Node with a SharedArrayBuffer wins outright** (≈50–110 ms) whenever the
  data can be flattened into typed arrays; protoJS's advantage is that it
  needs no such encoding — arbitrary object graphs, strings, nested objects
  and prototypes are shared as they are.
- **protoJS's object footprint is large**: 2.48 GB for 500,000 small objects
  (≈5 KB each, mostly protoCore's per-object mutable state and attribute
  tables) against ≈80 MB in Node. A 1,000,000-object run reached 3.9 GB RSS
  and needs `PROTOCORE_HEAP_LIMIT_CELLS` raised above the 10M-cell default;
  that is why the table uses 500,000. This is the main practical limit of the
  shared-data approach today and is a protoCore/protoJS representation issue,
  not a threading one.
- On master, 4 "Deferreds" take longer than the 4 sequential passes.

## (c) Task overhead: 2,000 small tasks

Each task sums 200 integers. protoJS `deferred`: 2,000 Deferreds started at
once and awaited with `Promise.all`; `promise`: 2,000 promises resolved on the
main thread; `sync`: plain calls. Node `pool`: 12 workers fed by
`postMessage` (one message per task each way; start-up included), as the
protoJS pool also starts on first use.

| variant | protoJS branch (ms) | protoJS master (ms) | Node (ms) |
|---|---:|---:|---:|
| Deferred / worker pool | **87** (44 µs/task) | 157 | 150 (75 µs/task) |
| Promise on the main thread | 124 | 124 | 3 |
| plain calls | 30 | 32 | 2 |

Peak memory: protoJS Deferred 79 MB, Node pool 157 MB.

- A protoJS Deferred costs about 44 µs end to end here, including pinning the
  function, queueing, running on a pool thread and settling as a microtask —
  less than a round trip through a Node worker (75 µs), because nothing is
  serialised and no isolate is started per worker.
- On the branch, 2,000 Deferreds finish faster than 2,000 main-thread promises
  (87 vs 124 ms): the pool runs the work in parallel while the main thread only
  settles the results. On master the same Deferreds took 157 ms.
- Node's promise machinery is ≈40× faster than protoJS's; for work this small,
  plain promises in Node beat everything here by an order of magnitude.

## Conclusions

- **The change works as specified and as measured**: on master `Deferred` was
  sequential (N Deferreds ≈ the sequential time or worse); on the branch
  CPU-bound Deferreds scale to 4.2× on 6 physical cores, the same efficiency
  Node's `worker_threads` reach on this machine, and plateau at the physical
  core count.
- **Where protoJS wins:** sharing large, irregular object graphs between
  parallel tasks — no serialisation, no per-task copy, memory independent of
  the number of tasks, and in the task phase faster than Node's idiomatic
  `postMessage` from 4 tasks up; and per-task overhead (44 µs vs 75 µs for a
  Node worker round trip), with no code changes beyond `new Deferred`.
- **Where Node wins:** everything that depends on single-thread speed. Node's
  JIT makes the CPU-bound loop ≈55× faster per thread, so 12 protoJS threads
  remain far slower than one Node thread; building objects is ≈50× faster and
  ≈30× smaller in memory; and when data fits a `SharedArrayBuffer`, Node shares
  it with no copy either.
- Parallel Deferreds therefore matter for protoJS programs that are already
  committed to protoJS (its object model, its immutable-sharing semantics):
  they multiply throughput by the core count. They do not make protoJS
  competitive with Node on raw compute.

## Reproducing

```bash
python3 benchmarks/deferred/run.py --protojs build_release/protojs \
    --protojs-master <master build>/protojs --reps 5
# the LIMIT=2,000,000 rows:
N=6 LIMIT=2000000 build_release/protojs benchmarks/deferred/cpu_primes.js
N=6 LIMIT=2000000 node benchmarks/deferred/cpu_primes_node.js
```

Integer arithmetic in the protoJS scripts is kept integral with `| 0`: a number
produced by `Math.ceil` or `Number(string)` is stored as a double in protoJS,
and the interpreter's double path is about 3× slower than its integer path (a
loop counter that starts as a double makes the whole loop 3× slower). That is a
pre-existing interpreter property, unrelated to Deferred, found while writing
these benchmarks.
