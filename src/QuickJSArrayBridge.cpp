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
    // Legacy QuickJS array bridge intentionally disabled.
    // - On the protoCore eval path, the QuickJS interpreter does not run, so
    //   no array mirroring is needed; arrays are handled directly by
    //   ProtoArrayAdapter / ProtoInterpreter on the protoCore heap.
    // - On the legacy JS_Eval path, QuickJS arrays behave exactly like in
    //   upstream QuickJS; no protoCore backing is maintained.
    (void)ctx;
    (void)this_obj;
    (void)idx;
}

void protojs_array_mirror_after_set_length(JSContext* ctx,
                                           JSValueConst this_obj)
{
    // See comment in protojs_array_mirror_after_set_index: the legacy array
    // mirroring bridge is fully disabled.
    (void)ctx;
    (void)this_obj;
}

} // extern "C"

