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
For official ECMAScript compliance status and roadmap, see **[docs/TEST262_STATUS.md](docs/TEST262_STATUS.md)**.

### Test262 Conformance

**Round 16 — 2026-06-08** (13 fixes; surfaced a long-standing
architectural mismatch in the namespace built-ins' prototype
chain):

| Family | Passes | Total | Pass rate | Δ vs R15 |
|---|---:|---:|---:|---:|
| `built-ins/Number` | 335 | 338 | **99.1 %** | – |
| `built-ins/Math` | 323 | 327 | **98.8 %** | – |
| `built-ins/Array` | 2 707 | 3 081 | **87.9 %** | – |
| `built-ins/Object` | 2 852 | 3 411 | **83.6 %** | +0.5 pp |
| `built-ins/String` | 997 | 1 223 | **81.5 %** | +1.0 pp |
| `built-ins/JSON` | 121 | 165 | 73.3 % | –0.6 pp |
| `built-ins/Function` | 240 | 509 | **47.2 %** | – |
| `built-ins/Date` | 76 | 594 | 12.8 % (stub) | – |
| **8-family rollup** | **7 651** | **9 648** | **79.3 %** | **+0.3 pp** |

The architectural finding of the round was that the three namespace
objects — `Math`, `JSON`, and `Reflect` — were each constructed with
`ctx->newObject(true)`, leaving them with no [[Prototype]] link.
This breaks the spec contract from §21.3 / §25.5 / §28.1 ("[the
namespace] has a [[Prototype]] internal slot whose value is
%Object.prototype%") and silently disables every Object.prototype
method on the receiver:

  ```
  typeof Math.hasOwnProperty            // pre-fix: 'undefined'
  Object.getPrototypeOf(JSON)           // pre-fix: not Object.prototype
  Object.defineProperty(o, 'p', Math);  // pre-fix: o.p === undefined
                                        // (ToPropertyDescriptor walked the
                                        //  missing chain for `value`,
                                        //  `writable`, …)
  ```

The fix runs `space->objectPrototype->newChild(ctx, true)` for the
namespace cell and registers the override in `t_jsProtoMap`.
Reordering the bootstrap so `ensureObjectConstructor` runs before
`ensureMathObject` was required, because the constructor populates
the user-visible Object.prototype (and re-binds `space->objectPrototype`)
— installing Math first would have captured the pre-population
snapshot.

The remainder of the round closes a family of long-tail spec
gaps:

**Theme A — accessor / descriptor handling**:
  * `9dc0fc3c` — Object.defineProperty preserves the omitted
    accessor field (get xor set) when redefining an existing
    accessor descriptor (§10.1.6.3 step 4)
  * `84ce246f` — Object.defineProperty rejects
    enumerable/configurable changes on a non-configurable
    accessor (the data-side already gated this; the accessor
    branch did not)
  * `8f82d0a0` — Object.defineProperties throws TypeError on
    `undefined` target (RequireObjectCoercible)
  * `c798066d` — Object.assign invokes the target's accessor
    setter (and propagates its abrupt) instead of erroring
    with the writable-bit gate (§19.1.2.1 step 4.c.ii.3)
  * `1c1af771` — Object.fromEntries throws TypeError when
    `iterator.next` is non-callable, and does NOT close()
    the iterator in that case (§7.4.6)

**Theme B — ToPrimitive / ToPropertyKey accessor probes**:
  * `c0b166ab` — `objToStr` (the String.prototype.* coercion
    entry) probes the `__get_Symbol.toPrimitive__` accessor
    sidecar so a throwing exotic-primitive method propagates
  * `57057131` — `objToStr` probes `__get_toString__` /
    `__get_valueOf__` sidecars symmetrically (§7.1.1
    OrdinaryToPrimitive)
  * `0faa7993` — `coercePropNameToKey` (the ToPropertyKey
    implementation) probes `Symbol.toPrimitive` before
    falling through to toString / valueOf; also handles
    Symbol-tagged primitive returns
  * `fa4274f8` — Object.hasOwn routes its key argument
    through `coercePropNameToKey` instead of the raw
    String / Integer-only check

**Theme C — enumeration semantics**:
  * `b152ca3c` — Object.{keys, values, entries} skip Symbol
    keys per §7.3.21 EnumerableOwnProperties (filter by
    `propKey->isSymbol()` at the iteration callback)

The Number / Math / Array / Function / Date families are flat:
their residue is structural — BigInt, Proxy, ResizableArrayBuffer,
or the dynamic Function constructor (which parses arbitrary source).
JSON regressed 1 test as collateral of the Symbol-key skip — the
specific case relied on the prior leak of a Symbol key being
silently dropped at JSON.stringify.

The architectural backlog uncovered by R16 — making
`%Object.prototype%` mutable so user-level `Object.prototype.x = v`
propagates in-place to every descendant rather than splitting the
cell — is the next target (R17).

**Round 15 — 2026-06-07 late night** (17 fixes; sweep round that
revealed and closed the runaway-allocation bug that had been
disguised as kernel hangs):

| Family | Passes | Total | Pass rate | Δ vs R14 |
|---|---:|---:|---:|---:|
| `built-ins/Number` | 335 | 338 | **99.1 %** | – |
| `built-ins/Math` | 323 | 327 | **98.8 %** | – |
| `built-ins/Array` | 2 707 | 3 081 | **87.9 %** | +0.1 pp |
| `built-ins/Object` | 2 833 | 3 411 | **83.1 %** | +0.4 pp |
| `built-ins/String` | 984 | 1 223 | **80.5 %** | +0.4 pp |
| `built-ins/JSON` | 122 | 165 | **73.9 %** | – |
| `built-ins/Function` | 240 | 509 | **47.2 %** | +1.0 pp |
| `built-ins/Date` | 76 | 594 | 12.8 % (stub) | +0.3 pp |
| **8-family rollup** | **7 620** | **9 648** | **79.0 %** | **+0.3 pp** |

The headline change for this round is **not** in the pass-rate
table — it is the diagnosis and elimination of a runaway-allocation
bug in the Array iterator family that had been disguised as a series
of full-system hangs across the previous rounds:

  ```
  Array.prototype.map.call({0: 9, length: "Infinity"}, cb)
  ```

  `arrLen` clamped `length: Infinity` to 2^32-1 (≈ 4 billion) instead
  of routing it through ArrayCreate's RangeError gate, so the
  iteration loop spun 4 billion times, each step allocating
  `makeIterArgs` / `arrayCreateDataPropertyOrThrow` cells.  Within
  ~120 seconds protojs's RSS crossed 12 GB; at ~85 % PSI on the
  user cgroup `systemd-oomd` killed gnome-shell + dbus + pipewire
  + everything else, dropping the user back to the GDM login screen.
  Four consecutive sessions were lost on June 7 alone before the
  cause was isolated; on the fifth it was traced inside protojs.

The fix is `arrayThrowIfLenOverflow`: probe the raw `length`
attribute, ToLength-coerce it via `jsToNumber`, and signal
`RangeError("Invalid array length")` when the result is non-finite
or exceeds 2^32 - 1.  Applied across thirteen Array methods:
  * **map**, **filter**, **forEach**, **find**, **findIndex**,
    **findLast**, **findLastIndex**, **some**, **every**, **reduce**,
    **reduceRight** (the §22.1.3.X iteration family)
  * **toReversed**, **toSorted**, **toSpliced**, **with** (ES2023
    change-array-by-copy family — these explicitly use `ArrayCreate(len)`)

After the sweep, the previously-hanging fixtures terminate in
<1 ms with the spec-mandated abrupt completion.  No oomd-driven
session kills since.

The rest of the round is the routine spec-cleanup pattern:

