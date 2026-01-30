# protoJS

**A modern JavaScript runtime based on protoCore**

[![Language](https://img.shields.io/badge/Language-C%2B%2B20-blue.svg)](https://isocpp.org/)
[![Build System](https://img.shields.io/badge/Build-CMake-green.svg)](https://cmake.org/)
[![Status](https://img.shields.io/badge/Status-Phase%206%20Complete-green.svg)]()
[![License](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

Copyright (c) 2026 Gustavo Marino <gamarino@gmail.com>

protoJS is a JavaScript runtime that uses **protoCore** (https://github.com/gamarino/protoCore)as the foundation for internal object representation, memory management, and concurrency. It uses **QuickJS** as a parser and compiler, but completely replaces the QuickJS runtime with protoCore, leveraging its unique features of immutability, GIL-free concurrency, and efficiency.

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
- **No GIL**: Real concurrency without Global Interpreter Lock
- **Efficient GC**: Concurrent Garbage Collector from protoCore

---

## 📋 Requirements

- **C++20** compatible compiler (GCC 10+, Clang 12+)
- **CMake** 3.16+
- **protoCore** (official name of the shared library; must be built and available as `libprotoCore.so` / `libprotoCore.dylib` / `protoCore.dll`)
- **pthread** (for concurrency)

---

## 🚀 Building

protoJS links against the **protoCore shared library** (official name: **protoCore**). Build protoCore first, then protoJS.

```bash
# 1. Build protoCore shared library (official name: protoCore)
cd ../protoCore
cmake -B build -S .
cmake --build build --target protoCore

# 2. Build protoJS (finds libprotoCore.so / libprotoCore.dylib / protoCore.dll)
cd ../protoJS
mkdir -p build && cd build
cmake ..
make
```

If protoCore was built in a different directory (e.g. `build_check`), CMake will search for the shared library there automatically.

---

## 💻 Basic Usage

### Run a script

```bash
./protojs script.js
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

## 📚 Documentation

### Main Documentation

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

- **[docs/BENCHMARK_ANALYSIS.md](docs/BENCHMARK_ANALYSIS.md)** - **Node.js comparison benchmark analysis** (19.83x faster overall)
- **[docs/PERFORMANCE_REPORT.md](docs/PERFORMANCE_REPORT.md)** - Performance benchmarks and results
- **[docs/PERFORMANCE_TESTING.md](docs/PERFORMANCE_TESTING.md)** - Performance testing methodology

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

protoJS demonstrates significant performance advantages over Node.js:

- **Overall Performance:** 19.83x faster than Node.js
- **Array Operations:** 34.18x faster (immutable operations with structural sharing)
- **Concurrent Operations:** 3.00x faster (GIL-free architecture)
- **Consistent Improvements:** 2.67x to 34.18x speedup across all tested categories

For detailed benchmark analysis and results, see [docs/BENCHMARK_ANALYSIS.md](docs/BENCHMARK_ANALYSIS.md).

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

**Goal:** Production-ready developer tools and advanced networking capabilities.

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
- ✅ **Module system** (CommonJS require, ES Modules import/export, Module interop; **require** loads JS or native addons (.node/.so/.protojs) transparently)
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

## 📧 Contact

[To be defined]

---

## ⚠️ Important Note

**This project is in active development (Phase 6 Complete - Ecosystem & Compatibility).**

- Phase 6 complete: Extended npm support, performance benchmarking, and Node.js test suite compatibility
- **Performance:** 19.83x faster than Node.js overall, with 34.18x speedup in array operations
- Core modules functional: fs, path, http, stream, events, util, crypto, url, buffer, net
- Advanced modules: worker_threads, cluster, dgram, child_process, dns
- Developer tools: Memory Analyzer, Visual Profiler, Integrated Debugger with Chrome DevTools Protocol
- npm support: Full registry communication, semver version resolution, package installation
- Benchmarking: Comprehensive benchmarking framework with Node.js comparison ([see analysis](docs/BENCHMARK_ANALYSIS.md))
- Test compatibility: Node.js test suite compatibility checker
- Module system working: CommonJS and ES Modules supported
- CLI tools available: REPL and Node.js-compatible flags
- Ready for Phase 7: Advanced features and optimizations
- API may change in future phases
- Recommended for development and production use

---

## 🔗 Related Links

- [protoCore](../protoCore/) - Runtime foundation
- [QuickJS](https://bellard.org/quickjs/) - JavaScript parser
