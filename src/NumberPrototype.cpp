#include "NumberPrototype.h"
#include "FunctionPrototype.h"
#include "JSSymbols.h"
#include "PrototypeUtils.h"
#include "headers/protoCore.h"
#include "runtime/ProtoInterpreter.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>

namespace protojs {

namespace {

double getNumberValue(proto::ProtoContext* context, const proto::ProtoObject* self) {
    if (!self || self == PROTO_NONE) return 0.0;
    if (self->isInteger(context)) return static_cast<double>(self->asLong(context));
    if (self->isDouble(context) || self->isFloat(context)) return self->asDouble(context);
    // Number wrapper object: extract from __primitive_value__.
    const proto::ProtoString* pvKey = JSSymbols::primitiveValue(context);
    if (pvKey) {
        const proto::ProtoObject* pv = self->getAttribute(context, pvKey, false);
        if (pv && pv != PROTO_NONE) {
            if (pv->isInteger(context)) return static_cast<double>(pv->asLong(context));
            if (pv->isDouble(context) || pv->isFloat(context)) return pv->asDouble(context);
        }
    }
    return 0.0;
}

/** Throws TypeError if this is null, undefined, or not a Number value/wrapper.
 *  Returns false to abort; caller must return PROTO_NONE. */
static bool requireNumberThis(proto::ProtoContext* ctx,
                               const proto::ProtoObject* self) {
    if (!self || self == PROTO_NONE || self->isNone(ctx)) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Number.prototype method called on incompatible receiver"));
        return false;
    }
    const proto::ProtoObject* nullSentinel = getNullSentinel();
    if (nullSentinel && self == nullSentinel) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Number.prototype method called on incompatible receiver"));
        return false;
    }
    // Valid if self is a numeric primitive.
    if (self->isInteger(ctx) || self->isDouble(ctx) || self->isFloat(ctx))
        return true;
    // Valid if self is a Number wrapper object (has __primitive_value__ that is numeric).
    const proto::ProtoString* pvk = JSSymbols::primitiveValue(ctx);
    if (pvk) {
        const proto::ProtoObject* pv = self->getAttribute(ctx, pvk, false);
        if (pv && pv != PROTO_NONE &&
            (pv->isInteger(ctx) || pv->isDouble(ctx) || pv->isFloat(ctx)))
            return true;
    }
    signalNativeException(makeNativeError(ctx, "TypeError",
        "Number.prototype method called on incompatible receiver"));
    return false;
}

const proto::ProtoObject* numberValueOf(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* /*positionalParameters*/,
    const proto::ProtoSparseList* /*keywordParameters*/)
{
    if (!requireNumberThis(context, self)) return PROTO_NONE;
    // Primitive number: return as-is.
    if (self->isInteger(context) || self->isDouble(context) || self->isFloat(context))
        return self;
    // Number wrapper object: extract and return __primitive_value__.
    const proto::ProtoString* pvKey = JSSymbols::primitiveValue(context);
    if (pvKey) {
        const proto::ProtoObject* pv = self->getAttribute(context, pvKey, false);
        if (pv && pv != PROTO_NONE &&
            (pv->isInteger(context) || pv->isDouble(context) || pv->isFloat(context)))
            return pv;
    }
    return context->fromDouble(0.0); // fallback
}