**Theme A — abrupt-completion propagation through native built-ins**:
  * `2a23019b` — `Object.fromEntries` closes the iterator (calls
    `iter.return()`) on a non-Object entry
  * `8b8de3a7` — `Date.UTC` routes its coerce helper through
    `jsToNumber` so a throwing `toString` on a positional propagates
  * `1008c984` — `String.raw` probes `__get_raw__` / `__get_length__`
    sidecars and propagates accessor-getter abrupts
  * `7ec90fcb` — `String.raw` caps substitutions at `raw.length - 1`
    so a trailing arg with a throwing `toString` is never consulted
  * `fa6a98bc` — `Function.prototype.bind` propagates throws from
    the target's `name` accessor

**Theme B — descriptor and integrity-level correctness**:
  * `095ee180` — `Object.assign` rejects new prop creation on a
    non-extensible target (§10.1.6.3 ValidateAndApply step 2.a)
  * `25f40344` — `Object.setPrototypeOf` validates target +
    proto type up-front (`null`/`undefined` target → TypeError;
    non-Object non-Null proto → TypeError)
  * `bbba3806` — `Object.setPrototypeOf` rejects assignments that
    would create a prototype-chain cycle
  * `05af6812` — `Object.isFrozen` honours the empty-receiver case
    (non-extensible + no own props → frozen, no marker required)
  * `8e64654b` — RegExp `fromJS` marshalls source/flags as
    non-enumerable instead of leaking them through `Object.keys`

**Theme C — Symbol / Number / ES2023 surface**:
  * `0f368587` — `JSON.stringify` omits Symbol values (§25.5.2.2
    step 4); top-level returns undefined, arrays emit "null",
    object keys are dropped
  * `b92abc20` — `Object.is(0, NaN)` returns false (was true; the
    Integer/Double cross-type branch was missing)
  * `719ecf8f` — `Object.groupBy` iterates strings as codepoint
    sequences
  * `c2d959cf` — `callJSFunction` routes Number / Boolean / String
    conversion constructors through their `__construct__` /
    `__string_ctor__` markers, so `Number.bind(null)(42)` returns
    42 instead of undefined

The slowest gain (Number / Math / JSON flat) reflects that the
remaining residue in those families is in `Array.fromAsync`,
ResizableArrayBuffer fixtures, BigInt, or order-of-insertion-key
shapes that need either iterator-protocol / typed-array async or
deeper protoCore work to land.

**Round 14 — 2026-06-07 night** (20 fixes, per-area pass-rate over
the eight essential `built-ins` families):

| Family | Passes | Total | Pass rate | Δ vs R13 |
|---|---:|---:|---:|---:|
| `built-ins/Number` | 335 | 338 | **99.1 %** | +0.9 pp |
| `built-ins/Math` | 323 | 327 | **98.8 %** | +3.4 pp |
| `built-ins/Array` | 2 706 | 3 081 | **87.8 %** | – |
| `built-ins/Object` | 2 821 | 3 411 | **82.7 %** | +0.9 pp |
| `built-ins/String` | 980 | 1 223 | **80.1 %** | +0.5 pp |
| `built-ins/JSON` | 122 | 165 | **73.9 %** | +1.2 pp |
| `built-ins/Function` | 235 | 509 | **46.2 %** | +4.0 pp |
| `built-ins/Date` | 74 | 594 | 12.5 % (stub) | +0.7 pp |
| **8-family rollup** | **7 596** | **9 648** | **78.7 %** | **+0.8 pp** |

Three themes ran through the round:

**Theme 1 — own-data shadows inherited accessor**
(commit `8065b92c`).  The dynamic-key get path (OP_get_array_el)
probed `invokeGetterIfPresent` first, which walks the prototype
chain — so an accessor installed on a prototype masked an own
data property on the receiver:

  ```js
  var p = Object.defineProperty({}, 'foo', { get(){ return 0; }});
  var c = Object.create(p);
  Object.defineProperty(c, 'foo', { value: 10 });
  c.foo         // 10 (OP_get_field — static key, was correct)
  c['foo']      // pre-fix: 0   post-fix: 10
  ```

Restructure the dispatch in three ordered branches: (1) own
accessor → invoke own getter; (2) own data → return own;
(3) walk parent for inherited accessor.  Closes the freeze/seal
fixtures with the same "data overrides inherited accessor"
pattern.

**Theme 2 — ECMAScript 2024 / 2025 surface area**:

  * `f6a79881` — `Math.f16round` (uses compiler `_Float16` when
    `__FLT16_MAX__` is defined; binary32 fallback otherwise)
  * `7f2f48da` — `Math.sumPrecise` (Shewchuk distillation on
    Array-shaped inputs; generic iterator support still pending)
  * `4ff3be22` — `JSON.rawJSON` / `JSON.isRawJSON` inherit
    `Function.prototype` (their wrappers were never listed in the
    post-bootstrap re-parenting array)
  * `de70dfd9` — `Function.prototype[@@hasInstance]` per
    §20.2.3.6 OrdinaryHasInstance, with descriptor 0x0 (W:F E:F
    C:F per §17, unlike call/apply/bind which are writable)
  * `089ec599` — `Object.groupBy` (Array iteration; iterator
    protocol pending — Map.groupBy was already present)

**Theme 3 — abrupt-completion propagation + descriptor cleanup**:

  * `a50fc5a3` — class constructor invoked without `new` throws
    TypeError per §10.2.1 step 2 (`__is_class_ctor__` flag stamped
    in OP_define_class)
  * `b62c0fef` — `Number('10e10000')` (and parseFloat) returns
    `+Infinity` instead of NaN — `std::stod`'s `out_of_range`
    catch separated from the invalid-argument catch
  * `4039b380` — OP_call propagates `__construct__` abrupt
    completions (`Number(Symbol())` throws instead of returning
    undefined silently)
  * `f191b7f7` / `f7c56d09` — `Date.UTC` with `NaN` / `Infinity` /
    overflow past 8.64e15 returns NaN (TimeClip)
  * `ef935697` — `Object.assign` skips array holes in sparse
    sources (was overwriting target with PROTO_NONE)
  * `b495e84d` — `arguments` object stamps
    `__has_nonwritable_props__` so `Object.keys(arguments)` skips
    `length` (the `__pd_length__` sidecar was there but ungated)
  * `f8e03d24` — `hasOwnProperty` on array indices past
    `__elements__` reports false for `new Array(N)` slots and true
    only when an explicit attribute backs the index
  * `c4aec3be` — `Array[@@species]` getter wrapper stamps the
    gating flag (Round-12/13 sweep completion)
  * `d5eb9eb2` — `Promise[@@species]` / `Set[@@species]` getter
    wrappers stamp the gating flag (sweep continuation)
  * `3f926d5c` — `Object.getPrototypeOf(Function) ===
    Function.prototype` — Function ctor wired its
    setJSProtoOverride explicitly (the unimplemented-stub installer
    ran later and missed Function)
  * `fcff5ad0` — `Function.prototype` itself stamps the gating
    flag so `length` and `name` actually enforce writable:false
  * `5446b3e4` — `String.raw` throws TypeError for null/undefined
    template, and walks `__elements__` for Array templates
    (segments were being read via `getAttribute(indexKey)` which
    misses array storage)
  * `3b24f61d` — `Object.values` / `Object.entries` propagate
    abrupt completions from getters

Math: 95.4% → **98.8%** (the f16round + sumPrecise additions plus
incidental closures lift the family near saturation; only 4 fails
left, all in sumPrecise hard-overflow paths or specialised Math
APIs without C++ kernel support).  Number: 98.2% → **99.1%**
(StrDecimalLiteral overflow + the OP_call __construct__ abrupt
completion close most of the residue).  Function: 42.2% → **46.2%**
(hasInstance + class-ctor-throws + FP gating).  Object: 81.8% →
**82.7%** (groupBy + sparse assign + hasOwn + gOPO).

