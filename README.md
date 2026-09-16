# protoJS

**A JavaScript runtime built on protoCore**

[![Language](https://img.shields.io/badge/Language-C%2B%2B20-blue.svg)](https://isocpp.org/)
[![Build System](https://img.shields.io/badge/Build-CMake-green.svg)](https://cmake.org/)
[![Status](https://img.shields.io/badge/Status-not%20production%20ready-orange.svg)](#-current-status)
[![License](https://img.shields.io/badge/License-MIT-blue.svg)](LICENSE)

protoJS is a JavaScript runtime that uses [protoCore](https://github.com/numaes/protoCore) for object representation, memory management and concurrency. It uses [QuickJS](https://bellard.org/quickjs/) only as a parser and bytecode compiler: the bytecode runs on a protoCore-native interpreter, so JavaScript values are protoCore objects, collections use protoCore's immutable, structurally shared structures, and threads run without a global interpreter lock.

> [!WARNING]
> protoJS is **not production ready**. It is open for community review: architectural feedback, edge cases and performance critiques are welcome through [GitHub issues](https://github.com/gamarino/protoJS/issues).

---

## Ecosystem

Four language runtimes (protoJS, protoPython, protoST, protoClojure) and protoCpp's C++ examples are built on protoCore.

| Project | Role | Repository |
|---|---|---|
| protoCore | C++20 object model and runtime kernel: immutable structures, concurrent GC, GIL-free threads | https://github.com/numaes/protoCore |
| protoJS | JavaScript runtime on protoCore | https://github.com/gamarino/protoJS |
| protoPython | Python 3 runtime (protopy) and ahead-of-time compiler (protopyc) on protoCore | https://github.com/gamarino/protoPython |
| protoST | Smalltalk-inspired actor language on protoCore | https://github.com/gamarino/protoST |
| protoClojure | Clojure dialect on protoCore (early stage) | https://github.com/gamarino/protoClojure |
| protoCpp | Examples and benchmarks using protoCore directly from C++ | https://github.com/gamarino/protoCpp |

---

## 🎯 Key Features

- **protoCore execution path.** QuickJS (vendored in `deps/quickjs/`) compiles source to bytecode; `src/runtime/` loads that bytecode and executes it on a protoCore-native interpreter. There is no QuickJS interpreter fallback (see [src/runtime/README.md](src/runtime/README.md)).
- **protoCore data structures.** Array elements are stored in protoCore's immutable `ProtoList` and updated by structural sharing; strings are protoCore ropes.
- **Native threads.** `protoCore.runInThread` runs a registered C++ worker on a new protoCore thread that shares the same object space, without serialising arguments or results.
- **Garbage collection** is provided by protoCore's collector.
- **Node.js-style modules** (`fs`, `path`, `http`, `net`, `stream`, `events`, `crypto`, `worker_threads`, and others) and developer tools (memory analyzer, profiler, Chrome DevTools Protocol debugger) implemented in C++.
- **Command-line interface** with Node.js-style flags and an interactive REPL.

---

## 📋 Requirements

- A **C++20** compiler (the build uses GCC/Clang options such as `-rdynamic`)
- **CMake** 3.16 or later
- The **protoCore** shared library (`libprotoCore`), built from source or installed under a prefix
- **OpenSSL** (`libssl`, `libcrypto`), **pthread** and **libdl**, which `protojs` links against
- For the unit tests: **Catch2** v3; if CMake does not find it, the build downloads v3.5.2 with `FetchContent`

---

## 📦 Installation

No prebuilt protoJS packages are published. Build protoJS from source as described in [Building](#-building); you can then run the binary from the build tree or install it.

**Install to a prefix.** The install rule places `protojs` in `<prefix>/bin` (CMake's default prefix is `/usr/local`) with an RPATH of `$ORIGIN/../<libdir>`, so libprotoCore is found when it is installed under the same prefix:

```bash
cmake --install build                      # default prefix
cmake --install build --prefix "$HOME/.local"
```

**Build your own packages.** `CMakeLists.txt` configures CPack for the package `protojs`, version 0.1.0:

| Platform | CPack generators | Dependency declared |
|---|---|---|
| Linux | `DEB`, `RPM`, `TGZ` | DEB depends on `protocore`; RPM requires `protoCore` |
| macOS | `DragNDrop` | — |
| Windows | `NSIS`, `ZIP` | — |

Run `cpack` in the build directory (or `cmake --build build --target package`). With CPack's default file naming, the Linux packages are `protojs-0.1.0-Linux.deb`, `protojs-0.1.0-Linux.rpm` and `protojs-0.1.0-Linux.tar.gz`.

See [docs/INSTALLATION.md](docs/INSTALLATION.md) for details and [packaging/PROCEDURES.md](packaging/PROCEDURES.md) for packaging procedures.

---

## 🚀 Building

protoJS links against the protoCore shared library, so build protoCore first. When `PROTO_CORE_PREFIX` is not set, CMake looks for the library in `../protoCore/build_release`, then `../protoCore/build`, then `../protoCore/build_check`, and uses the first of those that holds it — so clone both repositories side by side:

```bash
git clone https://github.com/numaes/protoCore.git
git clone https://github.com/gamarino/protoJS.git

# 1. Build the protoCore shared library
cmake -S protoCore -B protoCore/build
cmake --build protoCore/build --target protoCore

# 2. Build protoJS (the default build type is Release)
cmake -S protoJS -B protoJS/build
cmake --build protoJS/build
```

When protoCore comes from a sibling build directory, the build-tree RPATH points at it, so `./build/protojs` runs without `LD_LIBRARY_PATH` or `DYLD_LIBRARY_PATH`.

Useful configuration options:

| Option | Effect |
|---|---|
| `-DPROTO_CORE_PREFIX=<prefix>` | Use an installed protoCore (`<prefix>/lib` or `lib64`, and `<prefix>/include/protoCore.h`) instead of the sibling checkout |
| `-DBUILD_TESTING=OFF` | Skip the unit-test executable and the Catch2 dependency |
| `-DENABLE_COVERAGE=ON` | Compile with coverage instrumentation |

---

## 💻 Basic Usage

### Running code

```bash
./build/protojs script.js                    # run a script
./build/protojs -e "console.log('Hello')"    # evaluate inline code
./build/protojs -p -e "6 * 7"                # evaluate and print the result
./build/protojs --cpu-threads 4              # options without a script: start the REPL
```

### Command-line options

All options are parsed by `src/main.cpp`.

| Option | Meaning |
|---|---|
| `<file>` | Script to run. Every argument that does not start with `-` is read as the script file, so extra positional arguments for the script are not supported. |
| `-e "code"` | Evaluate `code` instead of a file. |
| `-p`, `--print` | Print the result of the evaluation when it is not `undefined`. |
| `-c`, `--check` | Parse the input without executing it. A file that parses prints nothing and exits 0; otherwise the syntax error is printed and the status is 1. Combining it with `-e` exits with status 9. With `--input-type=module` the module itself is parsed, but its imports cannot be resolved: any `import` is reported as `ReferenceError: could not load module`, so the check is limited to modules without imports. |
| `-v`, `--version` | Print `protoJS v0.1.0` and exit. |
| `--input-type=module` | Compile the input as an ES module. |
| `--cpu-threads N` | Size of the CPU thread pool (default: the number of hardware threads). |
| `--io-threads N` | Size of the I/O thread pool (default: hardware threads × the I/O factor, rounded up). |
| `--io-threads-factor F` | I/O factor used when `--io-threads` is not given (default: 3.0). |
| `--preload file.js` | Evaluate `file.js` as a script before the main input (may be repeated). protojs exits with status 1 if the file cannot be read or throws. |
| `--minimal` | Install only `console`, `JSON`, the timing APIs, `Deferred`, `protoCore`, `__filename` and `__dirname`; no `process`, `io`, `require` or Node.js-style modules. protojs exits right after the evaluation without running the event loop. Intended to isolate compiler and interpreter problems. |
| `--proto-eval` | Deprecated. Accepted for compatibility; it has no effect because the protoCore interpreter is always used. |

Without arguments, protojs prints its usage and exits with status 1. When options are given but no script, `-e` or `-c`, it starts the REPL: the prompt is `> `, incomplete input continues on a `... ` prompt, and `.help` and `.exit` (or `.quit`) are available.

After the main evaluation, protojs keeps processing the event loop while there are pending callbacks, `Deferred` instances, worker threads, HTTP servers or clients, or `net` handles, for up to 180 seconds. The exit status is 1 if the main evaluation threw and 0 otherwise.

### Examples

Globals and asynchronous work:

```javascript
console.log("Hello from protoJS!");

// Deferred: the function runs on a later turn of the event loop;
// its return value fulfils the Deferred and an exception rejects it.
new Deferred(() => 6 * 7)
    .then((value) => console.log("Deferred result:", value))
    .catch((error) => console.log("Deferred failed:", error));

// protoCore.runInThread: run a native C++ worker registered by name
// (currently "cpuChunk") on a new protoCore thread.
protoCore.runInThread("cpuChunk", [200000])
    .then((sum) => console.log("cpuChunk result:", sum));
```

Process information and file I/O (`platform`, `arch` and `cwd` are methods in protoJS):

```javascript
console.log("Arguments:", process.argv);
console.log("Home:", process.env.HOME);
console.log("Platform:", process.platform(), "Arch:", process.arch());
console.log("CWD:", process.cwd());

io.writeFile("output.txt", "Hello, protoJS!");
console.log(io.readFile("output.txt"));
```

Developer tools. The modules are installed as the globals `memory`, `profiler` and `debugger`; because `debugger` is a reserved word, read it through `globalThis`:

```javascript
// Memory analyzer: snapshots are identified by the order in which they were taken.
memory.takeHeapSnapshot();            // snapshot 0
// ... run code ...
memory.takeHeapSnapshot();            // snapshot 1
console.log(memory.detectLeaks(0, 1));

// Profiler
profiler.startProfiling();
// ... run code ...
profiler.stopProfiling();
profiler.exportProfile("profile.json");      // Chrome DevTools format
profiler.generateHTMLReport("profile.html");

// Chrome DevTools Protocol debugger
const dbg = globalThis["debugger"];
dbg.startCDPServer(9229);
dbg.setBreakpoint("script.js", 10);          // script, line[, column]
```

For more examples, see [docs/EXAMPLES.md](docs/EXAMPLES.md). For thread pool configuration, see [docs/THREAD_POOLS.md](docs/THREAD_POOLS.md).

---

## 🏗️ Architecture

```
JavaScript source
    ↓
QuickJS parser and compiler (bytecode only)
    ↓
protoJS runtime (src/runtime/)
    ├── ProtoBytecodeLoader   bytecode → ProtoBytecodeModule
    ├── ProtoInterpreter      executes the bytecode on protoCore objects
    └── Built-in prototypes, modules, TypeBridge, GCBridge
    ↓
protoCore
    ├── ProtoSpace            object space and garbage collector
    ├── ProtoContext          per-call execution context
    └── Threads               native threads sharing one object space
```

For details, see [ARCHITECTURE.md](ARCHITECTURE.md) and [src/runtime/README.md](src/runtime/README.md).

---

## 📊 Conformance and Performance

### Test262 Conformance

- **Full suite.** The last full Test262 run recorded in [docs/TEST262_STATUS.md](docs/TEST262_STATUS.md) is dated **2026-06-01** and covers the `language` and `built-ins` directories (46 963 tests): **28 830 passed, 61.39 %**. The runner counts a test as passed when protojs exits without an error, so a test that stops at an unsupported opcode before reaching its assertions can count as a pass (see the methodology notes in that document).
- **Subset.** The last archived per-family measurement, dated **2026-06-13**, covers **18 `built-ins` families** (Array, Boolean, Date, Error, Function, JSON, Map, Math, Number, Object, Promise, Proxy, Reflect, Set, String, Symbol, WeakMap, WeakSet; 11 784 tests): **10 923 passed, 92.69 %**. This is a subset figure, not full-suite conformance.

The detailed history is in [docs/archive/TEST262_ROUNDS.md](docs/archive/TEST262_ROUNDS.md).

### Performance Benchmarks

protoJS executes bytecode in an interpreter; there is no JIT compiler. The newest dated reading in [docs/archive/PERFORMANCE_LOG.md](docs/archive/PERFORMANCE_LOG.md) was recorded on **2026-06-16** with the standard in-process suite ([tests/benchmarks/standard/](tests/benchmarks/standard/)). It compares `protojs` (Release build) with Node.js 22.17.0 and with QuickJS rebuilt with `-O3 -DNDEBUG`. Times are each benchmark's own in-process measurement (median of 3 outer runs, each averaging 5 inner iterations), reported in whole milliseconds.

| Comparison (geometric mean, 11 single-thread benchmarks) | protoJS time relative to the baseline |
|---|---|
| protoJS / QuickJS | 15.2× slower |
| protoJS / Node.js | about 95× slower |

In the same reading, `string_repeated_doubling` (repeated `s = s + s`) took 1 ms in protoJS and QuickJS against 40 ms in Node.js; protoCore's rope strings avoid copying the whole string on each concatenation. The `parallel_cpu` benchmark now runs the same workload (2e6 iterations per task) in every runtime, but it is still not a like-for-like comparison: under protoJS the work runs in the native C++ `cpuChunk` worker on four protoCore threads, while Node.js and QuickJS run the JavaScript loop sequentially. Its results are therefore not used here.

To reproduce, build protoJS and run `node tests/benchmarks/run_standard_comparison.js` (against Node.js) or `node tests/benchmarks/run_standard_comparison_quickjs.js` (against QuickJS); both accept a `PROTOJS_BIN` environment variable pointing at the binary. Raw results are in [tests/benchmarks/results/](tests/benchmarks/results/).

---

## 🔬 Current Status

**Version:** 0.1.0. protoJS is **not production ready**, and its APIs may change.

**Available:**

- Script, inline and ES module evaluation on the protoCore interpreter; the REPL.
- Globals installed on the protoCore-native global object: `console`, `JSON`, `globalThis`, `Deferred`, `protoCore` (`Set`, `Multiset`, `SparseList`, `Tuple`, `ImmutableObject`, `MutableObject`, `isImmutable`, `makeImmutable`, `makeMutable`, `runInThread`), `process`, `io`, `require`, `fs`, `path`, `url`, `http`, `events`, `stream`, `util`, `crypto` (linked against OpenSSL), `Buffer`, `net`, `worker_threads`, `cluster`, `dgram`, `child_process`, `dns`, `memory`, `profiler` and `debugger`. These modules are native C++ implementations; their coverage of the Node.js API varies and has not been measured.
- A CommonJS `require()` loader for JavaScript files and native addons (`.node`, `.so`/`.dylib`/`.dll`, `.protojs`); see [docs/NATIVE_MODULES.md](docs/NATIVE_MODULES.md).
- C++ unit tests (Catch2) for the thread pools, event loop, npm registry client, semver handling, benchmark runner and Node.js test runner.

**Known gaps:**

- Test262 conformance is 61.39 % on the last full run (see [Test262 Conformance](#test262-conformance)). The remaining failures recorded on 2026-06-13 include insertion-order tracking for attribute storage, real `eval()` execution, the `$262` cross-realm harness, source text of generator and async functions for `Function.prototype.toString`, and resizable `ArrayBuffer` and `SuppressedError` subclassing.
- `require()` of a **relative JavaScript file** does not run the module body: its exports come back empty. Requiring a built-in name works (`require('fs') === fs`); requiring your own `./module.js` does not.
- npm registry and semver components exist in `src/npm/`, but the `protojs` command line has no package-management command.
- The interpreter is 15.2× slower than QuickJS and about 95× slower than Node.js on the benchmark reading above.

---

## 🗺️ Roadmap

Open work documented in the repository:

- Close the Test262 gaps listed above; the per-family detail is in [docs/TEST262_STATUS.md](docs/TEST262_STATUS.md) and [docs/archive/TEST262_ROUNDS.md](docs/archive/TEST262_ROUNDS.md).
- Continue moving QuickJS-side bindings onto the protoCore-native global ([docs/MIGRATION_QUICKJS_TO_PROTOCORE.md](docs/MIGRATION_QUICKJS_TO_PROTOCORE.md)).
- Reduce interpreter overhead measured by the standard benchmark suite.

The original implementation plan is kept for reference in [docs/archive/PLAN.md](docs/archive/PLAN.md).

---

## 📚 Documentation

Full index: **[docs/README.md](docs/README.md)** — user guides, contributor documentation and the archive.

### User Guides

- **[docs/INSTALLATION.md](docs/INSTALLATION.md)** - Installation: building from source, installing and packaging
- **[docs/API_REFERENCE.md](docs/API_REFERENCE.md)** - API reference
- **[docs/EXAMPLES.md](docs/EXAMPLES.md)** - Advanced examples
- **[docs/DEFERRED_USAGE.md](docs/DEFERRED_USAGE.md)** - Deferred usage guide
- **[docs/PROTOCORE_MODULE.md](docs/PROTOCORE_MODULE.md)** - protoCore module guide
- **[docs/NATIVE_MODULES.md](docs/NATIVE_MODULES.md)** - Native addon modules (C++ shared libraries)
- **[docs/THREAD_POOLS.md](docs/THREAD_POOLS.md)** - Thread pool configuration
- **[docs/TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md)** - Solutions to common problems

### Contributor Documentation

- **[ARCHITECTURE.md](ARCHITECTURE.md)** - Technical architecture
- **[TESTING_STRATEGY.md](TESTING_STRATEGY.md)** - Testing strategy
- **[tests/README.md](tests/README.md)** - Running each test layer
- **[docs/TEST262_STATUS.md](docs/TEST262_STATUS.md)** - Latest full Test262 run
- **[CHANGELOG.md](CHANGELOG.md)** - Notable changes

### Conformance and Performance History

- **[docs/archive/TEST262_ROUNDS.md](docs/archive/TEST262_ROUNDS.md)** - Dated Test262 measurements (archived)
- **[docs/archive/PERFORMANCE_LOG.md](docs/archive/PERFORMANCE_LOG.md)** - Dated benchmark readings (archived)

---

## 🧪 Testing

Run the C++ unit tests from the build directory:

```bash
cd build
ctest --output-on-failure
```

Run an integration script or a benchmark directly:

```bash
./build/protojs tests/integration/basic/hello_world.js
./build/protojs tests/benchmarks/array_operations.js
```

For the other test layers (smoke, Test262, integration, conformity), see [tests/README.md](tests/README.md) and [TESTING_STRATEGY.md](TESTING_STRATEGY.md). Test262 run instructions are in [docs/TEST262_STATUS.md](docs/TEST262_STATUS.md).

---

## 🤝 Contributing

Contributions are welcome, especially:

- Fixes for Test262 failures
- Tests and documentation
- Interpreter performance work
- Bug reports and fixes

Please open an issue or a pull request on [GitHub](https://github.com/gamarino/protoJS).

---

## The Swarm of One

protoJS is designed and maintained by a single architect, Gustavo Marino, working with AI coding agents that draft code, tests and documentation under human review. The repository's history starts in January 2026, and the protoCore-native interpreter that executes protoJS bytecode (`src/runtime/ProtoInterpreter.cpp`) is about 18 000 lines of C++. The same protoCore object model also backs protoPython, protoST and protoClojure.

---

## Lead the Shift

Review the code, reproduce the benchmarks, report conformance gaps and propose changes. Every measurement in this README cites its date and scope so that it can be checked. **Think Different, As All We.**

---

## License

Copyright (c) 2023-2026 Gustavo Marino. Released under the MIT License; see [LICENSE](LICENSE).

---

## 🙏 Acknowledgments

- **[protoCore](https://github.com/numaes/protoCore)**: runtime foundation
- **[QuickJS](https://bellard.org/quickjs/)**: JavaScript parser and compiler, created by Fabrice Bellard

---

## 📧 Contact

Questions, bug reports and proposals: [GitHub issues](https://github.com/gamarino/protoJS/issues).
