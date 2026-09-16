# Deferred

`Deferred` is a global constructor for a promise-like object with `then` and `catch` callbacks. It is implemented natively on protoCore in `src/ProtoDeferred.cpp`.

A `Deferred` does not run its function on a worker thread: the function runs on a later turn of the event loop, on the thread that runs the script. For CPU-bound work on another thread, use [`protoCore.runInThread`](PROTOCORE_MODULE.md#native-multithreading-runinthread), which also returns a `Deferred`.

## Creating a Deferred

```javascript
const d = new Deferred(() => {
    let sum = 0;
    for (let i = 0; i < 1000; i++) {
        sum += i;
    }
    return sum;
});

d.then((value) => {
    console.log("Result:", value);   // Result: 499500
});
```

Behaviour, as implemented in `src/ProtoDeferred.cpp`:

- The constructor takes one function and schedules it on the event loop. The function is called **with no arguments**; the Deferred is fulfilled with its return value. Code written in the `Promise` style, `new Deferred((resolve, reject) => { ... })`, receives `undefined` for `resolve` and `reject`.
- The argument must be callable: `Deferred()` and `Deferred(42)` throw a `TypeError` straight away, instead of creating a Deferred that nothing can ever settle.
- `Deferred(fn)` without `new` behaves the same as `new Deferred(fn)`.
- `then(onFulfilled)` registers a fulfilment callback. If the Deferred is already fulfilled, the callback runs on a later event-loop turn.
- `then(onFulfilled, onRejected)` also registers a rejection handler, following Promises/A+. Either argument may be omitted, so `then(undefined, onRejected)` registers only the rejection handler.
- `catch(callback)` registers a rejection callback. A `catch` registered after the Deferred has already been rejected still runs, on a later turn.
- `then` and `catch` return the **same** Deferred, not a new one. Calls can be chained on that object, but a callback's return value is not passed to the next callback.
- An exception thrown by the constructor's function **rejects** the Deferred, and the thrown value reaches the rejection handlers unchanged — an `Error` object, or a primitive such as `throw 42`. Rejections also come from native operations that return a Deferred, such as `protoCore.runInThread` when the thread cannot be created, or `io.readFileAsync` / `io.writeFileAsync` when the I/O operation fails.
- An exception thrown by a `then` or `catch` callback is reported on stderr as `Uncaught exception in Deferred callback: <Name: message>`. It does not settle any other Deferred and does not disturb the native call that runs next.
- A rejection with no registered handler is silent: there is no unhandled-rejection warning.

## Process lifetime

After the main script finishes, `protojs` keeps processing event-loop callbacks while any Deferred is pending (and while workers, HTTP servers or clients, or `net` sockets are active). It stops waiting after 180 seconds and prints `Warning: Event loop timeout reached. Some callbacks may not have completed.` (see `src/main.cpp`).

## Parallel work

- `protoCore.runInThread(workerName, args)` runs a registered native worker on a protoCore thread and fulfils the returned Deferred with the worker's result. See [PROTOCORE_MODULE.md](PROTOCORE_MODULE.md).
- The `worker_threads` global provides a `Worker` class that runs a script in its own runtime instance on a separate OS thread.

## See also

- [API reference](API_REFERENCE.md)
- [Thread pool configuration](THREAD_POOLS.md)
