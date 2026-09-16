# Troubleshooting Guide

Solutions to common problems when building and running protoJS.

---

## Table of contents

1. [Build problems](#build-problems)
2. [Command-line and runtime messages](#command-line-and-runtime-messages)
3. [Missing globals and modules](#missing-globals-and-modules)
4. [Deferred and asynchronous code](#deferred-and-asynchronous-code)
5. [Performance and threads](#performance-and-threads)
6. [Reporting problems](#reporting-problems)

---

## Build problems

### "protoCore shared library not found"

```
CMake Error: protoCore shared library not found. Build protoCore first: ...
```

Without `PROTO_CORE_PREFIX`, CMake looks for `libprotoCore` only in `../protoCore/build_release`, `../protoCore/build` and `../protoCore/build_check`, in that order, and takes the first that holds it. Either build protoCore there:

```bash
cmake -S ../protoCore -B ../protoCore/build_release
cmake --build ../protoCore/build_release --target protoCore
```

The path CMake settled on is printed as `-- Found protoCore: <path>`. If protoJS behaves as though a protoCore change had not landed, check that line: an older sibling build directory earlier in the search order will have been used.

or point CMake at an installed protoCore with `-DPROTO_CORE_PREFIX=<prefix>` (it must contain `lib/libprotoCore` or `lib64/libprotoCore`, and `include/protoCore.h`). See [INSTALLATION.md](INSTALLATION.md).

### "quickjs.h: No such file or directory"

QuickJS is vendored in `deps/quickjs/` and is part of the repository. The copy is modified for protoJS (for example `deps/quickjs/quickjs.c` defines `protojs_get_function_bytecode`, which the compile step uses), so do not replace it with an upstream QuickJS checkout. Restore the directory from the repository instead.

### Undefined references to `SSL_*`, `EVP_*` or similar

The runtime links the `ssl` and `crypto` libraries. Install the OpenSSL development package (`libssl-dev` on Debian/Ubuntu, `openssl-devel` on Fedora).

### Undefined references to `proto::...`

protoJS was configured against a protoCore library that is missing or older than the headers it compiled with. Rebuild protoCore (`cmake --build ../protoCore/build --target protoCore`), then rebuild protoJS.

### C++20 errors

The build requires a compiler with C++20 support. Check the compiler version with `g++ --version` or `clang++ --version`.

### The configure step fails while downloading Catch2

With tests enabled (the default), CMake downloads Catch2 v3.5.2 when it is not installed. Install Catch2 v3, allow network access, or configure with `-DBUILD_TESTING=OFF`.

---

## Command-line and runtime messages

### The usage text is printed and the exit status is 1

`protojs` was started without arguments. Pass a script (`protojs script.js`) or code (`protojs -e "..."`). Starting `protojs` with options but no script opens the REPL.

### "Unknown option: ..." or "Could not open file: ..."

The option is not recognised (see [API_REFERENCE.md](API_REFERENCE.md#command-line)) or the script path cannot be read.

### "[protojs] compile failed, fallback to QuickJS eval"

The protoCore compile step failed for the code (for example because of a syntax error), and `protojs` retried with QuickJS evaluation. Set `PROTOJS_NO_FALLBACK=1` to report the compile error instead of retrying.

### "[ProtoInterpreter] unsupported opcode 0x.. at byte offset N"

The interpreter reached a QuickJS bytecode instruction it does not implement, and the current function stops executing. Reduce the script to the construct that triggers it and report it with the message.

### "Warning: Event loop timeout reached. Some callbacks may not have completed."

After the main script finishes, `protojs` waits for pending `Deferred`s, workers, HTTP servers and clients and `net` sockets for at most 180 seconds. A server that keeps listening, or a Deferred that never settles, reaches this limit.

---

## Missing globals and modules

### `protoCore.Set(...)` throws `Constructor Set requires 'new'`

`Set`, `Multiset` and `SparseList` are constructors: call them with `new`. Note also that `size()` is a method, not a property, so `set.size` is the function itself and `set.size()` is the count. See [PROTOCORE_MODULE.md](PROTOCORE_MODULE.md).

### `require('./my-module.js')` returns an object with nothing on it

File modules execute their body, so this is almost always the `exports`
rebinding trap, which behaves exactly as it does in Node: assigning to
`exports` itself only replaces the local parameter and publishes nothing.

```js
exports = { a: 1 };        // wrong: rebinds the parameter, exports stay empty
module.exports = { a: 1 }; // right: replaces the module's exports object
exports.a = 1;             // right: mutates the exports object in place
```

A module whose body throws propagates the error to the `require()` call rather
than returning a half-built object, and is not cached, so the next `require()`
runs the body again. See
[MODULE_DISCOVERY_PROTOCORE.md](MODULE_DISCOVERY_PROTOCORE.md).

### `Error: Cannot find module 'x'`

The specifier is neither a built-in name nor a file that the resolver found.
The error carries `code: 'MODULE_NOT_FOUND'`, as in Node. The built-in names
`require()` accepts are `child_process`, `cluster`, `crypto`, `dgram`, `dns`,
`events`, `fs`, `http`, `net`, `path`, `process`, `stream`, `url`, `util`,
`worker_threads` and `buffer`, with or without a `node:` prefix. Other host
globals such as `console`, `io`, `memory`, `profiler` and `debugger` are
deliberately **not** requirable; use them directly.

### `setTimeout` or `setInterval` is not defined

protoJS does not install timer functions. Use `setImmediate(callback)` to run code on a later event-loop turn.

### `process.platform` prints a function

`process.platform` and `process.arch` are functions in protoJS: call `process.platform()` and `process.arch()`.

### Other `ReferenceError`s for host globals

Check the list of installed globals in [API_REFERENCE.md](API_REFERENCE.md#globals). With `--minimal`, only `console`, `print`, `JSON`, `performance`, `Deferred`, `protoCore` and the script globals are installed.

---

## Deferred and asynchronous code

### `TypeError` when calling `resolve` inside `new Deferred((resolve, reject) => ...)`

The Deferred function receives no arguments. Return the value instead; it fulfils the Deferred. See [DEFERRED_USAGE.md](DEFERRED_USAGE.md).

### `TypeError: Deferred requires a function argument`

The Deferred constructor needs a callable argument. `Deferred()` and
`Deferred(42)` throw this error immediately. Previously they created a Deferred
that nothing could settle, and the process waited out the 180-second event-loop
timeout before exiting.

### Deferreds do not run in parallel

A Deferred runs its function on the main thread's event loop, one at a time. For parallel CPU work use `protoCore.runInThread`, or `worker_threads`.

---

## Performance and threads

### Many threads or high memory use during I/O-heavy scripts

The I/O pool defaults to three threads per hardware thread. Reduce it with `--io-threads N` or `--io-threads-factor F`. See [THREAD_POOLS.md](THREAD_POOLS.md).

### `--cpu-threads` has no visible effect on a script

The CPU pool does not run script code; only `protoCore.runInThread` uses it, to wait for protoCore threads. Parallelism comes from the number of `runInThread` calls.

### Comparing performance

Use the standard suite in `tests/benchmarks/standard/` to measure a specific build against Node.js or QuickJS; dated results are in `tests/benchmarks/results/`.

---

## Reporting problems

When reporting a bug, include:

1. The protoJS commit (`git rev-parse HEAD`) and protoCore commit.
2. The operating system, compiler and CMake versions.
3. The smallest script that reproduces the problem, the command line used, and the full output.

## See also

- [API Reference](API_REFERENCE.md)
- [Examples](EXAMPLES.md)
- [Installation](INSTALLATION.md)
- [Documentation index](README.md)
