#include "JSPrototypes.h"
#include "ObjectPrototype.h"
#include "NumberPrototype.h"
#include "StringPrototype.h"
#include "BooleanPrototype.h"
#include "RegExpPrototype.h"
#include "MapPrototype.h"
#include "SetPrototype.h"
#include "runtime/BehaviorRegistry.h"

namespace protojs {

void BootstrapJSPrototypes(proto::ProtoSpace* space, proto::ProtoContext* ctx, JSPrototypes* out) {
    if (!space || !ctx || !out) return;

    const proto::ProtoObject* objectProto = space->objectPrototype;
    if (!objectProto) return;

    // Install Object.prototype instance methods (hasOwnProperty, toString, valueOf, etc.)
    // directly on space->objectPrototype so that every JS object created by TypeBridge
    // (which uses objectProto as parent) inherits them via getAttribute(key, true).
    // ProtoObjects are immutable: setAttribute returns a new pointer; update the space field.
    objectProto = installObjectInstanceMethods(ctx, objectProto);
    space->objectPrototype = const_cast<proto::ProtoObject*>(objectProto);

    out->object = objectProto;
    // Mutable so JS-level mutations (Array.prototype.x = y, etc.) are in-place.
    out->array = objectProto->newChild(ctx, true);
    out->arguments = objectProto->newChild(ctx, true);
    // Mutable so RegExp.prototype.x = y mutates in place (the
    // alternative is the same write-suppression bug R28 fixed on
    // Object.prototype: the first setAttribute forks the identity
    // and the user-visible RegExp.prototype slot ends up pointing
    // at the pre-init snapshot).
    out->regexp = objectProto->newChild(ctx, true);

    BuildNumberPrototype(space, ctx, objectProto);
    BuildStringPrototype(space, ctx, objectProto);
    BuildBooleanPrototype(space, ctx, objectProto);
    out->regexp = BuildRegExpPrototype(space, ctx, out->regexp);
    BuildMapPrototype(space, ctx, objectProto);
    BuildSetPrototype(space, ctx, objectProto);

    // Initialize markers and behaviors
    out->frozenMarker = ctx->newObject(false);
    out->nonExtensibleMarker = ctx->newObject(false);
    out->sealedMarker = ctx->newObject(false);

    BehaviorRegistry::instance().registerBehavior(out->frozenMarker, std::make_unique<FrozenBehavior>());
    BehaviorRegistry::instance().registerBehavior(out->nonExtensibleMarker, std::make_unique<NonExtensibleBehavior>());
    BehaviorRegistry::instance().registerBehavior(out->sealedMarker, std::make_unique<NonExtensibleBehavior>());
}

} // namespace protojs
