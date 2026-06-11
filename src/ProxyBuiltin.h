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

/**
 * [[GetOwnProperty]] dispatch per §10.5.5: calls handler.getOwnProperty-
 * Descriptor(target, P) if present, otherwise forwards to the target.
 * Returns a descriptor object (the same shape FromPropertyDescriptor
 * produces) or PROTO_NONE if the property is absent.  On abrupt or
 * invariant violation, signals via the standard exception plumbing and
 * returns PROTO_NONE.
 */
const proto::ProtoObject* proxyDispatchGetOwnPropertyDescriptor(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* proxy,
    const proto::ProtoString* propKey);

/**
 * [[DefineOwnProperty]] dispatch per §10.5.6: calls handler.define-
 * Property(target, P, Desc) if present, otherwise the caller may
 * proceed with its default define-on-target path.
 *
 * Returns:
 *   PROTO_TRUE / PROTO_FALSE — trap result (ToBoolean'd).
 *   PROTO_NONE on abrupt completion (caller must check hasCallException).
 *   The sentinel proxy::FORWARD_TO_TARGET (= nullptr) when no trap is
 *   present and the caller should perform the default define on target.
 */
const proto::ProtoObject* proxyDispatchDefineProperty(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* proxy,
    const proto::ProtoString* propKey,
    const proto::ProtoObject* descriptor);

/**
 * [[GetPrototypeOf]] dispatch per §10.5.1: calls handler.getPrototypeOf
 * (target) if present, otherwise forwards.  Returns the resolved
 * prototype object, getNullSentinel() for a null [[Prototype]], or
 * PROTO_NONE on abrupt.
 */
const proto::ProtoObject* proxyDispatchGetPrototypeOf(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* proxy);

/**
 * [[SetPrototypeOf]] dispatch per §10.5.2: calls handler.setPrototypeOf
 * (target, V) if present, otherwise returns nullptr so the caller may
 * fall through.  Returns PROTO_TRUE / PROTO_FALSE / PROTO_NONE-on-abrupt.
 */
const proto::ProtoObject* proxyDispatchSetPrototypeOf(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* proxy,
    const proto::ProtoObject* newProto);

/** Install Proxy + Reflect on the global root.  Called from bootstrap. */
void installProxyAndReflect(proto::ProtoContext* ctx,
                             const proto::ProtoObject** globalRoot);

} // namespace protojs

#endif // PROTOJS_PROXYBUILTIN_H
