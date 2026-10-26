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
| `PROTOJS_SPECIALISER` | `src/runtime/BytecodeSpecialiser.cpp` | Bytecode specialiser: on by default (`on`; the former names `compact` and `nop` also mean on), `off` disables it. |
| `PROTOCORE_GC_CONTEXT_THRESHOLD` | protoCore | Per-context allocation threshold used by protoCore's garbage collector trigger. |
| `PROTOJS_EXIT_TIMEOUT_SECONDS` | `src/main.cpp` | After the main script, `protojs` runs while work is pending, without a time limit (Node's behaviour). A positive value makes it stop waiting after that many seconds, printing `Warning: Event loop timeout reached`. Until 2026-10-03 a fixed 180-second limit applied. |
| `PROTOJS_LITERAL_BUILD` | `src/runtime/BytecodeSpecialiser.cpp` | Object literals are built immutable and made mutable once, after their last field (`markObjectLiterals`). Set to `off` to build them field by field on a mutable object, as before 2026-10-02. |
| `PROTOJS_PUTFIELD_GROUPS` | `src/runtime/BytecodeSpecialiser.cpp` | A run of `obj.name = value` statements on one receiver whose values cannot run code is published as one new version of the object (`markPutFieldGroups`, protoCore's `ProtoObject::setAttributes`). Set to `off` to publish every write separately, as before 2026-10-03. `PROTOJS_PUTFIELD_GROUPS_DUMP=1` prints each run found at load time. |
| `PROTOCORE_HEAP_LIMIT_CELLS` | protoCore, `src/JSContext.cpp`, `src/HeapLimit.cpp` | The heap limit, in 64-byte cells, of the process's protoCore space (Deferreds and workers share it): `<hard>` or `<soft>,<hard>`. A sequential program runs with it as a fixed ceiling; once Deferreds run it is the adaptive controller's budget (see [Memory](#memory)). A live set that does not fit ends the process with status 3. Default: 75 % of physical memory, or of the cgroup memory limit on Linux when one is set and smaller (a container, systemd `MemoryMax`); `protoCore.gcStats().heapLimitCells` shows the effective value. Set a lower value to bound a program's footprint; one limit covers the main thread, its Deferreds and its workers, since they share one space (until 2026-10-24 each worker had a space, and a limit, of its own). Until 2026-10-02 the default was 10,000,000 cells (640 MB), or a quarter of physical memory if smaller. `0` removes the ceiling and keeps the adaptive controller off. |
| `PROTOCORE_ADAPTIVE_HEAP` | protoCore, `src/JSContext.cpp`, `src/HeapLimit.cpp` | `1` runs protoCore's adaptive heap controller from the start, also for sequential programs; `0` never runs it, also with Deferreds (the fixed ceiling throughout). Unset (or any other value): the controller starts with the first Deferred. See [Memory](#memory). |
| `PROTOCORE_HEAP_TRACE` | protoCore | `1` prints one line per collection cycle on stderr (`fixed` for the fixed ceiling, the controller's state otherwise). For diagnosis. |

`PROTOJS_USE_PROTO_EVAL`, which some test scripts set, is not read by `protojs`.

### Memory

protoJS's objects live in protoCore's heap, whose collector runs concurrently
with the program. The process has one space, shared by the main thread, its
Deferreds and its workers. How large its heap may grow is decided in two modes:

- **A sequential program** has a fixed ceiling: by default 75 % of physical
  memory, or of the cgroup memory limit on Linux when one is set and smaller.
  The collector runs as the heap approaches it, so a program that allocates
  steadily grows towards the ceiling before its garbage is collected. This
  is the fastest mode for such programs: most of them never collect.
- **A program that runs Deferreds** switches, when its first Deferred starts,
  to protoCore's adaptive heap controller (protoCore
  [docs/GarbageCollector.md](https://github.com/numaes/protoCore/blob/master/docs/GarbageCollector.md),
  "Adaptive heap controller"). Its budget is the same 75 % of the memory
  available (or `PROTOCORE_HEAP_LIMIT_CELLS`); within it, collection starts at
  128 MiB and the limit grows only while the threads wait for the collector.
  Several threads allocating at once then keep the heap far below the
  budget, at a time cost that depends on the program (below).

`PROTOCORE_ADAPTIVE_HEAP=1` / `=0` force the controller on / off;
`protoCore.gcStats().adaptiveHeap` says which mode a space is in.

In both modes, memory the heap has taken from the operating system is not
returned to it until the process ends: the resident size of a process is its
peak heap. The measured trade-off (synthetic workloads, one notebook-class
machine): run on every program, the controller makes sequential structure
benchmarks 13-36 % slower for 77-96 % less memory (protoCore
`docs/reports/2026-10-04-final-remeasurement.md`), which is why sequential
programs keep the fixed ceiling. With 6 or 12 Deferreds, protoJS's own
before/after (CHANGELOG, 2026-10-10) measured 23-65 % less peak memory and
wall time from 11 % faster to 26 % slower depending on the workload.

#### Property keys that are data

protoCore interns the name of every attribute in a process-wide symbol table
whose entries are never freed. That is right for a program's vocabulary and
wrong for data used as keys (request ids, session ids, words, the keys of the
JSON a server parses), so protoJS keeps such keys out of protoCore's attribute
names (since 2026-10-11; docs/MEMORY_SAFETY_PROPOSALS.md, section 2):

- A computed key -- `obj[k] = v`, `{[k]: v}`, `JSON.parse`, `Object.assign`,
  `Object.fromEntries`, `Object.groupBy`, `Reflect.set`, spread, a
  `worker_threads` message, incoming HTTP header names -- that is a heap string
  (longer than six bytes, or not ASCII), not an identifier or string literal
  of the loaded code, and not already a property of the object or of its
  prototypes, is stored in the object's **property dictionary**: a hidden
  per-object hash map whose keys are ordinary strings, collected with the
  object. Reading, testing, deleting or enumerating a key never interns it.
- Identifier keys (`o.name`, `{name: v}`), keys that the program's code names,
  and ASCII keys of up to six bytes (held in the pointer, no table entry) stay
  named attributes, on protoCore's fast path.
- Arrays, typed arrays, functions, proxies, `String` wrappers and the global
  object keep every key as a named attribute; so does a key given an accessor
  or a non-default descriptor with `Object.defineProperty` (giving one to a
  dictionary key moves it to the named attributes first).

The resident set of a program that keeps using new keys therefore stays
bounded by its live data: 500,000 distinct keys through `obj[k] = v` and
`JSON.parse` under a 1,000,000-cell heap ceiling grow it by under 1 MB
(`cli/dynamic-keys-bounded-memory`); before, every such key cost about
480 bytes for the life of the process. `JSON.parse` also no longer interns
string values.

A dictionary key is an own data property like any other: it shadows a
property of the same name that a prototype gains later -- assigned by code
loaded afterwards (`new Function`, a module), an accessor added with
`Object.defineProperty` or a class, a prototype set with
`Object.setPrototypeOf`. `obj.k` and `obj[k]` read the own value, and
`obj.k = v` updates it without calling an inherited setter (since
2026-10-11). The check costs one atomic load per `obj.k` access for names
whose spelling was never data; see docs/MEMORY_SAFETY_PROPOSALS.md, section 2,
"Shadowing".

Dictionary keys enumerate in insertion order, after the named string keys; see
[Property enumeration order](#property-enumeration-order).

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
| `Deferred` | `src/ProtoDeferred.cpp`, `src/DeferredPool.cpp` | A promise (`instanceof Promise`) whose function runs in parallel on the Deferred pool, sharing the script's objects. `new Deferred(fn)` and `Deferred(fn)` both work; `fn` takes no arguments, its return value fulfils and a thrown value rejects; it settles on the calling thread and `then`/`catch`/`finally` behave as on any promise. See [DEFERRED_USAGE.md](DEFERRED_USAGE.md) |
| `protoCore` | `src/ProtoCoreNativeBindings.cpp` | protoCore collections (`Set`, `Multiset`, `SparseList`, `Tuple`), the mutability helpers (`ImmutableObject`, `MutableObject`, `isImmutable`, `makeImmutable`, `makeMutable`), `runInThread`, `threadId` and the `isSmallInteger` diagnostic; see [PROTOCORE_MODULE.md](PROTOCORE_MODULE.md) |
| `io` | `src/modules/IOModule.cpp` | Simple file I/O; see below |
| `process` | `src/modules/ProcessModule.cpp` | Process information; see below |
| `require` | `src/modules/CommonJSLoader.cpp` | CommonJS loader with `require.resolve` and `require.cache`; see below |
| `path`, `fs`, `url`, `http`, `events`, `stream`, `util`, `crypto`, `Buffer`, `net`, `worker_threads`, `cluster`, `dgram`, `child_process`, `dns` | `src/modules/<name>/` | Node.js-style modules, installed as globals |
| `profiler` | `src/profiling/Profiler.cpp`, `src/profiling/VisualProfiler.cpp` | `startProfiling`, `stopProfiling`, `getProfile`, `startMemoryProfiling`, `stopMemoryProfiling`, `getMemoryProfile`, plus report-export functions added by `VisualProfiler` |
| `memory` | `src/memory/MemoryAnalyzer.cpp` | `takeHeapSnapshot`, `detectLeaks`, `exportSnapshot`, `getMemoryUsage`, `startAllocationTracking`, `stopAllocationTracking` |
| `debugger` | `src/debugging/IntegratedDebugger.cpp` | `startCDPServer`, `stopCDPServer`, `setBreakpoint`, `removeBreakpoint`, `getCallStack`, `evaluate`, `stepOver`, `stepInto`, `stepOut`, `continue` |
| `__filename`, `__dirname`, `__protojs__` | `src/main.cpp` | Path of the main script, its directory, and `true` |

Timer functions such as `setTimeout` and `setInterval` are not installed.

### Shared objects, Deferreds and grouped writes

A `Deferred` runs its function in parallel and shares the script's objects
([DEFERRED_USAGE.md](DEFERRED_USAGE.md#sharing-objects-what-a-deferred-sees)).
Each write to a shared object is atomic: a reader sees the object before the
write or after it, never half of it. A run of consecutive writes to one object
whose values cannot run code (`o.a = x; o.b = y + 1; o.c = 0`) is published as
one new version (see `PROTOJS_PUTFIELD_GROUPS` above and
[PERFORMANCE_NOTES.md](PERFORMANCE_NOTES.md)), and is atomic as a whole: another
thread sees all of the run or none of it. When the runtime declines to group a
run, each of its writes is published on its own.

**A thread terminated in the middle of a run.** When the thread executing a run
of writes to one object is terminated before the run completes -- the process
exits from another thread, or the thread ends abnormally -- which of the run's
writes survive is indeterminate: none of them when the run was grouped and its
single publication had not happened yet, the ones already executed when it was
not grouped. This is the intended behaviour, not a defect. Grouping is an
optimisation the runtime applies or declines at run time, and the language
promises no order in which another thread observes the writes of a run, so a
program must not rely on a partial run having become visible. A run that must
be observed as a unit should be written as one: build the new state and
publish it with a single write (`shared.state = { a, b, c }`), or use
protoCore's collections, which republish with a compare-and-swap.

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
| `process.argv` | As in Node.js: the program path, the script path, then the arguments after the script name (`protojs app.js a b` gives `[<protojs>, "app.js", "a", "b"]`); `protojs` options are not included. Until 2026-10-02 it held every argument, options included, and arguments after the script were rejected as further files. |
| `process.env` | Object with one string property per environment variable. |
| `process.cwd()` | Current working directory. |
| `process.platform()` | A function (not a property) returning `"linux"`, `"darwin"`, `"win32"` or the raw `uname` system name. |
| `process.arch()` | A function returning `"x64"`, `"ia32"`, `"arm"` or the raw `uname` machine name. |
| `process.memoryUsage()` | `{rss, heapTotal, heapUsed, external, arrayBuffers}` in bytes, Node's names: `rss` is the current resident set; `heapTotal` is the protoCore heap (cells obtained from the OS) and `heapUsed` excludes the cells on the space's free list (cells held by threads' local free lists count as used); `external` and `arrayBuffers` are 0 (not tracked). |
| `process.resourceUsage()` | `{maxRSS}`: the peak resident set in KiB, as in Node.js; no other field. |
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

## `worker_threads`

`new Worker(file, { workerData })` runs `file` on a new thread. The worker
is a protoCore thread of the main thread's space: the process holds one
ProtoSpace (`protoCore.gcStats().liveSpaces` is 1), the worker shares the
heap, and nothing is serialized. Until 2026-10-24 each worker built a
ProtoSpace of its own, and messages crossed as JSON text.

- **Messages are structured clones.** `postMessage(value)` (either way) and
  `workerData` hand the receiver the structured clone of the value, made by
  the sender at the call, as in Node: a later change to the value is not seen,
  and the receiver never gets the sender's object. The clone is made inside
  the one space, so what cannot change is shared instead of copied, being
  indistinguishable from a copy: numbers (integers and floating point keep
  their kind), strings, booleans, BigInts, `undefined`, `null` and protoCore's
  immutable values. Arrays and objects are copied with their own enumerable
  data properties, recursively; a value reached twice is cloned once, so
  cycles and shared parts survive. A function or a Symbol cannot be cloned:
  `postMessage` throws a `DataCloneError`. `Map`, `Set`, `Date`, typed arrays
  and other built-in objects are copied as plain objects of their own data
  properties (not yet with their type, as before).
- **What is per worker.** The worker's global object, its top-level bindings,
  its `require` module cache and its job queue are its own. Its global object
  is a child of the main thread's global object, so the main thread's
  globals (its built-in modules, `console`, explicit `globalThis.x = ...`
  properties) are visible to it; a global the worker defines is not visible
  to the main thread or to another worker.
- **What is shared: the realm's intrinsics.** `Object.prototype`,
  `Array.prototype` and the other built-in prototypes and constructors are
  those of the one space, shared by every worker and Deferred. A worker that
  changes a built-in prototype changes it for every thread. This is a
  deliberate deviation from Node, where each worker has its own realm.
- `parentPort` and `workerData` are globals in a worker (Node has them only on
  the module), `isMainThread` is a function (`require('worker_threads').isMainThread()`),
  and `SharedArrayBuffer` behaves as before. A worker's message listeners run
  on the main thread's event loop once its script has finished, as before.

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

`Object.keys`, `Object.values`, `Object.entries`, `Object.getOwnPropertyNames`,
`Object.getOwnPropertyDescriptors`, `Reflect.ownKeys`, `for-in`,
`Object.assign`, `Object.defineProperties`, object spread and rest
(`{...o}`, `const {a, ...r} = o`), `JSON.stringify` and `util.inspect` report
an object's string-keyed properties in the order protoCore walks the object's
attributes -- the order of the addresses of the interned key names -- not in
insertion order as ECMA-262 (OrdinaryOwnPropertyKeys, section 10.1.11.1)
requires:

```js
const o = {};
o.zeta = 1; o.alpha = 2; o.mid = 3;
Object.keys(o);   // Node: ['zeta', 'alpha', 'mid']; protoJS on Linux: ['mid', 'zeta', 'alpha']
```

The order is unspecified: it follows the addresses of the interned key names,
so it can differ between platforms and between two runs of the same program
(observed on Windows and, in October 2026, on macOS). Integer-like keys and
symbol keys are not affected (array indices first in ascending order; symbols
after the string keys, by creation).

Keys that are data rather than program vocabulary (see
[Property keys that are data](#property-keys-that-are-data)) live in the
object's property dictionary and come **in insertion order**, after the named
string keys; with such keys present, every integer index comes first in
ascending order. An object whose keys all came from data -- a parsed JSON
object, a `counts[word]` map -- therefore enumerates exactly as in Node.js:

```js
const p = JSON.parse('{"zeta-key":1,"alpha-key":2,"mid-key":3}');
Object.keys(p);   // ['zeta-key', 'alpha-key', 'mid-key'] in Node and protoJS
```

A spelling that the program's own code contains (an identifier or a string
literal anywhere in the loaded code) is vocabulary and stays a named key, so an
object that mixes such keys with data keys lists the named ones first.

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
[TEST262_STATUS.md](TEST262_STATUS.md#property-enumeration-order). Two of
them are skipped in the per-commit gate (`tests/test262/config/skip_proto_eval.json`)
because their verdict changes from run to run with the key addresses:
`built-ins/Object/defineProperties/15.2.3.7-6-a-93-1.js` and
`built-ins/Object/defineProperties/15.2.3.7-6-a-93-3.js`. Both define
`property` and then `property1` and expect the first to be applied before
the second throws; macOS CI runs 40237571147 and 40238979889 of the same code
failed one each. The maintainer confirmed the deviation on 2026-10-25: the key
order belongs to protoCore's object model and is not changed.

### Module error types and `instanceof`

`x instanceof T` where `T` is an object a module defines outside JavaScript --
a plain protoCore prototype exported by a C++ module or by another language on
protoCore, not callable -- tests `T` against `x`'s prototype chain, where
ECMA-262 §13.10.2 would throw a `TypeError` for a right-hand side that is not
callable:

```js
const geometry = require('example/geometry');
try { geometry.circleArea(-1); }
catch (e) { e instanceof geometry.NegativeRadius; }   // true
```

It applies only to objects no JavaScript program can create: anything
JavaScript makes descends from `Object.prototype` or carries a `[[Prototype]]`
override, and keeps the specification's behaviour. The portable form is
`Object.prototype.isPrototypeOf.call(geometry.NegativeRadius, e)`. Such
objects also convert to a string as an `Error` does (`"<name>: <message>"`)
instead of throwing, since they inherit no `toString`. Decided 2026-10-21 with
protoCore's unified exception model; see [EXCEPTIONS.md](EXCEPTIONS.md).

## See also

- [Exceptions](EXCEPTIONS.md)
- [Deferred](DEFERRED_USAGE.md)
- [The `protoCore` global](PROTOCORE_MODULE.md)
- [Thread pool configuration](THREAD_POOLS.md)
- [Examples](EXAMPLES.md)
- [Troubleshooting](TROUBLESHOOTING.md)
