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
| protoIO | Shared input and output for the runtimes: files, processes, TCP, UDP, TLS and HTTP/1.1 (used by protoST, protoScala and protoClojure) | https://github.com/gamarino/protoIO |

---

## 🎯 Key Features

- **protoCore execution path.** QuickJS (vendored in `deps/quickjs/`) compiles source to bytecode; `src/runtime/` loads that bytecode and executes it on a protoCore-native interpreter. There is no QuickJS interpreter fallback (see [src/runtime/README.md](src/runtime/README.md)).
- **protoCore data structures.** Array elements are stored in protoCore's immutable `ProtoList` and updated by structural sharing; strings are protoCore ropes.
- **Parallel JavaScript with `Deferred`.** `new Promise(executor)` runs on the calling thread; `new Deferred(fn)` runs `fn` on a pool of protoCore threads of the same object space, sharing the script's objects without copying (immutable values directly, mutable objects as atomic references to immutable snapshots), and settles as an ordinary promise on the calling thread. No new syntax: the constructor chooses. See [docs/DEFERRED_USAGE.md](docs/DEFERRED_USAGE.md) and the [benchmarks against Node.js worker_threads](benchmarks/reports/2026-10-02-parallel-deferred.md).
- **Native threads.** `protoCore.runInThread` runs a registered C++ worker on a new protoCore thread that shares the same object space, without serialising arguments or results.
- **Garbage collection** is provided by protoCore's concurrent collector. A sequential program runs under a fixed heap ceiling (75 % of physical memory, or of the cgroup limit); a program that uses Deferreds switches, at its first Deferred, to protoCore's adaptive heap controller, which keeps the heap far below that budget (23-65 % less peak memory with 6-12 Deferreds, at -11 to +26 % wall time depending on the workload). Heap memory is not returned to the operating system before the process ends. See [Memory](docs/API_REFERENCE.md#memory).
- **Promises and async functions per ECMA-262.** Promise reactions run as jobs on a per-thread microtask queue, drained after the main script and after every event-loop callback, in the order Node runs them; `await` suspends the async function and resumes it from a job; async generators queue their requests; an unhandled rejection ends the process with status 1. See [Promises and async functions](docs/API_REFERENCE.md#promises-and-async-functions).
- **One exception type with every language on protoCore.** A JavaScript `throw` is a C++ `throw` of protoCore's `ProtoException`, so the errors a C++ module (or another language) raises are caught by a JavaScript `catch` naming the module's types (`e instanceof geometry.NegativeRadius`), protoCore's own errors arrive as `SyntaxError` / `TypeError` / `RangeError`, and a C++ standard exception a module lets escape arrives as an `Error`. See [docs/EXCEPTIONS.md](docs/EXCEPTIONS.md).
- **Node.js-style modules** (`fs`, `path`, `http`, `net`, `stream`, `events`, `crypto`, `worker_threads`, and others) and developer tools (memory analyzer, profiler) implemented in C++.
- **Debugging in VS Code** through `protojs --dap`, a Debug Adapter Protocol server built into the executable: breakpoints, exception breakpoints, stepping, the call stack, variables and the Debug Console. See [Debugging in VS Code](#-debugging-in-vs-code).
- **Command-line interface** with Node.js-style flags and an interactive REPL.

---

## 📋 Requirements

- A **C++20** compiler: GCC or Clang, or MSVC (Visual Studio 2022) on Windows
- **CMake** 3.16 or later
- The **protoCore** shared library, **3.0.0 or newer** (`libprotoCore`), built from source or installed under a prefix
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

**Build your own packages.** `CMakeLists.txt` configures CPack for the package `protojs`, version 0.2.0:

| Platform | CPack generators | Dependency declared |
|---|---|---|
| Linux | `DEB`, `RPM`, `TGZ` | DEB depends on `protocore`; RPM requires `protoCore` |
| macOS | `DragNDrop` | — |
| Windows | `ZIP` (and `NSIS` when `makensis` is found) | — |

Run `cpack` in the build directory (or `cmake --build build --target package`). With CPack's default file naming, the Linux packages are `protojs-0.2.0-Linux.deb`, `protojs-0.2.0-Linux.rpm` and `protojs-0.2.0-Linux.tar.gz`.

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

On **Windows**, protoJS builds natively with Visual Studio 2022 (MSVC) against
an installed protoCore package and runs scripts, `-e` and the REPL from
`cmd.exe` or PowerShell; `cpack -G ZIP` produces a self-contained ZIP (protoCore,
OpenSSL and the Visual C++ runtime included). The steps, and what differs there
(binary standard streams, `path` = `path.win32` as in Node, drive-letter paths,
`.dll` addons that link `protojs.lib`, no `cluster.fork()`), are in
[docs/INSTALLATION.md — Windows (MSVC)](docs/INSTALLATION.md#windows-msvc).

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
| `--dap` | Run as a Debug Adapter Protocol server on stdin/stdout, for VS Code and other DAP clients (see [Debugging in VS Code](#-debugging-in-vs-code)). |
| `-c`, `--check` | Parse the input without executing it. A file that parses prints nothing and exits 0; otherwise the syntax error is printed and the status is 1. Combining it with `-e` exits with status 9. With `--input-type=module` the module itself is parsed, but its imports cannot be resolved: any `import` is reported as `ReferenceError: could not load module`, so the check is limited to modules without imports. |
| `-v`, `--version` | Print `protoJS v0.2.0` and exit. |
| `--input-type=module` | Compile the input as an ES module. |
| `--cpu-threads N` | Size of the CPU thread pool (default: the number of hardware threads). |
| `--io-threads N` | Size of the I/O thread pool (default: hardware threads × the I/O factor, rounded up). |
| `--io-threads-factor F` | I/O factor used when `--io-threads` is not given (default: 3.0). |
| `--preload file.js` | Evaluate `file.js` as a script before the main input (may be repeated). protojs exits with status 1 if the file cannot be read or throws. |
| `--minimal` | Install only `console`, `JSON`, the timing APIs, `Deferred`, `protoCore`, `__filename` and `__dirname`; no `process`, `io`, `require` or Node.js-style modules. protojs exits right after the evaluation without running the event loop. Intended to isolate compiler and interpreter problems. |
| `--proto-eval` | Deprecated. Accepted for compatibility; it has no effect because the protoCore interpreter is always used. |

Without arguments, protojs prints its usage and exits with status 1. When options are given but no script, `-e` or `-c`, it starts the REPL: the prompt is `> `, incomplete input continues on a `... ` prompt, and `.help` and `.exit` (or `.quit`) are available.

After the main evaluation, protojs keeps processing the event loop while there are pending callbacks, `Deferred` instances, worker threads, HTTP servers or clients, `net` handles or pending `fs` operations, for up to 180 seconds. The microtask queue (promise reactions, `await` continuations, `queueMicrotask`) is drained after the main evaluation and after every callback; a pending promise alone does not keep the process alive. The exit status is 1 if the main evaluation threw or a promise rejection went unhandled, and 0 otherwise.

### Examples

Globals and asynchronous work:

```javascript
console.log("Hello from protoJS!");

// Deferred: the function runs in parallel on a pool thread, sharing the
// script's objects; its return value fulfils the Deferred (a promise of the
// calling thread) and an exception rejects it.
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

// The `debugger` module records breakpoints but never stops the program;
// to debug, use VS Code with `protojs --dap` (below).
const dbg = globalThis["debugger"];
dbg.setBreakpoint("script.js", 10);          // script, line[, column]
```

For more examples, see [docs/EXAMPLES.md](docs/EXAMPLES.md). For thread pool configuration, see [docs/THREAD_POOLS.md](docs/THREAD_POOLS.md).

---

## 🐞 Debugging in VS Code

`protojs --dap` is a [Debug Adapter Protocol](https://microsoft.github.io/debug-adapter-protocol/) server built into the `protojs` executable, and [`editor-integration/vscode`](editor-integration/vscode) is a small VS Code extension that registers it as the **protoJS** debugger for JavaScript files. VS Code's own JavaScript support and its Node.js debugger are untouched: protoJS is offered next to Node.js.

**Install** (once; needs Node.js for `npx`):

```bash
cd editor-integration/vscode
npx @vscode/vsce@3.2.1 package --skip-license
code --install-extension protojs-debug-0.3.0.vsix
```

The extension runs `protojs` from the `protojs.path` setting, else from the default install location (`%LOCALAPPDATA%\Programs\proto\bin\protojs.exe` on Windows, `~/.local/bin/protojs` elsewhere), else from the `PATH`.

**Start a session:** open a `.js` file, set breakpoints in the gutter, press **F5** and pick **protoJS**. Without a `launch.json` the active file is debugged; **Run → Add Configuration… → protoJS** writes one (`program`, `args`, `cwd`, `env`, `stopOnEntry`), and [`examples/.vscode/launch.json`](examples/.vscode/launch.json) has ready-made configurations for [`examples/debug_demo.js`](examples/debug_demo.js).

**What works:** line breakpoints in the main script and in `require`d files (pending until the file loads, moved to the next line with code), with conditions, hit counts (`N`, `>= N`, `% N`...) and log messages (logpoints, `{expression}` interpolated in the frame), continue / step over / step into / step out / pause, `stopOnEntry`, the call stack with files and lines, Locals / Closure / Script variables with expandable objects and arrays, evaluate in the selected frame (Debug Console, Watch, hover), exception breakpoints (**All Exceptions** and **Uncaught Exceptions**, stopping at the throw with the stack and locals intact, each with an optional condition on the exception, bound as `error`: `error instanceof TypeError`); a condition that fails stops and says why, and conditions run with breakpoints switched off, and the program's stdout and stderr in the Debug Console; `process.exit` and uncaught exceptions end the session with the program's status.

**Limitations:** only the main thread is debugged (a `Deferred`'s function, `worker_threads` workers and `runInThread` run in contexts of their own, which would need a child debug session each, and run without stopping); programs run as ES modules (`--input-type=module`) are not debuggable; an assignment to a local variable in the Debug Console changes a copy, not the program's variable (globals, top-level bindings and object properties do change); **Uncaught Exceptions** is predicted at the throw from the `try`/`catch` blocks on the stack, and a throw that rejects a promise (in an async function, an executor or a `then` callback) is not uncaught; a rejected promise stops only where it is awaited; no `debugger;` statements or attach; a compute-bound program runs about 1.3-1.5 times slower while breakpoints are set. The full guide is [docs/DEBUGGING.md](docs/DEBUGGING.md).

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

protoJS has **one** authoritative Test262 figure. It is the whole corpus, not a subset.

**As of 2026-10-11: 34 276 of 53 571 tests pass — 63.98 %** (CI run 38153596074).

Reproduce it (about 1 h; the run must be sequential):

```bash
TEST262_ROOT=../test262 \
TEST262_CONCURRENCY=1 \
TEST262_PATTERNS=annexB,built-ins,harness,intl402,language,staging \
PROTOJS=./build_release/protojs \
node tests/test262/runner/test262_runner.js
```

| | |
|---|---|
| Corpus | the whole Test262 `test/` tree — `annexB`, `built-ins`, `harness`, `intl402`, `language`, `staging` |
| Corpus commit | `aae8cf6eed6d6c6a203be48c1184bb194880f66b` |
| Discovered / skipped / denominator | 53 582 / 11 / **53 571** |
| Passed | **34 276** (63.98 %) |
| Failures | 17 159 semantics, 1 228 syntax, 804 async, 3 negative, 101 timeouts |
| Wall clock | 1 993 s sequential (`TEST262_CONCURRENCY=1`), GitHub Actions Ubuntu runner |
| protoCore | 2.14.5 (`063c6d55`), the CI pin at the time of the run |

Nothing is excluded for being unimplemented: `intl402` (0.66 %) and `staging`
(48.75 %) are in the denominator, and tests requiring features protoJS lacks are
run and counted as failures. The full exclusion policy — how `negative`,
`module`, `async` and `raw` tests are treated, and the fact that strict-mode
variants are not yet run — is in
[docs/TEST262_STATUS.md](docs/TEST262_STATUS.md). Read it before quoting the
number.

**Earlier figures, all superseded, none of them the headline:**

- **61.39 %** (28 830 of 46 963, 2026-06-01) — `language` + `built-ins` only, under the runner's older lenient classification. See [docs/TEST262_STATUS.md](docs/TEST262_STATUS.md#historical-language--built-ins-lenient-classification-2026-06-01).
- **92.69 %** (10 923 of 11 784, 2026-06-13) — a named subset of 18 `built-ins` families. Subset figure, archived in [docs/archive/TEST262_ROUNDS.md](docs/archive/TEST262_ROUNDS.md).
- **71.9 %** (6 763 of 9 400, 2026-06-04) — a ten-pattern `built-ins` subset, in [CONFORMANCE_JS.md](CONFORMANCE_JS.md).
- **3 619 of 3 875** (2026-09-26) — the three-pattern `built-ins/{Object,Reflect,Proxy}` **per-commit regression gate**, not a conformance figure. See [docs/CONFORMANCE.md](docs/CONFORMANCE.md).

The detailed history is in [docs/archive/TEST262_ROUNDS.md](docs/archive/TEST262_ROUNDS.md).

### Performance Benchmarks

**protoCore's unified exception model (measured 2026-10-22, branch
`perf/cpp-exceptions` at `40f7d66f3`, not merged).**
- A JavaScript exception is now a `proto::ProtoException` selected by
  prototype, so `instanceof` in a `catch` also matches error types that a
  C++ module or another language defines.
- Compared against the branch just before the port (`fb95f3c5d`, protoCore
  `cf52f9f5`): both Release, interleaved, three rounds.
- The machine had 2-4 busy CPUs, so absolute times run about 20 % above the
  2026-10-21 run. Only the ratios count.

| | AFTER / BEFORE |
|---|---:|
| standard comparison, geomean of 11 workloads of 10 ms or more | 1.017 |
| `numeric_loop` | **1.16** (1 % fewer instructions, 13-23 % more cycles; code layout suspected, not isolated) |
| structure benchmarks, geomean of 50 cells | 1.028 (not alternated; worst cell `join` N=1, 1.38) |
| `throw new Error` caught | 0.94x |
| a throw five frames deep | 0.93x |
| `undefined.x` caught | 1.01x |
| entering a `try` that does not throw | 0.97x |

- The port itself costs nothing on the exception paths.
- The 4-11x cost of a throw relative to the pending-flag design remains.
  That cost is the 2026-10-21 change below.
- Two of the three test262 tests that had timed out under load pass when
  rerun. The third, `length-truncate-nonconfigurable-sparse`, fails
  identically before the port. It is a known bug, not a regression.
- Raw results: [`benchmarks/reports/2026-10-22-unified-exceptions/`](benchmarks/reports/2026-10-22-unified-exceptions/).

**JavaScript exceptions as C++ exceptions (measured 2026-10-21, branch
`perf/cpp-exceptions`, not merged).** `throw` is a C++ `throw` and each `try`
a nested interpreter level inside a C++ `try`; the pending-exception check
after every call is gone. Against its base (`8182924ea`), both Release on
protoCore 3.0.0, interleaved, three rounds:

| | branch / base |
|---|---:|
| standard comparison, geomean of 11 workloads of 10 ms or more | 1.058 (`tree_traversal` 1.19, `function_calls` 1.09) |
| structure benchmarks, geomean of 50 cells | 1.012 (noise) |
| `throw i` caught in the same function | 11.5x slower (Node: 2.3x faster than the branch) |
| `throw new Error` caught | 1.72x slower |
| `undefined.x` caught | 3.9x slower |
| a throw five frames deep | 4.7x slower |
| entering a `try` that does not throw | 1.52x slower |

The cost is C++ unwinding (FDE lookup, personality routine, two scans of the
interpreter's 457-entry call-site table per frame) against one local check
per dispatch before. test262 gains 26 tests (33,849 passes against 33,824).
Raw results: [`benchmarks/reports/2026-10-21-cpp-exceptions/`](benchmarks/reports/2026-10-21-cpp-exceptions/).

**protoCore 2.14.8 (measured 2026-10-20).** master at `45658eaf`, Release,
built against protoCore 2.14.8; each run behind a quiet gate (1.0-2.1 busy
CPUs) and on a fresh copy of the library. `run_standard_comparison.js`
against Node.js 22.17.0, three rounds, medians (in-process time, whole
milliseconds; a ratio is given only where Node takes 5 ms or more):

| Workload | protoJS ms | Node ms | Node faster by |
|---|---:|---:|---:|
| `object_property` | 553 | 36 | 15.4x |
| `object_write_only` | 1572 | 13 | 121x |
| `control_flow` | 187 | 5 | 37x |
| `array_literal`, `function_calls`, `numeric_loop`, `string_concat`, `object_read_only`, `tree_traversal` | 57-207 | 1-2 | 57-190x (Node's JIT; 1 ms resolution) |
| `parallel_cpu` | 3 | 42 | **protoJS 14x faster** |
| `string_repeated_doubling` | 1 | 37 | **protoJS 37x faster** (ropes) |

The structure benchmarks (`benchmarks/structures/run.py`, reps 3, scale 1),
protoJS ms / Node ms, one and twelve copies of the work, sequential and on
workers (Node pays structured clone; its `graph` on workers fails with
`Maximum call stack size exceeded` in `postMessage`, as on 2026-10-18):

| Workload | seq, 1 | seq, 12 | workers, 1 | workers, 12 |
|---|---:|---:|---:|---:|
| `records` | 1419 / 10 | 21483 / 89 | 1636 / 158 | 5736 / 884 |
| `join` | 1613 / 39 | 20979 / 221 | 1650 / 91 | 8048 / 392 |
| `doctree` | 1317 / 17 | 15532 / 57 | 1278 / 42 | 5397 / 170 |
| `wordfreq` | 995 / 16 | 11683 / 212 | 1158 / 20 | 3062 / 50 |
| `graph` | 1992 / 18 | 26878 / 163 | 2056 / fails | 10821 / fails |

Against the 2026-10-18 run, protoJS's times are 6-9 % higher and Node's 11 %
higher on the structures: the machine, not the code. A same-moment A/B of
the 2026-10-18 binary and this one, each on protoCore 2.14.7 and 2.14.8
(three interleaved rounds of the standard comparison), puts all four
combinations within -2.8 % to +0.8 % of each other (geomean of the ten
workloads of 10 ms or more): protoCore 2.14.8 and the `put_field` change
leave protoJS unchanged. Raw results, the A/B included:
[`benchmarks/reports/2026-10-20/`](benchmarks/reports/2026-10-20/).

**protoCore 2.14.7 (measured 2026-10-19).** 2.14.7 changes the write path: a
new value for an existing key copies the root-to-key path without rebalancing,
and allocating a cell no longer takes a spinlock. The same binaries on
protoCore 2.14.6 and 2.14.7, each run loading a fresh copy of the library,
three rounds: `run_standard_comparison.js` against Node.js 22.17.0 moves the
ratio by -2.1 % (geomean of 11 workloads; `function_calls` -5.0 %,
`string_concat` -5.9 %), and the structure benchmarks by +0.1 % (raw times,
one run per side); an attribute write costs 2,849 cycles against 2,919 (within
the A/A spread of 5 %). The adaptive heap controller
(`PROTOCORE_ADAPTIVE_HEAP=1`), kept off for sequential programs since
2026-10-04 because it measured 13-36 % slower, now runs the five structure
workloads -10 % to +4 % in time with 4-7.5x less resident memory (2.2-2.5 GB
-> 0.15-0.5 GB); turning it on by default is recommended and not done here.
Measured under TLP's battery power profile (powersave, EPP balance_power):
compare ratios, not absolute times, with older figures. Details: [protoCore's
report](https://github.com/numaes/protoCore/blob/master/docs/reports/2026-10-18-write-path.md).

**protoCore 2.14.6 (measured 2026-10-13).**  The same commit (`a57b2c688`)
built against protoCore 2.14.5 and 2.14.6, `run_standard_comparison.js`
against Node.js 22.17.0, three rounds: `control_flow` 207 -> 154 ms (about
52x -> 39x Node's time; `perf stat` cycles -27 %), because `===` between two
different integers calls protoCore's comparison, which no longer allocates
a temporary.  `numeric_loop` is unchanged (67 -> 69 ms): protoJS's own
SmallInteger paths for `<`, `%` and arithmetic never reached that code.  The
structure benchmarks (`benchmarks/structures/run.py`) gain from 2.14.6's
bounded mark work list at 12 workers: `records` 6,886 -> 5,442 ms, `graph`
11,982 -> 9,529 ms, `wordfreq` 3,901 -> 3,359 ms. Measured under TLP's battery power profile (powersave, EPP balance_power): compare ratios, not absolute times, with older figures.  Details: [protoCore's report](https://github.com/numaes/protoCore/blob/master/docs/reports/2026-10-12-cross-runtime-int-compare.md).

protoJS executes bytecode in an interpreter; there is no JIT compiler. The newest dated reading in [docs/archive/PERFORMANCE_LOG.md](docs/archive/PERFORMANCE_LOG.md) was recorded on **2026-06-16** with the standard in-process suite ([tests/benchmarks/standard/](tests/benchmarks/standard/)). It compares `protojs` (Release build) with Node.js 22.17.0 and with QuickJS rebuilt with `-O3 -DNDEBUG`. Times are each benchmark's own in-process measurement (median of 3 outer runs, each averaging 5 inner iterations), reported in whole milliseconds.

| Comparison (geometric mean, 11 single-thread benchmarks) | protoJS time relative to the baseline |
|---|---|
| protoJS / QuickJS | 15.2× slower |
| protoJS / Node.js | about 95× slower |

In the same reading, `string_repeated_doubling` (repeated `s = s + s`) took 1 ms in protoJS and QuickJS against 40 ms in Node.js; protoCore's rope strings avoid copying the whole string on each concatenation. The `parallel_cpu` benchmark now runs the same workload (2e6 iterations per task) in every runtime, but it is still not a like-for-like comparison: under protoJS the work runs in the native C++ `cpuChunk` worker on four protoCore threads, while Node.js and QuickJS run the JavaScript loop sequentially. Its results are therefore not used here.

To reproduce, build protoJS and run `node tests/benchmarks/run_standard_comparison.js` (against Node.js) or `node tests/benchmarks/run_standard_comparison_quickjs.js` (against QuickJS); both accept a `PROTOJS_BIN` environment variable pointing at the binary. Raw results are in [tests/benchmarks/results/](tests/benchmarks/results/).

---

## 🔬 Current Status

**Version:** 0.2.0. protoJS is **not production ready**, and its APIs may change.

**Available:**

- Script, inline and ES module evaluation on the protoCore interpreter; the REPL.
- Globals installed on the protoCore-native global object: `console`, `JSON`, `globalThis`, `Promise`, `queueMicrotask`, `Deferred`, `protoCore` (`Set`, `Multiset`, `SparseList`, `Tuple`, `ImmutableObject`, `MutableObject`, `isImmutable`, `makeImmutable`, `makeMutable`, `runInThread`, `threadId`, `isSmallInteger`, `gcStats`), `process`, `io`, `require`, `fs`, `path`, `url`, `http`, `events`, `stream`, `util`, `crypto` (linked against OpenSSL), `Buffer`, `net`, `worker_threads`, `cluster`, `dgram`, `child_process`, `dns`, `memory`, `profiler` and `debugger`. These modules are native C++ implementations; their coverage of the Node.js API varies and has not been measured.
- A CommonJS `require()` loader for JavaScript files and native addons (`.node`, `.so`/`.dylib`/`.dll`, `.protojs`); see [docs/NATIVE_MODULES.md](docs/NATIVE_MODULES.md).
- C++ unit tests (Catch2) for the thread pools, event loop, npm registry client, semver handling, benchmark runner and Node.js test runner.

**Known gaps:**

- Test262 conformance is **63.98 %** of the whole corpus as of 2026-10-11 (see [Test262 Conformance](#test262-conformance)). The largest single gaps: ECMA-402 is not implemented at all (`intl402` passes 0.66 %), 101 tests time out on the CI runner, and classes are incomplete (async functions and async generators follow the specification's job order since 2026-10; their remaining differences are listed in [Promises and async functions](docs/API_REFERENCE.md#promises-and-async-functions)). The failures catalogued on 2026-06-13 also remain: real `eval()` execution, the `$262` cross-realm harness, source text of generator and async functions for `Function.prototype.toString`, and resizable `ArrayBuffer` and `SuppressedError` subclassing. Property enumeration order is a deliberate deviation, not a pending fix: identifier keys come in protoCore's attribute order, not insertion order; keys that are data (computed at run time, parsed from JSON) live in a per-object dictionary and keep insertion order (see [Deliberate deviations](docs/API_REFERENCE.md#deliberate-deviations)).
- Strict-mode Test262 variants are not run yet: each test file executes once, in sloppy mode, so roughly half of Test262's required executions are unmeasured.
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
- **[docs/EXCEPTIONS.md](docs/EXCEPTIONS.md)** - Exceptions: catching module error types, protoCore's errors, uncaught exceptions
- **[docs/DEFERRED_USAGE.md](docs/DEFERRED_USAGE.md)** - Deferred usage guide
- **[docs/PROTOCORE_MODULE.md](docs/PROTOCORE_MODULE.md)** - protoCore module guide
- **[docs/NATIVE_MODULES.md](docs/NATIVE_MODULES.md)** - Native addon modules (C++ shared libraries)
- **[docs/THREAD_POOLS.md](docs/THREAD_POOLS.md)** - Thread pool configuration
- **[docs/DEBUGGING.md](docs/DEBUGGING.md)** - Debugging in VS Code (`protojs --dap`)
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
