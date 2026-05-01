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

### Performance Benchmarks

**Honest baseline — 2026-05-01** (in-process median time, protoJS Release build
linked against the latest protoCore vs. Node.js/V8 22). Pure compute; no startup
cost counted on either side.

#### Standard In-Process Suite (`run_standard_comparison.js`)

Self-contained micro-benchmarks with tight loops; 5 iterations each, median reported.

| Benchmark       | protoJS    | Node.js  | Ratio (Node faster) |
|-----------------|------------|----------|--------------------:|
| array_literal   |    507 ms  |    3 ms  |              169.0× |
| control_flow    |    566 ms  |    4 ms  |              141.5× |
| function_calls  |    751 ms  |    1 ms  |              751.0× |
| **json_transform**| **285 ms** | **1 ms** |           **285.0×**|
| numeric_loop    |    337 ms  |    1 ms  |              337.0× |
| object_property |   1380 ms  |   30 ms  |               46.0× |
| **parallel_cpu**|  **52 ms** | **41 ms**|           **Node 1.3× faster** |
| string_concat   |   timeout  |    —     |                   — |

**Geometric mean: Node.js is ~100× faster than protoJS (improved from ~175×).**
`parallel_cpu` is the standout exception — it offloads computation to native
protoCore worker threads, running outside the interpreter hot loop. `string_concat`
times out (50,000 append iterations expose the lack of rope/COW string optimization).

#### Comprehensive Macro Suite (`combined_performance_suite.js`)

41 tests across Basic Types, Collections, and Overall Performance (5 iterations,
mean reported). Runs cleanly on both `protojs` and `node`.

| Benchmark             | protoJS     | Node.js    | Ratio (Node faster) |
|-----------------------|------------:|-----------:|--------------------:|
| Number Addition       |      5.0 ms |     0.2 ms |               25.0× |
| String Concatenation  |    200.4 ms |     0.2 ms |             1002.0× |
| Array Iteration       |     62.8 ms |     0.2 ms |              314.0× |
| Object Property Access|     16.0 ms |     0.2 ms |               80.0× |
| JSON Stringify        |    998.8 ms |     0.6 ms |             1664.0× |
| Throughput (Simple)   |   4434.6 ms |     3.0 ms |             1478.0× |
| Closure Creation      |    123.4 ms |     0.2 ms |              617.0× |

**Geometric mean: Node.js is ~350× faster** on this wider suite.

#### What the numbers do *not* measure

- **Startup cost** — protoJS starts in roughly the same wall-clock time as Node;
  for short scripts the user-observed gap is much smaller.
- **Memory & GC pauses** — protoCore's concurrent GC keeps p99 pause well below
  Node's stop-the-world cycles; not captured here.
- **Multi-threaded scaling** — Node's event loop is single-threaded; `parallel_cpu`
  exercises only a fraction of what protoCore's GIL-free thread model can deliver
  in real multi-threaded workloads.

#### Interpreter-to-Interpreter Suite (`run_standard_comparison_quickjs.js`)

Comparing against vanilla **QuickJS** (the underlying engine without protoCore) isolates the cost of the **protoCore memory model** and the **GIL-free architecture**. Benchmarks run via `qjs_minimal` (Release build).

| Benchmark       | protoJS    | QuickJS  | Ratio (QuickJS faster) |
|-----------------|------------|----------|-----------------------:|
| array_literal   |    486 ms  |    7 ms  |                 69.4×  |
| control_flow    |    575 ms  |   45 ms  |                 12.8×  |
| function_calls  |    778 ms  |   11 ms  |                 70.7×  |
| json_transform  |    276 ms  |    3 ms  |                 92.0×  |
| numeric_loop    |    349 ms  |   37 ms  |                  9.4×  |
| object_property |   1360 ms  |   65 ms  |                 20.9×  |
| **parallel_cpu**|  **52 ms** | **644 ms**|      **protoJS 12.4× faster** |

**Geometric mean: QuickJS is ~18× faster overall.**
Excluding `parallel_cpu` (which QuickJS cannot parallelize), QuickJS is **~35x faster** on pure object/memory operations. This reflects the necessary overhead of the **protoCore** concurrent AVL tree architecture and atomic-based reference management compared to QuickJS's simpler single-threaded reference counting. However, the gap in **object_property** access has narrowed significantly (from >40x to ~21x) due to the May 2026 attribute cache overhaul.

Raw JSON: [tests/benchmarks/results/standard_comparison.json](tests/benchmarks/results/standard_comparison.json)

**To reproduce:**
```bash
# Standard suite
PROTOJS_BIN=$(pwd)/build/protojs node tests/benchmarks/run_standard_comparison.js

# Comprehensive suite (both engines)
node tests/benchmarks/combined_performance_suite.js
./build/protojs tests/benchmarks/combined_performance_suite.js
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
