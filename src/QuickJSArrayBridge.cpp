/**
 * QuickJSArrayBridge: mirrors QuickJS array index/length writes into protoCore.
 * LEGACY PATH ONLY: when eval uses the protoCore path (setUseProtoEval(true)),
 * the QuickJS interpreter is not run, so these hooks are never called during
 * script execution. They are only used when the legacy JS_Eval path is active.
 */
#include "quickjs.h"
#include "quickjs_array_bridge.h"
#include "ExecutionEngine.h"
#include "GCBridge.h"
#include "JSContext.h"
#include "ProtoArrayAdapter.h"
#include "ProtoJSStringCache.h"
#include "TypeBridge.h"

namespace protojs {

using namespace proto;

/**
 * Internal helper to obtain (or lazily create) the protoCore backing
 * array associated with a given JS Array value. Uses JS Array prototype
 * and newChild(mutable=true) when creating a new backing.
 */
static const ProtoObject* ensure_array_backing(JSContext* ctx,
                                               JSValueConst this_obj,
                                               ProtoContext* pContext)
{
    const ProtoObject* backing =
        GCBridge::getProtoObject((JSValue)this_obj, ctx);
    if (backing) {
        return backing;
    }

    JSContextWrapper* wrapper = static_cast<JSContextWrapper*>(JS_GetContextOpaque(ctx));
    const ProtoObject* arrayProto = wrapper ? wrapper->getJSArrayPrototype() : nullptr;
    if (arrayProto) {
        backing = arrayProto->newChild(pContext, true);
        const ProtoString* lengthKey = ProtoJSStringCache::getKey(pContext, "length");
        backing = backing->setAttribute(pContext, lengthKey, pContext->fromInteger(0));
    } else {
        backing = ProtoArrayAdapter::createArray(pContext);
    }
    GCBridge::registerMapping((JSValue)this_obj, backing, ctx);
    return backing;
}

} // namespace protojs

extern "C" {

void protojs_array_mirror_after_set_index(JSContext* ctx,
                                          JSValueConst this_obj,
                                          uint32_t idx)
{
    // Only handle real Arrays; bail out quickly otherwise.
    if (!JS_IsArray(ctx, this_obj)) {
        return;
    }

    proto::ProtoContext* pContext =
        protojs::ExecutionEngine::getProtoContext(ctx);
    if (!pContext) {
        return;
    }

    // Obtain or create the backing array in protoCore.
    const proto::ProtoObject* backing =
        protojs::ensure_array_backing(ctx, this_obj, pContext);

    // Read the current JS value at the index and mirror it.
    JSValue val = JS_GetPropertyUint32(ctx, this_obj, idx);
    if (JS_IsException(val)) {
        return;
    }

    const proto::ProtoObject* pVal =
        protojs::TypeBridge::fromJS(ctx, val, pContext);
    JS_FreeValue(ctx, val);

    const proto::ProtoString* indexKey = protojs::ProtoJSStringCache::getIndexKey(pContext, idx);
    const proto::ProtoObject* newBacking = backing->setAttribute(pContext, indexKey, pVal);

    protojs::GCBridge::registerMapping((JSValue)this_obj, newBacking, ctx);
}

void protojs_array_mirror_after_set_length(JSContext* ctx,
                                           JSValueConst this_obj)
{
    // Only handle real Arrays; bail out quickly otherwise.
    if (!JS_IsArray(ctx, this_obj)) {
        return;
    }

    proto::ProtoContext* pContext =
        protojs::ExecutionEngine::getProtoContext(ctx);
    if (!pContext) {
        return;
    }

    // Obtain or create backing, then read JS length and mirror to protoCore.
    const proto::ProtoObject* backing =
        protojs::ensure_array_backing(ctx, this_obj, pContext);

    JSValue lenVal = JS_GetPropertyStr(ctx, this_obj, "length");
    if (JS_IsException(lenVal)) {
        return;
    }
    uint32_t len = 0;
    if (JS_ToUint32(ctx, &len, lenVal) < 0) {
        JS_FreeValue(ctx, lenVal);
        return;
    }
    JS_FreeValue(ctx, lenVal);

    const proto::ProtoString* lengthKey = protojs::ProtoJSStringCache::getKey(pContext, "length");
    const proto::ProtoObject* newBacking = backing->setAttribute(pContext, lengthKey, pContext->fromInteger(static_cast<long long>(len)));
    protojs::GCBridge::registerMapping((JSValue)this_obj, newBacking, ctx);
}

} // extern "C"