const proto::ProtoObject* numberToString(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* /*keywordParameters*/)
{
    if (!requireNumberThis(context, self)) return PROTO_NONE;
    int radix = 10;
    if (positionalParameters && positionalParameters->getSize(context) > 0) {
        const proto::ProtoObject* radixObj = positionalParameters->getAt(context, 0);
        if (radixObj && radixObj != PROTO_NONE) {
            if (radixObj->isInteger(context)) {
                radix = static_cast<int>(radixObj->asLong(context));
            } else if (radixObj->isDouble(context)) {
                radix = static_cast<int>(radixObj->asDouble(context));
            }
        }
    }
    if (radix < 2 || radix > 36) {
        signalNativeException(makeNativeError(context, "RangeError",
            "Number.prototype.toString() radix must be between 2 and 36"));
        return PROTO_NONE;
    }
    double value = getNumberValue(context, self);
    std::string result;
    // Spec-mandated names for non-finite values (ECMA-262 §7.1.12.1).
    // C's "%g" outputs "nan", "inf", "-inf" which JS test262 rejects.
    if (std::isnan(value)) {
        return context->fromUTF8String("NaN");
    }
    if (std::isinf(value)) {
        return context->fromUTF8String(value < 0 ? "-Infinity" : "Infinity");
    }
    if (radix == 10) {
        char buf[64];
        if (value == static_cast<long long>(value) && value >= -9007199254740992.0 && value <= 9007199254740991.0) {
            snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(value));
        } else {
            snprintf(buf, sizeof(buf), "%.15g", value);
        }
        result = buf;
    } else {
        long long intVal = static_cast<long long>(value);
        if (intVal < 0) {
            result = "-";
            intVal = -intVal;
        }
        const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
        std::string rev;
        unsigned long long u = static_cast<unsigned long long>(intVal);
        do {
            rev += digits[u % static_cast<unsigned>(radix)];
            u /= radix;
        } while (u);
        result.append(rev.rbegin(), rev.rend());
    }
    return context->fromUTF8String(result.c_str());
}

const proto::ProtoObject* numberToFixed(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* /*keywordParameters*/)
{
    if (!requireNumberThis(context, self)) return PROTO_NONE;
    int fractionDigits = 0;
    if (positionalParameters && positionalParameters->getSize(context) > 0) {
        const proto::ProtoObject* fdObj = positionalParameters->getAt(context, 0);
        if (fdObj && fdObj != PROTO_NONE) {
            if (fdObj->isInteger(context)) {
                fractionDigits = static_cast<int>(fdObj->asLong(context));
            } else if (fdObj->isDouble(context)) {
                fractionDigits = static_cast<int>(fdObj->asDouble(context));
            }
        }
    }
    if (fractionDigits < 0 || fractionDigits > 100) {
        signalNativeException(makeNativeError(context, "RangeError",
            "Number.prototype.toFixed() fractionDigits must be between 0 and 100"));
        return PROTO_NONE;
    }
    double value = getNumberValue(context, self);
    char buf[256];
    snprintf(buf, sizeof(buf), "%.*f", fractionDigits, value);
    return context->fromUTF8String(buf);
}

const proto::ProtoObject* numberToExponential(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* /*keywordParameters*/)
{
    if (!requireNumberThis(context, self)) return PROTO_NONE;
    int fractionDigits = -1;
    if (positionalParameters && positionalParameters->getSize(context) > 0) {
        const proto::ProtoObject* fdObj = positionalParameters->getAt(context, 0);
        if (fdObj && fdObj != PROTO_NONE) {
            if (fdObj->isInteger(context)) {
                fractionDigits = static_cast<int>(fdObj->asLong(context));
            } else if (fdObj->isDouble(context)) {
                fractionDigits = static_cast<int>(fdObj->asDouble(context));
            }
        }
    }
    double value = getNumberValue(context, self);
    char buf[256];
    if (fractionDigits >= 0 && fractionDigits <= 100) {
        snprintf(buf, sizeof(buf), "%.*e", fractionDigits, value);
    } else {
        snprintf(buf, sizeof(buf), "%e", value);
    }
    return context->fromUTF8String(buf);
}