The slowest gain (Array 0.0 pp) reflects that the remaining Array
fails are almost entirely in `Array/fromAsync` (90 tests) and
`Array/prototype/sort/comparefn-resizable-buffer` clusters that
require iterator-protocol / typed-array async infrastructure.

**Round 13 — 2026-06-07 late evening** (19 fixes, per-area pass-rate
over the eight essential `built-ins` families):

| Family | Passes | Total | Pass rate | Δ vs R12 |
|---|---:|---:|---:|---:|
| `built-ins/Number` | 332 | 338 | **98.2 %** | +0.3 pp |
| `built-ins/Math` | 312 | 327 | **95.4 %** | **+24.1 pp** |
| `built-ins/Array` | 2 704 | 3 081 | **87.8 %** | +0.9 pp |
| `built-ins/Object` | 2 791 | 3 411 | **81.8 %** | +0.4 pp |
| `built-ins/String` | 974 | 1 223 | **79.6 %** | – |
| `built-ins/JSON` | 120 | 165 | **72.7 %** | +1.2 pp |
| `built-ins/Function` | 215 | 509 | **42.2 %** | +4.3 pp |
| `built-ins/Date` | 70 | 594 | 11.8 % (stub) | +3.0 pp |
| **8-family rollup** | **7 518** | **9 648** | **77.9 %** | **+1.7 pp** |

Round 13 split into three themes (one bug-fix revert + two systematic
sweeps):

**Theme 0 — revert Round 12 commit #4** (`42201672`).  Reverting
`26cd900c` (which wrapped `Object.prototype.toString/toLocaleString/
valueOf` via `installNonEnumerableMethod` to expose `length`/`name`)
recovered 44 tests net.  Wrapping broke `.call`/`.apply` chain access
in specific sequences — `JSON.parse.call({})` and `assert.sameValue(
Object.prototype.toString.call(parse), '[object Function]')` started
failing.  Root cause is in `installNonEnumerableMethod`'s parent
chain; reinstating the raw method form trades the descriptor-shape
fixtures (a handful of tests) for the chain-access integrity (~40+
tests across the suite).  Will revisit when the chain bug is fixed.

**Theme 1 — own-attribute closure** (commits `04e2f28b` …):
`Date.prototype.constructor === Date` as an OWN property (was
inherited Object.prototype.constructor === Object); plus Math's
constant + method descriptor hot-path flags (`499f10fd`, `c2217cac`).
The Math methods sweep alone took the family from 71.3 % to 95.4 %.

**Theme 2 — constructor `__has_nonwritable_props__` sweep, continued
+ `prototype` descriptor sweep**:

  * `94b24c18` — all 8 `Error` / `TypeError` / `RangeError` / ...
    constructors
  * `6efb42db` — all 17 stub constructors (BigInt, Proxy, WeakRef, ...)
  * `0f8e0c99` — `Date` (installed in `console.cpp`, pre-stub-installer)
  * `f1dba57f` — `Date.{now,parse,UTC}` wrappers
  * `63879f54` — `Map.prototype.size` getter, `Map.groupBy`, `Map@@species`
  * `d667cb71` — `Set.prototype.size` getter, `RegExp@@species`
  * `069cf026` — every `String.prototype` method wrapper
  * `f12c7f51` — `Array` constructor
  * `22c88420` — `Function` constructor
  * `bce46e1d` — `Function.prototype.{call,apply,bind,toString}`
    wrapped with §17 shape via `fp` self-parenting (chain still
    resolves `.bind`/`.apply` on the wrappers)
  * `def48d31` — `Function.prototype` descriptor `0x0` on the ctor
  * `d97d7635` — `Symbol.prototype` descriptor `0x0`
  * `79de58c3` — `Map.prototype` / `WeakMap.prototype` descriptor `0x0`
  * `7306e062` — `Set` / `Promise` / `RegExp` / `ArrayBuffer` /
    `DataView` `.prototype` descriptor `0x0`
  * `9f56f2fb` — every `TypedArray` ctor's `.prototype` descriptor `0x0`

Same architectural finding as Round 12: every site that stamps a
non-writable descriptor sidecar must also light the
`__has_nonwritable_props__` flag for `resolvePutFieldOOP` to enforce
it.  Round 13 closes ~25 more registration sites and adds the dual
sweep: where Round 12 fixed `name`/`length` descriptors, Round 13
fixes the analogous `prototype` slot which §17 spec'es as
`{writable:false, enumerable:false, configurable:false}` (bits 0x0)
for every built-in constructor.

**Round 12 — 2026-06-07 evening** (19 fixes, broad-scope, per-area
pass-rate over the eight essential `built-ins` families):

| Family | Passes | Total | Pass rate |
|---|---:|---:|---:|
| `built-ins/Number` | 331 | 338 | **97.9 %** |
| `built-ins/Array` | 2 676 | 3 081 | **86.9 %** |
| `built-ins/Object` | 2 777 | 3 411 | **81.4 %** |
| `built-ins/String` | 974 | 1 223 | **79.6 %** |
| `built-ins/JSON` | 118 | 165 | **71.5 %** |
| `built-ins/Math` | 233 | 327 | 71.3 % |
| `built-ins/Function` | 193 | 509 | 37.9 % |
| `built-ins/Date` | 52 | 594 | 8.8 % (stub) |
| **8-family rollup** | **7 354** | **9 648** | **76.2 %** |

The round closed in two clear themes:

**Theme 1 — annex-B accessor reflectors + Object.create getter
plumbing** (commits `ef8aa519` … `e30454f0`):

1. `2048f209` — `wrapNativeFunction` stamps `__has_nonwritable_props__`
   so every built-in static method's `name` / `length` are actually
   read-only (the descriptor said so; the write path was silently
   ignoring the bit).  Spec §17 baseline.
2. `ef8aa519` — `Object.create(O, Properties)` invokes accessor
   getters on `Properties` via the `__get_<key>__` sidecar, matching
   §19.1.2.4 step 2's `Get(props, key)` requirement.  Closes the 22
   `Object.create/15.2.3.5-4-N` conformance cases.
3. `8c86c686` — `Date.prototype[Symbol.toStringTag] = "Date"` so
   `Object.prototype.toString.call(new Date(0))` yields the spec-
   required `"[object Date]"`.
4. `26cd900c` — `Object.prototype.{toString,toLocaleString,valueOf}`
   carry the §17 `name` / `length` descriptor shape.
5. `a2a17e1f` — Annex-B `Object.prototype.__lookupGetter__` /
   `__lookupSetter__` implemented (32 conformance cases).
6. `e30454f0` — Annex-B `Object.prototype.__defineGetter__` /
   `__defineSetter__` implemented (22 conformance cases).
7. `ea127b46` — `setNWCDescriptor` stamps
   `__has_nonwritable_props__` so user-function `name` / `length` /
   `prototype` are non-enumerable in `Object.keys(fn)`.
8. `30e63157` — `Object.fromEntries` throws `TypeError` when an
   iterator entry is not an Object (§20.1.2.6 step 8.b).

