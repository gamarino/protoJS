# Deferred

`Deferred` is a global constructor. `new Deferred(fn)` runs `fn` **in
parallel**, on a thread of the Deferred pool, and returns a promise of the
calling thread that settles with `fn`'s outcome. `new Promise(executor)` runs
on the calling thread. The constructor is the whole choice between sequential
and parallel execution: there is no new syntax.

```javascript
const data = loadBigTable();             // built once, on the main thread

// Four aggregates over the same data, computed at the same time.
const sums = await Promise.all([1, 2, 3, 4].map(k =>
    new Deferred(() => {
        let s = 0;
        for (const row of data) s += row.v * k;   // reads `data` directly: no copy
        return s;
    })));
```

Implemented in `src/ProtoDeferred.cpp` (the constructor and the promise side)
and `src/DeferredPool.cpp` (the threads).

## What `new Deferred(fn)` does

- `fn` is called with **no arguments** on a pool thread. Its return value
  fulfils the Deferred; an exception it throws rejects it, with the thrown value
  unchanged (an `Error`, or a primitive such as `throw 42`).
- The argument must be callable: `Deferred()` and `Deferred(42)` throw a
  `TypeError` at once. `Deferred(fn)` without `new` behaves like
  `new Deferred(fn)`.
- The Deferred is a real promise of the calling thread: `d instanceof Deferred`
  and `d instanceof Promise` are both true, and
  `Object.prototype.toString.call(d)` is `"[object Deferred]"`. It settles on
  the calling thread, as a microtask, once `fn` has returned; from then on it is
  an ordinary promise:
  - `then`, `catch` and `finally` work as on any promise, and return ordinary
    promises (`d.constructor === Promise`, so derived promises are `Promise`s
    and run their callbacks on the calling thread);
  - `await d` works, and so do `Promise.all`, `race`, `any` and `allSettled`
    over any mix of Deferreds and Promises, and `Promise.resolve(d)`;
  - a rejection nobody handles is reported as `Uncaught (in promise) ...` and
    ends the process with status 1, as for any promise (Node's default,
    `--unhandled-rejections` applies).
- If `fn` returns a promise or a thenable (an `async` function, say), the
  Deferred adopts it. Jobs that `fn` queues on its own thread (an `await` of an
  already settled value, `queueMicrotask`) run on the pool thread right after
  `fn` returns, before the result is handed back.

The process stays alive while a Deferred is pending.

## The pool

One pool per runtime instance (per protoCore space), started on the first
`new Deferred`, with as many threads as the CPU pool: `--cpu-threads N`, or the
number of hardware threads by default (see [THREAD_POOLS.md](THREAD_POOLS.md)).
Deferreds beyond that number wait in a queue, first in, first out. The pool
threads are protoCore threads (`ProtoSpace::newThread`) of the same space as
the script, so they share its objects; an idle pool thread waits outside the
collector's running set and never delays a collection.

`protoCore.threadId()` returns a small integer naming the calling thread, which
shows where code runs:

```javascript
const main = protoCore.threadId();
const inFn = await new Deferred(() => protoCore.threadId());   // !== main
```

## Sharing objects: what a Deferred sees

A Deferred's function closes over the caller's variables and reads the caller's
objects directly. Nothing is copied or serialised: not the arguments, not the
result, not the data it reads. This is what makes a Deferred cheap to start
and lets several of them work on one large data structure at once. The rules
come from protoCore's object model:

- **Immutable values** (numbers, strings, BigInts, frozen protoCore values) are
  shared as they are.
- **Mutable objects** (ordinary objects, arrays, Maps, Sets) are shared too:
  each is an atomic reference to an immutable snapshot of its state. A read
  sees one complete snapshot; a write publishes a new snapshot atomically. Two
  threads never see a half-written object, and the runtime never corrupts
  memory.
- **Each write is atomic on its own; there are no transactions.** A
  read-modify-write such as `counter.n = counter.n + 1`, or `list.push(x)` on
  an array another thread also pushes to, is two steps: when two threads do it
  at the same time, one of the updates can be lost. Nothing is corrupted, and
  each object is always in a state some thread wrote.
