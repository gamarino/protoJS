#include "PrototypeUtils.h"
#include "JSSymbols.h"
#include <string>

namespace protojs {

const proto::ProtoObject* installNonEnumerableMethod(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* proto,
    const char* methodName,
    proto::ProtoMethod fn,
    int argc)
{
    if (!ctx || !proto || !methodName || !fn) return proto;

    // Create a mutable wrapper inheriting from methodPrototype so that
    // .call/.apply/.bind resolve via prototype chain.
    const proto::ProtoObject* parent = (ctx->space && ctx->space->methodPrototype)
        ? ctx->space->methodPrototype : nullptr;
    const proto::ProtoObject* methodObj = parent
        ? parent->newChild(ctx, true)
        : ctx->newObject(true);
    if (!methodObj) return proto;

    // Store raw ProtoMethod as __native_fn__ (dispatch checks this).
    const proto::ProtoString* nfKey = JSSymbols::nativeFn(ctx);
    if (!nfKey) return proto;  // Cannot register native fn — skip entire install
    proto::ProtoObject* mMethodObj = const_cast<proto::ProtoObject*>(methodObj);
    const proto::ProtoObject* rawMethod = ctx->fromMethod(mMethodObj, fn);
    if (rawMethod) methodObj = methodObj->setAttribute(ctx, nfKey, rawMethod);

    // Set length: {value: argc, writable: false, enumerable: false, configurable: true}
    // bits = 0x2 → bit0(writable)=0, bit1(configurable)=1, bit2(enumerable)=0
    const proto::ProtoString* lenKey = JSSymbols::length(ctx);
    if (lenKey) {
        methodObj = methodObj->setAttribute(ctx, lenKey,
                                            ctx->fromInteger(static_cast<long long>(argc)));
        const proto::ProtoObject* pdLenObj = ctx->fromUTF8String("__pd_length__");
        const proto::ProtoString* pdLen = pdLenObj ? pdLenObj->asString(ctx) : nullptr;
        if (pdLen) methodObj = methodObj->setAttribute(ctx, pdLen, ctx->fromInteger(0x2LL));
    }

    // Set name: {value: methodName, writable: false, enumerable: false, configurable: true}
    const proto::ProtoString* nmKey = JSSymbols::name(ctx);
    if (nmKey) {
        methodObj = methodObj->setAttribute(ctx, nmKey, ctx->fromUTF8String(methodName));
        const proto::ProtoObject* pdNmObj = ctx->fromUTF8String("__pd_name__");
        const proto::ProtoString* pdNm = pdNmObj ? pdNmObj->asString(ctx) : nullptr;
        if (pdNm) methodObj = methodObj->setAttribute(ctx, pdNm, ctx->fromInteger(0x2LL));
    }

    // Install on proto: {writable: true, enumerable: false, configurable: true}
    // bits = 0x3 → bit0(writable)=1, bit1(configurable)=1, bit2(enumerable)=0
    const proto::ProtoObject* mko = ctx->fromUTF8String(methodName);
    const proto::ProtoString* mk = mko ? mko->asString(ctx) : nullptr;
    if (mk) {
        proto = proto->setAttribute(ctx, mk, methodObj);
        std::string pdStr = std::string("__pd_") + methodName + "__";
        const proto::ProtoObject* pdko = ctx->fromUTF8String(pdStr.c_str());
        const proto::ProtoString* pdks = pdko ? pdko->asString(ctx) : nullptr;
        if (pdks) proto = proto->setAttribute(ctx, pdks, ctx->fromInteger(0x3LL));
    }

    return proto;
}

} // namespace protojs