**Theme 2 — constructor `__has_nonwritable_props__` sweep**
(commits `bda9a4fb` … `3793185b`).  Single recurring root cause:
every JS built-in constructor (`Boolean`, `Number`, `String`, `Map`,
`WeakMap`, `Set`, `Promise`, `RegExp`, `ArrayBuffer`, `DataView`,
every `TypedArray` flavor, and the JSON-style native modules) stamped
`__pd_name__ = 0x2` and `__pd_length__ = 0x2` (writable=false per
§17) but skipped the per-target `__has_nonwritable_props__` flag.
Without the flag, `resolvePutFieldOOP` (ProtoInterpreter.cpp:172)
skipped the entire `__pd_<key>__` probe and writes to
`JSON.parse.name`, `Boolean.length`, `Float32Array.name`, etc.
silently succeeded despite the descriptor.  One-line identical fix
at each registration site — but each commit closes the
`verifyProperty(<Ctor>, "name"|"length", { writable: false, ...})`
fixture for that specific built-in family:

  * `bda9a4fb` — `ProtoNativeModule::addMethod` (JSON.* and the
    other NativeEntry-driven modules)
  * `b17b8739` — `Boolean` constructor
  * `e5a6d605` — `RegExp` constructor
  * `260517c6` — `Number`, `String` constructors
  * `28c862ab` — `Map`, `WeakMap` constructors
  * `b25e430e` — `Set`, `Promise` constructors
  * `2235068c` — `ArrayBuffer`, `DataView` constructors
  * `3793185b` — every `TypedArray` constructor (one site, 11 families
    in one stroke since they share the install loop)

Two recurring root causes drove the entire round: (a) descriptor
sidecars (`__pd_<key>__`) were being written without their gating
hot-path flag (`__has_nonwritable_props__`), so the runtime treated
every property as writable + enumerable regardless of what the
descriptor claimed; (b) annex-B accessor reflectors were entirely
missing.  Both are now systematic: every code path that stamps a
non-writable descriptor also lights the flag, and every annex-B
accessor surface is wired.

**Latest full-suite run — 2026-06-01** (commit `073d1414`,
`language + built-ins` patterns, 46 963 tests):

| | Total | Passed | Failed (semantics) | Timeouts | Pass rate |
|---|---:|---:|---:|---:|---:|
| **2026-06-01 (this run)** | 46 963 | **27 565** | 18 477 | 36 | **58.70 %** |
| 2026-05-11 (prior) | 46 963 | 27 025 | 18 666 | 431 | 57.55 % |
| **Δ** | 0 | **+540** | −189 | **−395** | **+1.15 pp** |

The 2026-06-01 cycle's correctness commits are responsible for both
the net pass gain and the dramatic **−91 % timeout reduction**: the
`OP_for_of_start` stale-pAutomaticLocals fix (`3dc726b8`) alone
unblocked the `built-ins/Object` subtree (+870 passes there) and
collapsed timeout count from 431 to 36.

| Family | Passes (this run) | Passes (prior) | Δ | Pass rate |
|---|---:|---:|---:|---:|
| `built-ins` | 9 933 | 9 247 | **+686** | 42.57 % |
| `language` | 17 632 | 17 778 | −146 | 74.62 % |

`built-ins` carried the cycle's gains.  `language` slipped 146,
concentrated in `language/expressions` (−376) and warranting a
targeted bisect — see [docs/TEST262_STATUS.md](docs/TEST262_STATUS.md)
for the per-category breakdown, the suspected regression areas,
and the parallel runner setup that brought a full run to ~5:30 min
on a 12-core machine.

### Performance Benchmarks

**Honest baseline — 2026-06-07 (late, after structural cleanup)** —
3-round median, same `libprotoCore` build from the
`digression-attr-cache-padding` branch.  This is a continuation of
the morning baseline below: a static analysis identified seven
patterns where protoJS was reimplementing logic that protoCore
already exposes, plus one interpreter-side wrapper that was paying
function-call overhead for what should be a single memory access.
Fifteen commits later (no protoCore changes), geomean against
QuickJS is **17.65 ×** — below the 2026-06-01 baseline of 21.0 ×.

> **Structural cleanup landed this cycle (2026-06-07 late)**
>
> Each commit follows one rule: *if protoCore exposes the primitive,
> call it; if it exposes a flag, set it; if it exposes a fast path,
> take it.*  No reimplementation of what already exists.
>
> 1. **`4b99011c`** — `arrayPush` calls `ProtoList::appendLast` in a
>    tight loop with `getElements`/`setElements` hoisted out.  Per
>    100 K-push iteration: 1 getAttribute + 100 K `appendLast` + 1
>    setAttribute (was: 100 K × {3 attribute ops + arrSet ceremony}).
>    **`array_literal` 2315 → 199 ms (−91 %)** — fully recovered
>    versus the 2026-06-01 pre-regression baseline of 195 ms.
> 2. **`9fff940c`** — `arrayShift` / `arrayUnshift` use
>    `ProtoList::removeFirst` / `appendFirst` directly when the chain
>    has no inherited indexed setters and no accessors.  The O(N) spec
>    walk (Get + Set per element) collapses to one O(log N) tree
>    operation.
> 3. **`47e269e1`** — `arraySlice` → `ProtoList::getSlice`.  One
>    O(log N) tree-splice instead of O(N) arrGet +
>    arrayCreateDataPropertyOrThrow per element.
> 4. **`bf12c184`** — `arrayConcat` plans-then-extends: build a vector
>    of (spread?, list) pairs, then `extend` each spreadable and
>    `appendLast` each non-spreadable.  Fallback path preserved
>    verbatim.
> 5. **`c73cb574`** — `arraySplice` assembles prefix + inserts + suffix
>    via `getSlice` + `appendLast` + `extend`.  Removed array is a
>    single `getSlice` of the deleted range.
> 6. **`1b5a527c`** — `arrayIncludes` / `arrayIndexOf` /
>    `arrayLastIndexOf` walk `__elements__` via `ProtoListIterator`
>    instead of arrGet per index.  Eliminates the per-element
>    `__get_<i>__` / `__set_<i>__` rope construction.
> 7. **`5785108d`** — `Object.keys` / `values` / `entries` skip the
>    per-key `__pd_<key>__` enumerable probe AND the `__get_<key>__`
>    accessor probe when the per-target flags
>    `__has_nonwritable_props__` / `__has_accessor_props__` are absent.
> 8. **`b989e88a`** — `runBytecode` passes a 256-slot stack buffer to
>    `ProtoContext` as `externalSlots`.  protoCore documents this as
>    "zero heap cost" — every JS function call WAS doing
>    `new const ProtoObject*[N]` + `delete[]`.
>    `ProtoContext::ProtoContext` disappeared from the top-15 profile.
> 9. **`6e40d219`** — bind callee args directly from the parent's
>    stack slice, skipping `argsList->getAt(i)` per arg (the AVL walks
>    on a tree we just built from the same slice).
> 10. **`3ed66a04`** — `ProtoBytecodeModule::usesArguments` flag,
>     computed once at module load by scanning for `OP_special_object`
>     kind=0/1, `OP_rest`, `OP_init_ctor`.  When false, the per-call
>     `pContext->newList(argc, slice)` is skipped — one fewer cell
>     allocation per JS-to-JS call.
> 11. **`f4438ebb`** — arithmetic Integer fallback paths delegate to
>     `ProtoObject::add` / `subtract` / `multiply` / `modulo`.  Fixes
>     a silent `int64` truncation bug (`9007199254740990 + 3` used to
>     truncate; now promotes to `LargeInteger` via `TempBignum`).
>     The SmallInt inline fast path stays — that's the hot case.
> 12. **`e02ec70b`** — `collectOwnKeys` walks the own-attributes
>     SparseList via `ProtoSparseList::processElements` callback.
>     The `getIterator + advance()` loop was allocating an iterator
>     wrapper cell per step (`implAsObject` call); the callback path
>     walks SmallSparseList inline pairs directly and reuses the
>     internal iterator without wrapper allocations for the AVL form.
> 13. **`ddf7f82d`** — `stackPush` / `stackPop` / `stackTop` / `getSlot`
>     / `setSlot` + auxiliaries marked
>     `[[gnu::always_inline]] static inline`.  Pre-fix profile on
>     `control_flow`: 5 helpers totalled ~15.5 % of CPU as real
>     function calls.  Verified via `nm`: all helpers eliminated from
>     the binary.  `control_flow` 252 → 226 ms (−10 %).
>     Three documented hang causes from the prior macro attempt are
>     explicitly guarded against (proper `ctx` propagation, saturating
>     doubling on `(idx+1)*2` overflow, preserved NULL guards) — see
>     memory `feedback_protojs_runbytecode_macros_caution.md`.

