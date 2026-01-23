#include "Deferred.h"
#include "TypeBridge.h"
#include <iostream>

namespace protojs {

static JSClassID protojs_deferred_class_id;

typedef struct {
    JSValue func;
    JSValue resolve;
    JSValue reject;
    const proto::ProtoThread* thread;
} DeferredData;

void Deferred::init(JSContext* ctx) {
    JS_NewClassID(&protojs_deferred_class_id);
    JSClassDef class_def = {
        "Deferred",
        finalizer
    };
    JS_NewClass(JS_GetRuntime(ctx), protojs_deferred_class_id, &class_def);

    JSValue proto = JS_NewObject(ctx);
    // Add methods to prototype if needed
    JS_SetClassProto(ctx, protojs_deferred_class_id, proto);

    JSValue ctor = JS_NewCFunction2(ctx, constructor, "Deferred", 1, JS_CFUNC_constructor, 0);
    JS_SetConstructor(ctx, ctor, proto);
    
    JSValue global_obj = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global_obj, "Deferred", ctor);
    JS_FreeValue(ctx, global_obj);
}

JSValue Deferred::constructor(JSContext* ctx, JSValueConst new_target, int argc, JSValueConst* argv) {
    if (argc < 1 || !JS_IsFunction(ctx, argv[0])) {
        return JS_ThrowTypeError(ctx, "Deferred expects a function");
    }

    JSValue obj = JS_NewObjectClass(ctx, protojs_deferred_class_id);
    if (JS_IsException(obj)) return obj;

    DeferredData* data = (DeferredData*)js_mallocz(ctx, sizeof(DeferredData));
    data->func = JS_DupValue(ctx, argv[0]);
    // Create internal promise if we want to follow JS standard, 
    // but the user asked for a custom 'deferred' based on protoCore threads.
    
    JS_SetOpaque(obj, data);

    // Start protoCore thread
    // We need protoContext and pSpace
    // This is a bit tricky: how to get the current JSContextWrapper?
    // We might need a way to store it in JSContext opaque or global.
    
    // For now, let's assume we have access to it.
    // I'll use a placeholder and fix it in main or JSContextWrapper.
    
    return obj;
}

void Deferred::finalizer(JSRuntime* rt, JSValue val) {
    DeferredData* data = (DeferredData*)JS_GetOpaque(val, protojs_deferred_class_id);
    if (data) {
        JS_FreeValueRT(rt, data->func);
        JS_FreeValueRT(rt, data->resolve);
        JS_FreeValueRT(rt, data->reject);
        js_free_rt(rt, data);
    }
}

const proto::ProtoObject* Deferred::threadMain(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters
) {
    // This runs in a SEPARATE thread.
    // It must create its own JSContext if it wants to run JS.
    // OR the main thread should have prepared something.
    
    // Actually, each worker should probably have its own JSContext but share the JSRuntime (if thread-safe)
    // or its own JSRuntime.
    
    std::cout << "Deferred thread running..." << std::endl;
    
    return PROTO_NONE;
}

} // namespace protojs
