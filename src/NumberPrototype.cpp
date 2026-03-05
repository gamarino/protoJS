#include "NumberPrototype.h"
#include "ProtoJSStringCache.h"
#include "headers/protoCore.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace protojs {

namespace {

double getNumberValue(proto::ProtoContext* context, const proto::ProtoObject* self) {
    if (self->isInteger(context)) {
        return static_cast<double>(self->asLong(context));
    }
    if (self->isDouble(context)) {
        return self->asDouble(context);
    }
    return 0.0;
}

const proto::ProtoObject* numberValueOf(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* /*positionalParameters*/,
    const proto::ProtoSparseList* /*keywordParameters*/)
{
    (void)context;
    return self;
}

const proto::ProtoObject* numberToString(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* /*keywordParameters*/)
{
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
        radix = 10;
    }
    double value = getNumberValue(context, self);
    std::string result;
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
        fractionDigits = 0;
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

} // namespace

void BuildNumberPrototype(proto::ProtoSpace* space, proto::ProtoContext* ctx,
                         const proto::ProtoObject* objectProto) {
    if (!space || !ctx || !objectProto) return;

    const proto::ProtoObject* numberProto = objectProto->newChild(ctx, false);
    proto::ProtoObject* mutableProto = const_cast<proto::ProtoObject*>(numberProto);

    const proto::ProtoString* keyValueOf = ProtoJSStringCache::getKey(ctx, "valueOf");
    const proto::ProtoString* keyToString = ProtoJSStringCache::getKey(ctx, "toString");
    const proto::ProtoString* keyToFixed = ProtoJSStringCache::getKey(ctx, "toFixed");
    const proto::ProtoString* keyToExponential = ProtoJSStringCache::getKey(ctx, "toExponential");
    const proto::ProtoString* keyToPrecision = ProtoJSStringCache::getKey(ctx, "toPrecision");

    numberProto = numberProto->setAttribute(ctx, keyValueOf,
        ctx->fromMethod(mutableProto, numberValueOf));
    numberProto = numberProto->setAttribute(ctx, keyToString,
        ctx->fromMethod(mutableProto, numberToString));
    numberProto = numberProto->setAttribute(ctx, keyToFixed,
        ctx->fromMethod(mutableProto, numberToFixed));
    numberProto = numberProto->setAttribute(ctx, keyToExponential,
        ctx->fromMethod(mutableProto, numberToExponential));
    numberProto = numberProto->setAttribute(ctx, keyToPrecision,
        ctx->fromMethod(mutableProto, numberToPrecision));

    space->smallIntegerPrototype = const_cast<proto::ProtoObject*>(numberProto);
    space->largeIntegerPrototype = const_cast<proto::ProtoObject*>(numberProto);
    space->doublePrototype = const_cast<proto::ProtoObject*>(numberProto);
}

} // namespace protojs