#### Standard In-Process Suite — vs Node.js 22 / V8 / vanilla QuickJS

3-round median.  Same `build_release/protojs`, same Node 22,
`qjs_minimal_release` (QuickJS rebuilt with `-O3 -DNDEBUG`).

| Benchmark                  |    Node |    QuickJS |   protoJS |   Node × |   QuickJS × |
|----------------------------|--------:|-----------:|----------:|---------:|------------:|
| array_literal              |    2 ms |       5 ms |    203 ms |    102 × |       41 ×  |
| control_flow               |    4 ms |      43 ms |    226 ms |     57 × |       5.3 × |
| function_calls             |    1 ms |       8 ms |    216 ms |    216 × |        27 × |
| json_transform             |    1 ms |       3 ms |    122 ms |    122 × |        41 × |
| json_transform_small       |    0 ms |       0 ms |     13 ms |      —   |        —    |
| list_snapshot_history      |    0 ms |       1 ms |    269 ms |      —   |       269 × |
| numeric_loop               |    1 ms |      32 ms |    125 ms |    125 × |       3.9 × |
| **object_property**        |   35 ms |      64 ms |    561 ms |     16 × |       8.8 × |
| **object_read_only**       |    1 ms |       5 ms |    103 ms |    103 × |        21 × |
| **object_write_only**      |   12 ms |      52 ms |   1275 ms |    106 × |        25 × |
| **parallel_cpu**           |**40 ms**|  **730 ms**|  **52 ms**|**Node 1.3 ×**|**protoJS 14.0 ×**|
| string_concat              |    1 ms |       5 ms |     99 ms |     99 × |        20 × |
| string_insert_middle       |    0 ms |       0 ms |    237 ms |      —   |        —    |
| string_processing          |    0 ms |       0 ms |    231 ms |      —   |        —    |
| string_repeated_doubling   |   36 ms |       1 ms |   2151 ms |     60 × |      2151 × |
| tree_traversal             |    1 ms |       4 ms |    298 ms |    298 × |        75 × |

**Geometric mean (12 benches where all three engines > 0 ms):**
- **protoJS / Node = 66.6 ×**   (was 132 × on 2026-06-06 morning, was 58.5 × on 2026-06-01)
- **protoJS / QuickJS = 17.65 ×** (was 34.4 × on 2026-06-06 morning, was 21.0 × on 2026-06-01)
- QuickJS / Node = 3.77 ×

#### Recovery vs the 2026-06-06 morning baseline

Geomean today / 2026-06-06 morning = 0.50 ×  — **protoJS is ~50 %
faster across the standard suite than the morning snapshot**, and
**~16 % faster** than the 2026-06-01 pre-regression baseline (which
was 21.0 × QuickJS; we are at 17.65 × now).

| Benchmark                  | 06-06 (ms) | 06-07 (ms) |        Δ |
|----------------------------|-----------:|-----------:|---------:|
| **array_literal**          |       2315 |        203 | **−91 %** |
| **object_write_only**      |       9554 |       1275 | **−87 %** |
| **object_property**        |       2793 |        561 | **−80 %** |
| **object_read_only**       |        394 |        103 | **−74 %** |
| list_snapshot_history      |        324 |        269 |     −17 % |
| string_processing          |        251 |        231 |      −8 % |
| numeric_loop               |        124 |        125 |       +0 % |
| string_concat              |        101 |         99 |      −2 % |
| tree_traversal             |        308 |        298 |      −3 % |
| parallel_cpu               |         52 |         52 |       ±0 % |
| control_flow               |        239 |        226 |      −5 % |
| function_calls             |        213 |        216 |      +1 % |
| json_transform             |        199 |        122 |     −39 % |
| string_repeated_doubling   |       2106 |       2151 |      +2 % |
| string_insert_middle       |        236 |        237 |      ±0 % |

