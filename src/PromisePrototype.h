#ifndef PROTOJS_PROMISEPROTOTYPE_H
#define PROTOJS_PROMISEPROTOTYPE_H

#include "protoCore.h"

// Promise objects, ECMA-262 §27.2.
//
// A promise keeps its internal slots in one own attribute, an interned symbol
// key holding an immutable record
//
//     [ [[PromiseState]], [[PromiseResult]], reactions, [[PromiseIsHandled]] ]
//
// so that every state change is a single publish on the (mutable) promise.
// `reactions` is the list of PromiseReaction records still waiting -- the
// spec's [[PromiseFulfillReactions]] and [[PromiseRejectReactions]] kept as one
// list of records that each carry both handlers -- and is dropped as soon as
// the promise settles.  A reaction never runs inside then() or inside the call
// that settles the promise: settling queues one PromiseReactionJob per
// reaction on the agent's job queue (MicrotaskQueue.h), and then() on a
// settled promise queues the job at once.
//
// Resolve and reject functions are native closures (bound functions whose
// receiver is a small record holding the promise and the [[AlreadyResolved]]
// flag).  Resolving with a thenable -- any object whose `then` is callable,
// a Deferred included -- queues a NewPromiseResolveThenableJob, so adopting a
// thenable takes the two extra jobs the specification prescribes.

namespace protojs {

/**
 * Register the Promise constructor and Promise.prototype in globalRoot, and
 * record them as this thread's %Promise% and %Promise.prototype% intrinsics.
 */
void ensurePromiseConstructor(proto::ProtoContext* ctx,
                              const proto::ProtoObject** globalRoot);

// ---------------------------------------------------------------------------
// Abstract operations, for native modules and the interpreter.
// ---------------------------------------------------------------------------

/** A new pending promise whose [[Prototype]] is %Promise.prototype%. */
const proto::ProtoObject* newPromise(proto::ProtoContext* ctx);

/**
 * The algorithm of a promise's resolve function (§27.2.1.3.2): fulfil with a
 * non-thenable, reject on self-resolution or a throwing `then` getter, and
 * adopt a thenable through a NewPromiseResolveThenableJob.  No-op on a
 * promise that is no longer pending.
 */
void resolvePromise(proto::ProtoContext* ctx, const proto::ProtoObject* promise,
                    const proto::ProtoObject* resolution);

/** RejectPromise (§27.2.1.7); no-op on a promise that is no longer pending. */
void rejectPromise(proto::ProtoContext* ctx, const proto::ProtoObject* promise,
                   const proto::ProtoObject* reason);

/**
 * PromiseResolve(%Promise%, value) (§27.2.4.7.1): `value` itself when it is a
 * promise whose `constructor` is %Promise%, else a new promise resolved with
 * it.  Returns nullptr with the exception signalled (signalNativeException)
 * when reading `value.constructor` throws.
 */
const proto::ProtoObject* promiseResolveIntrinsic(proto::ProtoContext* ctx,
                                                  const proto::ProtoObject* value);

/** Kinds of PromiseReaction record.  See PromisePrototype.cpp. */
enum PromiseReactionKind : long long {
    kReactPromise         = 0,  // settle a derived %Promise% instance directly
    kReactCapability      = 1,  // call a PromiseCapability's resolve / reject
    kReactAwait           = 2,  // resume an async function or async generator
    kReactIterResult      = 3,  // resolve a promise with {value, done}
    kReactAsyncGenReturn  = 4,  // AsyncGeneratorAwaitReturn's continuation
    kReactNone            = 5,  // run the handler; nothing to settle
};

/**
 * PerformPromiseThen with an internal reaction: `target` is what the reaction
 * settles or resumes (see PromiseReactionKind).  Marks the promise handled.
 */
void performPromiseThenInternal(proto::ProtoContext* ctx,
                                const proto::ProtoObject* promise,
                                PromiseReactionKind kind,
                                const proto::ProtoObject* target,
                                const proto::ProtoObject* onFulfilled = PROTO_NONE,
                                const proto::ProtoObject* onRejected = PROTO_NONE);

/** A fulfilled / rejected promise (for natives that produce settled results). */
const proto::ProtoObject* makeResolvedPromise(proto::ProtoContext* ctx,
                                               const proto::ProtoObject* value);
const proto::ProtoObject* makeRejectedPromise(proto::ProtoContext* ctx,
                                               const proto::ProtoObject* reason);

/** IsPromise: obj has the promise slots (own, not inherited). */
bool isPromiseObject(proto::ProtoContext* ctx, const proto::ProtoObject* obj);

/** The promise state: 0 pending, 1 fulfilled, 2 rejected. */
int getPromiseStatePublic(proto::ProtoContext* ctx, const proto::ProtoObject* p);

/** The fulfilment value or rejection reason (undefined while pending). */
const proto::ProtoObject* getPromiseValuePublic(proto::ProtoContext* ctx,
                                                 const proto::ProtoObject* p);

/** [[PromiseIsHandled]]. */
bool isPromiseHandled(proto::ProtoContext* ctx, const proto::ProtoObject* p);
void markPromiseHandled(proto::ProtoContext* ctx, const proto::ProtoObject* p);

/** {value, done}, an iterator result object. */
const proto::ProtoObject* makeIteratorResult(proto::ProtoContext* ctx,
                                             const proto::ProtoObject* value, bool done);

/**
 * A native function object carrying `data`: calling it runs `fn` with `data`
 * as its receiver.  Built as a bound function, so every call path of the
 * interpreter handles it; `length` and `name` are the spec's descriptors.
 */
const proto::ProtoObject* makeNativeClosure(proto::ProtoContext* ctx,
                                            proto::ProtoMethod fn,
                                            const proto::ProtoObject* data,
                                            long long length,
                                            const char* name);

/**
 * [[Get]] from native code, as the promise built-ins perform it: a Proxy's
 * get trap, an own data property, an accessor on the chain (stored under the
 * `__get_<name>__` sidecar), then inherited data.  May run JavaScript; the
 * caller checks hasCallException().
 */
const proto::ProtoObject* jsGetProperty(proto::ProtoContext* ctx,
                                        const proto::ProtoObject* obj,
                                        const char* name);

/** Type(v) is Object (not a primitive, Symbol or BigInt). */
bool jsIsObject(proto::ProtoContext* ctx, const proto::ProtoObject* v);

/** IsCallable(v). */
bool jsIsCallable(proto::ProtoContext* ctx, const proto::ProtoObject* v);

// ---------------------------------------------------------------------------
// Jobs, run by MicrotaskQueue.
// ---------------------------------------------------------------------------

/** PromiseReactionJob(reaction, argument); `state` is 1 fulfilled, 2 rejected. */
void promiseReactionJob(proto::ProtoContext* ctx, const proto::ProtoObject* reaction,
                        const proto::ProtoObject* argument, int state);

/** NewPromiseResolveThenableJob(promise, thenable, then). */
void promiseResolveThenableJob(proto::ProtoContext* ctx, const proto::ProtoObject* promise,
                               const proto::ProtoObject* thenable,
                               const proto::ProtoObject* then);

/** Install queueMicrotask on the global. Returns the (possibly new) global. */
const proto::ProtoObject* installQueueMicrotask(proto::ProtoContext* ctx,
                                                const proto::ProtoObject* global);

} // namespace protojs

#endif // PROTOJS_PROMISEPROTOTYPE_H