const proto::ProtoObject* numberToPrecision(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* /*keywordParameters*/)
{
    if (!requireNumberThis(context, self)) return PROTO_NONE;
    if (!positionalParameters || positionalParameters->getSize(context) == 0) {
        return numberToString(context, self, nullptr, nullptr, nullptr);
    }
    const proto::ProtoObject* precObj = positionalParameters->getAt(context, 0);
    int precision = 0;
    if (precObj && precObj != PROTO_NONE) {
        if (precObj->isInteger(context)) {
            precision = static_cast<int>(precObj->asLong(context));
        } else if (precObj->isDouble(context)) {
            precision = static_cast<int>(precObj->asDouble(context));
        }
    }
    if (precision < 1 || precision > 100) {
        return numberToString(context, self, nullptr, nullptr, nullptr);
    }
    double value = getNumberValue(context, self);
    char buf[256];
    snprintf(buf, sizeof(buf), "%.*g", precision, value);
    return context->fromUTF8String(buf);
}

// ---------------------------------------------------------------------------
// Number static methods
// ---------------------------------------------------------------------------

const proto::ProtoObject* numberIsNaN(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!args || args->getSize(ctx) == 0) return PROTO_FALSE;
    const proto::ProtoObject* v = args->getAt(ctx, 0);
    if (!v || v == PROTO_NONE) return PROTO_FALSE;
    if (!v->isDouble(ctx) && !v->isFloat(ctx)) return PROTO_FALSE;
    return std::isnan(v->asDouble(ctx)) ? PROTO_TRUE : PROTO_FALSE;
}

const proto::ProtoObject* numberIsFinite(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!args || args->getSize(ctx) == 0) return PROTO_FALSE;
    const proto::ProtoObject* v = args->getAt(ctx, 0);
    if (!v || v == PROTO_NONE) return PROTO_FALSE;
    if (v->isInteger(ctx)) return PROTO_TRUE;
    if (v->isDouble(ctx) || v->isFloat(ctx))
        return std::isfinite(v->asDouble(ctx)) ? PROTO_TRUE : PROTO_FALSE;
    return PROTO_FALSE;
}

const proto::ProtoObject* numberIsInteger(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!args || args->getSize(ctx) == 0) return PROTO_FALSE;
    const proto::ProtoObject* v = args->getAt(ctx, 0);
    if (!v || v == PROTO_NONE) return PROTO_FALSE;
    if (v->isInteger(ctx)) return PROTO_TRUE;
    if (v->isDouble(ctx) || v->isFloat(ctx)) {
        double d = v->asDouble(ctx);
        if (!std::isfinite(d)) return PROTO_FALSE;
        return (d == std::trunc(d)) ? PROTO_TRUE : PROTO_FALSE;
    }
    return PROTO_FALSE;
}

const proto::ProtoObject* numberIsSafeInteger(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!args || args->getSize(ctx) == 0) return PROTO_FALSE;
    const proto::ProtoObject* v = args->getAt(ctx, 0);
    if (!v || v == PROTO_NONE) return PROTO_FALSE;
    double d = 0.0;
    if (v->isInteger(ctx)) d = static_cast<double>(v->asLong(ctx));
    else if (v->isDouble(ctx) || v->isFloat(ctx)) d = v->asDouble(ctx);
    else return PROTO_FALSE;
    if (!std::isfinite(d) || d != std::trunc(d)) return PROTO_FALSE;
    return (std::abs(d) <= 9007199254740991.0) ? PROTO_TRUE : PROTO_FALSE;
}