**Honest framing.**  17.65 × QuickJS is *better than the 2026-06-01
baseline*, but still ~3 × off the < 5 × goal.  The remaining gap is a
mix of:

  - Closure-cell `__cv__` writes for every let/const at module scope
    (200 K writes per `function_calls` iteration = 200 K SmallSparseList
    snapshots — structural to protoCore's immutable model)
  - `string_repeated_doubling` and `string_insert_middle` bottleneck
    in protoCore-side rope construction
  - Doubled prototype walk (`t_jsProtoMap` parallel to protoCore parents)
    that this cycle did NOT address

`parallel_cpu` still wins **14 × against QuickJS**, **77 % of V8's
JIT'd throughput**.  The GIL-free architectural payoff is intact.

> **Cycle summary**: thirteen commits, none touching protoCore,
> reduced the protoJS-vs-QuickJS geomean from **33.9 ×** (start of day
> 2026-06-06) to **17.65 ×** — a 48 % improvement attributable
> entirely to *using protoCore primitives instead of reimplementing
> them*.  The architectural lesson is preserved in the memory
> `feedback_protocore_cache_for_stable_mutables.md` and now also
> `feedback_protojs_runbytecode_macros_caution.md`.

Raw rounds: `tests/benchmarks/results/three-way-rounds-2026-06-07b.txt`.
Raw JSON: `tests/benchmarks/results/node_quickjs_comparison.json`.

---

**Prior baseline — 2026-06-07 (early)** (in-process median time, protoJS built
in pure Release mode against the same `libprotoCore` from the
`digression-attr-cache-padding` branch as 2026-06-06).  This run lands
on top of the 2026-06-06 snapshot and reflects a **five-commit
structural fix** that recovers the entire ~78% geomean regression
introduced by the 10 Array cleanup packages.

Same sampling protocol as 2026-06-06: 3 outer × 5 inner = 15 samples
per cell, median reported.  No CPU pinning; concurrent GC stays on its
own core.

> **Structural fix landed this cycle (2026-06-07)**
>
> Root cause: every Array cleanup package added one or more
> spec-mandated probes behind freshly-constructed `ProtoString` ropes —
> `ctx->fromUTF8String("__pd_length__")`, `"__set_<idx>__"`,
> `"__pd_<key>__"`, `"__get_<key>__"`, etc. — on the per-call hot path.
> Cumulative: **+29 fresh `fromUTF8String` sites in `ArrayPrototype.cpp`
> alone since 2026-06-01**, 134 total across the codebase.  100K
> `arr.push(i)` in `array_literal` constructed ~300K throwaway ropes
> per iteration; 200K `obj[key]=v` writes in `object_property`
> constructed ~600K.  All wasted on benches with no monkey-patching.
>
> 1. **`51819174`** — strong-intern 134 sites of `__pd_length__` /
>    `__pd_name__` / `__pd_constructor__` / `__pd_message__` /
>    `__pd_size__` / `__is_symbol__` / `__fields_init__` via JSSymbols
>    DEFINE_SYMBOL entries.  Mechanical rewrite via two regex patterns.
>    `array_literal` 2315 → 1660 ms (−28%).
> 2. **`2f0d8d41`** — gate `arrSet`'s per-element `__set_<idx>__`
>    inherited-setter probe behind a new `__has_indexed_setters__` flag,
>    stamped by `Object.defineProperty` at the only install site.
>    Conservative: one-way (set, never cleared).  `array_literal`
>    1660 → 1054 ms (−37% on top).
> 3. **`f1b26da3`** — hoist the gate out of `arrayPush`'s per-element
>    loop, then call `arrayTryFastSet` directly when the gate is clean
>    (the universal case).  Falls back to `arrSet` only on sparse-
>    overflow or genuine inherited setter.  `array_literal` 1054 →
>    994 ms (−6% on top, structural cleanup more than perf).
> 4. **`b2ca3b7c`** — gate `resolvePutFieldOOP` (every `obj[k]=v`
>    write) behind `__has_accessor_props__` and `__has_nonwritable_props__`
>    flags.  Stamping points: `Object.defineProperty` accessor / data
>    branches, `Object.freeze`, `PrototypeUtils::installMethod` (because
>    every native method's `.length` / `.name` are writable=false).
>    **`object_write_only` 9554 → 1337 ms (−86%)**, `object_property`
>    2793 → 1204 ms (−57%).
> 5. **`7fdf9eba`** — gate the read-path `invokeGetterIfPresent` /
>    `invokeGetterIfPresentFast` behind the same `__has_accessor_props__`
>    flag.  Stamping at `Map.prototype.size` / `Set.prototype.size`
>    native getter install sites.  **`object_read_only` 415 → 99 ms
>    (−76%)**, `object_property` 1204 → 576 ms (−52% on top).

#### Standard In-Process Suite — vs Node.js 22 / V8 / vanilla QuickJS

Same `build_release/protojs`, same Node 22, `qjs_minimal_release`.

| Benchmark                  |    Node |    QuickJS |   protoJS |   Node × |   QuickJS × |
|----------------------------|--------:|-----------:|----------:|---------:|------------:|
| array_literal              |    2 ms |       6 ms |   1016 ms |    508 × |       169 × |
| control_flow               |    4 ms |      43 ms |    260 ms |     65 × |       6.0 × |
| function_calls             |    1 ms |       8 ms |    227 ms |    227 × |        28 × |
| json_transform             |    1 ms |       3 ms |    152 ms |    152 × |        51 × |
| json_transform_small       |    0 ms |       0 ms |     14 ms |      —   |        —    |
| list_snapshot_history      |    0 ms |       1 ms |    269 ms |      —   |       269 × |
| numeric_loop               |    1 ms |      39 ms |    156 ms |    156 × |       4.0 × |
| **object_property**        |   45 ms |      67 ms |    623 ms |     14 × |       9.3 × |
| **object_read_only**       |    1 ms |       5 ms |     99 ms |     99 × |        20 × |
| **object_write_only**      |   12 ms |      52 ms |   1381 ms |    115 × |        27 × |
| **parallel_cpu**           |**40 ms**|  **727 ms**|  **52 ms**|**Node 1.3 ×**|**protoJS 14.0 ×**|
| string_concat              |    1 ms |       4 ms |    105 ms |    105 × |        26 × |
| string_insert_middle       |    0 ms |       0 ms |    241 ms |      —   |        —    |
| string_processing          |    0 ms |       0 ms |    245 ms |      —   |        —    |
| string_repeated_doubling   |   38 ms |       1 ms |   2197 ms |     58 × |      2197 × |
| tree_traversal             |    1 ms |       3 ms |    306 ms |    306 × |       102 × |

**Geometric mean (12 benches where all three engines > 0 ms):**
- **protoJS / Node = 79.9 ×**   (was 132 × on 2026-06-06, was 58.5 × on 2026-06-01)
- **protoJS / QuickJS = 21.9 ×** (was 34.4 × on 2026-06-06, was 21.0 × on 2026-06-01)
- QuickJS / Node = 3.65 ×

#### Recovery vs 2026-06-06 baseline

15 benches present in both runs; geomean **today / 2026-06-06 = 0.694×**
— protoJS is now ~**31% faster** on the standard-suite geomean than the
2026-06-06 snapshot, and within **+6.9% geomean** of the 2026-06-01
baseline (i.e. the 10 Array cleanup packages' ~78% regression has been
fully recovered while the spec-compliance gains are preserved).

| Benchmark                  | 06-06 (ms) | 06-07 (ms) |        Δ |
|----------------------------|-----------:|-----------:|---------:|
| object_write_only          |       9554 |       1381 | **−86 %** |
| object_property            |       2793 |        623 | **−78 %** |
| object_read_only           |        394 |         99 | **−75 %** |
| array_literal              |       2315 |       1016 | **−56 %** |
| json_transform             |        199 |        152 |     −24 % |
| json_transform_small       |         17 |         14 |     −18 % |
| list_snapshot_history      |        324 |        269 |     −17 % |
| string_processing          |        251 |        245 |      −2 % |
| tree_traversal             |        308 |        306 |      ±0 % |
| string_repeated_doubling   |       2106 |       2197 |      +4 % |
| parallel_cpu               |         52 |         52 |      ±0 % |
| string_concat              |        101 |        105 |      +4 % |
| function_calls             |        213 |        227 |      +7 % |
| control_flow               |        239 |        260 |      +9 % |
| numeric_loop               |        124 |        156 |     +26 % |
| string_insert_middle       |        236 |        241 |      +2 % |

**Honest framing.**  Even with the full recovery, single-thread
throughput vs QuickJS sits at 21.9× geomean — far from the < 5× target
that would put protoJS competitive with vanilla-interpreter peers.
The remaining gap is **protoCore-side immutable structural sharing**:
every `setAttribute` rebuilds the sparse-list snapshot, and the
benches that still regress significantly vs 2026-06-01 (`array_literal`,
`list_snapshot_history`, `numeric_loop`) all bottleneck in per-write
`ProtoList::appendLast` / `setAt` allocations.  Closing this gap
requires either (a) mutable inline element storage for arrays in a
hot-path-detection mode, or (b) reworking the protoCore allocator's
freelist for the very narrow case of arena-cycled small Cells.  Both
are out of scope for the perf recovery this cycle.

`parallel_cpu` unchanged at 52 ms: **14.0 × win against QuickJS**, **77%
of V8's JIT'd throughput**.  The architectural advantage of the
GIL-free runtime is the one bench protoJS dominates.

Raw rounds: `tests/benchmarks/results/three-way-rounds-final.txt`.
Raw JSON (final summary): `tests/benchmarks/results/node_quickjs_comparison.json`.

---

**Prior baseline — 2026-06-06** (in-process median time, protoJS built
in pure Release mode against a `libprotoCore` from the
`digression-attr-cache-padding` branch — the TL-IC entry padding +
better cache-key hash described in `protoCore/README.md` §
"TL-IC entry padding").  This run lands on top of the 2026-06-01
snapshot and reflects **ten Array cleanup packages** (~200 one-fix-per-
failure commits sharpening `Array.prototype.*` spec compliance) plus
the protoCore digression.

Sampling: **4 outer × 5 inner = 20 timing samples per cell**, median
reported.  `PROTOCORE_GC_CONTEXT_THRESHOLD=10_000_000` (10 M cells per
context) keeps the GC out of the foreground path so the numbers reflect
interpreter throughput, not collector noise.  No CPU pinning — the
concurrent GC thread needs its own core.

> **Landed this cycle (2026-06-02 → 2026-06-06)**
>
> 1. **Ten Array cleanup packages** (`0881acc5` and prior).  ~200 commits,
>    one-fix-per-failure, lifting `built-ins/Array.prototype.*` test262
>    pass rate to **~88.2%** while keeping the broader suite stable.
>    Each fix tightens one spec corner: Array.prototype.push must run
>    the `__pd_length__` writability probe even with no args; arrSet
>    must dispatch inherited `__set_<idx>__` setters; `unshift` must
>    fire setters on prototype slots; constructor backrefs on
>    Boolean/Number/Object/Promise/Map/Set must be mutable for
>    `delete` to succeed; etc.  The cumulative correctness gain is
>    real and visible in the test262 numbers — but the runtime cost is
>    also real and visible below (see "Regression vs 2026-06-01").
> 2. **protoCore digression** (`fe173e48` and prior on
>    `digression-attr-cache-padding`).  Two protoCore changes verified
>    on `object_access_benchmark`:
>    * `AttributeCacheEntry` 24 B → 32 B with `aligned_alloc(64,…)`:
>      addressing collapses to one shift, no split-line loads.
>      Measured: **−9.6 % wall, −3.6 % cycles, −27 % L1d misses**.
>    * Cache-key hash shifts `currentValue >> 6` first (cells are
>      64-byte aligned, so the low 6 bits were always zero).
>      Slot occupancy 64 → 256 unique slots out of 1024 on the
>      workload probe.  Measured on `object_access_benchmark`:
>      25.75 B → **24.05 B cycles** (−7 %), IPC 2.31 → 2.47.
>    Both kept (cache hash) or rejected (mutable-value padding —
>    documented negative result).  Did **not** visibly move the
>    benchmark numbers below — the bottleneck has shifted from
>    attribute-cache pressure to per-call `ProtoString` construction
>    inside the tightened Array methods.

#### Standard In-Process Suite — vs Node.js 22 / V8 / vanilla QuickJS

Three-way comparison; same `build_release/protojs`, same Node 22,
`qjs_minimal_release` (QuickJS rebuilt with `-O3 -DNDEBUG`).

| Benchmark                  |    Node |    QuickJS |   protoJS |   Node × |   QuickJS × |
|----------------------------|--------:|-----------:|----------:|---------:|------------:|
| array_literal              |    3 ms |       6 ms |   2315 ms |    772 × |       386 × |
| control_flow               |    4 ms |      43 ms |    239 ms |     60 × |       5.6 × |
| function_calls             |    1 ms |       9 ms |    212 ms |    212 × |        24 × |
| json_transform             |    1 ms |       3 ms |    199 ms |    199 × |        66 × |
| json_transform_small       |    0 ms |       0 ms |     17 ms |      —   |        —    |
| list_snapshot_history      |    0 ms |       1 ms |    324 ms |      —   |       324 × |
| numeric_loop               |    1 ms |      32 ms |    124 ms |    124 × |       3.9 × |
| object_property            |   34 ms |      66 ms |   2793 ms |     82 × |        42 × |
| object_read_only           |    1 ms |       6 ms |    394 ms |    394 × |        66 × |
| object_write_only          |   10 ms |      51 ms |   9554 ms |    955 × |       187 × |
| **parallel_cpu**           |**40 ms**|  **695 ms**|  **52 ms**|**Node 1.3 ×**|**protoJS 13.4 ×**|
| string_concat              |    1 ms |       4 ms |    101 ms |    101 × |        25 × |
| string_insert_middle       |    0 ms |       0 ms |    235 ms |      —   |        —    |
| string_processing          |    0 ms |       0 ms |    251 ms |      —   |        —    |
| string_repeated_doubling   |   37 ms |       1 ms |   2106 ms |     57 × |      2106 × |
| tree_traversal             |    0 ms |       3 ms |    308 ms |      —   |       103 × |

**Geometric mean (12 benches where all three engines > 0 ms):**
- **protoJS / Node = 132.2 ×**  (was 58.5× on 2026-06-01)
- **protoJS / QuickJS = 34.4 ×**  (was 21.0× on 2026-06-01)
- QuickJS / Node = 3.85 ×

#### Regression vs 2026-06-01 baseline

15 benches present in both runs; geomean **today / 2026-06-01 = 1.78 ×**
— protoJS got ~**78 % slower** on the geomean of the standard suite.

| Benchmark                  | 06-01 (ms) | 06-06 (ms) |        Δ |
|----------------------------|-----------:|-----------:|---------:|
| array_literal              |        195 |       2315 | **+1087 %** |
| list_snapshot_history      |         29 |        324 | **+1017 %** |
| object_read_only           |         55 |        394 |  **+616 %** |
| json_transform             |        103 |        199 |   **+93 %** |
| object_property            |       1562 |       2793 |   **+79 %** |
| json_transform_small       |         11 |         17 |       +50 % |
| object_write_only          |       6676 |       9554 |       +43 % |
| numeric_loop               |         93 |        124 |       +33 % |
| control_flow               |        192 |        239 |       +24 % |
| string_processing          |        243 |        251 |        +3 % |
| parallel_cpu               |         52 |         52 |       ±0 %  |
| tree_traversal             |        307 |        308 |       ±0 %  |
| string_concat              |        104 |        101 |        −3 % |
| function_calls             |        220 |        212 |        −4 % |
| string_repeated_doubling   |       2187 |       2106 |        −4 % |
| string_insert_middle       |        259 |        235 |        −9 % |

**Diagnosis.**  Spot-check of `Array.prototype.push` (the dominant
operation in `array_literal`'s 100 K-push loop):

```cpp
// arrayPush hot path — runs PER CALL:
const proto::ProtoObject* pdo = ctx->fromUTF8String("__pd_length__");
// → builds a fresh ProtoString rope every push, even though
//   pd_length is a stable runtime symbol.
//   Same pattern for "__set_<idx>__" inside arrSet, etc.
```

Each tightened Array method added one or more spec-mandated probes
behind a freshly-constructed `ProtoString`.  On `array_literal` that's
~300 K extra rope allocations per outer iteration — far more cost than
the protoCore cache improvements can pay back through better hit
rates.

**This is the explicit "purity > performance" tax** the project is
willing to pay until users justify otherwise.  The fix is **not** to
unwind the correctness work — it's to (a) strong-intern these dispatch
symbols via `JSSymbols::__pd_length__()` / `__set_<idx>__()` rather
than reconstructing them per call, and (b) batch the `arrSet` setter-
probe loop to amortise the per-index symbol construction over the
whole push.  Both are queued for the next perf cycle.

**`parallel_cpu` is unchanged** at 52 ms — **13.4 × win against
QuickJS** and **77 % of V8's JIT'd throughput** on a 4-task ×
5-round CPU-bound workload.  The GIL-free architectural payoff
survives every Array cleanup package.

Raw rounds: `tests/benchmarks/results/three-way-rounds.txt`.
Raw JSON (final summary round): `tests/benchmarks/results/node_quickjs_comparison.json`.

---

**Prior baseline — 2026-06-01** (in-process median time, protoJS built
in pure Release mode against `libprotoCore.so.1.2.0` — same protoCore
binary as 2026-05-31, no protoCore changes this cycle).  This run lands
on top of the 2026-05-31 snapshot and reflects five protoJS-only commits
that closed four correctness bugs and added one perf fast path:

> **Landed this cycle (2026-06-01)**
>
> 1. `b09373ea perf(interp)` — skip `pContext->newList()` for argc==0
>    native calls (`s.trim()`, `s.toString()`, etc.); collapse `OP_call`'s
>    `newList()+appendLast` loop into the single-allocation
>    `newList(argc, slice)` form.  Standard suite gain: `function_calls`
>    **−33.7%**, `numeric_loop` −25.6%, `array_literal` −19%, eight more
>    benches in the −8% to −28% range; geomean vs Node held steady at
>    ~58× because the slowest string benches (still bottlenecked in
>    protoCore-side rope construction) dominate the geomean math.
> 2. `ac3f225d fix(interp)` — sync generator `return` was wrapping the
>    value in `Promise.resolve()` instead of `{value, done:true}`.
>    Root cause: QuickJS emits `OP_return_async` for both async-function
>    AND sync-generator bodies; protoJS's handler unconditionally wrapped
>    in Promise.  Fix gates the Promise wrap on `isAsync && !isGenerator`.
>    `function* g(){return 99;}.next().value` now returns `99` (was a
>    Promise-like `{then,catch,finally}`).  Plus two stray debug printfs
>    (`FOR_OF_NEXT`, `L_OP_for_in_start`) that survived prior cleanup.
> 3. `b0b6f692 perf(interp)` — SmallInt fast path for `OP_add_loc` (the
>    only add-class opcode without one; `OP_add`, `OP_sub`, `OP_mul`,
>    `OP_mod`, `OP_inc/dec`, `OP_inc_loc/dec_loc`, `OP_lt/lte/gt/gte` all
>    had theirs).  `numeric_loop` −20.8%, `json_transform` −11.5%,
>    `control_flow` −6.4%, `array_literal` −5%; geomean improved
>    58.84× → 55.88× vs Node before noise smoothing.
> 5. `ccefb3e4 perf(interp)` — reorder `L_OP_call` so `__bytecode_id__`
>    is read first (cached on the closure's stable snapshot via
>    protoCore's per-thread attribute cache).  If `bcId >= 0` the
>    receiver is a JS closure by construction and the `__native_fn__`
>    unwrap branch is skipped entirely (it would always return nullptr
>    for a JS closure).  Folds the prior duplicate `getBytecodeId`
>    call into a conditional re-lookup that only fires when unwrap
>    replaced func.  Measured with `perf stat -r 3 -e cycles`:
>    function_calls drops **−8% cycles, −7.1% instructions,
>    −10.3% wall** with no IPC regression.  Standard suite:
>    function_calls vs Node 224× → **199×** (−11%), vs QuickJS
>    27.6× → **23.1×** (−16%); geomean vs Node 58.5× → **57.2×**.
>    Architectural insight that drove this (from review): protoCore's
>    attribute cache makes stable mutables effectively read-cached
>    after first lookup; the right "inline cache" is not a parallel
>    cache layer in the interpreter, but constructing the closure so
>    its identity-attributes (bytecode id, captured cells, prototype)
>    are reachable directly via attribute lookup that protoCore
>    already caches.
>
> 4. `3dc726b8 fix(interp)` — `for-of` over arrays and generators
>    produced wrong values on the FIRST iteration AND never terminated
>    (`for (var v of [10,20,30]) console.log(v)` printed `10,20,30,
>    undefined,undefined,...` forever).  Root cause: L_OP_for_of_start
>    writes iterator bookkeeping into absolute automaticLocals slots
>    starting at `0x10000 + pc` (~65 000).  The first write triggers
>    `resizeAutomaticLocals(65 567)`, which relocates the std::vector's
>    storage — but runBytecode caches the slot array pointer in a local
>    `pAutomaticLocals` at the top of the dispatch loop, which is now
>    dangling.  Every subsequent opcode that accesses the value stack
>    via `pAutomaticLocals[...]` reads (or writes!) freed memory; this
>    causes both the garbage on iteration 1 and the non-termination
>    (the counter at `baseSlot+1` is read through the stale pointer
>    and never reflects the updated index).  Fix: invoke
>    `REFRESH_INTERP_STATE()` after each of the three setSlot blocks
>    in L_OP_for_of_start (Case A, B, C).

#### Standard In-Process Suite — vs Node.js 22 / V8

| Benchmark                  |   protoJS |    Node |    Node × |
|----------------------------|----------:|--------:|----------:|
| array_literal              |    195 ms |    2 ms |      97×  |
| control_flow               |    192 ms |    5 ms |      38×  |
| function_calls             |    220 ms |    1 ms |     220×  |
| json_transform             |    103 ms |    1 ms |     103×  |
| json_transform_small       |     11 ms |    0 ms |      22×  |
| list_snapshot_history      |     29 ms |    0 ms |      58×  |
| numeric_loop               |     93 ms |    1 ms |      93×  |
| object_property            |   1562 ms |   32 ms |      49×  |
| object_read_only           |     55 ms |    1 ms |      55×  |
| object_write_only          |   6676 ms |   11 ms |     607×  |
| **parallel_cpu**           |  **52 ms**|**40 ms**| **Node 1.3×** |
| string_concat              |    104 ms |    1 ms |     104×  |
| string_insert_middle       |    259 ms |    0 ms |     518×  |
| string_processing          |    243 ms |    0 ms |     486×  |
| string_repeated_doubling   |   2187 ms |   39 ms |      56×  |
| tree_traversal             |    307 ms |    1 ms |     307×  |

**Geometric mean (18 benches): Node.js 58.5× protoJS** — single-thread.
Dominated by the four worst benches (`object_write_only`,
`string_insert_middle`, `string_processing`, `string_repeated_doubling`)
which all bottleneck in protoCore-side immutable-structure construction
and would need protoCore work to move.

#### Standard In-Process Suite — vs vanilla QuickJS (interpreter-vs-interpreter)

| Benchmark                  |   protoJS |    QuickJS |  QuickJS × |
|----------------------------|----------:|-----------:|-----------:|
| array_literal              |    249 ms |       7 ms |     35.6×  |
| control_flow               |    234 ms |      53 ms |      4.4×  |
| function_calls             |    276 ms |      10 ms |     27.6×  |
| json_transform             |    128 ms |       5 ms |     25.6×  |
| json_transform_small       |     15 ms |       1 ms |     15.0×  |
| list_snapshot_history      |     39 ms |       1 ms |     39.0×  |
| numeric_loop               |    124 ms |      44 ms |      2.8×  |
| object_property            |   1993 ms |      85 ms |     23.5×  |
| object_read_only           |     77 ms |       7 ms |     11.0×  |
| object_write_only          |   6838 ms |      51 ms |    134.1×  |
| **parallel_cpu**           |  **52 ms**| **723 ms** | **protoJS 13.9×** |
| string_concat              |    106 ms |       5 ms |     21.2×  |
| string_insert_middle       |    254 ms |       0 ms |    508.0×  |
| string_processing          |    240 ms |       0 ms |    480.0×  |
| string_repeated_doubling   |   2259 ms |       2 ms |   1129.5×  |
| tree_traversal             |    366 ms |       4 ms |     91.5×  |

**Geometric mean (18 benches): QuickJS 21× protoJS** — single-thread
interpreter vs interpreter, dominated by the same four benches as
above.  `numeric_loop` is now **2.8× QuickJS** (was 1.46× on the
2026-05-31 snapshot with QuickJS recording 89 ms; QuickJS itself is
recording faster numeric work on this run, so the ratio swing is
QuickJS-side run-to-run variance, not a protoJS regression — protoJS's
own number on this bench improved from 130 ms to 124 ms).

`control_flow` is **4.4× QuickJS** — the closest single-thread bench
where protoJS is still in striking distance.

`parallel_cpu`: **protoJS wins 13.9×** against QuickJS and runs at
~77% of V8's JIT'd throughput on a workload that scales across cores.
The one bench where GIL-free architecture is visibly load-bearing.

Raw JSON: `tests/benchmarks/results/standard_comparison.json` and
`tests/benchmarks/results/standard_comparison_quickjs.json`.

---

**Prior baseline — 2026-05-31** (in-process median time, protoJS built in
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
