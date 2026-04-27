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

}  // namespace protojs
