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

    if (JS_IsBigInt(ctx, val)) {
        // Convert BigInt to LargeInteger
        int64_t v;
        if (JS_ToBigInt64(ctx, &v, val) == 0) {
            return pContext->fromLong(v);
        }
        // For very large BigInt beyond int64_t, we'd need LargeInteger support
        // For now, truncate to int64_t
        return pContext->fromLong(0); // Placeholder - should use LargeInteger
    }

    if (JS_IsArray(ctx, val)) {
        // Map JS Array to ProtoList (inmutable) or ProtoSparseList (mutable/sparse)
        // For Fase 1, we'll use ProtoList for dense arrays (inmutable by default)
        JSValue lenVal = JS_GetPropertyStr(ctx, val, "length");
        uint32_t len;
        JS_ToUint32(ctx, &len, lenVal);
        JS_FreeValue(ctx, lenVal);

        // Check if array is sparse (has holes)
        bool isSparse = false;
        for (uint32_t i = 0; i < len; i++) {
            if (!JS_HasProperty(ctx, val, i)) {
                isSparse = true;
                break;
            }
        }

        if (isSparse || len > 10000) {
            // Use ProtoSparseList for sparse or very large arrays
            const proto::ProtoSparseList* pList = pContext->newSparseList();
            for (uint32_t i = 0; i < len; i++) {
                if (JS_HasProperty(ctx, val, i)) {
                    JSValue item = JS_GetPropertyUint32(ctx, val, i);
                    const proto::ProtoObject* pItem = fromJS(ctx, item, pContext);
                    pList = pList->setAt(pContext, i, pItem);
                    JS_FreeValue(ctx, item);
                }
            }
            return pList->asObject(pContext);
        } else {
            // Use ProtoList for dense arrays (inmutable)
            const proto::ProtoList* pList = pContext->newList();
            for (uint32_t i = 0; i < len; i++) {
                JSValue item = JS_GetPropertyUint32(ctx, val, i);
                const proto::ProtoObject* pItem = fromJS(ctx, item, pContext);
                pList = pList->appendLast(pContext, pItem);
                JS_FreeValue(ctx, item);
            }
            return pList->asObject(pContext);
        }
    }

    if (JS_IsFunction(ctx, val)) {
        // Map JS Function to protoCore ProtoMethod
        // For Phase 1, we store the function as a reference in a ProtoObject
        // In full implementation, we'd compile to ProtoMethod
        const proto::ProtoObject* pObj = pContext->newObject(true);
        // Store function reference - in full implementation, would convert to ProtoMethod
        // For now, we keep the JSValue and convert it back when needed
        return pObj;
    }

    if (JS_IsDate(ctx, val)) {
        // Map JS Date to protoCore Date
        // Get timestamp
        double timestamp;
        if (JS_ToFloat64(ctx, &timestamp, val) == 0) {
            // protoCore Date would be created from timestamp
            // For Phase 1, store as number in ProtoObject
            return pContext->fromDouble(timestamp);
        }
        return pContext->fromDouble(0);
    }

    if (JS_IsObject(val)) {
        // Map JS Object to protoCore ProtoObject
        const proto::ProtoObject* pObj = pContext->newObject(true); // Mutable by default for JS objects
        
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
        long long val = obj->asLong(pContext);
        // Check if it fits in 32-bit, otherwise use BigInt
        if (val >= INT32_MIN && val <= INT32_MAX) {
            return JS_NewInt32(ctx, static_cast<int32_t>(val));
        } else {
            // Use BigInt for large integers
            return JS_NewBigInt64(ctx, val);
        }
    }

    if (obj->isDouble(pContext)) {
        return JS_NewFloat64(ctx, obj->asDouble(pContext));
    }

    if (obj->isString(pContext)) {
        // Convert ProtoString to UTF-8 string
        // Use asList to iterate over characters
        const proto::ProtoString* pStr = obj->asString(pContext);
        const proto::ProtoList* charList = pStr->asList(pContext);
        
        std::string result;
        result.reserve(pStr->getSize(pContext) * 4); // Reserve space for UTF-8
        
        unsigned long size = charList->getSize(pContext);
        for (unsigned long i = 0; i < size; i++) {
            const proto::ProtoObject* charObj = charList->getAt(pContext, i);
            // Character is stored as UnicodeChar (unsigned int)
            unsigned int unicodeChar = charObj->asLong(pContext);
            
            // Convert Unicode to UTF-8
            if (unicodeChar < 0x80) {
                result += static_cast<char>(unicodeChar);
            } else if (unicodeChar < 0x800) {
                result += static_cast<char>(0xC0 | (unicodeChar >> 6));
                result += static_cast<char>(0x80 | (unicodeChar & 0x3F));
            } else if (unicodeChar < 0x10000) {
                result += static_cast<char>(0xE0 | (unicodeChar >> 12));
                result += static_cast<char>(0x80 | ((unicodeChar >> 6) & 0x3F));
                result += static_cast<char>(0x80 | (unicodeChar & 0x3F));
            } else {
                result += static_cast<char>(0xF0 | (unicodeChar >> 18));
                result += static_cast<char>(0x80 | ((unicodeChar >> 12) & 0x3F));
                result += static_cast<char>(0x80 | ((unicodeChar >> 6) & 0x3F));
                result += static_cast<char>(0x80 | (unicodeChar & 0x3F));
            }
        }
        
        return JS_NewString(ctx, result.c_str());
    }

    // Check for ProtoList
    if (const proto::ProtoList* list = obj->asList(pContext)) {
        JSValue arr = JS_NewArray(ctx);
        unsigned long size = list->getSize(pContext);
        for (unsigned long i = 0; i < size; i++) {
            const proto::ProtoObject* item = list->getAt(pContext, i);
            JS_SetPropertyUint32(ctx, arr, i, toJS(ctx, item, pContext));
        }
        return arr;
    }

    // Check for ProtoTuple
    if (obj->isTuple(pContext)) {
        const proto::ProtoTuple* tuple = obj->asTuple(pContext);
        JSValue arr = JS_NewArray(ctx);
        unsigned long size = tuple->getSize(pContext);
        for (unsigned long i = 0; i < size; i++) {
            const proto::ProtoObject* item = tuple->getAt(pContext, i);
            JS_SetPropertyUint32(ctx, arr, i, toJS(ctx, item, pContext));
        }
        // Make array read-only to reflect immutability
        JS_DefinePropertyValueStr(ctx, arr, "length", JS_NewInt32(ctx, size), JS_PROP_WRITABLE);
        return arr;
    }

    // Check for ProtoSparseList
    // Note: SparseList conversion will be handled by protoCore module wrapper
    // For now, return empty array as placeholder
    // TODO: Implement proper SparseList iteration when iterator API is confirmed

    // Check for ProtoSet
    if (obj->isSet(pContext)) {
        const proto::ProtoSet* set = obj->asSet(pContext);
        // For now, return as array of values
        // TODO: Return proper Set object when Set wrapper is available
        JSValue arr = JS_NewArray(ctx);
        // Use processValues if available, otherwise skip for now
        // ProtoSet iteration needs to be implemented properly
        // For Fase 1, return empty array as placeholder
        return arr;
    }

    // Check for Date (stored as Double timestamp)
    if (obj->isDouble(pContext)) {
        // Could be a Date - for Phase 1, we'll check if it's in a reasonable date range
        double d = obj->asDouble(pContext);
        // If it's a reasonable timestamp (between 1970 and 2100), treat as Date
        if (d > 0 && d < 4102444800000.0) { // Jan 1, 1970 to Jan 1, 2100 in milliseconds
            JSValue date = JS_NewDate(ctx, d);
            if (!JS_IsException(date)) {
                return date;
            }
            JS_FreeValue(ctx, date);
        }
        // Otherwise, return as number
        return JS_NewFloat64(ctx, d);
    }

    if (obj->isCell(pContext)) {
        // Map back to JS Object
        // TODO: Implement proper attribute iteration when getAttributes is available
        // For now, return empty object
        JSValue jsObj = JS_NewObject(ctx);
        // const proto::ProtoSparseList* attrs = obj->getAttributes(pContext);
        // We need to iterate over attributes... ProtoSparseList has processElements
        // I will need a helper or just skip for now until I have it figured out.
        return jsObj;
    }

    return JS_UNDEFINED;
}

} // namespace protojs
