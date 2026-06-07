#include "BooleanPrototype.h"
#include "JSSymbols.h"
#include "PrototypeUtils.h"
#include "TypeBridge.h"
#include "runtime/ProtoInterpreter.h"
#include "headers/protoCore.h"
#include <cmath>
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
        // Falsy: undefined (PROTO_NONE / t_undefinedSentinel) and null
        // (t_nullSentinel).  Pre-fix the loop reached `val = true` for
        // both `null` and the heap undefined sentinel because they are
        // non-null pointers other than PROTO_NONE.
        if (!a || a == PROTO_NONE
            || a == protojs::getUndefinedSentinel()
            || a == protojs::getNullSentinel()
            || a->isNone(ctx)) {
            val = false;
        } else if (a == PROTO_TRUE) {
            val = true;
        } else if (a == PROTO_FALSE) {
            val = false;
        } else if (a->isBoolean(ctx)) {
            val = a->asBoolean(ctx);
        } else if (a->isInteger(ctx)) {
            val = a->asLong(ctx) != 0;
        } else if (a->isDouble(ctx) || a->isFloat(ctx)) {
            double d = a->asDouble(ctx);
            val = (d != 0.0) && !std::isnan(d);
        } else if (a->isString(ctx)) {
            std::string s;
            const proto::ProtoString* ps = a->asString(ctx);
            if (ps) {
                ps->toUTF8String(ctx, s);
                val = !s.empty();
            }
        } else {
            val = true; // any other non-falsy object → truthy
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

    // Must be mutable so JS-level assignments (Boolean.prototype.x = y) modify
    // the object in-place. An immutable prototype would produce a new snapshot
    // on every setAttribute, leaving space->booleanPrototype stale and causing
    // attribute lookups on primitive booleans (PROTO_TRUE/PROTO_FALSE) to miss
    // the newly assigned properties.
    const proto::ProtoObject* bp = objectProto->newChild(ctx, true);

    // ECMA-262 §20.3.3: "The Boolean prototype object is itself an
    // ordinary object … it has a [[BooleanData]] internal slot whose
    // value is false."  Install the slot now so methods that consult
    // thisBooleanValue (valueOf / toString) treat `Boolean.prototype`
    // as the primitive `false` rather than throwing TypeError.
    {
        const proto::ProtoString* pvKey = JSSymbols::primitiveValue(ctx);
        if (pvKey) bp = bp->setAttribute(ctx, pvKey, PROTO_FALSE);
    }
    // Install via installNonEnumerableMethod so each method receives
    // the spec name + length attributes. methodPrototype isn't set yet
    // (BuildBooleanPrototype runs before ensureFunctionPrototype), so
    // ensureBooleanConstructor re-installs to attach .call/.apply/.bind.
    bp = installNonEnumerableMethod(ctx, bp, "valueOf",  booleanValueOf,  0);
    bp = installNonEnumerableMethod(ctx, bp, "toString", booleanToString, 0);

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

    // BuildBooleanPrototype ran before ensureFunctionPrototype, so the
    // method wrappers have parent=null and don't inherit .call/.apply/
    // .bind from Function.prototype. Re-install now that
    // methodPrototype is available.
    if (ctx->space && ctx->space->booleanPrototype && ctx->space->methodPrototype) {
        const proto::ProtoObject* bp = ctx->space->booleanPrototype;
        bp = installNonEnumerableMethod(ctx, bp, "valueOf",  booleanValueOf,  0);
        bp = installNonEnumerableMethod(ctx, bp, "toString", booleanToString, 0);
        ctx->space->booleanPrototype = const_cast<proto::ProtoObject*>(bp);
    }

    // Create constructor as child of methodPrototype (Function.prototype)
    const proto::ProtoObject* ctorParent =
        (ctx->space && ctx->space->methodPrototype) ? ctx->space->methodPrototype : nullptr;
    const proto::ProtoObject* ctor = ctorParent
        ? ctorParent->newChild(ctx, true)
        : ctx->newObject(true);
    if (!ctor) return;

    // ctor.name = "Boolean" with §17 descriptor 0x2 (!writable,
    // !enumerable, configurable).
    const proto::ProtoString* nameKey = JSSymbols::name(ctx);
    if (nameKey) {
        ctor = ctor->setAttribute(ctx, nameKey, ctx->fromUTF8String("Boolean"));
        const proto::ProtoString* pdns = JSSymbols::pdName(ctx);
        if (pdns) ctor = ctor->setAttribute(ctx, pdns, ctx->fromInteger(0x2LL));
    }
    // Boolean.length === 1 per §20.3.1.1; descriptor non-writable,
    // non-enumerable, configurable (bits 0x2).
    const proto::ProtoString* lenKey = JSSymbols::length(ctx);
    if (lenKey) {
        ctor = ctor->setAttribute(ctx, lenKey, ctx->fromInteger(1LL));
        const proto::ProtoString* pdlk = JSSymbols::pdLength(ctx);
        if (pdlk) ctor = ctor->setAttribute(ctx, pdlk, ctx->fromInteger(0x2LL));
    }
    // Hot-path hint mirroring wrapNativeFunction / setNWCDescriptor /
    // ProtoNativeModule::addMethod earlier this round: __pd_name__ and
    // __pd_length__ both stamp writable=false, but the writability
    // enforcement in resolvePutFieldOOP only runs when
    // __has_nonwritable_props__ is set on the target.  Without it
    // `Boolean.name = "X"` succeeded silently despite the descriptor.
    const proto::ProtoString* hnwK = JSSymbols::hasNonWritableProps(ctx);
    if (hnwK) ctor = ctor->setAttribute(ctx, hnwK, PROTO_TRUE);

    // ctor.prototype = Boolean.prototype
    // Per ECMA-262 §20.3.2.1 the `prototype` property of a built-in
    // constructor has attributes {writable:false, enumerable:false,
    // configurable:false} — bits 0x0.  Pre-fix the property was
    // installed without a descriptor sidecar, defaulting to fully
    // enumerable, so `for-in (Boolean)` listed "prototype" and
    // `Boolean.propertyIsEnumerable('prototype')` returned true.
    const proto::ProtoString* protoKey = JSSymbols::prototype(ctx);
    const proto::ProtoObject* boolProto = ctx->space ? ctx->space->booleanPrototype : nullptr;
    if (protoKey && boolProto && boolProto != PROTO_NONE) {
        ctor = ctor->setAttribute(ctx, protoKey, boolProto);
        const proto::ProtoObject* pdpo = ctx->fromUTF8String("__pd_prototype__");
        const proto::ProtoString* pdpk = pdpo ? pdpo->asString(ctx) : nullptr;
        if (pdpk) ctor = ctor->setAttribute(ctx, pdpk, ctx->fromInteger(0x0LL));
    }

    // Explicitly mark as a constructor for OP_call_constructor.
    const proto::ProtoObject* isCtorObj = ctx->fromUTF8String("__is_constructor__");
    const proto::ProtoString* isCtorKey = isCtorObj ? isCtorObj->asString(ctx) : nullptr;
    if (isCtorKey) ctor = ctor->setAttribute(ctx, isCtorKey, PROTO_TRUE);

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

    // Boolean.prototype.constructor === Boolean per §20.3.2.1.
    // Without this the spec-mandated `b.constructor === Boolean`
    // identity is broken — `b.constructor.prototype` is undefined,
    // every prop-desc test on the constructor reference fails.
    if (boolProto && boolProto != PROTO_NONE) {
        const proto::ProtoString* ctorWordKey = JSSymbols::constructor(ctx);
        if (ctorWordKey) {
            const proto::ProtoObject* updatedProto =
                boolProto->setAttribute(ctx, ctorWordKey, ctor);
            // Non-enumerable per §20.3.2.1 — bits 0x3 (writable+configurable).
            if (updatedProto && updatedProto != PROTO_NONE) {
                const proto::ProtoString* pdk = JSSymbols::pdConstructor(ctx);
                if (pdk) updatedProto = updatedProto->setAttribute(ctx, pdk,
                    ctx->fromInteger(0x3LL));
            }
            if (ctx->space && updatedProto && updatedProto != PROTO_NONE) {
                ctx->space->booleanPrototype = const_cast<proto::ProtoObject*>(updatedProto);
            }
        }
    }

    *globalRoot = (*globalRoot)->setAttribute(ctx, keyBoolean, ctor);
    // §17 globalThis.Boolean descriptor 0x3.
    {
        const proto::ProtoObject* pdo = ctx->fromUTF8String("__pd_Boolean__");
        const proto::ProtoString* pdk = pdo ? pdo->asString(ctx) : nullptr;
        if (pdk) *globalRoot = (*globalRoot)->setAttribute(ctx, pdk,
            ctx->fromInteger(0x3LL));
    }
}

} // namespace protojs
