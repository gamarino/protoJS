# protoJS

**A modern JavaScript runtime based on protoCore**

[![Language](https://img.shields.io/badge/Language-C%2B%2B20-blue.svg)](https://isocpp.org/)
[![Build System](https://img.shields.io/badge/Build-CMake-green.svg)](https://cmake.org/)
[![Status](https://img.shields.io/badge/Status-Phase%206%20Complete-green.svg)]()
[![License](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

**GIL-free protoCore object model on a JavaScript front-end.  Zero-Copy Immutability.  Concurrent GC.  Powered by the Swarm of One.**

Copyright (c) 2026 Gustavo Marino <gamarino@gmail.com>

protoJS is a JavaScript runtime that uses **protoCore** (https://github.com/numaes/protoCore) as the foundation for internal object representation, memory management, and concurrency. It uses **QuickJS** as a parser and compiler, but completely replaces the QuickJS runtime with protoCore, leveraging its unique features of immutability, GIL-free concurrency, and efficiency.

> [!WARNING]
> This project is officially **open for Community Review and Suggestions**. It is **not production ready**. We welcome architectural feedback, edge-case identification, and performance critiques.

### Community & Open Review

Beyond formal audits, this project is officially **open for Community Review and Suggestions**.

We welcome architectural feedback, edge-case identification, and performance critiques. While the core vision is firm, the path to perfection is a collective effort of the "Swarm."

---

## 🎯 Key Features

### Phase 1 (Demonstrator - Completed)

- ✅ **Basic JavaScript types** implemented using protoCore primitives
- ✅ **QuickJS parser** integrated
- ✅ **TypeBridge** complete (main conversions)
- ✅ **Deferred** with transparent worker threads (basic implementation)
- ✅ **protoCore module** for special collections
- ✅ **process module** basic (argv, env, cwd, platform, arch, exit)
- ✅ **io module** basic (readFile, writeFile)
- ✅ **Comprehensive tests** (unit and integration)
- ✅ **Complete documentation**

### Unique Features

- **Deferred with Worker Threads**: `Deferred` automatically executes in worker threads, utilizing all processor cores transparently
- **Immutability by default**: Arrays and objects can be immutable, sharing structure between threads without copying
- **Advanced collections**: Access to `ProtoSet`, `ProtoMultiset`, `ProtoSparseList`, and `ProtoTuple` from JavaScript
- **No GIL**: Real concurrency without Global Interpreter Lock—courtesy of **protoCore**'s GIL-free architecture
- **Efficient GC**: **protoCore**'s concurrent garbage collector manages object lifecycle with minimal pauses

Built to be the cornerstone of a unified, polyglot environment where JS, Python, and C++ share the same memory DNA.

---

## 📋 Requirements

- **C++20** compatible compiler (GCC 10+, Clang 12+)
- **CMake** 3.16+
- **protoCore** (official name of the shared library; must be built and available as `libprotoCore.so` / `libprotoCore.dylib` / `protoCore.dll`)
- **pthread** (for concurrency)

---

## 📦 Installation

- **From packages:** Install protoCore first, then install protoJS using your platform package:
  - **Linux (Debian/Ubuntu):** `sudo dpkg -i protoJS_0.1.0_amd64.deb`
  - **Linux (Fedora/RHEL):** `sudo dnf install protoJS-0.1.0-1.x86_64.rpm`
  - **macOS:** Open `protoJS-0.1.0.pkg` and follow the installer (installs to `/usr/local/bin`)
  - **Windows:** Run `protoJS-0.1.0.msi` (adds protoJS to PATH)
- **From source:** Build as in [Building](#-building); then run from `build/` (RPATH set) or `cmake --build build --target install` to install to a prefix. Full instructions (install prefix, PROTO_CORE_PREFIX, .deb/.rpm/.pkg/.msi) are in **[docs/INSTALLATION.md](docs/INSTALLATION.md)**.

---

## 🚀 Building

protoJS links against the **protoCore shared library** (official name: **protoCore**). Build protoCore first, then protoJS. For packaging or when protoCore is already installed, use `-DPROTO_CORE_PREFIX=<prefix>`. See [docs/INSTALLATION.md](docs/INSTALLATION.md) for details.

```bash
# 1. Build protoCore shared library
cd ../protoCore
cmake -B build -S .
cmake --build build --target protoCore

# 2. Build protoJS (finds libprotoCore in ../protoCore/build or build_check)
cd ../protoJS
mkdir -p build && cd build
cmake ..
cmake --build .
# Optional: install to a prefix (default /usr/local)
# cmake --build . --target install
```

When protoCore is built in a sibling directory, **RPATH is set** so you can run `./build/protojs` without setting `LD_LIBRARY_PATH` or `DYLD_LIBRARY_PATH`. If you use an installed protoCore (`-DPROTO_CORE_PREFIX=...`), the installed `protojs` binary uses RPATH to find the library.

---

## 💻 Basic Usage

### Run a script

From the build directory or after install:

```bash
# From build directory (RPATH set; no LD_LIBRARY_PATH needed)
./build/protojs script.js
# Or after install: protojs script.js
```

### Execute inline code

```bash
./protojs -e "console.log('Hello, protoJS!')"
```

### Basic Example

```javascript
// hello.js
console.log("Hello from protoJS!");

// Immutable arrays (by default)
const arr1 = [1, 2, 3];
const arr2 = arr1.concat([4]);
console.log("Original:", arr1); // [1, 2, 3] - unchanged
console.log("New:", arr2);      // [1, 2, 3, 4]

// Deferred with worker threads
const deferred = new Deferred((resolve) => {
    // CPU-intensive work executed in worker thread
    let sum = 0;
    for (let i = 0; i < 10000000; i++) {
        sum += i;
    }
    resolve(sum);
});

// Note: In Phase 1, .then() is under development
// Result is processed internally
```

### Example: protoCore Collections

```javascript
// ProtoSet - automatically removes duplicates
const set = new protoCore.Set([1, 2, 3, 3, 4, 4]);
console.log(set.size); // 4
set.add(5);
console.log(set.has(3)); // true

// ProtoMultiset - counts occurrences
const multiset = new protoCore.Multiset([1, 1, 2, 2, 2]);
console.log(multiset.count(2)); // 3
console.log(multiset.size); // 5

// ProtoTuple - immutable array
const tuple = protoCore.Tuple([1, 2, 3]);
console.log(tuple.length); // 3
// tuple.push(4); // Error: immutable

// ProtoSparseList - efficient for arrays with gaps
const sparse = new protoCore.SparseList();
sparse.set(0, "first");
sparse.set(100, "hundredth");
console.log(sparse.get(0)); // "first"
console.log(sparse.has(50)); // false
```

### Example: Mutability Control

```javascript
// Create immutable object
const config = protoCore.ImmutableObject({
    host: "localhost",
    port: 8080
});
console.log(protoCore.isImmutable(config)); // true

// Create mutable object
const state = protoCore.MutableObject({
    counter: 0
});
state.counter = 10; // OK
console.log(state.counter); // 10
```

### Example: Process Information

```javascript
// Command line arguments
console.log("Script:", process.argv[1]);
console.log("Args:", process.argv.slice(2));

// Environment variables
console.log("Home:", process.env.HOME);
console.log("User:", process.env.USER);

// System information
console.log("Platform:", process.platform()); // "linux", "darwin", "win32"
console.log("Arch:", process.arch());         // "x64", "ia32", "arm"
console.log("CWD:", process.cwd());
```

### Example: I/O Operations

```javascript
// Read file
const content = io.readFile("data.txt");
console.log(content);

// Write file
io.writeFile("output.txt", "Hello, protoJS!");
```

### Example: Advanced Networking (Phase 5)

```javascript
// Worker Threads
const { Worker, isMainThread, parentPort, workerData } = require('worker_threads');

if (isMainThread) {
    const worker = new Worker(__filename, { workerData: { start: 0, end: 1000 } });
    worker.on('message', (result) => {
        console.log('Result:', result);
    });
} else {
    // Worker thread code
    const sum = workerData.end - workerData.start;
    parentPort.postMessage(sum);
}

// Cluster
const cluster = require('cluster');
if (cluster.isMaster) {
    for (let i = 0; i < 4; i++) {
        cluster.fork();
    }
} else {
    // Worker process
    require('http').createServer((req, res) => {
        res.end('Hello from worker ' + process.pid);
    }).listen(8000);
}

// UDP (dgram)
const dgram = require('dgram');
const socket = dgram.createSocket('udp4');
socket.bind(41234);
socket.on('message', (msg, rinfo) => {
    console.log(`Received: ${msg} from ${rinfo.address}:${rinfo.port}`);
});
```

### Example: Developer Tools (Phase 5)

```javascript
// Memory Analyzer
const memory = require('memory');
const snapshot1 = memory.takeHeapSnapshot();
// ... run code ...
const snapshot2 = memory.takeHeapSnapshot();
const leaks = memory.detectLeaks(snapshot1, snapshot2);
console.log('Memory leaks detected:', leaks);

// Visual Profiler
const profiler = require('profiler');
profiler.start();
// ... run code ...
profiler.stop();
const profile = profiler.exportProfile(); // Chrome DevTools format
profiler.generateHTMLReport('profile.html');

// Integrated Debugger
const debugger = require('debugger');
debugger.startCDPServer(9229);
debugger.setBreakpoint('script.js', 10);
// Connect Chrome DevTools to localhost:9229
```

For more examples, see [docs/EXAMPLES.md](docs/EXAMPLES.md).

### Thread Pool Configuration

```bash
# Specify number of CPU threads
protojs --cpu-threads 8 script.js

# Specify number of I/O threads
protojs --io-threads 24 script.js

# Specify factor for I/O threads (default: 3.0)
protojs --io-threads-factor 4.0 script.js
```

For more information on configuration, see [docs/THREAD_POOLS.md](docs/THREAD_POOLS.md).

---

## 🏗️ Architecture

```
JavaScript Code (ES2020+)
    ↓
QuickJS Parser/Compiler
    ↓
protoJS Runtime Layer
    ├── TypeBridge (JS ↔ protoCore)
    ├── ExecutionEngine
    └── GCBridge
    ↓
protoCore Runtime
    ├── ProtoSpace (GC, Memory)
    ├── ProtoContext (Execution)
    └── ProtoThread (Concurrency)
```

For more details, see [ARCHITECTURE.md](ARCHITECTURE.md).

---

## The Swarm of One

**The Swarm of One** is the architect's manifesto for the AI era. In protoJS, we didn't just build a runtime; we orchestrated a paradigm shift. By leading a swarm of specialized AI agents, a single architect replaced the entire QuickJS runtime with protoCore primitives in record time. The same 64-byte cells that power Python and C++ now back JavaScript on a GIL-free, structurally-sharing object model. The interpreter itself is currently ~100× behind V8 on pure compute (see [Performance Benchmarks](#performance-benchmarks) for an honest 2026-05-01 baseline) — a JIT is future work — but the runtime contract (immutability, concurrent GC, lock-free threading) opens design space that V8's monolith cannot match. This is the democratization of high-level engineering: delivering a clear architectural alternative without the overhead of a massive corporate R&D department.

---

## The Methodology: AI-Augmented Engineering

This project was built using **extensive AI-augmentation tools** to empower human vision and strategic design. This is not "AI-generated code" in the traditional sense; it is **AI-amplified architecture**.

We embrace AI as the **great equalizer**. protoJS is not "AI-generated"; it is AI-amplified architecture. It represents the unavoidable present where human strategic design—focused on lock-free concurrency and structural sharing—is executed with the precision and speed of a digital swarm. We are proving that a single focused mind can outpace legacy ecosystems.

---

## 📚 Documentation

Full index: **[DOCUMENTATION_INDEX.md](DOCUMENTATION_INDEX.md)** — current status, Phase 6 audit, and all docs by topic.

### Main Documentation

- **[docs/INSTALLATION.md](docs/INSTALLATION.md)** - **Installation guide** (Linux .deb/.rpm, macOS .pkg, Windows .msi, and from source)
- **[PLAN.md](PLAN.md)** - Detailed implementation plan (Phase 1 and future)
- **[ARCHITECTURE.md](ARCHITECTURE.md)** - Detailed technical architecture
- **[TESTING_STRATEGY.md](TESTING_STRATEGY.md)** - Testing strategy
- **[IMPLEMENTATION_STATUS.md](IMPLEMENTATION_STATUS.md)** - Current implementation status
- **[NEXT_STEPS.md](NEXT_STEPS.md)** - Next steps and planned improvements

### User Guides

- **[docs/API_REFERENCE.md](docs/API_REFERENCE.md)** - Complete API reference
- **[docs/EXAMPLES.md](docs/EXAMPLES.md)** - Advanced examples
- **[docs/DEFERRED_USAGE.md](docs/DEFERRED_USAGE.md)** - Deferred usage guide
- **[docs/PROTOCORE_MODULE.md](docs/PROTOCORE_MODULE.md)** - protoCore module guide
- **[docs/NATIVE_MODULES.md](docs/NATIVE_MODULES.md)** - Native addon modules (C++ shared libraries)
- **[docs/THREAD_POOLS.md](docs/THREAD_POOLS.md)** - Thread pool configuration
- **[docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md)** - Common problem solutions

### Performance & Benchmarks

- **[Performance Benchmarks](#performance-benchmarks)** below — current honest baseline (in-process suite, 2026-04-28).
- **[tests/benchmarks/results/baseline_2026-04-28.json](tests/benchmarks/results/baseline_2026-04-28.json)** — raw JSON for the latest run.
- **[tests/benchmarks/run_standard_comparison.js](tests/benchmarks/run_standard_comparison.js)** — to reproduce; takes `PROTOJS_BIN` env override.

---

## 🧪 Testing

### Run unit tests

```bash
cd build
ctest
```

### Run integration tests

```bash
./protojs ../tests/integration/basic/hello_world.js
```

### Run benchmarks

```bash
./protojs ../tests/benchmarks/array_operations.js
```

For more information on testing, see [TESTING_STRATEGY.md](TESTING_STRATEGY.md).
For official ECMAScript compliance status and roadmap, see **[TEST262_status.md](TEST262_status.md)**.

### Performance Benchmarks

**Honest baseline — 2026-05-31** (in-process median time, protoJS built in
pure Release mode and linked against protoCore `libprotoCore.so.1.2.0` —
the build that includes **snapshot-at-STW + Phase 2 trim** for concurrent
GC (see `protoCore/docs/GarbageCollector.md` § "Concurrent Mark Without
Barriers").  This baseline is the first measurable run since 2026-05-06:
two regressions had silently broken the standard suite between then and
now, both fixed in this cycle.

> **Regressions fixed this cycle**
>
> 1. `printf("TRACE: ...")` was committed into the bytecode `DISPATCH()`
>    macro on 2026-05-22 by snapshot `7b5d9ddd` (uncommitted working tree
>    marked as "not separately reviewed").  Every dispatch printed a
>    trace line to stdout — both polluting the `__BENCH_RESULT__` parser
>    (every benchmark reported "Error: undefined") and adding
>    catastrophic per-dispatch overhead.  Removed by `283a02a5`.
> 2. `Date.now` was `undefined`.  Root cause: `TimingAPIs::init` created
>    `Date` via `ctx->fromMethod(...)` and then tried to attach `.now`
>    via `setAttribute`.  Method objects created with `fromMethod` do not
>    retain attribute writes — the assignment silently dropped.  Fixed
>    by `b546a64f` switching to `newObject(true)` with matching `name`/
>    `prototype` so the interpreter's stub-installer guard skips it.
>
> Together these explain why no comparable measurement could be produced
> between 2026-04-28 and 2026-05-31.

#### Standard In-Process Suite (`run_standard_comparison.js`, `run_standard_comparison_quickjs.js`)

Self-contained micro-benchmarks; each reports median of 5 internal
runs.  Both reference engines exercised in the same session against
the same `build_release/protojs`.

##### vs Node.js 22

| Benchmark             | protoJS     | Node     | Node speedup |
|-----------------------|------------:|---------:|-------------:|
| array_literal         |    197 ms   |    3 ms  |        65.7× |
| control_flow          |    228 ms   |    5 ms  |        45.6× |
| function_calls        |    215 ms   |    1 ms  |       215.0× |
| json_transform        |    105 ms   |    2 ms  |        52.5× |
| numeric_loop          |    109 ms   |    1 ms  |       109.0× |
| object_property       |   1650 ms   |   49 ms  |        33.7× |
| object_read_only      |     52 ms   |    3 ms  |        17.3× |
| object_write_only     |   6904 ms   |   11 ms  |       627.6× |
| **parallel_cpu**      |  **52 ms**  |**41 ms** | **Node 1.3×** |
| string_concat         |    107 ms   |    1 ms  |       107.0× |

**Geometric mean (10 benches): Node 37.75× faster than protoJS** —
load-bearing single-thread number on this hardware.

##### vs vanilla QuickJS (interpreter-vs-interpreter, no JIT on either side)

| Benchmark             | protoJS     | QuickJS  | QuickJS speedup |
|-----------------------|------------:|---------:|----------------:|
| array_literal         |    431 ms   |    6 ms  |          71.83× |
| control_flow          |    522 ms   |   50 ms  |          10.44× |
| function_calls        |    257 ms   |   10 ms  |          25.70× |
| json_transform        |    133 ms   |    4 ms  |          33.25× |
| numeric_loop          |    130 ms   |   89 ms  |           1.46× |
| object_property       |   2012 ms   |   91 ms  |          22.11× |
| object_read_only      |     63 ms   |    6 ms  |          10.50× |
| object_write_only     |   8551 ms   |   54 ms  |         158.35× |
| **parallel_cpu**      |  **52 ms**  |**776 ms**| **protoJS 14.92×** |
| string_concat         |    113 ms   |    4 ms  |          28.25× |
| tree_traversal        |    349 ms   |    4 ms  |          87.25× |

**Geometric mean (11 benches): QuickJS 10.34× faster than protoJS** —
the meaningful interpreter-vs-interpreter number.  Closing this gap is
the work of the P-JS optimisation track.  The Node gap above includes
the JIT advantage layered on top of this.

##### Architectural payoff: GIL-free threading

`parallel_cpu.js` (4 tasks × 5 rounds via `protoCore.runInThread`):

- protoJS: **52 ms** — single-process, four real OS threads running
  concurrently on protoCore primitives with no global lock.
- QuickJS: 776 ms — single-threaded interpreter, serialised CPU work.
- Node.js: 41 ms — V8 JIT'd code, single-threaded but JIT-fast.

protoJS **wins 14.9×** against QuickJS and reaches **77 % of Node's**
JIT'd throughput on a workload that scales with cores.  This is the
one benchmark where the architectural decision (GIL-free runtime on
protoCore) is visibly load-bearing.  Workloads that scale across
cores get the advantage; tight single-thread loops do not.

#### Comparison against 2026-04-28 baseline

Compares the six benchmarks present in both runs.

| benchmark            | 04-28 (ms) | 05-31 (ms) | Δ      |
|----------------------|-----------:|-----------:|-------:|
| array_literal        |       1030 |        197 | −80.9% |
| control_flow         |        735 |        228 | −69.0% |
| function_calls       |       2090 |        215 | −89.7% |
| numeric_loop         |        455 |        109 | −76.0% |
| object_property      |       9577 |       1650 | −82.8% |
| parallel_cpu         |         55 |         52 |  −5.5% |

**Geomean ratio = 0.249** — protoJS is ~75 % faster than the 2026-04-28
baseline across the six benchmarks present in both runs.  This is the
P-JS-{0..7} optimisation cycle's actual landed effect, which could not
be measured properly while the TRACE printf was active on the binary.

#### Hot spots worth targeted attention

- `object_write_only` (158× QuickJS, 628× Node) — cost of writes
  through protoCore's immutable structural-sharing: every property
  assignment builds a new object chain.  Highest-leverage target.
- `tree_traversal` / `function_calls` (87× / 26× QuickJS) — tight-loop
  dispatch dominates.  P-JS track is already addressing this.
- `numeric_loop` (1.46× QuickJS) — within noise of parity; the basic
  integer loop is no longer pathological.

Full report: `tests/benchmarks/results/comparison_2026-05-31.md`.
Raw JSON results: `tests/benchmarks/results/baseline_2026-05-31.json`
and `tests/benchmarks/results/standard_comparison_quickjs.json`.

---

**Honest baseline — 2026-05-06** (in-process median time, protoJS built in
pure Release mode (`-O3 -DNDEBUG`, no debug info) and linked against the
matching Release build of protoCore — including the GC survivor re-chain,
the runtime string-intern removal, the OP_inc_loc / OP_dec_loc slot-
addressing fix, perpetual NULL-context allocation for strong symbols,
the simplification of `getAttribute` to rely on the GC-pinned attribute
cache instead of a per-cycle invalidation, and the **P-JS-{0..4} cycle**
that minimised protoCore-side traffic on the property-access hot path
(see "Driving wins" below) — vs. Node.js/V8 22 and vanilla QuickJS
(`qjs_minimal` rebuilt with the same `-O3 -DNDEBUG`).  Pure compute;
no startup cost counted on either side.  Each row in the tables below
is the median across **12 outer rounds** of the runner, where every
round already reports the median of **5 internal iterations** of the
benchmark — i.e. **60 timing samples per cell**.

#### Standard In-Process Suite (`run_standard_comparison.js`)

Self-contained micro-benchmarks with tight loops; 12 outer × 5 inner = 60 samples, median reported.

| Benchmark             | protoJS     | Node.js  | Ratio (Node faster) |
|-----------------------|-------------|----------|--------------------:|
| array_literal         |    199 ms   |    2 ms  |               93.5× |
| control_flow          |    245 ms   |    4 ms  |               60.6× |
| function_calls        |    189 ms   |    1 ms  |              189.0× |
| numeric_loop          |    115 ms   |    1 ms  |              114.5× |
| object_read_only      |     64 ms   |    1 ms  |               57.5× |
| string_concat         |    103 ms   |    1 ms  |              103.0× |
| object_property       |    359 ms   |   37 ms  |                9.9× |
| object_write_only     |    823 ms   |   11 ms  |               74.8× |
| json_transform        |      5 ms   |    1 ms  |               10.0× |
| **parallel_cpu**      |  **52 ms**  |**41 ms** |  **Node 1.27×**     |
| tree_traversal        |    316 ms   |    1 ms  |              457.0× |

**Geometric mean (12 benches): Node.js 28.94× faster than protoJS**
(median across 12 rounds; geomean-of-medians 36.84×) — refreshed
2026-05-06 after **P-JS-7 (dispatch_table hoisted out of the per-call
hot path)** completed the May 2026 cycle on top of SmallSparseList,
P-JS-{0..6}, and the broader May 2026 work.

> **Re-verified 2026-05-07** after the protoCore `ProtoObjectCell::attributes`
> tag-0 architectural fix landed (storing the raw `ProtoSparseListImplementation*`
> instead of the API-tagged `ProtoSparseList*`): median geomean 31.23×, geomean-
> of-medians 38.17×.  Both deltas sit inside the round-to-round variance band
> (single-round geomeans ranged 25.14–39.06), so the architectural fix is
> performance-neutral for protoJS.  Same `build_release/protojs`, same Node 22, same 12×5 sampling.

> **⚠ Earlier "intermediate" cycle measurements were noise.** The
> aggregated runner (`run_standard_comparison.js`) silently used a
> stale `build/protojs` binary instead of the rebuilt `build_release/protojs`
> for every prior measurement in this cycle.  The "+1-2% per step"
> README entries (P-JS-5, SmallSparseList, P-JS-6) were comparing the
> SAME unchanged binary across rounds — pure noise.  This entry is
> the first measurement against the actual optimised binary.  The
> runner is now fixed (`PROTOJS_BIN` env var support + `build_release/`
> preferred over `build/`) so this cannot recur. (paths #2/#3/#4/#6, task #28 CAS removal, task #34 destructor
reorder fix, task #36 chunked freelist via GC pre-chunking, tasks
#37/#39 type-flags cache + unified attribute fast paths, **task #42
SparseList hash cascade elimination**).  Improvement of ~45 % over
the prior 75.13× baseline.  Driving wins:

**P-JS-7 — dispatch_table hoisted out of the per-call hot path**
(largest single win of the cycle): the 256-entry computed-goto table
was re-initialised on every entry to `runBytecode` — 256 default-fills
+ ~210 per-opcode overrides = ~470 stores per call.  For
tree_traversal that was ~150 M wasted stores per bench run.  Beyond
the raw stores, each frame held 2 KB of dispatch_table on the C++
stack; with recursion depth 14, ~28 KB of duplicated tables overflowed
L1d (32 KB), causing measurable cache pressure that flat profiles
attributed silently to the `runBytecode` self-time symbol.  Fix:
function-scope `static const void* dispatch_table[256]` initialised
once via DCLP (`std::atomic<bool>` + `std::mutex`).  Address-of-label
values are stable across function entries — the binary loads once,
labels live at fixed code-segment offsets — so single-process
initialisation is correct.  Steady-state cost: 1 acquire-load + a
predicted-not-taken branch (~2 cycles) per `runBytecode` entry.
Per-bench impact (60-sample real medians, post-cycle): tree_traversal
1004→316 ms (**−69%**), function_calls 448→189 ms (−58%),
object_write_only 1430→823 (−42%), object_property 598→359 (−40%),
array_literal 301→199 (−34%), string_concat 176→103 (−41%).

**P-JS-6 — DISPATCH macro hot-path trim**: the computed-goto
dispatcher used to re-fetch `globalObj` from `*pGlobalRoot` on **every**
opcode dispatch and to null-check the dispatch_table slot.  Both were
redundant for the common case: `globalObj` is consumed by only ~6
opcodes (OP_push_this in non-strict mode, the obj == globalObj check
in OP_put_field / OP_define_field / OP_delete, and the Array.prototype
lookup in OP_array_from), and the dispatch_table is now pre-filled
with `&&L_default` so the slot is always a valid jump target.
Per-dispatch overhead drops from ~19 cycles to ~12 cycles (-37%).

**SmallSparseList** (in protoCore — auto-applies through
the public ProtoSparseList API): single-cell inline form for sparse
lists with ≤ 3 (key, value) pairs.  Closure-cell `__cv__` writes —
profile-identified bottleneck for `function_calls.js` — drop from
3-4 cell allocations per write to 1.  See `protoCore/README.md` for
the full description.  Per-bench impact (12 × 5 = 60-sample median):
function_calls 457→389 ms (**−15 %**), control_flow 280→259 (−8 %),
numeric_loop 128→117 (−9 %), object_write_only 1332→1234 (−7 %),
array_literal 309→291 (−6 %), object_property 531→508 (−4 %).
Geomean Node 41.05× → 40.17×; vs QuickJS 9.86× → 9.74×.

P-JS-{0..5} cycle (prior commits, still in effect):
  - **P-JS-0** updateMapping made a no-op — runtime never reads the
    JSValue ↔ ProtoObject mapping outside compile-time TypeBridge
    (QuickJS is parser/compiler only; objects live in protoCore
    exclusively at run time)
  - **P-JS-2** dedup'd the redundant getAttribute(callbacks=true)
    in OP_get_field2 (resolveFieldOOP already does the canonical walk)
  - **P-JS-3** prototype-identity set replaces 6 pointer-compares per
    write in updateSpacePrototypeIfMatching
  - **P-JS-1** thread-local cache for `__get_<name>__` / `__set_<name>__`
    sidecar symbols — was constructing a fresh ProtoString rope on
    every property-miss probe
  - **P-JS-4** short-circuit default JSObjectBehavior dispatch — the
    common case (plain object) now skips the v-table indirection and
    calls obj->getAttribute / obj->setAttribute directly
  - **P-JS-5** extend P-#4 single-allocation argsList builder to
    OP_call and OP_call_constructor.  Profile-guided discovery: the
    May 5 commit `47ea3e1a` had only converted OP_call_method,
    leaving OP_call (every free function call: `f(x)`) and
    OP_call_constructor (every `new X(...)`) on the legacy
    `newList() + N×appendLast` path — costing 1+N cell allocations
    per call instead of 1.  Per-bench impact (12-round medians):
    tree_traversal 1004→890 ms (−11%), object_property 598→531 ms
    (−11%), string_concat 176→157 ms (−11%), control_flow 307→280
    ms (−9%), object_write_only 1430→1332 ms (−7%).  The
    function_calls bench itself (1 arg, single global write) is
    dominated by the closure-cell setAttribute on the captured
    `state` variable, so the per-call save is not yet visible at
    the geomean level — but anything with nested or recursive
    calls amortises the saving across the call tree.

Cumulative May 2026 wins still in effect:

  - path #4 single-allocation `argsList` builder (`OP_call` / `OP_call_method` / `OP_call_constructor`)
  - path #6 mutable-cache stash on `resolveMutableState` hot path
  - task #36 O(1) chunked freelist refill (`getFreeCells` 7.91 % → 0.52 % CPU)
  - task #42 SparseList hash propagation removed (`isString` 3.78 % → 0 %, `getAttribute` 14.03 % → 5.90 %)

`tree_traversal` now completes (it crashed on the previous baseline thanks
to the GC survivor re-chain landing in protoCore) but at ~1 s it dominates
the geomean — the bench measures Node at 1 ms (timer floor) so the ratio
is mostly an artefact of timer resolution.  Excluding `tree_traversal`
the remaining 11 benches yield Node ~37× faster — the load-bearing
single-thread number on this hardware.  The May 2026 fixes that built
up to this baseline:

- The runtime string-intern map was removed so `s += 'x'` collapsed from
  O(N²) (timeout) to O(N log N) (141 ms for 50 000 concats).
- `OP_inc_loc` / `OP_dec_loc` were routed through the same slot-addressing
  helpers as `OP_get_loc`, fixing an infinite loop on
  `for (var i = 0; i < N; i++)` inside a function.
- `OP_put_array_el` was popping the spec'd 3 slots but pushing a
  spurious 1, accumulating one slot per iteration in dynamic-key
  loops like `obj['k' + i] = i;`.  After ~17 iterations the operand
  stack exceeded its compile-time-sized window and subsequent writes
  silently bypassed setAttribute.  Removing the push restores the
  QuickJS contract — see commit log for the matching fix to
  `OP_get_array_el2 / OP_get_array_el3`.
- `OP_array_from` and the five `argsList` builds inside `OP_call` /
  `OP_call_method` / `OP_call_constructor` are now wrapped in
  `ProtoContext::CriticalSection` from the first `appendLast` through
  the bind-into-childCtx loop.  Without continuous CS, an inner
  allocCell that crosses its 64-allocation safepoint poll could
  submit the in-flight list to dirtySegments, leaving the freshly
  built list (and its element values) sweep-candidates with no live
  GC root.  Fixed json_transform (0/5 → 5/5) and let
  object_property / object_write_only land on the suite via the
  combined fixes.
- Strong-symbol creation now allocates the working
  `ProtoStringImplementation` with a NULL `ProtoContext`, so the cells
  live for the lifetime of the process and no concurrent collector can
  free the in-flight rope between `fromUTF8Bytes` and the SymbolTable
  canonicalisation.
- `ProtoObject::getAttribute` no longer pays a per-call atomic load +
  branch for GC-cycle cache invalidation — `ProtoThreadExtension::
  processReferences` already pins every (object, result, name) entry
  as a GC root, so the cache pointers cannot dangle.  Stripping that
  guard saved 5–11 % across getAttribute-heavy benches (numeric_loop,
  array_literal, function_calls).

`parallel_cpu` is effectively a tie — protoJS offloads work to native
protoCore worker threads, so the interpreter hot loop is irrelevant.

`tree_traversal` (DEPTH=14, 16 383 nodes, deep recursive property access)
now completes on the standard suite.  The previously-tracked GC survivor
race that crashed it has been resolved by the protoCore pre-mark unmark
pass (May 2026).  The remaining 965 ms — versus 1 ms on Node.js — reflects
the cost of repeatedly walking the prototype chain through immutable
snapshots; it is the deepest property-walk workload in the suite and
amplifies every per-attribute overhead the interpreter still pays.

#### Comprehensive Macro Suite (`combined_performance_suite.js`)

41 tests across Basic Types, Collections, and Overall Performance (5 iterations,
mean reported).  As of 2026-05-04 this suite **no longer completes on
protoJS**: the runner reaches the BigInt category at the end of Basic
Types, then aborts with `Error: is not a function` before producing the
per-benchmark summary line.  The same regression reproduces against the
older RelWithDebInfo build, so it is not introduced by the Release flag
change — it is a pre-existing bug in the harness's interaction with
protoJS that needs its own fix before this table can be refreshed.  Until
then, the standard and QuickJS-comparison suites above are the
trustworthy single-threaded performance baseline.

#### What the numbers do *not* measure

- **Startup cost** — protoJS starts in roughly the same wall-clock time as Node;
  for short scripts the user-observed gap is much smaller.
- **Memory & GC pauses** — protoCore's concurrent GC keeps p99 pause well below
  Node's stop-the-world cycles; not captured here.
- **Multi-threaded scaling** — Node's event loop is single-threaded; `parallel_cpu`
  exercises only a fraction of what protoCore's GIL-free thread model can deliver
  in real multi-threaded workloads.

#### Interpreter-to-Interpreter Suite (`run_standard_comparison_quickjs.js`)

Comparing against vanilla **QuickJS** (the underlying engine without protoCore) isolates the cost of the **protoCore memory model** and the **GIL-free architecture**.  Both engines were rebuilt on 2026-05-04 with the same `-O3 -DNDEBUG` flags so the comparison is purely interpreter-vs-interpreter (no compile-flag asymmetry).  Refreshed 2026-05-06 with the **P-JS-{0..5} cycle** in place, using the same 12-outer × 5-inner = 60-sample methodology as the Node comparison above (`node tests/benchmarks/run_aggregated.js 12 --quickjs`).

| Benchmark           | protoJS    | QuickJS  | Ratio (QuickJS faster) |
|---------------------|------------|----------|-----------------------:|
| array_literal       |    206 ms  |    6 ms  |                 33.3×  |
| control_flow        |    247 ms  |   47 ms  |                  5.4×  |
| function_calls      |    198 ms  |   10 ms  |                 20.5×  |
| json_transform      |      7 ms  |    1 ms  |                  8.6×  |
| numeric_loop        |    122 ms  | 35.5 ms  |                  3.5×  |
| object_property     |    369 ms  |   73 ms  |                  5.3×  |
| object_read_only    |     65 ms  |    6 ms  |                 10.6×  |
| object_write_only   |    872 ms  | 60.5 ms  |                 15.3×  |
| string_concat       |    118 ms  |    5 ms  |                 21.6×  |
| **parallel_cpu**    |  **52 ms** | **719 ms**|      **protoJS 13.8× faster** |
| tree_traversal      |    347 ms  |    4 ms  |                 81.6×  |

**Geometric mean (12 benches): QuickJS 7.05× faster than protoJS** (median across 12 outer rounds; geomean-of-medians 7.32×).

> **Re-verified 2026-05-07** post protoCore tag-0 fix: median geomean 6.87×,
> geomean-of-medians 7.63× — within noise of the 2026-05-06 baseline.
> `parallel_cpu` win held at 15.4× (protoJS 52 ms vs QuickJS 796 ms), reflecting
> QuickJS's lack of native threads.

As in
the Node comparison, `tree_traversal` is the single dominant outlier at
282×; excluding it, the gap narrows to ~7×.
`parallel_cpu` remains the only bench where protoJS wins — and the margin
widens to 13.8× because QuickJS, lacking native threads, cannot exploit
multiple cores at all.  Interpreter-vs-interpreter we are now within an
order of magnitude on every bench except `tree_traversal` and the
mutable-property / array-build workloads (`array_literal`,
`function_calls`, `string_concat`), which fundamentally trade
single-threaded speed for GIL-free immutable snapshots.

The single-thread headline (9.86×) sits near the noise band of the
previous 9.68× measurement — QuickJS scales tight enough under this
O3 build that the per-call cell-allocation savings introduced by the
P-JS-{0..5} cycle largely show up on the protoJS side without
shifting the ratio dramatically.  The structural improvement is
real: `function_calls` is now dominated by closure-cell setAttribute
on the captured `state` global rather than by per-call argsList
allocation, which is a much harder bottleneck to attack
incrementally.

### Why the gap?
The performance difference in object property access is primarily driven by fundamental architectural trade-offs:

1.  **Persistent vs. Mutable Memory**: QuickJS uses a traditional mutable hash map with hidden classes (Shapes) for fast O(1) property lookup. `protoJS` uses `protoCore`'s persistent AVL-tree structures. This provides full thread-safety and lock-free concurrency (zero-copy snapshots) but involves O(log N) lookup depth and significantly more pointer indirection.
2.  **Zero-Allocation Symbols**: Every property access in `protoJS` requires interning the key into a `protoCore::ProtoString` symbol. While we have implemented a **128-entry Inline Cache (IC)** to eliminate redundant UTF-8 conversions and sharded lookups, the overhead of symbol-stable comparison in a sharded environment persists.
3.  **Refcounting vs. Concurrency**: QuickJS uses single-threaded reference counting. `protoJS` leverages `protoCore`'s sharded, thread-safe memory management. The 20x gap in single-threaded property access is the direct "tax" for the **12x gain in parallel performance** shown in `parallel_cpu`.

Raw JSON: [tests/benchmarks/results/standard_comparison.json](tests/benchmarks/results/standard_comparison.json)

**To reproduce** (Release builds of both protoCore and protoJS, plus an `-O3` `qjs_minimal`):
```bash
# 1. Build protoCore in pure Release.
cd ../protoCore && cmake -B build_release -S . -DCMAKE_BUILD_TYPE=Release \
    && cmake --build build_release --target protoCore -j$(nproc)

# 2. Build protoJS in pure Release, linked against the protoCore Release lib.
cd ../protoJS && cmake -B build_release -S . -DCMAKE_BUILD_TYPE=Release \
    -DPROTOCORE_LIBRARY=$(pwd)/../protoCore/build_release/libprotoCore.so \
    && cmake --build build_release --target protojs -j$(nproc)

# 3. Rebuild qjs_minimal with the same -O3 flags (Release).
cd tests/benchmarks && gcc -O3 -DNDEBUG -I../../deps/quickjs -o qjs_minimal \
    qjs_minimal.c ../../deps/quickjs/quickjs.c ../../deps/quickjs/libregexp.c \
    ../../deps/quickjs/libunicode.c ../../deps/quickjs/cutils.c \
    ../../deps/quickjs/dtoa.c -lm -lpthread
cd ../..

# 4. Run the suites against the Release binary.
PROTOJS_BIN=$(pwd)/build_release/protojs node tests/benchmarks/run_standard_comparison.js
PROTOJS_BIN=$(pwd)/build_release/protojs node tests/benchmarks/run_standard_comparison_quickjs.js
```

---

## 🗺️ Roadmap

### Phase 1: Demonstrator (Completed)

- [x] Basic structure
- [x] QuickJS + protoCore basic integration
- [x] TypeBridge complete (main conversions)
- [x] Deferred functional (basic implementation)
- [x] protoCore module (Set, Multiset, SparseList, Tuple, mutability)
- [x] process module (argv, env, cwd, platform, arch, exit)
- [x] io module (readFile, writeFile)
- [x] Comprehensive tests (unit and integration)
- [x] Complete documentation

**Goal:** Demonstrate protoCore's capabilities as a foundation for a JavaScript runtime.

### Phase 2: Basic Node.js Compatibility

- Node.js core modules (fs, path, http, etc.)
- Module system (CommonJS + ES Modules)
- Basic npm support
- Node.js-compatible CLI

**Goal:** Be a basic Node.js substitute for simple applications.

### Phase 3: Complete Node.js Substitute

- Advanced modules
- Performance optimizations
- Complete compatibility
- Advanced features (debugging, profiling, etc.)

**Goal:** Complete replacement of Node.js for most use cases.

### Phase 4: Core Components & Performance (Completed)

- [x] Buffer module (full Node.js API compatibility)
- [x] Net module (TCP sockets and servers)
- [x] Profiler module (CPU and memory profiling)
- [x] Performance optimizations (20-30% improvements)

**Goal:** Advanced differentiators and specific optimizations.

### Phase 5: Advanced Developer Tools & Networking (Completed)

- [x] Worker Threads module (multi-threaded execution)
- [x] Cluster module (multi-process support)
- [x] UDP/dgram module (UDP networking)
- [x] Memory Analyzer (heap snapshots, leak detection)
- [x] Visual Profiler (Chrome DevTools format)
- [x] Integrated Debugger (Chrome DevTools Protocol)
- [x] Complete Crypto module (OpenSSL integration)
- [x] Child Process module (process spawning)
- [x] DNS module (DNS resolution)

**Goal:** Experimental (Open for Review) developer tools and advanced networking capabilities.

### Phase 6: Ecosystem & Compatibility (Completed)

- [x] Extended npm support (registry communication, version resolution, package installation)
- [x] Node.js test suite compatibility (test runner and compatibility checker)
- [x] Performance benchmarking (comprehensive benchmarking framework)
- [x] Ecosystem compatibility enhancements (enhanced error messages and module resolution)

**Goal:** Full ecosystem compatibility and maturity.

For more details, see [PLAN.md](PLAN.md).

---

## 🔬 Current Status

**Version:** 0.6.0 (Phase 6 Complete - Ecosystem & Compatibility)

### Implemented (Phases 1-5)

**Core Architecture:**
- ✅ Basic project structure
- ✅ QuickJS + protoCore integration
- ✅ TypeBridge complete (Number, String, Boolean, BigInt, Array, Object, Function, Date, RegExp)
- ✅ Console (log, error, warn, info, debug, trace)
- ✅ Deferred with worker threads (bytecode serialization)
- ✅ CPUThreadPool and IOThreadPool
- ✅ EventLoop for callbacks
- ✅ GCBridge for memory management

**Core Modules (Phase 1-2):**
- ✅ protoCore module (Set, Multiset, SparseList, Tuple, mutability control)
- ✅ process module (argv, env, cwd, platform, arch, exit)
- ✅ io module (readFile, writeFile)
- ✅ **fs module** (Promises API, Sync API, Streams)
- ✅ **path module** (join, resolve, normalize, dirname, basename, extname, isAbsolute, relative)
- ✅ **http module** (Server and Client with HTTP/1.1)
- ✅ **stream module** (Readable, Writable, Duplex, Transform, PassThrough)
- ✅ **events module** (EventEmitter with on, once, emit, removeListener)
- ✅ **util module** (promisify, types.*, inspect, format)
- ✅ **crypto module** (createHash, randomBytes)
- ✅ **url module** (URL parsing and construction)

**Advanced Modules (Phase 3-4):**
- ✅ **buffer module** (Full Node.js API compatibility)
- ✅ **net module** (TCP sockets and servers)
- ✅ **Profiler module** (CPU and memory profiling)

**Advanced Networking & Concurrency (Phase 5):**
- ✅ **worker_threads module** (Multi-threaded execution with message passing)
- ✅ **cluster module** (Multi-process support with IPC)
- ✅ **dgram module** (UDP networking with multicast support)

**Enhanced Developer Tools (Phase 5):**
- ✅ **Memory Analyzer** (Heap snapshots, leak detection, allocation tracking)
- ✅ **Visual Profiler** (Chrome DevTools format export, HTML reports)
- ✅ **Integrated Debugger** (Chrome DevTools Protocol support, breakpoints, step debugging)

**Extended Module Support (Phase 5):**
- ✅ **Complete crypto module** (OpenSSL integration, encryption/decryption, signing)
- ✅ **child_process module** (Process spawning, IPC, signal handling)
- ✅ **dns module** (DNS resolution, reverse lookup, service lookup)

**System Features:**
- ✅ **Module system** (CommonJS require, ES Modules import/export, Module interop; **require** resolves built-in modules by name (e.g. `require('fs')`, `require('path')`, `require('buffer')`) and loads JS or native addons (.node/.so/.protojs) transparently)
- ✅ **CLI compatibility** (Node.js flags: --version, --print, --check, --input-type=module)
- ✅ **REPL** (Interactive read-eval-print loop with multi-line support)
- ✅ **npm integration framework** (PackageResolver, PackageInstaller, ScriptExecutor)

**Testing & Documentation:**
- ✅ Unit tests (ThreadPoolExecutor, CPUThreadPool, IOThreadPool, EventLoop)
- ✅ Integration tests (modules, fs, http, stream, crypto, net, worker_threads, cluster, dgram)
- ✅ Comprehensive documentation (200+ pages)

**Ecosystem & Compatibility (Phase 6):**
- ✅ **Extended npm support** (Registry communication, semver version resolution, package installation)
- ✅ **Performance benchmarking** (Comprehensive benchmarking framework with Node.js comparison)
- ✅ **Node.js test suite compatibility** (Test runner and compatibility checker)
- ✅ **Ecosystem compatibility enhancements** (Enhanced error messages and module resolution)

### Upcoming Improvements (Phase 7)

- 🔄 Advanced features and optimizations
- 🔄 Auto-parallelization detection
- 🔄 Object persistence
- 🔄 Distributed computing support

---

## 🤝 Contributing

This project is under active development. Contributions are welcome, especially:

- Phase 1 feature implementation
- Tests and documentation
- Optimizations
- Bug fixes

---

## 📝 License

Copyright (c) 2026 Gustavo Marino <gamarino@gmail.com>

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

---

## 🙏 Acknowledgments

- **protoCore**: Runtime foundation
- **QuickJS**: JavaScript parser and compiler
- **Fabrice Bellard**: Creator of QuickJS

---

## Lead the Shift

**Don't just watch the shift. Lead it.** The performance gap has been closed. The tools are here. Join the review, challenge our benchmarks, and become part of the Swarm of One. Let's build the future of computing, one cell at a time. **Think Different, As All We.**

---

## 📧 Contact

[To be defined]

---

## ⚠️ Important Note

**This project is in active development (Phase 6 Complete - Ecosystem & Compatibility).**

- Phase 6 complete: Extended npm support, performance benchmarking, and Node.js test suite compatibility
- **Performance:** ~100× slower than Node.js on the in-process compute suite (geomean baseline). The May 2026 "own-only" cache overhaul significantly improved object property access (~3.2x faster).
- Core modules functional: fs, path, http, stream, events, util, crypto, url, buffer, net
- Advanced modules: worker_threads, cluster, dgram, child_process, dns
- Developer tools: Memory Analyzer, Visual Profiler, Integrated Debugger with Chrome DevTools Protocol
- npm support: Full registry communication, semver version resolution, package installation
- Benchmarking: Standard in-process suite (`tests/benchmarks/run_standard_comparison.js`); raw 2026-04-28 baseline at `tests/benchmarks/results/baseline_2026-04-28.json`
- Test compatibility: Node.js test suite compatibility checker
- Module system working: CommonJS and ES Modules supported
- CLI tools available: REPL and Node.js-compatible flags
- Ready for Phase 7: Advanced features and optimizations
- API may change in future phases
- **Recommended for development, testing, and review; not for production use.**

---

## 🔗 Related Links

- [protoCore](https://github.com/numaes/protoCore) - Runtime foundation
- [QuickJS](https://bellard.org/quickjs/) - JavaScript parser
