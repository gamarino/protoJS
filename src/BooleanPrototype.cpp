#include "BooleanPrototype.h"
#include "JSSymbols.h"
#include "runtime/ProtoInterpreter.h"
#include "headers/protoCore.h"
#include <string>

namespace protojs {

namespace {

// --- Helper: extract boolean value from a primitive or Boolean wrapper object ---
static bool getBoolValue(proto::ProtoContext* ctx, const proto::ProtoObject* self) {
    if (self == PROTO_TRUE) return true;
    if (self == PROTO_FALSE) return false;
    if (self && self->isBoolean(ctx)) return self->asBoolean(ctx);
    // Boolean wrapper object: has __primitive_value__ attribute
    const proto::ProtoString* pvKey = JSSymbols::primitiveValue(ctx);
    if (pvKey) {
        const proto::ProtoObject* pv = self->getAttribute(ctx, pvKey, false);
        if (pv == PROTO_TRUE) return true;
        if (pv == PROTO_FALSE) return false;
        if (pv && pv != PROTO_NONE && pv->isBoolean(ctx)) return pv->asBoolean(ctx);
    }
    return false;
}

/** Throws TypeError if this is null, undefined, or not a Boolean value/wrapper. */
static bool requireBooleanThis(proto::ProtoContext* ctx, const proto::ProtoObject* self) {
    if (!self || self == PROTO_NONE || self->isNone(ctx)) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Boolean.prototype method called on incompatible receiver"));
        return false;
    }
    const proto::ProtoObject* nullSentinel = getNullSentinel();
    if (nullSentinel && self == nullSentinel) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Boolean.prototype method called on incompatible receiver"));
        return false;
    }
    // Valid if self is a boolean primitive.
    if (self->isBoolean(ctx)) return true;
    // Valid if self is a Boolean wrapper object (has __primitive_value__ that is boolean).
    const proto::ProtoString* pvKey = JSSymbols::primitiveValue(ctx);
    if (pvKey) {
        const proto::ProtoObject* pv = self->getAttribute(ctx, pvKey, false);
        if (pv && pv != PROTO_NONE && pv->isBoolean(ctx)) return true;
    }
    signalNativeException(makeNativeError(ctx, "TypeError",
        "Boolean.prototype method called on incompatible receiver"));
    return false;
}

