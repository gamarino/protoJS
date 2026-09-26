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
    // self is nullptr, NOT methodObj.  fromMethod's self is a strong, traced
    // reference (protoCore/headers/protoCore.h), so binding the cell to the very
    // object it is then installed on closes a reference cycle through a mutable —
    // and a cycle among mutables is never collected (protoCore
    // docs/MemoryModel.md S7).  That accounted for about 140 of the 177 cycles a
    // bare `protojs -e 1` process carried, and the binding was dead: protoJS has
    // zero asMethodSelf call sites and all 24 asMethod sites pass the real
    // receiver, so nothing could read it.  FunctionPrototype.cpp:1302 already
    // does the identical job with nullptr.  The memory saved is negligible —
    // these objects hang off globalThis and are perennial — but a detector that
    // reports 140 known-benign cycles is noise, and clearing the benign is what
    // makes the harmful visible.  Guarded by cli/no-method-self-cycles.
    const proto::ProtoObject* rawMethod = ctx->fromMethod(nullptr, fn);
    if (rawMethod) methodObj = methodObj->setAttribute(ctx, nfKey, rawMethod);

    // Set length: {value: argc, writable: false, enumerable: false, configurable: true}
    // bits = 0x2 → bit0(writable)=0, bit1(configurable)=1, bit2(enumerable)=0
    const proto::ProtoString* lenKey = JSSymbols::length(ctx);
    if (lenKey) {
        methodObj = methodObj->setAttribute(ctx, lenKey,
                                            ctx->fromInteger(static_cast<long long>(argc)));
        const proto::ProtoString* pdLen = JSSymbols::pdLength(ctx);
        if (pdLen) methodObj = methodObj->setAttribute(ctx, pdLen, ctx->fromInteger(0x2LL));
    }

    // Set name: {value: methodName, writable: false, enumerable: false, configurable: true}
    const proto::ProtoString* nmKey = JSSymbols::name(ctx);
    if (nmKey) {
        methodObj = methodObj->setAttribute(ctx, nmKey, ctx->fromUTF8String(methodName));
        const proto::ProtoString* pdNm = JSSymbols::pdName(ctx);
        if (pdNm) methodObj = methodObj->setAttribute(ctx, pdNm, ctx->fromInteger(0x2LL));
    }
    // Both length and name above are writable=false.  Stamp the hot-path
    // hint so resolvePutFieldOOP consults __pd_<key>__ when user code
    // writes to this method object: e.g. `arr.push.length = 99` must
    // silently fail per the spec.
    {
        const proto::ProtoString* hnw = JSSymbols::hasNonWritableProps(ctx);
        if (hnw) methodObj = methodObj->setAttribute(ctx, hnw, PROTO_TRUE);
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
