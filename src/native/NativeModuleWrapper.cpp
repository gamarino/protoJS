#include "NativeModuleWrapper.h"
#include "TypeBridge.h"
#include "../JSContext.h"
#include <stdexcept>

namespace protojs {

JSValue NativeModuleWrapper::wrapNativeFunction(
    ProtoJSNativeFunction func,
    const char* name,
    JSContext* ctx,
    proto::ProtoContext* pContext
) {
    // Store pContext in JSContext opaque for access in callback
    JSContextWrapper* wrapper = static_cast<JSContextWrapper*>(JS_GetContextOpaque(ctx));
    if (!wrapper) {
        return JS_ThrowTypeError(ctx, "JSContextWrapper not found");
    }
    
    // Store function pointer and context in a structure
    struct ClosureData {
        ProtoJSNativeFunction func;
        proto::ProtoContext* pContext;
    };
    
    auto* closureData = new ClosureData{func, pContext};
    
    // Create C function that calls native function
    JSValue jsFunc = JS_NewCFunction(ctx, [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
        JSContextWrapper* wrapper = static_cast<JSContextWrapper*>(JS_GetContextOpaque(ctx));
        if (!wrapper) {
            return JS_ThrowTypeError(ctx, "JSContextWrapper not found");
        }
        
        proto::ProtoContext* pContext = wrapper->getProtoContext();
        
        // Get function from closure (simplified - would use proper closure)
        // For now, we'll need to store it differently
        // This is a placeholder - full implementation would use JS_SetOpaque or similar
        
        return JS_UNDEFINED;
    }, name, argc);
    
    // TODO: Store closureData properly using JS_SetOpaque or closure mechanism
    
    return jsFunc;
}

JSValue NativeModuleWrapper::callNativeFunction(
    ProtoJSNativeFunction func,
    JSContext* ctx,
    proto::ProtoContext* pContext,
    JSValueConst this_val,
    int argc,
    JSValueConst* argv
) {
    try {
        // Convert this_val to protoCore
        const proto::ProtoObject* self = TypeBridge::fromJS(ctx, this_val, pContext);
        
        // Convert arguments to ProtoList
        proto::ProtoList* args = pContext->newList();
        for (int i = 0; i < argc; i++) {
            const proto::ProtoObject* arg = TypeBridge::fromJS(ctx, argv[i], pContext);
            args = args->appendLast(pContext, arg);
        }
        
        // Create empty keyword parameters (for now)
        proto::ProtoSparseList* kwargs = pContext->newSparseList();
        
        // Call native function
        const proto::ProtoObject* result = func(pContext, self, nullptr, args, kwargs);
        
        // Convert result back to JS
        JSValue jsResult = TypeBridge::toJS(ctx, result, pContext);
        
        return jsResult;
    } catch (const std::exception& e) {
        return handleNativeException(e, ctx);
    } catch (...) {
        return JS_ThrowTypeError(ctx, "Unknown exception in native function");
    }
}

JSValue NativeModuleWrapper::handleNativeException(
    const std::exception& e,
    JSContext* ctx
) {
    std::string errorMsg = "Native module error: ";
    errorMsg += e.what();
    
    JSValue error = JS_NewError(ctx);
    JSValue message = JS_NewString(ctx, errorMsg.c_str());
    JS_SetPropertyStr(ctx, error, "message", message);
    JS_FreeValue(ctx, message);
    
    return JS_Throw(ctx, error);
}

proto::ProtoList* NativeModuleWrapper::convertArgumentsToProtoList(
    JSContext* ctx,
    proto::ProtoContext* pContext,
    int argc,
    JSValueConst* argv
) {
    proto::ProtoList* list = pContext->newList();
    for (int i = 0; i < argc; i++) {
        const proto::ProtoObject* arg = TypeBridge::fromJS(ctx, argv[i], pContext);
        list = list->appendLast(pContext, arg);
    }
    return list;
}

proto::ProtoSparseList* NativeModuleWrapper::convertArgumentsToKeywordParams(
    JSContext* ctx,
    proto::ProtoContext* pContext,
    int argc,
    JSValueConst* argv
) {
    // For now, return empty sparse list
    // Full implementation would parse keyword arguments
    return pContext->newSparseList();
}

} // namespace protojs
