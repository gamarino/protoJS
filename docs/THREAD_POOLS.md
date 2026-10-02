# Thread Pool Configuration

Each `protojs` runtime instance has three thread pools:

- **Deferred pool** (`src/DeferredPool.cpp`): protoCore threads of the
  runtime's own space that run the functions of `new Deferred(fn)`. Started on
  the first `new Deferred`.
- **CPU thread pool** (`src/CPUThreadPool.cpp`): C++ threads, initialised when
  the runtime starts (`JSContextWrapper` constructor in `src/JSContext.cpp`).
- **I/O thread pool** (`src/IOThreadPool.cpp`): C++ threads, initialised at the
  same time.

JavaScript runs on the main thread and on the Deferred pool. The CPU and I/O
pools run C++ tasks only, and hand results back to the script through the event
loop.

## What uses each pool

| Pool | Used by |
|------|---------|
| Deferred | `new Deferred(fn)`: `fn` runs on a pool thread, sharing the script's objects without copying, and the Deferred settles on the calling thread. See [DEFERRED_USAGE.md](DEFERRED_USAGE.md). |
| CPU | `protoCore.runInThread`: a pool task waits for the protoCore thread to finish and then schedules the resolution of the returned `Deferred`. The worker itself runs on its own protoCore thread, not in the pool. |
| I/O | `io.readFile` / `io.writeFile` (the caller waits for the result), `io.readFileAsync` / `io.writeFileAsync`, asynchronous `fs` operations, and `dns` lookups. |

Promise reactions, `then` callbacks of Deferreds and `setImmediate` callbacks
run on the main thread's event loop.

The Deferred pool is per runtime instance because its threads must belong to
the script's protoCore space to share its objects. (`worker_threads` workers
have their own space, and `Deferred` is not installed in them.) The CPU and I/O
pools are process-wide and shared by all runtime instances.

### How the Deferred pool works

- Its threads are created with `ProtoSpace::newThread`, so the collector knows
  them: each takes part in stop-the-world pauses, and an idle thread waits
  outside the running set (`ProtoContext::UnmanagedScope`), so it never delays a
  collection.
- Each thread runs with the owner's runtime current, and has its own copy of
  the interpreter's per-thread state (frames, the pending-exception flag, its
  own QuickJS context for `JSON.parse` and regular expressions, its own job
  queue for microtasks queued by the function), while the realm's objects --
  `null`, `undefined`, prototypes, the global object, symbol registries -- are
  the owner's (`src/runtime/ThreadIdentity.h`, `src/runtime/PinnedBuiltin.h`).
- A task is a function, the Deferred and the module and global it resolves
  names in, carried as root-set handles; the result goes back to the owner
  thread through `MicrotaskQueue::enqueueFromAnyThread`, which settles the
  Deferred as a microtask and wakes the event loop at once.
- `~JSContextWrapper` shuts the pool down and joins its threads before
  anything else is released.

## Sizes

| Pool | Default | Options |
|------|---------|---------|
| CPU | `std::thread::hardware_concurrency()`, or 1 if that returns 0 | `--cpu-threads N` |
| Deferred | the size of the CPU pool | `--cpu-threads N` |
| I/O | `ceil(std::thread::hardware_concurrency() × 3.0)` | `--io-threads N` sets a fixed size; otherwise `--io-threads-factor F` replaces the factor 3.0 |

```bash
# CPU pool with 8 threads
protojs --cpu-threads 8 script.js

# I/O pool with exactly 24 threads
protojs --io-threads 24 script.js

# I/O pool sized at 4 × hardware threads
protojs --io-threads-factor 4.0 script.js

# Combined
protojs --cpu-threads 8 --io-threads-factor 3.5 script.js
```

Example: on a machine with 16 hardware threads and no options, the CPU pool has 16 threads and the I/O pool 48; with `--io-threads-factor 5.0` the I/O pool has 80.

## Considerations

- Tasks that block on I/O benefit from a larger I/O pool; CPU-bound tasks gain nothing from more threads than the machine has cores.
- On a machine with simultaneous multithreading (two hardware threads per core), CPU-bound Deferreds stop scaling at about the number of physical cores; see `benchmarks/reports/2026-10-02-parallel-deferred.md`.
- `--io-threads-factor` is ignored when `--io-threads` is given.
