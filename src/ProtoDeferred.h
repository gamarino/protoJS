#pragma once

// ProtoDeferred — protoCore-native Promise-like primitive for async work.
//
// Replaces the QuickJS-side Deferred class (src/Deferred.{h,cpp}) which
// used JS_NewClass + JS_NewCFunction2 + JSValue everywhere.  The
// QuickJS version was invisible to user code running through the
// protoCore-native interpreter; `typeof Deferred === 'undefined'`
// because it lived only on the QuickJS global.
//
// User-visible surface:
//   new Deferred(workerFn)
//     - workerFn: callable invoked on the event loop's next turn.
//       Its return value fulfils the Deferred; throwing rejects it.
//     - returns: an instance carrying .then and .catch methods.
//
//   instance.then(callback)
//     - registers `callback` to receive the fulfilment value.
//     - returns the same instance (chaining).
//
//   instance.catch(callback)
//     - registers `callback` to receive the rejection reason.
//
// Internal state (attributes on the instance, all `__df_*` private):
//   __df_state__  : SmallInteger — 0 pending, 1 fulfilled, 2 rejected
//   __df_value__  : resolved value / rejection reason
//   __df_then__   : ProtoList of pending then callbacks
//   __df_catch__  : ProtoList of pending catch callbacks
//
// C++ surface for runInThread / other native producers:
//   ProtoDeferred::createPending(ctx)
//     - returns a fresh pending instance, no worker scheduled.
//   ProtoDeferred::resolveFromAsync(ctx, instance, value, wrapper)
//     - sets state=fulfilled and drains the then queue via event loop.
//   ProtoDeferred::rejectFromAsync(ctx, instance, reason, wrapper)
//     - sets state=rejected and drains the catch queue via event loop.

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

    // Resolve a Deferred from C++.  Marks fulfilled and schedules pending
    // .then callbacks on the event loop.  `wrapper` is captured into the
    // event-loop lambda so callJSFunctionFromAsync can re-publish the
    // wrapper and rootModule when the callback fires.
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
 * Whether a value is callable: a raw ProtoMethod, a wrapped native function, a
 * bytecode closure or a bound function.
 */
bool isCallableValue(proto::ProtoContext* ctx, const proto::ProtoObject* v);

}  // namespace protojs
