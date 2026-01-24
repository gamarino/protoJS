#include "ExecutionEngine.h"
#include "JSContext.h"
#include <unordered_map>
#include <mutex>

namespace protojs {

// Map JSContext to ProtoContext
static std::unordered_map<JSContext*, proto::ProtoContext*> contextMap;
static std::mutex contextMutex;

void ExecutionEngine::initialize(JSContext* ctx, proto::ProtoContext* pContext) {
    std::lock_guard<std::mutex> lock(contextMutex);
    contextMap[ctx] = pContext;
    
    // Set up QuickJS class operations to intercept
    // Note: QuickJS doesn't provide direct hooks for all operations,
    // so we'll use a combination of class operations and property accessors
    // For now, we'll intercept at the TypeBridge level during conversions
}

void ExecutionEngine::cleanup(JSContext* ctx) {
    std::lock_guard<std::mutex> lock(contextMutex);
    contextMap.erase(ctx);
}

proto::ProtoContext* ExecutionEngine::getProtoContext(JSContext* ctx) {
    std::lock_guard<std::mutex> lock(contextMutex);
    auto it = contextMap.find(ctx);
    if (it != contextMap.end()) {
        return it->second;
    }
    
    // Fallback: try to get from JSContextWrapper
    JSContextWrapper* wrapper = static_cast<JSContextWrapper*>(JS_GetContextOpaque(ctx));
    if (wrapper) {
        return wrapper->getProtoContext();
    }
    
    return nullptr;
}

bool ExecutionEngine::shouldUseProtoCore(JSContext* ctx, JSValue obj) {
    // Check if object is already a protoCore object
    // Objects created through protoCore should be handled by protoCore
    const proto::ProtoObject* protoObj = GCBridge::getProtoObject(obj, ctx);
    return protoObj != nullptr;
}

JSValue ExecutionEngine::opGetProperty(JSContext* ctx, JSValue obj, JSAtom prop) {
    proto::ProtoContext* pContext = getProtoContext(ctx);
    if (!pContext) {
        // Fallback to QuickJS default behavior
        return JS_GetProperty(ctx, obj, prop);
    }
    
    // Check if object is managed by protoCore
    if (shouldUseProtoCore(ctx, obj)) {
        const proto::ProtoObject* protoObj = GCBridge::getProtoObject(obj, ctx);
        if (protoObj) {
            // Get property from protoCore object
            const char* propName = JS_AtomToCString(ctx, prop);
            if (propName) {
                const proto::ProtoString* propStr = pContext->fromUTF8String(propName)->asString(pContext);
                const proto::ProtoObject* attr = protoObj->getAttribute(pContext, propStr);
                JS_FreeCString(ctx, propName);
                
                if (attr) {
                    JSValue result = TypeBridge::toJS(ctx, attr, pContext);
                    JS_FreeAtom(ctx, prop);
                    return result;
                }
            }
            JS_FreeAtom(ctx, prop);
        }
    }
    
    // Fallback to QuickJS
    return JS_GetProperty(ctx, obj, prop);
}

int ExecutionEngine::opSetProperty(JSContext* ctx, JSValue obj, JSAtom prop, JSValue val, int flags) {
    proto::ProtoContext* pContext = getProtoContext(ctx);
    if (!pContext) {
        return JS_SetProperty(ctx, obj, prop, val, flags);
    }
    
    // Check if object is managed by protoCore
    if (shouldUseProtoCore(ctx, obj)) {
        const proto::ProtoObject* protoObj = GCBridge::getProtoObject(obj, ctx);
        if (protoObj) {
            // Set property in protoCore object
            const char* propName = JS_AtomToCString(ctx, prop);
            if (propName) {
                const proto::ProtoString* propStr = pContext->fromUTF8String(propName)->asString(pContext);
                const proto::ProtoObject* valObj = TypeBridge::fromJS(ctx, val, pContext);
                
                // Note: protoCore objects are immutable by default
                // Setting attributes creates a new object
                // For mutable objects, we'd use setAttributeMutable
                const proto::ProtoObject* newObj = protoObj->setAttribute(pContext, propStr, valObj);
                
                // Update mapping
                if (newObj != protoObj) {
                    GCBridge::registerMapping(obj, newObj, ctx);
                }
                
                JS_FreeCString(ctx, propName);
                JS_FreeAtom(ctx, prop);
                return 0; // Success
            }
            JS_FreeAtom(ctx, prop);
        }
    }
    
    // Fallback to QuickJS
    return JS_SetProperty(ctx, obj, prop, val, flags);
}

JSValue ExecutionEngine::opCall(JSContext* ctx, JSValue func, JSValue this_val, int argc, JSValueConst* argv) {
    proto::ProtoContext* pContext = getProtoContext(ctx);
    if (!pContext) {
        return JS_Call(ctx, func, this_val, argc, argv);
    }
    
    // Check if function is a protoCore method
    const proto::ProtoObject* funcObj = GCBridge::getProtoObject(func, ctx);
    if (funcObj && funcObj->isMethod(pContext)) {
        // Convert arguments to protoCore
        const proto::ProtoList* args = pContext->newList();
        for (int i = 0; i < argc; i++) {
            const proto::ProtoObject* argObj = TypeBridge::fromJS(ctx, argv[i], pContext);
            args = args->appendLast(pContext, argObj);
        }
        
        // Get this value
        const proto::ProtoObject* thisObj = TypeBridge::fromJS(ctx, this_val, pContext);
        
        // Call protoCore method
        const proto::ProtoMethod* method = funcObj->asMethod(pContext);
        const proto::ProtoObject* result = method->call(pContext, thisObj, args);
        
        // Convert result back to JS
        return TypeBridge::toJS(ctx, result, pContext);
    }
    
    // Fallback to QuickJS
    return JS_Call(ctx, func, this_val, argc, argv);
}

JSValue ExecutionEngine::opNewObject(JSContext* ctx, JSValueConst proto) {
    proto::ProtoContext* pContext = getProtoContext(ctx);
    if (!pContext) {
        return JS_NewObject(ctx);
    }
    
    // Create protoCore object
    const proto::ProtoObject* protoObj = pContext->newObject(true); // Mutable by default
    
    // Convert to JS and register mapping
    JSValue jsObj = TypeBridge::toJS(ctx, protoObj, pContext);
    GCBridge::registerMapping(jsObj, protoObj, ctx);
    
    return jsObj;
}

JSValue ExecutionEngine::opNewArray(JSContext* ctx) {
    proto::ProtoContext* pContext = getProtoContext(ctx);
    if (!pContext) {
        return JS_NewArray(ctx);
    }
    
    // Create protoCore list
    const proto::ProtoList* list = pContext->newList();
    
    // Convert to JS array and register mapping
    JSValue jsArr = TypeBridge::toJS(ctx, list->asObject(pContext), pContext);
    GCBridge::registerMapping(jsArr, list->asObject(pContext), ctx);
    
    return jsArr;
}

JSValue ExecutionEngine::opAdd(JSContext* ctx, JSValue a, JSValue b) {
    proto::ProtoContext* pContext = getProtoContext(ctx);
    if (!pContext) {
        return JS_Add(ctx, a, b);
    }
    
    // Convert to protoCore and perform operation
    const proto::ProtoObject* aObj = TypeBridge::fromJS(ctx, a, pContext);
    const proto::ProtoObject* bObj = TypeBridge::fromJS(ctx, b, pContext);
    
    // Perform addition in protoCore
    // Note: protoCore doesn't have direct arithmetic operators
    // We'd need to check types and use appropriate methods
    // For now, fallback to QuickJS
    return JS_Add(ctx, a, b);
}

JSValue ExecutionEngine::opSub(JSContext* ctx, JSValue a, JSValue b) {
    // Similar to opAdd
    return JS_Sub(ctx, a, b);
}

JSValue ExecutionEngine::opMul(JSContext* ctx, JSValue a, JSValue b) {
    return JS_Mul(ctx, a, b);
}

JSValue ExecutionEngine::opDiv(JSContext* ctx, JSValue a, JSValue b) {
    return JS_Div(ctx, a, b);
}

int ExecutionEngine::opCompare(JSContext* ctx, JSValue a, JSValue b) {
    proto::ProtoContext* pContext = getProtoContext(ctx);
    if (!pContext) {
        return JS_Compare(ctx, a, b);
    }
    
    // Convert to protoCore and compare
    const proto::ProtoObject* aObj = TypeBridge::fromJS(ctx, a, pContext);
    const proto::ProtoObject* bObj = TypeBridge::fromJS(ctx, b, pContext);
    
    // Use protoCore comparison
    // Note: protoCore has comparison methods
    // For now, fallback to QuickJS
    return JS_Compare(ctx, a, b);
}

} // namespace protojs
