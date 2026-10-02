#pragma once

// ProtoDeferred — `Deferred`, a promise whose function runs in parallel.
//
//   new Deferred(fn)   (or Deferred(fn))
//     - fn: called with no arguments on a thread of the Deferred pool
//       (src/DeferredPool.h): a protoCore thread of the SAME space, sharing
//       every object with the calling thread without copying.
//     - returns: a promise of the calling thread -- an instance of Deferred
//       and of Promise -- fulfilled with fn's return value, or rejected with
//       what fn throws. It settles on the calling thread, as a microtask, and
//       from then on behaves as any promise: then/catch/finally, await,
//       Promise.all/race/any/allSettled, unhandled-rejection tracking.
//
//   `new Promise(executor)` runs on the calling thread; `new Deferred(fn)`
//   on the pool. The constructor is the whole choice: no new syntax.
//
// Inside a Deferred's own function, `new Deferred(g)` runs g inline (a pool
// thread never waits for the pool).
//
// docs/DEFERRED_USAGE.md is the user documentation; it states what sharing
// objects between threads means for programs.
//
// C++ surface for native producers (protoCore.runInThread, io.*Async):
//   ProtoDeferred::createPending(ctx)
//     - a pending Deferred that keeps the process alive until settled.
//   ProtoDeferred::resolveFromAsync / rejectFromAsync(ctx, d, value, wrapper)
//     - settle it, on the owner thread.

#include <protoCore.h>

namespace protojs {

class JSContextWrapper;

class ProtoDeferred {
public:
    // Install `Deferred` as a constructor on the protoCore-native global.
    // Returns the (possibly new) global pointer — caller persists via
    // wrapper.updateNativeGlobal.
    static const proto::ProtoObject* init(
        proto::ProtoContext* ctx,
        const proto::ProtoObject* globalObj);

    // Create a pending Deferred from C++ — no worker scheduled.  Used by
    // protoCore.runInThread and any other native producer that resolves
    // the Deferred itself once their off-thread work is done.
    static const proto::ProtoObject* createPending(proto::ProtoContext* ctx);

    // Settle a Deferred from C++, on its owner thread (an event-loop
    // callback or a microtask): its reactions are queued as jobs on the
    // current job queue. `wrapper` is unused and kept for source
    // compatibility.
    static void resolveFromAsync(
        proto::ProtoContext* ctx,
        const proto::ProtoObject* deferred,
        const proto::ProtoObject* value,
        JSContextWrapper* wrapper);

    static void rejectFromAsync(
        proto::ProtoContext* ctx,
        const proto::ProtoObject* deferred,
        const proto::ProtoObject* reason,
        JSContextWrapper* wrapper);

    // Active count for event-loop drain coordination — replaces
    // Deferred::getActiveDeferredCount in main.cpp's drain loop.
    static int getActiveCount();

    // A pool task has been settled on the owner thread (DeferredPool.cpp).
    static void taskSettled();

    // Publish Deferred.prototype, a child of %Promise.prototype%, on the
    // `Deferred` constructor of globalRoot. Called once %Promise.prototype%
    // exists (ensurePromiseConstructor).
    static void ensurePrototype(proto::ProtoContext* ctx,
                                const proto::ProtoObject** globalRoot);
};

/**
 * Consume an exception a user callback left pending (callJSFunction reports a
 * throw through a thread-local flag). Nothing catches an exception that escapes
 * an event-loop callback, so, as in Node, it is fatal: it is reported on stderr
 * as "Uncaught exception in <where>: <name>: <message>" and the process ends
 * with status 1 at once (queued work does not run). Returns only when no
 * exception was pending. Every site that invokes a user callback from the event
 * loop calls it, and EventLoop::processCallbacks calls it after each callback
 * for the sites that do not.
 */
void drainCallbackException(proto::ProtoContext* ctx, const char* where);

/**
 * The end of an event-loop turn (after the main script, after each callback):
 * an exception still pending is fatal (drainCallbackException), and so is a
 * rejected promise that no handler has claimed -- reported as "Uncaught
 * (in promise) <name>: <message>" with status 1, Node's default
 * (--unhandled-rejections=throw) since v15.
 */
void endOfTurnChecks(proto::ProtoContext* ctx);

/**
 * Report a promise rejection no handler claimed by the end of its microtask
 * checkpoint: "Uncaught (in promise) <name>: <message>" on stderr, then, when
 * `fatal`, end the process with status 1 (Node's default).
 */
void reportUnhandledRejection(proto::ProtoContext* ctx, const proto::ProtoObject* reason,
                              bool fatal);

/**
 * Inside a Deferred's function (a pool thread), signal
 * "Error: <what> is not available inside a Deferred function" and return true;
 * elsewhere return false. For the natives that schedule work on the owner
 * thread's event loop or touch its single-threaded loader state (setImmediate,
 * require, new Worker): a Deferred computes, it does not schedule.
 */
bool refuseOnDeferredThread(proto::ProtoContext* ctx, const char* what);

/**
 * Whether a value is callable: a raw ProtoMethod, a wrapped native function, a
 * bytecode closure or a bound function.
 */
bool isCallableValue(proto::ProtoContext* ctx, const proto::ProtoObject* v);

}  // namespace protojs