const proto::ProtoObject* numberParseInt(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!args || args->getSize(ctx) == 0) return ctx->fromDouble(std::numeric_limits<double>::quiet_NaN());
    const proto::ProtoObject* strObj = args->getAt(ctx, 0);
    std::string s;
    if (strObj && strObj != PROTO_NONE) {
        if (strObj->isString(ctx)) {
            const proto::ProtoString* ps = strObj->asString(ctx);
            if (ps) ps->toUTF8String(ctx, s);
        } else if (strObj->isInteger(ctx)) {
            s = std::to_string(strObj->asLong(ctx));
        } else if (strObj->isDouble(ctx)) {
            char buf[64]; snprintf(buf, sizeof(buf), "%.15g", strObj->asDouble(ctx)); s = buf;
        }
    }
    // Trim leading whitespace
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r' || s[i] == '\f' || s[i] == '\v')) i++;
    s = s.substr(i);

    int radix = 10;
    if (args->getSize(ctx) > 1) {
        const proto::ProtoObject* radixObj = args->getAt(ctx, 1);
        if (radixObj && radixObj != PROTO_NONE) {
            if (radixObj->isInteger(ctx)) radix = static_cast<int>(radixObj->asLong(ctx));
            else if (radixObj->isDouble(ctx)) radix = static_cast<int>(radixObj->asDouble(ctx));
        }
    }

    // Handle 0x prefix for hex
    if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        if (radix == 10 || radix == 16) { radix = 16; s = s.substr(2); }
    } else if (s.size() >= 2 && s[0] == '0' && (s[1] == 'b' || s[1] == 'B') && radix == 2) {
        s = s.substr(2);
    } else if (s.size() >= 2 && s[0] == '0' && (s[1] == 'o' || s[1] == 'O') && radix == 8) {
        s = s.substr(2);
    }

    if (radix < 2 || radix > 36) return ctx->fromDouble(std::numeric_limits<double>::quiet_NaN());
    if (s.empty()) return ctx->fromDouble(std::numeric_limits<double>::quiet_NaN());

    char* end = nullptr;
    long long result = std::strtoll(s.c_str(), &end, radix);
    if (end == s.c_str()) return ctx->fromDouble(std::numeric_limits<double>::quiet_NaN());
    return ctx->fromInteger(result);
}

const proto::ProtoObject* numberParseFloat(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!args || args->getSize(ctx) == 0) return ctx->fromDouble(std::numeric_limits<double>::quiet_NaN());
    const proto::ProtoObject* strObj = args->getAt(ctx, 0);
    if (!strObj || strObj == PROTO_NONE) return ctx->fromDouble(std::numeric_limits<double>::quiet_NaN());
    if (strObj->isInteger(ctx)) return strObj;
    if (strObj->isDouble(ctx) || strObj->isFloat(ctx)) return strObj;
    std::string s;
    if (strObj->isString(ctx)) {
        const proto::ProtoString* ps = strObj->asString(ctx);
        if (ps) ps->toUTF8String(ctx, s);
    } else { return ctx->fromDouble(std::numeric_limits<double>::quiet_NaN()); }
    // Trim leading whitespace
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r' || s[i] == '\f' || s[i] == '\v')) i++;
    s = s.substr(i);
    if (s == "Infinity" || s == "+Infinity") return ctx->fromDouble(std::numeric_limits<double>::infinity());
    if (s == "-Infinity") return ctx->fromDouble(-std::numeric_limits<double>::infinity());
    char* end = nullptr;
    double result = std::strtod(s.c_str(), &end);
    if (end == s.c_str()) return ctx->fromDouble(std::numeric_limits<double>::quiet_NaN());
    return ctx->fromDouble(result);
}

} // namespace

void BuildNumberPrototype(proto::ProtoSpace* space, proto::ProtoContext* ctx,
                         const proto::ProtoObject* objectProto) {
    if (!space || !ctx || !objectProto) return;

    // Must be mutable so JS-level assignments (Number.prototype.x = y) modify
    // the object in-place, keeping space->smallIntegerPrototype consistent and
    // ensuring attribute lookups on primitive numbers find newly added properties.
    const proto::ProtoObject* numberProto = objectProto->newChild(ctx, true);

    numberProto = installNonEnumerableMethod(ctx, numberProto, "valueOf",       numberValueOf,       0);
    numberProto = installNonEnumerableMethod(ctx, numberProto, "toString",      numberToString,      1);
    numberProto = installNonEnumerableMethod(ctx, numberProto, "toFixed",       numberToFixed,       1);
    numberProto = installNonEnumerableMethod(ctx, numberProto, "toExponential", numberToExponential, 1);
    numberProto = installNonEnumerableMethod(ctx, numberProto, "toPrecision",   numberToPrecision,   1);

    space->smallIntegerPrototype = const_cast<proto::ProtoObject*>(numberProto);
    space->largeIntegerPrototype = const_cast<proto::ProtoObject*>(numberProto);
    space->doublePrototype       = const_cast<proto::ProtoObject*>(numberProto);
}

