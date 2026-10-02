#ifndef PROTOJS_PROMISEPROTOTYPE_H
#define PROTOJS_PROMISEPROTOTYPE_H

#include "protoCore.h"

namespace protojs {

/**
 * Register the Promise constructor and Promise.prototype in globalRoot.
 * Idempotent — no-op when "Promise" is already fully wired.
 */
void ensurePromiseConstructor(proto::ProtoContext* ctx,
                              const proto::ProtoObject** globalRoot);

/** Create a fulfilled Promise wrapping the given value. */
const proto::ProtoObject* makeResolvedPromise(proto::ProtoContext* ctx,
                                               const proto::ProtoObject* value);

/** Create a rejected Promise with the given reason. */
const proto::ProtoObject* makeRejectedPromise(proto::ProtoContext* ctx,
                                               const proto::ProtoObject* reason);

/** Returns true when obj is a Promise (carries __promise_state__). */
bool isPromiseObject(proto::ProtoContext* ctx, const proto::ProtoObject* obj);

/** Returns the promise state: 0=pending, 1=fulfilled, 2=rejected. */
int getPromiseStatePublic(proto::ProtoContext* ctx, const proto::ProtoObject* p);

/** Returns the promise value/reason. */
const proto::ProtoObject* getPromiseValuePublic(proto::ProtoContext* ctx,
                                                 const proto::ProtoObject* p);

// ---------------------------------------------------------------------------
// Unhandled-rejection tracking (HostPromiseRejectionTracker, ECMA-262 §27.2.1.9)
//
// A promise that becomes rejected while no handler is attached is recorded as
// unhandled; attaching a handler (then / catch / finally, a combinator that
// consumes it, or `await`) removes it.  The host checks at the end of every
// event-loop turn -- after the main script and after each callback -- and, as
// Node does by default since v15, reports the first rejection still unhandled
// and ends the process with status 1.  Recorded promises are pinned in the
// wrapper's root set until handled, so the reason survives until it is reported.
// ---------------------------------------------------------------------------

/** Record that `p` is rejected and has no handler. */
void trackPromiseRejection(proto::ProtoContext* ctx, const proto::ProtoObject* p);

/** Record that a handler now observes `p`'s rejection (no-op when not tracked). */
void markPromiseHandled(proto::ProtoContext* ctx, const proto::ProtoObject* p);

/**
 * When a rejected promise nobody handled is recorded on this thread, store its
 * reason in `reason`, forget every recorded promise and return true.
 */
bool takeUnhandledRejection(proto::ProtoContext* ctx, const proto::ProtoObject*& reason);

} // namespace protojs

#endif // PROTOJS_PROMISEPROTOTYPE_H
