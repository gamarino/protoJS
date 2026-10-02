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
| `--unhandled-rejections=<mode>` | What a promise rejection no handler claimed does, as Node's flag: `throw` (default) and `strict` print `Uncaught (in promise) <error>` and end the process with status 1; `warn` prints it and goes on; `none` ignores it. Any other value exits with status 9. The Test262 runner uses `none`. |
| `--minimal` | Install only `console`, `print`, `JSON`, `performance`, `Deferred`, `protoCore` and the script globals, then evaluate the input. Intended for isolating problems. |
| `--proto-eval` | Deprecated. Accepted for compatibility; has no effect (the protoCore interpreter is always used). |

- With no arguments, `protojs` prints the usage text and exits with status 1.
- With options but no script and no `-e`, `protojs` starts an interactive REPL.
- An unknown option or an unreadable script file prints an error and exits with status 1.
- If the main script throws, the error is printed and the exit status is 1; work the script queued (`setImmediate`, I/O callbacks) does not run.
- An exception that escapes an event-loop callback (`setImmediate`, an `fs`, `dns`, `http`, `net` or worker callback, a `Deferred` reaction) is printed as `Uncaught exception in <where>: <error>` and ends the process with status 1 at once, as in Node.
- A rejected `Promise` that no handler has claimed by the end of the microtask checkpoint in which it was rejected (the checkpoint after the main script, or after one callback; see [Promises and async functions](#promises-and-async-functions)) is printed as `Uncaught (in promise) <error>` and ends the process with status 1 -- Node's default since v15. A handler attached by a later job of the same checkpoint is in time; one attached in a later macrotask is not. `then`/`catch`/`finally`, `await` and the `Promise` combinators count as handlers. The REPL prints the message and goes on.
- If the live objects fill the heap ceiling (see `PROTOCORE_HEAP_LIMIT_CELLS` below), `protojs: out of memory: ...` is printed and the exit status is 3.

### Environment variables

| Variable | Read by | Effect |
|----------|---------|--------|
| `PROTOJS_NO_FALLBACK` | `src/JSContext.cpp` | When the protoCore compile step fails, `protojs` normally prints `[protojs] compile failed, fallback to QuickJS eval` and evaluates the code with QuickJS. Set to `1` to report the error instead. |
| `PROTOJS_SPECIALISER` | `src/runtime/BytecodeSpecialiser.cpp` | Bytecode specialiser mode: `compact` (default), `nop` or `off`. |
| `PROTOCORE_GC_CONTEXT_THRESHOLD` | protoCore | Per-context allocation threshold used by protoCore's garbage collector trigger. |
| `PROTOCORE_HEAP_LIMIT_CELLS` | protoCore, `src/JSContext.cpp` | The heap ceiling, in 64-byte cells, of each protoCore space (the main one and each worker's). protoCore's collector runs as the heap approaches the ceiling, so without one nothing is reclaimed during a run. Default: 10,000,000 cells (640 MB), or a quarter of physical memory if that is smaller -- the same policy as protoST. `0` removes the ceiling. |

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
| `Promise` | `src/PromisePrototype.cpp` | ECMA-262 promises: `then`, `catch`, `finally`, `Promise.resolve`, `reject`, `all`, `allSettled`, `any`, `race`, `withResolvers`, `try`, `Symbol.species`. See [Promises and async functions](#promises-and-async-functions) |
| `queueMicrotask` | `src/PromisePrototype.cpp` | `queueMicrotask(callback)` queues `callback` as a microtask; an exception it throws is uncaught |
| `Deferred` | `src/ProtoDeferred.cpp` | Promise-like object. `new Deferred(fn)` and `Deferred(fn)` both work; `fn` takes no arguments, its return value fulfils and a thrown value rejects. `then(onFulfilled, onRejected)` and `catch(onRejected)` return the same instance. See [DEFERRED_USAGE.md](DEFERRED_USAGE.md) |
| `protoCore` | `src/ProtoCoreNativeBindings.cpp` | protoCore collections (`Set`, `Multiset`, `SparseList`, `Tuple`), the mutability helpers (`ImmutableObject`, `MutableObject`, `isImmutable`, `makeImmutable`, `makeMutable`) and `runInThread`; see [PROTOCORE_MODULE.md](PROTOCORE_MODULE.md) |
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

## Promises and async functions

Promises, async functions and async generators follow ECMA-262 §27.2-§27.7,
with the job order Node shows (`tests/integration/promises/*.expected` are
Node's own output for the fixtures, checked by `cli/promise-fixtures`).

- **Jobs.** A promise reaction never runs inside `then()` or inside the call
  that settles the promise: it is queued as a job on the thread's microtask
  queue (`src/MicrotaskQueue.h`). The queue is drained -- jobs queued by jobs
  included, first in, first out -- after the main script, after every
  event-loop callback (`setImmediate`, I/O, worker messages) before the next
  one, and after each REPL input. A worker has its own queue. `queueMicrotask`
  adds a callback to it. Pending promises alone do not keep the process alive,
  as in Node.
- **Thenables.** Resolving a promise with an object whose `then` is callable
  -- another promise, a `Deferred`, any thenable -- calls that `then` from a
  job, so it costs the two extra jobs the specification prescribes; resolving
  a promise with itself rejects it with a `TypeError`.
- **`await`** suspends the async function: the caller continues, and the
  function resumes from a job once the awaited value settles (one job for a
  plain value or a native promise). A `throw` -- before or after the first
  `await` -- rejects the function's promise; `return` resolves it, adopting a
  returned promise. Async arrow functions and async methods behave the same.
- **Async generators** queue `next` / `return` / `throw` requests and serve
  them in order; `for await ... of` accepts async iterables and sync
  iterables (whose values are awaited); `yield*` delegates to an async
  iterator.
- **Native APIs.** `fs.promises` functions return promises settled from an
  event-loop callback, so their reactions run in the checkpoint after it.
  `Deferred` keeps its own model (see [DEFERRED_USAGE.md](DEFERRED_USAGE.md));
  as a thenable it can be awaited and passed to the combinators.
- **Unhandled rejections** are reported at the end of the checkpoint (see
  [Command line](#command-line)).

Known differences from Node:

- `process.on('unhandledRejection')` and `'rejectionHandled'` are not
  available (`process` is not an event emitter); what an unhandled rejection
  does is chosen with `--unhandled-rejections` (Node's default, `throw`,
  ends the process with status 1).
- Errors carry no stack, so the report is `Uncaught (in promise) Name:
  message` rather than Node's stack trace.
- When a `for await` loop is left early (`break`, `return`, an exception), the
  iterator's `return()` is called but its result is not awaited.
- An async generator object's prototype is the shared
  `%AsyncGeneratorPrototype%`, not the generator function's own `prototype`
  object.
- ES module code (`--input-type=module`) is evaluated by QuickJS, with
  QuickJS's own promises and job queue, drained once after the module's
  evaluation; top-level `await` works there as before. The job queue described
  here is the one of scripts, which run on the protoCore interpreter.
- Properties of iterator results and settled records are enumerated in
  protoJS's key order (`done` before `value`), not insertion order.
- In the REPL, the microtasks an input queues run before its result is
  printed (Node prints the result first).

---

## `process`

| Member | Description |
|--------|-------------|
| `process.argv` | Array of every command-line argument passed to `protojs`, starting with the program path and including options. |
| `process.env` | Object with one string property per environment variable. |
| `process.cwd()` | Current working directory. |
| `process.platform()` | A function (not a property) returning `"linux"`, `"darwin"`, `"win32"` or the raw `uname` system name. |
| `process.arch()` | A function returning `"x64"`, `"ia32"`, `"arm"` or the raw `uname` machine name. |
| `process.exit(code)` | Exits immediately with `code` if it is an integer, otherwise with 0, from any point including event callbacks: the standard streams are flushed first, and pending asynchronous work is abandoned (as in Node). |

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

## `util`

| Function | Description |
|----------|-------------|
| `util.format(format, ...args)` | Node's `util.format`: `%s`, `%d`, `%i`, `%f`, `%j`, `%o`, `%O`, `%c` and `%%`; arguments left over are appended, separated by spaces, strings as they are and other values through `util.inspect`. |
| `util.formatWithOptions(options, format, ...args)` | The same, with `util.inspect` options; a non-object `options` throws a `TypeError` with `code: 'ERR_INVALID_ARG_TYPE'`. |
| `util.inspect(value[, options])` | Node's `util.inspect`. Options: `depth` (default 2; `null` or `Infinity` for no limit), `compact` (default 3), `breakLength` (default 80), `maxArrayLength`, `maxStringLength`, `sorted`, `showHidden`, `customInspect` (objects with a `Symbol.for('nodejs.util.inspect.custom')` method). The legacy form `inspect(value, showHidden, depth)` is accepted. |
| `util.promisify(fn)` | A stub that returns `undefined`. |
| `util.types.isArray`, `isString`, `isNumber`, `isObject`, `isFunction`, `isDate` | Type predicates. |

`inspect`, `format` and `formatWithOptions` are a port of Node's
`lib/internal/util/inspect.js` (`src/modules/util/inspect.js`, embedded in the
binary). What they print matches Node, checked by Node's own
`test-util-format.js` (`tests/integration/util/test-util-format.js`), except:

- Object keys are printed in protoJS's key order, which is not always insertion
  order (a [deliberate deviation](#property-enumeration-order)), so an object
  with several keys may print them in another order than Node.
- Errors carry no stack in protoJS, so an error prints as `[Error: message]`,
  which is what Node prints for an error without a stack.
- Not supported: `colors` (accepted and ignored), `numericSeparator`,
  `showProxy`, `getters`, the boxed-primitive (`[Number: 3]`), typed-array,
  `ArrayBuffer`, `Promise`, `WeakMap`/`WeakSet` and iterator forms, async and
  generator function labels, and the class name of an object whose prototype
  is null (`[Foo: null prototype]` prints as `[Object: null prototype]`).
  `util.inspect.custom` and `util.inspect.defaultOptions` are not exposed; use
  `Symbol.for('nodejs.util.inspect.custom')`.
- `%d`, `%i` and `%f` print `0` where Node prints `-0` for a string such as
  `'-0.0'` and for `%i` of a negative fraction: protoJS's `Number`,
  `parseInt` and `parseFloat` lose the sign of zero there.

`console.log` does not go through `util.format`: it does not interpret format
specifiers, and prints objects in its own shorter form.

---

## `fs`

Synchronous functions: `readFileSync`, `writeFileSync`, `readdirSync`,
`mkdirSync`, `statSync`, `unlinkSync`, `rmdirSync`, `renameSync`,
`copyFileSync`; they return `undefined` or `false` on failure instead of
throwing. `fs.promises` has `readFile`, `writeFile`, `readdir`, `mkdir` and
`stat`, which return a `Promise`, rejected with an `Error` on failure.

The callback forms follow Node's API:

| Function | Callback receives |
|----------|-------------------|
| `fs.readFile(path[, options], callback)` | `(err, data)`, `data` a string |
| `fs.writeFile(file, data[, options], callback)` | `(err)`; `options.flag` starting with `a` appends |
| `fs.appendFile(path, data[, options], callback)` | `(err)` |
| `fs.stat(path[, options], callback)` | `(err, stats)` |
| `fs.readdir(path[, options], callback)` | `(err, names)`, in the order the system returns them |
| `fs.mkdir(path[, options], callback)` | `(err)`; with `{ recursive: true }`, `(err, first)`, the first directory created or `undefined` |
| `fs.unlink(path, callback)` | `(err)` |
| `fs.rmdir(path[, options], callback)` | `(err)` |

The operation runs on the I/O thread pool and the callback is invoked on the
event loop, never before the call returns; the process waits for pending
callbacks before it exits. On failure `err` is an `Error` with Node's `code`
(`ENOENT`, `EEXIST`, `EISDIR`, `ENOTDIR`, `ENOTEMPTY`, `EACCES`, ...), `errno`,
`syscall`, `path` and message, e.g. `ENOENT: no such file or directory, open
'/x'`. Invalid arguments -- a missing callback, a path that is not a string --
throw a `TypeError` with `code: 'ERR_INVALID_ARG_TYPE'` synchronously. An
exception thrown by the callback is reported on stderr as `Uncaught exception
in fs callback: ...`; it does not end the process.

Differences from Node:

- `data` is always a string (the file read as UTF-8), whatever the encoding
  option: protoJS's `fs` has no `Buffer` results yet, as for `readFileSync`.
  `writeFile` and `appendFile` accept string data only.
- Paths are strings: file descriptors, `Buffer` and `URL` paths are not
  supported. `readdir`'s `withFileTypes` and `rmdir`'s `recursive` are not
  supported.
- `stats` is the object `statSync` returns: `size`, `mtime` (milliseconds),
  and `isFile` / `isDirectory` as booleans, not methods.
- `errno` is the negated C `errno`, which is Node's value on Linux and macOS;
  on Windows Node reports libuv's own numbers (`ENOENT` is -4058) and protoJS
  the C runtime's (-2). `code`, `syscall`, `path` and the message are the same.

---

## `require`

`require(specifier)` returns the module's exports. For a bare specifier it tries, in order: the built-in module names, protoCore's module discovery, and file-based resolution including `node_modules`. Relative and absolute specifiers use file-based resolution only; native addons are loaded first when several candidate files exist.

A built-in name returns the object installed as the global, so `require('fs') === fs`. `require('events')` is the `EventEmitter` constructor, as in Node (`require('events').EventEmitter` is the same function); instances have `on`/`addListener`, `once`, `off`/`removeListener`, `removeAllListeners`, `emit`, `listenerCount`, `listeners`, `setMaxListeners` and `getMaxListeners`, event names are strings, and an `'error'` event with no listener is thrown. The accepted names are `child_process`, `cluster`, `crypto`, `dgram`, `dns`, `events`, `fs`, `http`, `net`, `path`, `process`, `stream`, `url`, `util` and `worker_threads`, plus `buffer`, which returns `{ Buffer }`; a `node:` prefix is accepted. This is a closed list, so other host globals (`console`, `io`, `memory`, `profiler`, `debugger`) are not requirable and cannot shadow an npm package of the same name. `require.resolve` of a built-in returns its name.

A specifier that resolves to nothing throws `Error: Cannot find module 'x'` with `code: 'MODULE_NOT_FOUND'`; a missing or non-string specifier throws a `TypeError`.

A relative or absolute specifier — and a bare specifier that resolves inside `node_modules` — executes the module body and returns its `module.exports`. Functions the module exports are callable, replacing the exports object with `module.exports = …` is honoured, `__filename` and `__dirname` are set to the module's own path, a second `require()` of the same file returns the same object without re-running the body, and a require cycle terminates with the partner seeing the half-initialised exports, as in Node. A module whose body throws propagates the error to the caller and is not cached, so the next `require()` runs it again. Details: [MODULE_DISCOVERY_PROTOCORE.md](MODULE_DISCOVERY_PROTOCORE.md) and [NATIVE_MODULES.md](NATIVE_MODULES.md).

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

## Deliberate deviations

### Property enumeration order

`Object.keys`, `Object.getOwnPropertyNames`, `for-in`, `Object.entries`,
`Object.assign`, `JSON.stringify` and `util.inspect` report an object's
string-keyed properties in the order protoCore walks the object's attributes,
not in insertion order as ECMA-262 (OrdinaryOwnPropertyKeys) requires:

```js
const o = {};
o.zeta = 1; o.alpha = 2; o.mid = 3;
Object.keys(o);   // Node: ['zeta', 'alpha', 'mid']; protoJS on Linux: ['mid', 'zeta', 'alpha']
```

The order is unspecified: it follows the addresses of the interned key names,
so it can differ between platforms and, on Windows, between two runs of the
same program. Integer-like keys and symbol keys are not affected (array indices
first in ascending order; symbols after the string keys, by creation).

This is deliberate (decided 2026-10-02): insertion order would need
insertion-ordered attributes in protoCore's object model, which all the
protoCore runtimes share, and that model is not changed for it. When order
matters, sort the keys explicitly or use a `Map`, which keeps insertion order:

```js
Object.keys(o).sort();                               // a defined order
const m = new Map([['zeta', 1], ['alpha', 2], ['mid', 3]]);
[...m.keys()];                                       // ['zeta', 'alpha', 'mid'] everywhere
```

Test262 tests that observe key order fail for this reason; see
[TEST262_STATUS.md](TEST262_STATUS.md#property-enumeration-order).

## See also

- [Deferred](DEFERRED_USAGE.md)
- [The `protoCore` global](PROTOCORE_MODULE.md)
- [Thread pool configuration](THREAD_POOLS.md)
- [Examples](EXAMPLES.md)
- [Troubleshooting](TROUBLESHOOTING.md)