// ---------------------------------------------------------------------------
// Number constructor helper — invoked by OP_call_constructor to initialise
// the newly-created wrapper object.
// ---------------------------------------------------------------------------
static const proto::ProtoObject* numberConstruct(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!ctx || !self || self == PROTO_NONE) return self;
    double val = 0.0;
    if (args && args->getSize(ctx) > 0) {
        const proto::ProtoObject* a = args->getAt(ctx, 0);
        if (a && a != PROTO_NONE) {
            if (a->isInteger(ctx)) val = static_cast<double>(a->asLong(ctx));
            else if (a->isDouble(ctx) || a->isFloat(ctx)) val = a->asDouble(ctx);
            else if (a->isBoolean(ctx)) val = (a == PROTO_TRUE) ? 1.0 : 0.0;
            else if (a->isString(ctx)) {
                std::string s;
                const proto::ProtoString* ps = a->asString(ctx);
                if (ps) {
                    ps->toUTF8String(ctx, s);
                    // Trim leading and trailing ASCII whitespace (ES spec ToNumber)
                    size_t start = s.find_first_not_of(" \t\n\r\f\v");
                    size_t end   = s.find_last_not_of(" \t\n\r\f\v");
                    if (start == std::string::npos) {
                        val = 0.0; // empty or whitespace-only string → 0
                    } else {
                        s = s.substr(start, end - start + 1);
                        if (s == "Infinity" || s == "+Infinity")
                            val = std::numeric_limits<double>::infinity();
                        else if (s == "-Infinity")
                            val = -std::numeric_limits<double>::infinity();
                        else {
                            char* endPtr = nullptr;
                            double parsed = std::strtod(s.c_str(), &endPtr);
                            val = (endPtr == s.c_str() + s.size())
                                      ? parsed
                                      : std::numeric_limits<double>::quiet_NaN();
                        }
                    }
                }
            }
        }
    }
    const proto::ProtoString* pvKey = JSSymbols::primitiveValue(ctx);
    if (pvKey)
        self = self->setAttribute(ctx, pvKey, ctx->fromDouble(val));
    return self;
}

