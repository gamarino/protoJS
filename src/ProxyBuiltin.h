#ifndef PROTOJS_PROXYBUILTIN_H
#define PROTOJS_PROXYBUILTIN_H

#include "headers/protoCore.h"

namespace protojs {

/**
 * Basic Proxy implementation per ECMA-262 §28.2.
 *
 * Supports the get / set / has / deleteProperty traps; the remaining
 * traps (apply, construct, getPrototypeOf, setPrototypeOf,
 * isExtensible, preventExtensions, defineProperty,
 * getOwnPropertyDescriptor, ownKeys) fall through to the target's
 * default behaviour without invariant enforcement.
 *
 * Internal storage: `__proxy_target__` and `__proxy_handler__`
 * sidecars on the proxy object.  isProxy() probes the first.
 */

const proto::ProtoObject* proxyConstructor(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* parent,
    const proto::ProtoList* args,
    const proto::ProtoSparseList* sparse);

/** True iff obj has the __proxy_target__ marker. */
bool isProxy(proto::ProtoContext* ctx, const proto::ProtoObject* obj);

/** Returns the wrapped target, or nullptr. */
const proto::ProtoObject* proxyTarget(proto::ProtoContext* ctx,
                                       const proto::ProtoObject* proxy);

/** Returns the handler object, or nullptr. */
const proto::ProtoObject* proxyHandler(proto::ProtoContext* ctx,
                                        const proto::ProtoObject* proxy);

/**
 * Dispatch [[Get]] on a proxy: look up handler.get; if callable, call
 * with (target, propKey, receiver) and return its result.  If the
 * trap is absent, fall through to target.[[Get]].
 *
 * Returns PROTO_NONE if the proxy was revoked or the handler is not
 * an object (the caller should signal a TypeError via the standard
 * exception plumbing).
 */
const proto::ProtoObject* proxyDispatchGet(proto::ProtoContext* ctx,
                                            const proto::ProtoObject* proxy,
                                            const proto::ProtoString* propKey,
                                            const proto::ProtoObject* receiver);

/** Same shape, for [[Set]]. Returns PROTO_TRUE / PROTO_FALSE. */
const proto::ProtoObject* proxyDispatchSet(proto::ProtoContext* ctx,
                                            const proto::ProtoObject* proxy,
                                            const proto::ProtoString* propKey,
                                            const proto::ProtoObject* value,
                                            const proto::ProtoObject* receiver);

/** Same shape, for [[HasProperty]]. */
const proto::ProtoObject* proxyDispatchHas(proto::ProtoContext* ctx,
                                            const proto::ProtoObject* proxy,
                                            const proto::ProtoString* propKey);

/** Same shape, for [[Delete]]. */
const proto::ProtoObject* proxyDispatchDelete(proto::ProtoContext* ctx,
                                               const proto::ProtoObject* proxy,
                                               const proto::ProtoString* propKey);

/**
 * Look up `trapName` on the proxy's handler.  Returns nullptr if the
 * handler is absent / the trap missing / the trap not callable.  Used
 * by the remaining Reflect helpers (getPrototypeOf / setPrototypeOf /
 * isExtensible / preventExtensions / defineProperty /
 * getOwnPropertyDescriptor / ownKeys) so they can dispatch to the
 * handler when present and fall back to the target otherwise.
 */
const proto::ProtoObject* proxyLookupTrap(proto::ProtoContext* ctx,
                                           const proto::ProtoObject* proxy,
                                           const char* trapName);

/** Install Proxy + Reflect on the global root.  Called from bootstrap. */
void installProxyAndReflect(proto::ProtoContext* ctx,
                             const proto::ProtoObject** globalRoot);

} // namespace protojs

#endif // PROTOJS_PROXYBUILTIN_H
