#include "TypeBridge.h"
#include "GCBridge.h"
#include "JSContext.h"
#include "ProtoArrayAdapter.h"
#include "ProtoArgumentsAdapter.h"
#include "JSSymbols.h"
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
        // Integers use ProtoInteger (SmallInteger or LargeInteger via fromInteger/fromLong).
        // Non-integer numbers use ProtoDouble (context->fromDouble).
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
        // JS Arrays are backed by protoCore; use JS Array prototype and newChild(mutable=true).
        if (const proto::ProtoObject* mapped = GCBridge::getProtoObject(val, ctx)) {
            return mapped;
        }

        JSContextWrapper* wrapper = static_cast<JSContextWrapper*>(JS_GetContextOpaque(ctx));
        const proto::ProtoObject* arrayProto = wrapper ? wrapper->getJSArrayPrototype() : nullptr;
        const proto::ProtoObject* backing;
        if (arrayProto) {
            backing = arrayProto->newChild(pContext, true);
            backing = backing->setAttribute(pContext, JSSymbols::length(pContext), pContext->fromInteger(0));
        } else {
            backing = ProtoArrayAdapter::createArray(pContext);
        }

        JSValue lenVal = JS_GetPropertyStr(ctx, val, "length");
        uint32_t len;
        JS_ToUint32(ctx, &len, lenVal);
        JS_FreeValue(ctx, lenVal);

        for (uint32_t i = 0; i < len; i++) {
            if (!JS_HasProperty(ctx, val, i)) {
                continue; // hole
            }
            JSValue item = JS_GetPropertyUint32(ctx, val, i);
            const proto::ProtoObject* pItem = fromJS(ctx, item, pContext);
            backing = backing->setAttribute(pContext, JSSymbols::indexKey(pContext, i), pItem);
            JS_FreeValue(ctx, item);
        }
        backing = backing->setAttribute(pContext, JSSymbols::length(pContext), pContext->fromInteger(static_cast<long long>(len)));

        // Register mapping so that future lookups can reuse the same backing.
        GCBridge::registerMapping(val, backing, ctx);
        return backing;
    }

    if (JS_IsFunction(ctx, val)) {
        if (const proto::ProtoObject* mapped = GCBridge::getProtoObject(val, ctx)) {
            return mapped;
        }
        // Map JS Function to protoCore ProtoMethod
        // Store function as a special object with function reference
        const proto::ProtoObject* pObj = pContext->newObject(true);
        // Register mapping so we can retrieve the JS function later
        GCBridge::registerMapping(val, pObj, ctx);
        // In full implementation, we'd compile JS bytecode to ProtoMethod
        return pObj;
    }

    if (JS_IsObject(val)) {
        if (const proto::ProtoObject* mapped = GCBridge::getProtoObject(val, ctx)) {
            return mapped;
        }

        // Check for RegExp / Arguments / other special classes
        JSClassID classId = JS_GetClassID(val);

        // JS_CLASS_ARGUMENTS (8) and JS_CLASS_MAPPED_ARGUMENTS (9) are
        // mirrored into protoCore using ProtoArgumentsAdapter to provide a
        // stable, immutable view of their indexed values and length.
        if (classId == 8 || classId == 9) {
            if (const proto::ProtoObject* mapped = GCBridge::getProtoObject(val, ctx)) {
                return mapped;
            }

            JSContextWrapper* wrapper = static_cast<JSContextWrapper*>(JS_GetContextOpaque(ctx));
            const proto::ProtoObject* argsProto = wrapper ? wrapper->getJSArgumentsPrototype() : nullptr;
            const proto::ProtoObject* backing;
            if (argsProto) {
                backing = argsProto->newChild(pContext, true);
                backing = backing->setAttribute(pContext, JSSymbols::length(pContext), pContext->fromInteger(0));
            } else {
                backing = ProtoArgumentsAdapter::createArguments(pContext);
            }

            JSValue lenVal = JS_GetPropertyStr(ctx, val, "length");
            uint32_t len = 0;
            JS_ToUint32(ctx, &len, lenVal);
            JS_FreeValue(ctx, lenVal);

            for (uint32_t i = 0; i < len; i++) {
                if (!JS_HasProperty(ctx, val, i)) {
                    continue;
                }
                JSValue item = JS_GetPropertyUint32(ctx, val, i);
                const proto::ProtoObject* pItem = fromJS(ctx, item, pContext);
                if (argsProto) {
                    backing = backing->setAttribute(pContext, JSSymbols::indexKey(pContext, i), pItem);
                } else {
                    backing = ProtoArgumentsAdapter::set(pContext, backing, i, pItem);
                }
                JS_FreeValue(ctx, item);
            }
            if (argsProto) {
                backing = backing->setAttribute(pContext, JSSymbols::length(pContext), pContext->fromInteger(static_cast<long long>(len)));
            }

            GCBridge::registerMapping(val, backing, ctx);
            return backing;
        }

        if (classId == 139) { // JS_CLASS_REGEXP
            // Map JS RegExp to protoCore object with pattern and flags (mutable for lastIndex etc.)
            JSContextWrapper* wrapper = static_cast<JSContextWrapper*>(JS_GetContextOpaque(ctx));
            const proto::ProtoObject* regexpProto = wrapper ? wrapper->getJSRegExpPrototype() : nullptr;
            const proto::ProtoObject* pObj = regexpProto ? regexpProto->newChild(pContext, true) : pContext->newObject(true);
            
            JSValue sourceVal = JS_GetPropertyStr(ctx, val, "source");
            JSValue flagsVal = JS_GetPropertyStr(ctx, val, "flags");
            
            if (JS_IsString(sourceVal)) {
                const char* source = JS_ToCString(ctx, sourceVal);
                const proto::ProtoObject* pSource = pContext->fromUTF8String(source);
                pObj = pObj->setAttribute(pContext, JSSymbols::source(pContext), pSource);
                JS_FreeCString(ctx, source);
            }

            if (JS_IsString(flagsVal)) {
                const char* flags = JS_ToCString(ctx, flagsVal);
                const proto::ProtoObject* pFlags = pContext->fromUTF8String(flags);
                pObj = pObj->setAttribute(pContext, JSSymbols::flags(pContext), pFlags);
                JS_FreeCString(ctx, flags);
            }
            
            JS_FreeValue(ctx, sourceVal);
            JS_FreeValue(ctx, flagsVal);
            return pObj;
        }
        
        // Check for Map (class ID 156 = JS_CLASS_MAP)
        if (classId == 156) {
            // Map JS Map to protoCore ProtoSparseList (key-value pairs)
            const proto::ProtoSparseList* mapList = pContext->newSparseList();
            JSValue iter = JS_GetPropertyStr(ctx, val, "entries");
            if (JS_IsFunction(ctx, iter)) {
                JSValue entries = JS_Call(ctx, iter, val, 0, nullptr);
                if (JS_IsObject(entries)) {
                    // Iterate over entries
                    // Note: QuickJS Map iteration requires manual handling
                    // For now, return empty sparse list as placeholder
                }
                JS_FreeValue(ctx, entries);
            }
            JS_FreeValue(ctx, iter);
            return mapList->asObject(pContext);
        }
        
        // Check for Set (class ID 157 = JS_CLASS_SET)
        if (classId == 157) {
            // Map JS Set to protoCore ProtoSet
            const proto::ProtoSet* pSet = pContext->newSet();
            JSValue iter = JS_GetPropertyStr(ctx, val, "values");
            if (JS_IsFunction(ctx, iter)) {
                JSValue values = JS_Call(ctx, iter, val, 0, nullptr);
                if (JS_IsObject(values)) {
                    // Iterate over values and add to ProtoSet
                    // Note: QuickJS Set iteration requires manual handling
                }
                JS_FreeValue(ctx, values);
            }
            JS_FreeValue(ctx, iter);
            return pSet->asObject(pContext);
        }
        
        // Check for TypedArray (class IDs 142-154)
        if (classId >= 142 && classId <= 154) {
            // Map JS TypedArray to protoCore ProtoList
            size_t byte_offset = 0;
            size_t byte_length = 0;
            size_t bytes_per_element = 0;
            JSValue buffer = JS_GetTypedArrayBuffer(ctx, val, &byte_offset, &byte_length, &bytes_per_element);
            if (!JS_IsException(buffer) && JS_IsObject(buffer)) {
                // Got the underlying ArrayBuffer
                const proto::ProtoObject* bufferObj = fromJS(ctx, buffer, pContext);
                JS_FreeValue(ctx, buffer);
                return bufferObj;
            }
            if (!JS_IsException(buffer)) {
                JS_FreeValue(ctx, buffer);
            }
            // Fallback: return empty object
            return pContext->newObject(true);
        }
        
        // Check for ArrayBuffer (class ID 140 = JS_CLASS_ARRAY_BUFFER)
        if (classId == 140) {
            // Map JS ArrayBuffer to protoCore ProtoList
            size_t len = 0;
            uint8_t* data = JS_GetArrayBuffer(ctx, &len, val);
            if (data && len > 0) {
                // Create ProtoList from byte data
                return pContext->newObject(true);
            }
        }
    }

    // Check for Symbol
    if (JS_IsSymbol(val)) {
        // Map JS Symbol to protoCore object with symbol description
        const proto::ProtoObject* pObj = pContext->newObject(true);
        JSValue desc = JS_GetPropertyStr(ctx, val, "description");
        if (JS_IsString(desc)) {
            const char* descStr = JS_ToCString(ctx, desc);
            const proto::ProtoObject* pDesc = pContext->fromUTF8String(descStr);
            pObj = pObj->setAttribute(pContext, JSSymbols::description(pContext), pDesc);
            JS_FreeCString(ctx, descStr);
        }
        JS_FreeValue(ctx, desc);
        return pObj;
    }

    // JS_IsDate doesn't exist in QuickJS - skip Date handling for now
    // TODO: Handle Date objects if needed

    if (JS_IsObject(val)) {
        if (const proto::ProtoObject* mapped = GCBridge::getProtoObject(val, ctx)) {
            return mapped;
        }

        // Map JS Object to protoCore ProtoObject (mutable child of JS Object prototype)
        JSContextWrapper* wrapper = static_cast<JSContextWrapper*>(JS_GetContextOpaque(ctx));
        const proto::ProtoObject* objectProto = wrapper ? wrapper->getJSObjectPrototype() : nullptr;
        const proto::ProtoObject* pObj = objectProto ? objectProto->newChild(pContext, true) : pContext->newObject(true);
        
        // Iterate over JS object properties and set as attributes in protoCore.
        // NOTE: ProtoObjects are immutable; setAttribute returns a new root that must
        // be captured, otherwise updates are silently dropped.
        JSPropertyEnum* props;
        uint32_t prop_count;
        if (JS_GetOwnPropertyNames(ctx, &props, &prop_count, val, JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK) == 0) {
            for (uint32_t i = 0; i < prop_count; i++) {
                JSValue prop_val = JS_GetProperty(ctx, val, props[i].atom);
                const char* prop_name = JS_AtomToCString(ctx, props[i].atom);
                
                const proto::ProtoObject* pVal = fromJS(ctx, prop_val, pContext);
                const proto::ProtoString* pName = pContext->fromUTF8String(prop_name)->asString(pContext);

                if (pName) {
                    pObj = pObj->setAttribute(pContext, pName, pVal);
                }
                
                JS_FreeValue(ctx, prop_val);
                JS_FreeCString(ctx, prop_name);
                JS_FreeAtom(ctx, props[i].atom);
            }
            js_free(ctx, props);
        }

        // Register mapping so future operations can resolve back to this protoCore object.
        GCBridge::registerMapping(val, pObj, ctx);
        return pObj;
    }

    return PROTO_NONE;
}