void ensureNumberConstructor(proto::ProtoContext* ctx,
                             const proto::ProtoObject** globalRoot) {
    if (!ctx || !globalRoot || !*globalRoot) return;
    const proto::ProtoString* keyNumber = JSSymbols::Number(ctx);
    if (!keyNumber) return;

    const proto::ProtoObject* existing = (*globalRoot)->getAttribute(ctx, keyNumber, false);
    if (existing && existing != PROTO_NONE) return;

    const proto::ProtoObject* ctorParent =
        (ctx->space && ctx->space->methodPrototype) ? ctx->space->methodPrototype : nullptr;
    const proto::ProtoObject* ctor = ctorParent
        ? ctorParent->newChild(ctx, true)
        : ctx->newObject(true);
    if (!ctor) return;

    auto reg = [&](const char* name, proto::ProtoMethod fn, long long length = 1) {
        const proto::ProtoString* key = ctx->fromUTF8String(name)->asString(ctx);
        if (key) {
            const proto::ProtoObject* wrapped = wrapNativeFunction(ctx, fn, name, length, globalRoot);
            if (wrapped && wrapped != PROTO_NONE)
                ctor = ctor->setAttribute(ctx, key, wrapped);
        }
    };

    // Static methods
    reg("isNaN",         numberIsNaN,         1);
    reg("isFinite",      numberIsFinite,       1);
    reg("isInteger",     numberIsInteger,      1);
    reg("isSafeInteger", numberIsSafeInteger,  1);
    reg("parseInt",      numberParseInt,       2);
    reg("parseFloat",    numberParseFloat,     1);

    // Constants
    auto setConst = [&](const char* name, double val) {
        const proto::ProtoObject* keyObj = ctx->fromUTF8String(name);
        const proto::ProtoString* key = keyObj ? keyObj->asString(ctx) : nullptr;
        if (!key) return;
        ctor = ctor->setAttribute(ctx, key, ctx->fromDouble(val));
        // Constants: {writable: false, enumerable: false, configurable: false} → bits = 0x0
        std::string pdKeyStr = "__pd_";
        pdKeyStr += name;
        pdKeyStr += "__";
        const proto::ProtoObject* pdko = ctx->fromUTF8String(pdKeyStr.c_str());
        const proto::ProtoString* pdk = pdko ? pdko->asString(ctx) : nullptr;
        if (pdk) ctor = ctor->setAttribute(ctx, pdk, ctx->fromInteger(0x0));
    };
    setConst("EPSILON",            std::numeric_limits<double>::epsilon());
    setConst("MAX_SAFE_INTEGER",   9007199254740991.0);
    setConst("MIN_SAFE_INTEGER",  -9007199254740991.0);
    setConst("MAX_VALUE",          std::numeric_limits<double>::max());
    setConst("MIN_VALUE",          std::numeric_limits<double>::denorm_min());
    setConst("POSITIVE_INFINITY",  std::numeric_limits<double>::infinity());
    setConst("NEGATIVE_INFINITY", -std::numeric_limits<double>::infinity());
    setConst("NaN",                std::numeric_limits<double>::quiet_NaN());

    const proto::ProtoString* nameKey = JSSymbols::name(ctx);
    if (nameKey) ctor = ctor->setAttribute(ctx, nameKey, ctx->fromUTF8String("Number"));

    // Number.prototype — point to the number prototype already on space.
    const proto::ProtoString* protoKey2 = JSSymbols::prototype(ctx);
    const proto::ProtoObject* numProto = ctx->space ? ctx->space->smallIntegerPrototype : nullptr;
    if (protoKey2 && numProto && numProto != PROTO_NONE)
        ctor = ctor->setAttribute(ctx, protoKey2, numProto);

    // Explicitly mark as a constructor for OP_call_constructor.
    const proto::ProtoString* isCtorKey = ctx->fromUTF8String("__is_constructor__")->asString(ctx);
    if (isCtorKey) ctor = ctor->setAttribute(ctx, isCtorKey, PROTO_TRUE);

    // __number_ctor__ marker for typeof/instanceof checks.
    const proto::ProtoString* numCtorKey = ctx->fromUTF8String("__number_ctor__")->asString(ctx);
    if (numCtorKey) ctor = ctor->setAttribute(ctx, numCtorKey, PROTO_TRUE);

    // __construct__ native — invoked by OP_call_constructor for native constructors.
    // Must be stored as a raw method (isMethod() == true), not a wrapped function object.
    const proto::ProtoString* ctorMethodKey = ctx->fromUTF8String("__construct__")->asString(ctx);
    if (ctorMethodKey) {
        proto::ProtoObject* mCtor2 = const_cast<proto::ProtoObject*>(ctor);
        const proto::ProtoObject* ctorMethodObj = ctx->fromMethod(mCtor2, numberConstruct);
        if (ctorMethodObj && ctorMethodObj != PROTO_NONE)
            ctor = ctor->setAttribute(ctx, ctorMethodKey, ctorMethodObj);
    }

    *globalRoot = (*globalRoot)->setAttribute(ctx, keyNumber, ctor);
}

} // namespace protojs
