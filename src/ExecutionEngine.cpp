#include "ExecutionEngine.h"
#include "JSContext.h"
#include "ProtoArrayAdapter.h"
#include "ProtoJSStringCache.h"
#include <mutex>
#include <string>
#include <cstring>

namespace protojs {

// Map JSContext to ProtoContext using protoCore ProtoSparseList
// Key: JSContext* hash (via pointer value)  
// Value: ProtoContext* wrapped in ProtoExternalPointer
// Note: We need a ProtoContext to access the map, so we'll use JSContextWrapper
// For now, we'll rely on JSContextWrapper stored in JSContext opaque
static std::mutex contextMutex;

void ExecutionEngine::initialize(JSContext* ctx, proto::ProtoContext* pContext) {
    // ExecutionEngine doesn't need to store context mapping
    // It can always get ProtoContext from JSContextWrapper stored in JSContext opaque
    // This is more efficient and avoids the need for a global map
    
    // Set up QuickJS class operations to intercept
    // Note: QuickJS doesn't provide direct hooks for all operations,
    // so we'll use a combination of class operations and property accessors
    // For now, we'll intercept at the TypeBridge level during conversions
}

void ExecutionEngine::cleanup(JSContext* ctx) {
    // No cleanup needed - JSContextWrapper handles it
}

proto::ProtoContext* ExecutionEngine::getProtoContext(JSContext* ctx) {
    // Get from JSContextWrapper stored in JSContext opaque
    JSContextWrapper* wrapper = static_cast<JSContextWrapper*>(JS_GetContextOpaque(ctx));
    if (wrapper) {
        return wrapper->getProtoContext();
    }
    
    return nullptr;
}

bool ExecutionEngine::shouldUseProtoCore(JSContext* ctx, JSValue obj) {
    // RegExp must always use QuickJS native path so that lastIndex is mutated
    // in place by exec/test (ECMA-262 requires mutable lastIndex for global/sticky).
    // Value must match JS_CLASS_REGEXP in deps/quickjs/quickjs.c.
    constexpr unsigned kJS_CLASS_REGEXP = 18;
    if (JS_IsObject(obj)) {
        JSClassID id = JS_GetClassID(obj);
        if (id == kJS_CLASS_REGEXP) {
            return false;
        }
    }
    // Check if object is already a protoCore object
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
                const proto::ProtoString* propStr = ProtoJSStringCache::getKey(pContext, propName);
                const proto::ProtoObject* attr = protoObj->getAttribute(pContext, propStr);
                JS_FreeCString(ctx, propName);
                
                if (attr && attr != PROTO_NONE) {
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
        return JS_SetProperty(ctx, obj, prop, val);
    }
    
    // Check if object is managed by protoCore
    if (shouldUseProtoCore(ctx, obj)) {
        const proto::ProtoObject* protoObj = GCBridge::getProtoObject(obj, ctx);
        if (protoObj) {
            // Set property in protoCore object
            const char* propName = JS_AtomToCString(ctx, prop);
            if (propName) {
                const proto::ProtoString* propStr = ProtoJSStringCache::getKey(pContext, propName);
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
    return JS_SetProperty(ctx, obj, prop, val);
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
        
        // Check if it's a method and call it
        // Note: protoCore methods are function pointers, not objects
        // For now, fallback to QuickJS
        return JS_Call(ctx, func, this_val, argc, argv);
    }
    
    // Fallback to QuickJS
    return JS_Call(ctx, func, this_val, argc, argv);
}

JSValue ExecutionEngine::opNewObject(JSContext* ctx, JSValueConst proto) {
    proto::ProtoContext* pContext = getProtoContext(ctx);
    if (!pContext) {
        return JS_NewObject(ctx);
    }

    JSContextWrapper* wrapper = static_cast<JSContextWrapper*>(JS_GetContextOpaque(ctx));
    const proto::ProtoObject* objectProto = wrapper ? wrapper->getJSObjectPrototype() : nullptr;
    if (!objectProto) {
        return JS_NewObject(ctx);
    }

    // Create mutable instance as child of JS Object prototype
    const proto::ProtoObject* protoObj = objectProto->newChild(pContext, true);
    
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

    JSContextWrapper* wrapper = static_cast<JSContextWrapper*>(JS_GetContextOpaque(ctx));
    const proto::ProtoObject* arrayProto = wrapper ? wrapper->getJSArrayPrototype() : nullptr;
    if (!arrayProto) {
        return JS_NewArray(ctx);
    }

    // Create mutable array instance as child of JS Array prototype; direct attributes "0", "1", ..., "length"
    const proto::ProtoObject* backing = arrayProto->newChild(pContext, true);
    const proto::ProtoString* lengthKey = ProtoJSStringCache::getKey(pContext, "length");
    backing = backing->setAttribute(pContext, lengthKey, pContext->fromInteger(0));

    JSValue jsArr = JS_NewArray(ctx);
    GCBridge::registerMapping(jsArr, backing, ctx);
    return jsArr;
}

JSValue ExecutionEngine::opAdd(JSContext* ctx, JSValue a, JSValue b) {
    // QuickJS doesn't have JS_Add - use evaluation of binary operation
    JSValue result;
    if (JS_IsNumber(a) && JS_IsNumber(b)) {
        double da, db;
        JS_ToFloat64(ctx, &da, a);
        JS_ToFloat64(ctx, &db, b);
        result = JS_NewFloat64(ctx, da + db);
    } else if (JS_IsString(a) || JS_IsString(b)) {
        // String concatenation
        const char* sa = JS_ToCString(ctx, a);
        const char* sb = JS_ToCString(ctx, b);
        std::string concat = std::string(sa ? sa : "") + std::string(sb ? sb : "");
        result = JS_NewString(ctx, concat.c_str());
        JS_FreeCString(ctx, sa);
        JS_FreeCString(ctx, sb);
    } else {
        result = JS_UNDEFINED;
    }
    return result;
}

JSValue ExecutionEngine::opSub(JSContext* ctx, JSValue a, JSValue b) {
    double da, db;
    if (JS_ToFloat64(ctx, &da, a) == 0 && JS_ToFloat64(ctx, &db, b) == 0) {
        return JS_NewFloat64(ctx, da - db);
    }
    return JS_UNDEFINED;
}

JSValue ExecutionEngine::opMul(JSContext* ctx, JSValue a, JSValue b) {
    double da, db;
    if (JS_ToFloat64(ctx, &da, a) == 0 && JS_ToFloat64(ctx, &db, b) == 0) {
        return JS_NewFloat64(ctx, da * db);
    }
    return JS_UNDEFINED;
}

JSValue ExecutionEngine::opDiv(JSContext* ctx, JSValue a, JSValue b) {
    double da, db;
    if (JS_ToFloat64(ctx, &da, a) == 0 && JS_ToFloat64(ctx, &db, b) == 0) {
        if (db != 0.0) {
            return JS_NewFloat64(ctx, da / db);
        }
        return JS_ThrowTypeError(ctx, "Division by zero");
    }
    return JS_UNDEFINED;
}

int ExecutionEngine::opCompare(JSContext* ctx, JSValue a, JSValue b) {
    // Comparison returns: -1, 0, or 1
    if (JS_IsNumber(a) && JS_IsNumber(b)) {
        double da, db;
        JS_ToFloat64(ctx, &da, a);
        JS_ToFloat64(ctx, &db, b);
        if (da < db) return -1;
        if (da > db) return 1;
        return 0;
    }
    if (JS_IsString(a) && JS_IsString(b)) {
        const char* sa = JS_ToCString(ctx, a);
        const char* sb = JS_ToCString(ctx, b);
        int result = strcmp(sa ? sa : "", sb ? sb : "");
        JS_FreeCString(ctx, sa);
        JS_FreeCString(ctx, sb);
        return (result < 0) ? -1 : (result > 0) ? 1 : 0;
    }
    return 0;  // Fallback: equal
}

} // namespace protojs
