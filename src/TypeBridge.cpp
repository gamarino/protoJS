#include "TypeBridge.h"
#include <string>
#include <vector>

namespace protojs {

const proto::ProtoObject* TypeBridge::fromJS(JSContext* ctx, JSValue val, proto::ProtoContext* pContext) {
    if (JS_IsNull(val) || JS_IsUndefined(val)) {
        return PROTO_NONE;
    }

    if (JS_IsBool(val)) {
        return pContext->fromBoolean(JS_ToBool(ctx, val));
    }

    if (JS_IsNumber(val)) {
        double d;
        JS_ToFloat64(ctx, &d, val);
        // Check if it's an integer to use SmallInteger if possible
        if (d == (long long)d) {
            return pContext->fromInteger((long long)d);
        }
        return pContext->fromDouble(d);
    }

    if (JS_IsString(val)) {
        const char* str = JS_ToCString(ctx, val);
        const proto::ProtoObject* pStr = pContext->fromUTF8String(str);
        JS_FreeCString(ctx, str);
        return pStr;
    }

    if (JS_IsArray(ctx, val)) {
        // Map JS Array to ProtoList or ProtoSparseList? 
        // User request says "Implement all basic types of javascript using the primitives of protoCore"
        // And "Collections that protoCore supports and do not have a clear equivalence in javascript will be implemented as new modules"
        // Modern JS Array is usually mapped to ProtoSparseList if we want to support large/sparse arrays efficiently.
        const proto::ProtoSparseList* pList = pContext->newSparseList();
        JSValue lenVal = JS_GetPropertyStr(ctx, val, "length");
        uint32_t len;
        JS_ToUint32(ctx, &len, lenVal);
        JS_FreeValue(ctx, lenVal);

        for (uint32_t i = 0; i < len; i++) {
            JSValue item = JS_GetPropertyUint32(ctx, val, i);
            const proto::ProtoObject* pItem = fromJS(ctx, item, pContext);
            pList = pList->setAt(pContext, i, pItem);
            JS_FreeValue(ctx, item);
        }
        return pList->asObject(pContext);
    }

    if (JS_IsObject(val)) {
        // Map JS Object to protoCore ProtoObject
        const proto::ProtoObject* pObj = pContext->newObject(true); // Mutable by default for JS objects?
        
        // Iterate over JS object properties and set as attributes in protoCore
        JSPropertyEnum* props;
        uint32_t prop_count;
        if (JS_GetOwnPropertyNames(ctx, &props, &prop_count, val, JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK) == 0) {
            for (uint32_t i = 0; i < prop_count; i++) {
                JSValue prop_val = JS_GetProperty(ctx, val, props[i].atom);
                const char* prop_name = JS_AtomToCString(ctx, props[i].atom);
                
                const proto::ProtoObject* pVal = fromJS(ctx, prop_val, pContext);
                const proto::ProtoString* pName = pContext->fromUTF8String(prop_name)->asString(pContext);
                
                pObj->setAttribute(pContext, pName, pVal);
                
                JS_FreeValue(ctx, prop_val);
                JS_FreeCString(ctx, prop_name);
                JS_FreeAtom(ctx, props[i].atom);
            }
            js_free(ctx, props);
        }
        return pObj;
    }

    return PROTO_NONE;
}

JSValue TypeBridge::toJS(JSContext* ctx, const proto::ProtoObject* obj, proto::ProtoContext* pContext) {
    if (obj == PROTO_NONE || obj == nullptr) {
        return JS_NULL;
    }

    if (obj->isBoolean(pContext)) {
        return JS_NewBool(ctx, obj->asBoolean(pContext));
    }

    if (obj->isInteger(pContext)) {
        return JS_NewInt64(ctx, obj->asLong(pContext));
    }

    if (obj->isDouble(pContext) || obj->isFloat(pContext)) {
        return JS_NewFloat64(ctx, obj->asDouble(pContext));
    }

    if (obj->isString(pContext)) {
        // This is tricky because ProtoString doesn't provide a direct const char* easily without iterator or buffer
        // Let's assume for now we can get a UTF8 string somehow. 
        // Looking at protoCore.h: there is no simple getString() method.
        // Wait, ProtoString has asObject()... I might need to see how to extract the content.
        // I'll use a placeholder for now and check how to implement it.
        return JS_NewString(ctx, "[ProtoCore String]"); 
    }

    if (obj->isTuple(pContext) || obj->asList(pContext)) {
        // Map list/tuple to JS Array
        JSValue arr = JS_NewArray(ctx);
        const proto::ProtoList* list = obj->asList(pContext);
        unsigned long size = list->getSize(pContext);
        for (unsigned long i = 0; i < size; i++) {
            const proto::ProtoObject* item = list->getAt(pContext, i);
            JS_SetPropertyUint32(ctx, arr, i, toJS(ctx, item, pContext));
        }
        return arr;
    }

    if (obj->isCell(pContext)) {
        // Map back to JS Object
        JSValue jsObj = JS_NewObject(ctx);
        const proto::ProtoSparseList* attrs = obj->getAttributes(pContext);
        // We need to iterate over attributes... ProtoSparseList has processElements
        // I will need a helper or just skip for now until I have it figured out.
        return jsObj;
    }

    return JS_UNDEFINED;
}

} // namespace protojs
