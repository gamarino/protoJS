#ifndef PROTOJS_JSPROTOTYPES_H
#define PROTOJS_JSPROTOTYPES_H

#include "headers/protoCore.h"

namespace protojs {

/**
 * JS-specific prototypes derived from ProtoSpace::objectPrototype.
 * Initialized once per ProtoSpace (at bootstrap). Used to create
 * plain objects, arrays, arguments, and RegExp via newChild(ctx, true).
 *
 * Number semantics: integers use ProtoInteger (protoCore SmallInteger or
 * LargeInteger via context->fromInteger/fromLong). Doubles use ProtoDouble
 * (context->fromDouble). The Number prototype (valueOf, toString, toFixed, etc.)
 * is built in BuildNumberPrototype and attached to space->smallIntegerPrototype,
 * largeIntegerPrototype, and doublePrototype.
 */
struct JSPrototypes {
    const proto::ProtoObject* object{};
    const proto::ProtoObject* array{};
    const proto::ProtoObject* arguments{};
    const proto::ProtoObject* regexp{};
    const proto::ProtoObject* frozenMarker{};
    const proto::ProtoObject* nonExtensibleMarker{};
    const proto::ProtoObject* sealedMarker{};
};

/**
 * Initialize JS Object and derived prototypes for the given space.
 * Uses space->objectPrototype as the base; creates array, arguments,
 * and regexp as objectPrototype->newChild(ctx, false).
 * Call once after ProtoSpace is constructed (e.g. in JSContextWrapper ctor).
 */
void BootstrapJSPrototypes(proto::ProtoSpace* space, proto::ProtoContext* ctx, JSPrototypes* out);

} // namespace protojs

#endif // PROTOJS_JSPROTOTYPES_H
