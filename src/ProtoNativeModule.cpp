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
    const proto::ProtoObject* fnObj = ctx->fromMethod(nullptr, fn);
    if (!fnObj) return obj;
    const proto::ProtoString* key = ctx->fromUTF8String(name)->asString(ctx);
    if (!key) return obj;
    return obj->setAttribute(ctx, key, fnObj);
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
