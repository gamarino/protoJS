# API Reference

Reference for the `protojs` command line and the globals that `protojs` 0.1.0 installs for scripts. The list follows the module initialization in `src/main.cpp`; each entry names the source file that implements it.

Scripts run on the protoCore interpreter against a protoCore-native global object. Only objects registered on that global are visible to scripts. For ECMAScript conformance of the standard built-ins (`Object`, `Array`, `String`, ...), see [TEST262_STATUS.md](TEST262_STATUS.md) and [CONFORMANCE_JS.md](../CONFORMANCE_JS.md).

---

## Command line

```bash
protojs [options] <script.js>
protojs [options] -e "<code>"
```

| Option | Effect |
|--------|--------|
| `--cpu-threads N` | Size of the CPU thread pool. Default: the number of hardware threads. See [THREAD_POOLS.md](THREAD_POOLS.md). |
| `--io-threads N` | Fixed size of the I/O thread pool. |
| `--io-threads-factor F` | When `--io-threads` is not given, the I/O pool has `ceil(hardware threads × F)` threads. Default `F`: 3.0. |
| `-e "<code>"` | Evaluate `<code>` instead of a file (the file name is reported as `eval`). |
| `-p`, `--print` | After evaluation, print the result if it is not `undefined` and no exception was thrown. |
| `-c`, `--check` | Parse the input without executing it: status 0 when it parses, 1 with the syntax error on stderr when it does not. The check runs in a bare QuickJS runtime, so no module, thread pool or `ProtoSpace` is created. Combining it with `-e` exits with status 9. Because that runtime has no module loader, `--input-type=module` parses the module itself but cannot resolve its imports: any `import` is reported as `ReferenceError: could not load module`. Unlike `node --check`, the check is therefore limited to modules without imports. |
| `-v`, `--version` | Print `protoJS v0.1.0` and exit. |
| `--input-type=module` | Evaluate the input as an ES module. Module code is evaluated by the QuickJS module evaluator, not by the protoCore interpreter. |
| `--preload <file>` | Evaluate `<file>` as a script before the main input; may be repeated. |
| `--minimal` | Install only `console`, `print`, `JSON`, `performance`, `Deferred`, `protoCore` and the script globals, then evaluate the input. Intended for isolating problems. |
| `--proto-eval` | Deprecated. Accepted for compatibility; has no effect (the protoCore interpreter is always used). |

- With no arguments, `protojs` prints the usage text and exits with status 1.
- With options but no script and no `-e`, `protojs` starts an interactive REPL.
- An unknown option or an unreadable script file prints an error and exits with status 1.
- If the main script throws, the exit status is 1.

### Environment variables

| Variable | Read by | Effect |
|----------|---------|--------|
| `PROTOJS_NO_FALLBACK` | `src/JSContext.cpp` | When the protoCore compile step fails, `protojs` normally prints `[protojs] compile failed, fallback to QuickJS eval` and evaluates the code with QuickJS. Set to `1` to report the error instead. |
| `PROTOJS_SPECIALISER` | `src/runtime/BytecodeSpecialiser.cpp` | Bytecode specialiser mode: `compact` (default), `nop` or `off`. |
| `PROTOCORE_GC_CONTEXT_THRESHOLD` | protoCore | Per-context allocation threshold used by protoCore's garbage collector trigger. |

`PROTOJS_USE_PROTO_EVAL`, which some test scripts set, is not read by `protojs`.

---

## Globals

