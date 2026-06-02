#ifndef PROTOJS_NUMBERPROTOTYPE_H
#define PROTOJS_NUMBERPROTOTYPE_H

#include "headers/protoCore.h"

namespace protojs {

/**
 * Build the JS Number prototype (valueOf, toString, toFixed, toExponential, toPrecision)
 * and attach it to the given space as smallIntegerPrototype, largeIntegerPrototype,
 * and doublePrototype. Call after objectPrototype is available.
 * numberProto is created as objectProto->newChild(ctx, false).
 */
void BuildNumberPrototype(proto::ProtoSpace* space, proto::ProtoContext* ctx,
                         const proto::ProtoObject* objectProto);

/**
 * Ensure the Number constructor with static methods and constants is registered
 * in the global root. Idempotent — no-op when "Number" is already present.
 */
void ensureNumberConstructor(proto::ProtoContext* ctx,
                             const proto::ProtoObject** globalRoot);

/**
 * Rebind Number.parseInt / Number.parseFloat to globalThis.parseInt /
 * globalThis.parseFloat for spec-mandated identity (§21.1.2.12 /
 * §21.1.2.13). Must run after the global parseInt/parseFloat
 * registration.
 */
void patchNumberParseFns(proto::ProtoContext* ctx,
                         const proto::ProtoObject** globalRoot);

} // namespace protojs

#endif // PROTOJS_NUMBERPROTOTYPE_H
