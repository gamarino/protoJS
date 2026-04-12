#ifndef PROTOJS_PROMISEPROTOTYPE_H
#define PROTOJS_PROMISEPROTOTYPE_H

#include "headers/protoCore.h"

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

} // namespace protojs

#endif // PROTOJS_PROMISEPROTOTYPE_H