| Global | Source | Contents |
|--------|--------|----------|
| `console` | `src/console.cpp` | `log`, `error`, `warn`, `info`, `debug`, `time`, `timeEnd`, `timeLog`, `assert`, `group`, `groupEnd`, `groupCollapsed`, `dir`, `dirxml`, `trace`, `count`, `countReset`, `table`, `clear` |
| `print` | `src/console.cpp` | Alias of `console.log` (used by Test262 harness files) |
| `performance` | `src/console.cpp` | `performance.now()` |
| `JSON` | `src/JSONBuiltin.cpp` | `JSON.parse`, `JSON.stringify` |
| `setImmediate` | `src/EventLoopBindings.cpp` | `setImmediate(callback)` runs `callback` on a later event-loop turn |
| `Deferred` | `src/ProtoDeferred.cpp` | Promise-like object. `new Deferred(fn)` and `Deferred(fn)` both work; `fn` takes no arguments, its return value fulfils and a thrown value rejects. `then(onFulfilled, onRejected)` and `catch(onRejected)` return the same instance. See [DEFERRED_USAGE.md](DEFERRED_USAGE.md) |
| `protoCore` | `src/ProtoCoreNativeBindings.cpp` | `protoCore.runInThread`; see [PROTOCORE_MODULE.md](PROTOCORE_MODULE.md) |
| `io` | `src/modules/IOModule.cpp` | Simple file I/O; see below |
| `process` | `src/modules/ProcessModule.cpp` | Process information; see below |
| `require` | `src/modules/CommonJSLoader.cpp` | CommonJS loader with `require.resolve` and `require.cache`; see below |
| `path`, `fs`, `url`, `http`, `events`, `stream`, `util`, `crypto`, `Buffer`, `net`, `worker_threads`, `cluster`, `dgram`, `child_process`, `dns` | `src/modules/<name>/` | Node.js-style modules, installed as globals |
| `profiler` | `src/profiling/Profiler.cpp`, `src/profiling/VisualProfiler.cpp` | `startProfiling`, `stopProfiling`, `getProfile`, `startMemoryProfiling`, `stopMemoryProfiling`, `getMemoryProfile`, plus report-export functions added by `VisualProfiler` |
| `memory` | `src/memory/MemoryAnalyzer.cpp` | `takeHeapSnapshot`, `detectLeaks`, `exportSnapshot`, `getMemoryUsage`, `startAllocationTracking`, `stopAllocationTracking` |
| `debugger` | `src/debugging/IntegratedDebugger.cpp` | `startCDPServer`, `stopCDPServer`, `setBreakpoint`, `removeBreakpoint`, `getCallStack`, `evaluate`, `stepOver`, `stepInto`, `stepOut`, `continue` |
| `__filename`, `__dirname`, `__protojs__` | `src/main.cpp` | Path of the main script, its directory, and `true` |

Timer functions such as `setTimeout` and `setInterval` are not installed.

---

## `process`

| Member | Description |
|--------|-------------|
| `process.argv` | Array of every command-line argument passed to `protojs`, starting with the program path and including options. |
| `process.env` | Object with one string property per environment variable. |
| `process.cwd()` | Current working directory. |
| `process.platform()` | A function (not a property) returning `"linux"`, `"darwin"`, `"win32"` or the raw `uname` system name. |
| `process.arch()` | A function returning `"x64"`, `"ia32"`, `"arm"` or the raw `uname` machine name. |
| `process.exit(code)` | Exits immediately with `code` if it is an integer, otherwise with 0. |

```javascript
console.log(process.argv.length, process.platform(), process.arch(), process.cwd());
```

---

## `io`

| Function | Description |
|----------|-------------|
| `io.readFile(path)` | Reads the file and returns its content as a string; returns `undefined` if the read fails. The read runs on the I/O pool while the caller waits. |
| `io.writeFile(path, content)` | Writes a string; returns `true` on success and `false` on failure. |
| `io.readFileAsync(path)` | Returns a `Deferred` fulfilled with the file content, or rejected if the read fails. |
| `io.writeFileAsync(path, content)` | Returns a `Deferred` settled when the write completes or fails. |

```javascript
io.writeFile("output.txt", "Hello, world!");
console.log(io.readFile("output.txt"));

io.readFileAsync("output.txt").then((text) => console.log("async:", text));
```

---

## `require`

`require(specifier)` returns the module's exports. For a bare specifier it tries, in order: protoCore's module discovery, a property of the same name on the QuickJS-side global object, and file-based resolution including `node_modules`. Relative and absolute specifiers use file-based resolution only; native addons are loaded first when several candidate files exist.

The standard modules are registered on the protoCore-native global, not on the QuickJS-side global that the second step reads, so use their globals (`fs`, `path`, ...) directly. Details: [MODULE_DISCOVERY_PROTOCORE.md](MODULE_DISCOVERY_PROTOCORE.md) and [NATIVE_MODULES.md](NATIVE_MODULES.md).

---

## C++ libraries without a JavaScript API

The `protojs_core` library also contains C++ components that are not exposed to scripts. They are exercised by the C++ unit tests in `tests/unit/`:

| Component | Header | Purpose |
|-----------|--------|---------|
| Semver | `src/npm/Semver.h` | Version parsing, comparison and range matching |
| NPMRegistry | `src/npm/NPMRegistry.h` | Package metadata lookup, version resolution and tarball download |
| BenchmarkRunner | `src/benchmarking/BenchmarkRunner.h` | Runs benchmark scripts under protoJS and Node.js and compares time and memory |
| NodeJSTestRunner | `src/testing/NodeJSTestRunner.h` | Runs a test file under Node.js and protoJS and compares the output |

The January 2026 guides for these components are archived in [archive/PHASE6_MODULE_GUIDES.md](archive/PHASE6_MODULE_GUIDES.md) and may not match the current code.

---

## See also

- [Deferred](DEFERRED_USAGE.md)
- [The `protoCore` global](PROTOCORE_MODULE.md)
- [Thread pool configuration](THREAD_POOLS.md)
- [Examples](EXAMPLES.md)
- [Troubleshooting](TROUBLESHOOTING.md)
