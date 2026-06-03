#include "TypeBridge.h"
#include "runtime/ProtoInterpreter.h"
#include "GCBridge.h"
#include "JSContext.h"
#include "ProtoArrayAdapter.h"
#include "ProtoArgumentsAdapter.h"
#include "ArrayElementsStorage.h"
#include "JSSymbols.h"
#include <cmath>
#include <string>
#include <vector>

namespace protojs {
thread_local const proto::ProtoObject* t_nullSentinel = nullptr;
thread_local const proto::ProtoObject* t_undefinedSentinel = nullptr;

void initializeUndefinedSentinel(proto::ProtoContext* ctx) {
    if (!t_undefinedSentinel) t_undefinedSentinel = ctx->newObject(true);
    if (!t_nullSentinel) t_nullSentinel = ctx->newObject(true);
}

void initializeNullSentinel(proto::ProtoContext* ctx) {
    initializeUndefinedSentinel(ctx);
}

const proto::ProtoObject* getUndefinedSentinel() { return t_undefinedSentinel; }
const proto::ProtoObject* getNullSentinel() { return t_nullSentinel; }

const proto::ProtoObject* TypeBridge::fromJS(JSContext* ctx, JSValue val, proto::ProtoContext* pContext) {
    if (JS_IsNull(val)) {
        // Return the null sentinel if the interpreter is active; fall back to PROTO_NONE.
        const proto::ProtoObject* s = protojs::getNullSentinel();
        return s ? s : PROTO_NONE;
    }
    if (JS_IsUndefined(val)) {
        const proto::ProtoObject* u = protojs::getUndefinedSentinel();
        return u ? u : PROTO_NONE;
    }

    if (JS_IsBool(val)) {
        return pContext->fromBoolean(JS_ToBool(ctx, val));
    }

    if (JS_IsNumber(val)) {
        double d;
        JS_ToFloat64(ctx, &d, val);
        // Preserve negative zero — collapsing -0.0 to (long long)0 and
        // then fromInteger destroys the sign, so JSON.parse('-0') was
        // returning +0. The signbit test catches -0 specifically; all
        // other integer doubles go through the SmallInteger fast path.
        if (d == 0.0 && std::signbit(d)) {
            return pContext->fromDouble(d);
        }
        if (d == (long long)d) {
            return pContext->fromInteger((long long)d);
        }
        return pContext->fromDouble(d);
    }

    if (JS_IsString(val)) {
        const char* str = JS_ToCString(ctx, val);
        const proto::ProtoString* pStr = proto::ProtoString::createSymbol(pContext, str);
        JS_FreeCString(ctx, str);
        return pStr->asObject(pContext);
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

        // Build native ProtoList element storage so the rest of the
        // runtime (arrayTryFastGet, OP_get_array_el, getOwnElements,
        // JSON.stringify's __elements__ path, etc.) sees the entries.
        // Pre-fix: elements were stored as indexed-attribute keys ("0",
        // "1", …) so consumers reading __elements__ saw the array as
        // empty.  This is what broke tagged-template strings arrays
        // (const-pool QuickJS array embedded into the bytecode): the
        // strings array was the right length but s[0] returned null.
        proto::ProtoContext::CriticalSection cs(pContext);
        const proto::ProtoList* list = pContext->newList();
        for (uint32_t i = 0; i < len; i++) {
            // JS_HasProperty on sealed template objects can return 0
            // even when JS_GetPropertyUint32 yields the cooked string,
            // so unconditionally fetch and check for exception/undefined
            // instead of gating on JS_HasProperty.
            JSValue item = JS_GetPropertyUint32(ctx, val, i);
            const proto::ProtoObject* pItem;
            if (JS_IsException(item)) {
                pItem = PROTO_NONE;
            } else {
                pItem = fromJS(ctx, item, pContext);
            }
            JS_FreeValue(ctx, item);
            list = list->appendLast(pContext, pItem ? pItem : PROTO_NONE);
        }
        backing = backing->setAttribute(pContext, JSSymbols::length(pContext), pContext->fromInteger(static_cast<long long>(len)));
        const proto::ProtoString* isArrKey = JSSymbols::isArray(pContext);
        if (isArrKey) backing = backing->setAttribute(pContext, isArrKey, PROTO_TRUE);
        if (list) protojs::setArrayElements(pContext, backing, list);

        // Tagged-template support: re-walk non-index own properties so
        // sidecars such as `raw` (a QuickJS-side array placed on the
        // template object via JS_DefinePropertyValue) make it across.
        JSPropertyEnum* tab = nullptr;
        uint32_t plen = 0;
        // Enumerate *all* own string properties, not just enumerable
        // ones: the tagged-template `raw` sidecar is installed by
        // QuickJS as a non-enumerable, non-writable, non-configurable
        // property (JS_DefinePropertyValue with only JS_PROP_THROW),
        // so JS_GPN_ENUM_ONLY skips it.
        if (JS_GetOwnPropertyNames(ctx, &tab, &plen, val,
                                   JS_GPN_STRING_MASK) == 0) {
            for (uint32_t pi = 0; pi < plen; ++pi) {
                JSAtom atom = tab[pi].atom;
                const char* keyName = JS_AtomToCString(ctx, atom);
                if (keyName) {
                    bool isIndex = (keyName[0] != '\0');
                    for (const char* p = keyName; isIndex && *p; ++p)
                        if (*p < '0' || *p > '9') isIndex = false;
                    if (!isIndex && strcmp(keyName, "length") != 0) {
                        JSValue propVal = JS_GetProperty(ctx, val, atom);
                        if (!JS_IsException(propVal)) {
                            const proto::ProtoObject* p =
                                fromJS(ctx, propVal, pContext);
                            const proto::ProtoObject* ko =
                                pContext->fromUTF8String(keyName);
                            const proto::ProtoString* ks =
                                ko ? ko->asString(pContext) : nullptr;
                            if (ks)
                                backing = backing->setAttribute(
                                    pContext, ks, p ? p : PROTO_NONE);
                            JS_FreeValue(ctx, propVal);
                        }
                    }
                    JS_FreeCString(ctx, keyName);
                }
                JS_FreeAtom(ctx, atom);
            }
            js_free(ctx, tab);
        }

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
                const proto::ProtoString* pSource = proto::ProtoString::createSymbol(pContext, source);
                pObj = pObj->setAttribute(pContext, JSSymbols::source(pContext), pSource->asObject(pContext));
                JS_FreeCString(ctx, source);
            }

            if (JS_IsString(flagsVal)) {
                const char* flags = JS_ToCString(ctx, flagsVal);
                const proto::ProtoString* pFlags = proto::ProtoString::createSymbol(pContext, flags);
                pObj = pObj->setAttribute(pContext, JSSymbols::flags(pContext), pFlags->asObject(pContext));
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
            const proto::ProtoString* pDesc = proto::ProtoString::createSymbol(pContext, descStr);
            pObj = pObj->setAttribute(pContext, JSSymbols::description(pContext), pDesc->asObject(pContext));
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

        // Register the (still-empty) mapping BEFORE walking properties.  The
        // QuickJS global object reaches itself via `globalThis` (and via
        // `Function.prototype.constructor` chains, etc.); without an early
        // mapping the recursive fromJS call would not find this object in
        // the cache and would start a fresh mutable child → infinite
        // recursion that hangs the require() path.  pObj is mutable, so the
        // same handle keeps resolving to the latest snapshot as we add
        // attributes below; the mapping does not need to be re-registered.
        GCBridge::registerMapping(val, pObj, ctx);

        // Iterate over JS object properties and set as attributes in protoCore.
        JSPropertyEnum* props;
        uint32_t prop_count;
        if (JS_GetOwnPropertyNames(ctx, &props, &prop_count, val, JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK) == 0) {
            for (uint32_t i = 0; i < prop_count; i++) {
                JSValue prop_val = JS_GetProperty(ctx, val, props[i].atom);
                const char* prop_name = JS_AtomToCString(ctx, props[i].atom);

                const proto::ProtoObject* pVal = fromJS(ctx, prop_val, pContext);
                const proto::ProtoString* pName = proto::ProtoString::createSymbol(pContext, prop_name);

                if (pName) {
                    pObj = pObj->setAttribute(pContext, pName, pVal);
                }

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
    const proto::ProtoObject* nullSentinel = protojs::getNullSentinel();
    if (nullSentinel && obj == nullSentinel) {
        return JS_NULL;
    }
    const proto::ProtoObject* undefinedSentinel = protojs::getUndefinedSentinel();
    if (undefinedSentinel && obj == undefinedSentinel) {
        return JS_UNDEFINED;
    }
    if (obj == PROTO_NONE || obj == nullptr) {
        return JS_UNDEFINED;
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