- A Deferred's function runs while the caller goes on: what it reads is the
  state of each object at the moment it reads it. Treat captured data as a
  snapshot.
- Everything a Deferred returns is visible to the caller when the Deferred
  settles, and so is every write it made to shared objects.

Recommended patterns:

1. **Return values, do not write shared state.** Give each Deferred its own
   share of the work, have it return its result, and combine the results on
   the calling thread (`Promise.all`).
2. **Read shared data freely.** Any number of Deferreds can read the same large
   structure at once; reading never needs coordination.
3. **Aggregate on the calling thread.** If results must go into one structure,
   do it in the `then` callback or after the `await`, which run on the calling
   thread, one at a time.
4. If several Deferreds must update one value, protoCore's collections
   (`protoCore.Set`, `Multiset`, `SparseList`) republish with a
   compare-and-swap and do not lose updates; plain objects and arrays do.

The realm is one: `null`, `undefined`, `Symbol.for(key)`, the built-in
prototypes (`Object.prototype`, `Array.prototype`, the iterator prototypes,
`Promise.prototype`, ...), the classes and functions the script defined, and
the variables closures capture are the same objects on every thread, so
`instanceof`, `===` and prototype checks behave the same inside and outside a
Deferred.

## What a Deferred's function cannot do

A Deferred computes; it does not schedule work on the caller's event loop or
load code. These throw `Error: <name> is not available inside a Deferred
function`, which rejects the Deferred:

- `setImmediate()`,
- `require()` and `require.resolve()` (require modules before creating the
  Deferred and use them from the closure),
- `new Worker()`.

`new Deferred(g)` **inside** a Deferred's function runs `g` inline, on the same
pool thread, and returns an already-settled Deferred: a pool thread never waits
for other pool threads, so nesting cannot deadlock the pool.

Promises created on the calling thread should not be awaited or chained inside
a Deferred's function: a promise belongs to the thread that settles it, and
its reactions run on whichever thread settles it.

`Deferred` is available to scripts run by the protoCore interpreter (the
default mode) on the main runtime; it is not installed in `worker_threads`
workers. ES modules run with `--input-type=module` are executed by
QuickJS, where the legacy QuickJS-side `Deferred` (`src/Deferred.cpp`) is
installed instead; it is not covered by this page.

## Native producers

`protoCore.runInThread(workerName, args)` runs a registered native C++ worker
on a protoCore thread and returns a Deferred that settles with its result, and
`io.readFileAsync` / `io.writeFileAsync` return Deferreds that settle with the
I/O result. These Deferreds behave exactly as above; only what runs differs.

## Tests and benchmarks

- `tests/integration/deferred/parallel_deferred.js`: runs on another thread,
  results and exceptions, the promise protocol, sharing without copies, one
  realm across threads, 400 Deferreds in flight, the refusals inside `fn`,
  nested Deferreds.
- `tests/integration/deferred/concurrent_deferred.js`,
  `tests/integration/deferred/test_deferred_reject.js`,
  `tests/integration/promises/deferred_adoption.js`.
- `tests/integration/gc/deferred_gc_stress.js` (ctest `cli/gc-deferred`):
  Deferreds allocating under a small heap ceiling.
- `tests/cli/uncaught-errors.sh`: an unhandled Deferred rejection is fatal.
- `benchmarks/deferred/`: CPU-bound, shared-data and task-overhead
  benchmarks against Node.js `worker_threads`; results in
  `benchmarks/reports/2026-10-02-parallel-deferred.md`.

## History

`Deferred` was specified as parallel execution from the start
(`docs/archive/PLAN.md`, principle 7 and section 1.3) and was implemented that
way on the QuickJS-based runtime (commits `c2a9ffaf7`, `33d12b2b3`). When the
runtime moved to the protoCore interpreter (`29bd97423`, 2026-04-26) it was
re-implemented to run its function on the event loop's next turn, on the main
thread, and stopped being parallel. This implementation restores the original
specification on the protoCore interpreter.

## See also

- [API reference](API_REFERENCE.md)
- [Thread pool configuration](THREAD_POOLS.md)
- [GC bridging rules](GC_BRIDGING.md)