JSValue TypeBridge::toJS(JSContext* ctx, const proto::ProtoObject* obj, proto::ProtoContext* pContext) {
    if (obj == PROTO_NONE || obj == nullptr) {
        return JS_NULL;
    }

    // Check if we already have a JSValue for this ProtoObject
    JSValue mapped = GCBridge::getJSValue(obj, ctx);
    if (!JS_IsException(mapped) && !JS_IsNull(mapped) && !JS_IsUndefined(mapped)) {
        return mapped;
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

    // Check for RegExp (stored as object with source and flags)
    if (obj->isCell(pContext)) {
        // Try to detect if it's a RegExp by checking attributes
        // For now, map back to JS Object
        JSValue jsObj = JS_NewObject(ctx);
        // Register mapping so round-trip works
        GCBridge::registerMapping(jsObj, obj, ctx);
        
        // Copy attributes to JS object
        const proto::ProtoSparseList* attrs = obj->getAttributes(pContext);
        if (attrs) {
            proto::ProtoSparseListIterator* iter = const_cast<proto::ProtoSparseListIterator*>(attrs->getIterator(pContext));
            while (iter && iter->hasNext(pContext)) {
                unsigned long hash = iter->nextKey(pContext);
                const proto::ProtoObject* val = iter->nextValue(pContext);
                
                std::string name = JSSymbols::getNameFromHash(pContext, hash);
                if (!name.empty()) {
                    JS_SetPropertyStr(ctx, jsObj, name.c_str(), toJS(ctx, val, pContext));
                }
                
                iter = const_cast<proto::ProtoSparseListIterator*>(iter->advance(pContext));
            }
        }
        
        return jsObj;
    }

    // Check for Symbol (stored as object with description)
    // Note: We can't fully recreate JS Symbol, so return as object
    // This is a limitation - Symbols are unique in JS

    // Check for ArrayBuffer/ByteBuffer
    // Note: ProtoByteBuffer methods don't match JS API expectations
    // For now, return a generic JS object representation
    JSValue jsObj = JS_NewObject(ctx);
    
    // Set a type indicator
    JS_SetPropertyStr(ctx, jsObj, "_type", JS_NewString(ctx, "ProtoObject"));
    
    // Register mapping
    GCBridge::registerMapping(jsObj, obj, ctx);

    return jsObj;
}

} // namespace protojs