// --- Boolean.prototype.valueOf ---
static const proto::ProtoObject* booleanValueOf(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{
    if (!ctx || !self || self == PROTO_NONE) return PROTO_NONE;
    if (!requireBooleanThis(ctx, self)) return PROTO_NONE;
    return getBoolValue(ctx, self) ? PROTO_TRUE : PROTO_FALSE;
}

// --- Boolean.prototype.toString ---
static const proto::ProtoObject* booleanToString(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{
    if (!ctx || !self || self == PROTO_NONE) return PROTO_NONE;
    if (!requireBooleanThis(ctx, self)) return PROTO_NONE;
    return ctx->fromUTF8String(getBoolValue(ctx, self) ? "true" : "false");
}

// --- Boolean constructor body: new Boolean(x) ---
static const proto::ProtoObject* booleanConstruct(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    if (!ctx || !self || self == PROTO_NONE) return self;
    bool val = false;
    if (args && args->getSize(ctx) > 0) {
        const proto::ProtoObject* a = args->getAt(ctx, 0);
        if (a && a != PROTO_NONE) {
            if (a == PROTO_TRUE) {
                val = true;
            } else if (a == PROTO_FALSE) {
                val = false;
            } else if (a->isBoolean(ctx)) {
                val = a->asBoolean(ctx);
            } else if (a->isInteger(ctx)) {
                val = a->asLong(ctx) != 0;
            } else if (a->isDouble(ctx) || a->isFloat(ctx)) {
                val = a->asDouble(ctx) != 0.0;
            } else if (a->isString(ctx)) {
                std::string s;
                const proto::ProtoString* ps = a->asString(ctx);
                if (ps) {
                    ps->toUTF8String(ctx, s);
                    val = !s.empty();
                }
            } else {
                val = true; // non-null object → truthy
            }
        }
    }
    const proto::ProtoString* pvKey = JSSymbols::primitiveValue(ctx);
    if (pvKey)
        self = self->setAttribute(ctx, pvKey, val ? PROTO_TRUE : PROTO_FALSE);
    return self;
}

} // anonymous namespace

// --- BuildBooleanPrototype ---
void BuildBooleanPrototype(proto::ProtoSpace* space, proto::ProtoContext* ctx,
                           const proto::ProtoObject* objectProto)
{
    if (!space || !ctx || !objectProto) return;

    const proto::ProtoObject* bp = objectProto->newChild(ctx, false);
    proto::ProtoObject* mutableBp = const_cast<proto::ProtoObject*>(bp);

    const proto::ProtoString* keyValueOf  =
        ctx->fromUTF8String("valueOf") ? ctx->fromUTF8String("valueOf")->asString(ctx) : nullptr;
    const proto::ProtoString* keyToString =
        ctx->fromUTF8String("toString") ? ctx->fromUTF8String("toString")->asString(ctx) : nullptr;

    if (keyValueOf)
        bp = bp->setAttribute(ctx, keyValueOf,
            ctx->fromMethod(mutableBp, booleanValueOf));
    if (keyToString)
        bp = bp->setAttribute(ctx, keyToString,
            ctx->fromMethod(mutableBp, booleanToString));

    space->booleanPrototype = const_cast<proto::ProtoObject*>(bp);
}

// --- ensureBooleanConstructor ---
void ensureBooleanConstructor(proto::ProtoContext* ctx, const proto::ProtoObject** globalRoot)
{
    if (!ctx || !globalRoot || !*globalRoot) return;

    const proto::ProtoObject* boolKeyObj = ctx->fromUTF8String("Boolean");
    const proto::ProtoString* keyBoolean = boolKeyObj ? boolKeyObj->asString(ctx) : nullptr;
    if (!keyBoolean) return;

    const proto::ProtoObject* existing = (*globalRoot)->getAttribute(ctx, keyBoolean, false);
    if (existing && existing != PROTO_NONE) return;

    // Create constructor as child of methodPrototype (Function.prototype)
    const proto::ProtoObject* ctorParent =
        (ctx->space && ctx->space->methodPrototype) ? ctx->space->methodPrototype : nullptr;
    const proto::ProtoObject* ctor = ctorParent
        ? ctorParent->newChild(ctx, true)
        : ctx->newObject(true);
    if (!ctor) return;

    // ctor.name = "Boolean"
    const proto::ProtoString* nameKey = JSSymbols::name(ctx);
    if (nameKey)
        ctor = ctor->setAttribute(ctx, nameKey, ctx->fromUTF8String("Boolean"));

    // ctor.prototype = Boolean.prototype
    const proto::ProtoString* protoKey = JSSymbols::prototype(ctx);
    const proto::ProtoObject* boolProto = ctx->space ? ctx->space->booleanPrototype : nullptr;
    if (protoKey && boolProto && boolProto != PROTO_NONE)
        ctor = ctor->setAttribute(ctx, protoKey, boolProto);

    // __boolean_ctor__ marker for instanceof checks
    const proto::ProtoObject* boolCtorKeyObj = ctx->fromUTF8String("__boolean_ctor__");
    const proto::ProtoString* boolCtorKey = boolCtorKeyObj ? boolCtorKeyObj->asString(ctx) : nullptr;
    if (boolCtorKey)
        ctor = ctor->setAttribute(ctx, boolCtorKey, PROTO_TRUE);

    // __construct__ — MUST use ctx->fromMethod (NOT wrapNativeFunction).
    // OP_call_constructor checks isMethod() directly on the __construct__ value;
    // wrapNativeFunction returns a ProtoObject (isMethod() == false), so the body
    // would never be invoked.
    const proto::ProtoObject* ctorKeyObj = ctx->fromUTF8String("__construct__");
    const proto::ProtoString* ctorKey = ctorKeyObj ? ctorKeyObj->asString(ctx) : nullptr;
    if (ctorKey) {
        proto::ProtoObject* mCtor = const_cast<proto::ProtoObject*>(ctor);
        const proto::ProtoObject* ctorMethod = ctx->fromMethod(mCtor, booleanConstruct);
        if (ctorMethod && ctorMethod != PROTO_NONE)
            ctor = ctor->setAttribute(ctx, ctorKey, ctorMethod);
    }

    *globalRoot = (*globalRoot)->setAttribute(ctx, keyBoolean, ctor);
}

} // namespace protojs
