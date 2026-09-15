# Thread Pool Configuration

Each `protojs` runtime instance initializes two C++ thread pools when it starts (`JSContextWrapper` constructor in `src/JSContext.cpp`):

- **CPU thread pool** (`src/CPUThreadPool.cpp`)
- **I/O thread pool** (`src/IOThreadPool.cpp`)

Script code does not run in these pools. The script runs on the main thread; the pools run C++ tasks and hand results back to the script through the event loop.

## What uses each pool

| Pool | Used by |
|------|---------|
| CPU | `protoCore.runInThread`: a pool task waits for the protoCore thread to finish and then schedules the resolution of the returned `Deferred`. The worker itself runs on its own protoCore thread, not in the pool. |
| I/O | `io.readFile` / `io.writeFile` (the caller waits for the result), `io.readFileAsync` / `io.writeFileAsync`, asynchronous `fs` operations, and `dns` lookups. |

`Deferred` callbacks and `setImmediate` callbacks run on the main thread's event loop and use neither pool.

## Sizes

| Pool | Default | Options |
|------|---------|---------|
| CPU | `std::thread::hardware_concurrency()`, or 1 if that returns 0 | `--cpu-threads N` |
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
- `--io-threads-factor` is ignored when `--io-threads` is given.
- The usage text printed by `protojs` describes the I/O default as "3-4x CPU cores"; the implemented default factor is 3.0.
