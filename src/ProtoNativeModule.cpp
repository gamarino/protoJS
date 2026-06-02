#include "ProtoNativeModule.h"
#include "JSSymbols.h"

namespace protojs {

const proto::ProtoObject* ProtoNativeModule::addMethod(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* obj,
    const char* name,
    proto::ProtoMethod fn)
{
    if (!ctx || !obj || !name || !fn) return obj;
    // Wrap in a function-object so the spec-mandated `name` and
    // `length` properties (descriptor 0x2: configurable, non-writable,
    // non-enumerable) are visible. Raw ProtoMethod cells expose only
    // the callable surface. We don't know the spec length from the
    // NativeEntry signature, so default to 0 — call sites that need a
    // specific length should install the method through their own
    // wrapNativeFunction.
    const proto::ProtoObject* wrapper = ctx->space && ctx->space->methodPrototype
        ? ctx->space->methodPrototype->newChild(ctx, true)
        : ctx->newObject(true);
    if (!wrapper) return obj;
    const proto::ProtoString* nfk = JSSymbols::nativeFn(ctx);
    if (nfk) wrapper = wrapper->setAttribute(ctx, nfk, ctx->fromMethod(nullptr, fn));
    const proto::ProtoString* lenk = JSSymbols::length(ctx);
    if (lenk) {
        wrapper = wrapper->setAttribute(ctx, lenk, ctx->fromInteger(0LL));
        const proto::ProtoObject* pdlo = ctx->fromUTF8String("__pd_length__");
        const proto::ProtoString* pdlk = pdlo ? pdlo->asString(ctx) : nullptr;
        if (pdlk) wrapper = wrapper->setAttribute(ctx, pdlk, ctx->fromInteger(0x2LL));
    }
    const proto::ProtoString* nmk = JSSymbols::name(ctx);
    if (nmk) {
        wrapper = wrapper->setAttribute(ctx, nmk, ctx->fromUTF8String(name));
        const proto::ProtoObject* pdno = ctx->fromUTF8String("__pd_name__");
        const proto::ProtoString* pdnk = pdno ? pdno->asString(ctx) : nullptr;
        if (pdnk) wrapper = wrapper->setAttribute(ctx, pdnk, ctx->fromInteger(0x2LL));
    }
    const proto::ProtoString* key = ctx->fromUTF8String(name)->asString(ctx);
    if (!key) return obj;
    obj = obj->setAttribute(ctx, key, wrapper);
    // ECMA-262 §17: built-in methods carry descriptor
    // {writable:true, enumerable:false, configurable:true} → 0x3.
    std::string pdStr = std::string("__pd_") + name + "__";
    const proto::ProtoObject* pdo = ctx->fromUTF8String(pdStr.c_str());
    const proto::ProtoString* pdk = pdo ? pdo->asString(ctx) : nullptr;
    if (pdk) obj = obj->setAttribute(ctx, pdk, ctx->fromInteger(0x3LL));
    return obj;
}

const proto::ProtoObject* ProtoNativeModule::buildModule(
    proto::ProtoContext* ctx,
    const NativeEntry* entries,
    size_t count)
{
    if (!ctx || !entries) return nullptr;
    const proto::ProtoObject* obj = ctx->newObject(true);
    if (!obj) return nullptr;
    for (size_t i = 0; i < count; i++) {
        if (!entries[i].name || !entries[i].fn) continue;
        obj = addMethod(ctx, obj, entries[i].name, entries[i].fn);
        if (!obj) return nullptr;
    }
    return obj;
}

const proto::ProtoObject* ProtoNativeModule::registerOnGlobal(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* globalObj,
    const char* name,
    const proto::ProtoObject* moduleObj)
{
    if (!ctx || !globalObj || !name || !moduleObj) return globalObj;
    const proto::ProtoString* key = ctx->fromUTF8String(name)->asString(ctx);
    if (!key) return globalObj;
    return globalObj->setAttribute(ctx, key, moduleObj);
}

} // namespace protojs
