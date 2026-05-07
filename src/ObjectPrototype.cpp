#include "ObjectPrototype.h"
#include "ArrayPrototype.h"
#include "FunctionPrototype.h"
#include "JSSymbols.h"
#include "headers/protoCore.h"
#include "JSContext.h"
#include "runtime/ProtoInterpreter.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <vector>

namespace protojs {

namespace {

// ---------------------------------------------------------------------------
// isInternalKey — returns true for interpreter bookkeeping attributes that
// must not be exposed as JS-visible own properties.  All internal keys use
// the "__name__" pattern (leading and trailing double underscore).
// ---------------------------------------------------------------------------
static bool isInternalKey(proto::ProtoContext* ctx, const proto::ProtoString* key) {
    if (!key) return false;
    std::string s;
    key->toUTF8String(ctx, s);
    return s.size() >= 4
        && s[0] == '_' && s[1] == '_'
        && s[s.size()-1] == '_' && s[s.size()-2] == '_';
}

// ---------------------------------------------------------------------------
// collectOwnKeys — fills `keys` and `vals` with the JS-visible own string
// properties of `obj`.  Internal (__*__) keys and the "length" property of
// arrays are always excluded.  When includeNonEnumerable is false (default),
// only enumerable properties are returned (used by Object.keys).  When true,
// all own properties are returned regardless of enumerable flag
// (used by Object.getOwnPropertyNames).
// ---------------------------------------------------------------------------
static void collectOwnKeys(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* obj,
    std::vector<std::string>&       keys,
    std::vector<const proto::ProtoObject*>* vals,
    bool includeNonEnumerable = false)
{
    if (!obj || obj == PROTO_NONE) return;
    const proto::ProtoSparseList* own = obj->getOwnAttributes(ctx);
    if (!own) return;

    // Detect arrays to suppress the "length" key (length is non-enumerable on arrays).
    const proto::ProtoString* isArrKey = JSSymbols::isArray(ctx);
    bool isArr = false;
    if (isArrKey) {
        const proto::ProtoObject* arrFlag = obj->getAttribute(ctx, isArrKey, false);
        isArr = arrFlag && arrFlag != PROTO_NONE;
    }
    const proto::ProtoString* lenSymbol = JSSymbols::length(ctx);

    const proto::ProtoSparseListIterator* it = own->getIterator(ctx);
    while (it && it->hasNext(ctx)) {
        unsigned long rawKey = it->nextKey(ctx);
        const proto::ProtoObject* val = it->nextValue(ctx);
        it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
        const proto::ProtoString* propKey =
            reinterpret_cast<const proto::ProtoString*>(rawKey);
        if (!propKey) continue;
        if (isInternalKey(ctx, propKey)) continue;
        // Suppress "length" on arrays (matches spec: array length is non-enumerable).
        if (isArr && lenSymbol && propKey == lenSymbol) continue;
        std::string kstr;
        propKey->toUTF8String(ctx, kstr);
        // Respect the enumerable descriptor flag (bit 2 of __pd_<key>__).
        // A missing __pd__ key means default = enumerable (bit 2 = 1).
        // Skip non-enumerable properties unless includeNonEnumerable is set.
        if (!includeNonEnumerable) {
            std::string pdKeyStr = "__pd_" + kstr + "__";
            const proto::ProtoObject* pko = ctx->fromUTF8String(pdKeyStr.c_str());
            const proto::ProtoString* pdk = pko ? pko->asString(ctx) : nullptr;
            if (pdk) {
                const proto::ProtoObject* pdv = obj->getAttribute(ctx, pdk, false);
                if (pdv && pdv != PROTO_NONE && pdv->isInteger(ctx)) {
                    uint8_t bits = static_cast<uint8_t>(pdv->asLong(ctx));
                    if (!(bits & 0x4)) continue; // not enumerable — skip
                }
            }
        }
        keys.push_back(kstr);
        if (vals) vals->push_back(val ? val : PROTO_NONE);
    }
}

// ---------------------------------------------------------------------------
// Object.keys(obj) → array of own enumerable string property names.
// ---------------------------------------------------------------------------
static const proto::ProtoObject* objectKeys(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    const proto::ProtoObject* obj = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0) : nullptr;

    std::vector<std::string> keys;
    collectOwnKeys(ctx, obj, keys, nullptr);

    const proto::ProtoObject* result = createNewArray(ctx, nullptr);
    const proto::ProtoString* lenKey = JSSymbols::length(ctx);
    const proto::ProtoString* isArrKey2 = JSSymbols::isArray(ctx);
    for (size_t i = 0; i < keys.size(); i++) {
        const proto::ProtoString* ik = JSSymbols::indexKey(ctx, static_cast<uint32_t>(i));
        const proto::ProtoObject* kv = ctx->fromUTF8String(keys[i].c_str());
        if (ik && kv) result = result->setAttribute(ctx, ik, kv);
    }
    if (lenKey) result = result->setAttribute(ctx, lenKey, ctx->fromInteger(static_cast<long long>(keys.size())));
    if (isArrKey2) result = result->setAttribute(ctx, isArrKey2, ctx->fromInteger(1LL));
    return result;
}

// ---------------------------------------------------------------------------
// Object.values(obj) → array of own enumerable property values.
// ---------------------------------------------------------------------------
static const proto::ProtoObject* objectValues(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    const proto::ProtoObject* obj = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0) : nullptr;

    std::vector<std::string> keys;
    std::vector<const proto::ProtoObject*> vals;
    collectOwnKeys(ctx, obj, keys, &vals);

    const proto::ProtoObject* result = createNewArray(ctx, nullptr);
    const proto::ProtoString* lenKey = JSSymbols::length(ctx);
    const proto::ProtoString* isArrKey2 = JSSymbols::isArray(ctx);
    for (size_t i = 0; i < vals.size(); i++) {
        const proto::ProtoString* ik = JSSymbols::indexKey(ctx, static_cast<uint32_t>(i));
        if (ik) result = result->setAttribute(ctx, ik, vals[i]);
    }
    if (lenKey) result = result->setAttribute(ctx, lenKey, ctx->fromInteger(static_cast<long long>(vals.size())));
    if (isArrKey2) result = result->setAttribute(ctx, isArrKey2, ctx->fromInteger(1LL));
    return result;
}

// ---------------------------------------------------------------------------
// Object.entries(obj) → array of [key, value] pair arrays.
// ---------------------------------------------------------------------------
static const proto::ProtoObject* objectEntries(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    const proto::ProtoObject* obj = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0) : nullptr;

    std::vector<std::string> keys;
    std::vector<const proto::ProtoObject*> vals;
    collectOwnKeys(ctx, obj, keys, &vals);

    const proto::ProtoObject* result = createNewArray(ctx, nullptr);
    const proto::ProtoString* lenKey = JSSymbols::length(ctx);
    const proto::ProtoString* isArrKey2 = JSSymbols::isArray(ctx);
    const proto::ProtoString* idx0 = JSSymbols::indexKey(ctx, 0);
    const proto::ProtoString* idx1 = JSSymbols::indexKey(ctx, 1);

    for (size_t i = 0; i < keys.size(); i++) {
        // Build pair [key, value] as a 2-element array.
        const proto::ProtoObject* pair = createNewArray(ctx, nullptr);
        const proto::ProtoObject* kv  = ctx->fromUTF8String(keys[i].c_str());
        if (idx0 && kv)       pair = pair->setAttribute(ctx, idx0, kv);
        if (idx1)             pair = pair->setAttribute(ctx, idx1, vals[i]);
        if (lenKey)           pair = pair->setAttribute(ctx, lenKey, ctx->fromInteger(2LL));
        if (isArrKey2)        pair = pair->setAttribute(ctx, isArrKey2, ctx->fromInteger(1LL));

        const proto::ProtoString* outerIdx = JSSymbols::indexKey(ctx, static_cast<uint32_t>(i));
        if (outerIdx) result = result->setAttribute(ctx, outerIdx, pair);
    }
    if (lenKey) result = result->setAttribute(ctx, lenKey, ctx->fromInteger(static_cast<long long>(keys.size())));
    if (isArrKey2) result = result->setAttribute(ctx, isArrKey2, ctx->fromInteger(1LL));
    return result;
}

// ---------------------------------------------------------------------------
// Object.assign(target, ...sources) → target
// Copies own enumerable string-keyed properties from each source into target.
// ---------------------------------------------------------------------------
static const proto::ProtoObject* objectAssign(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    int argc = args ? args->getSize(ctx) : 0;
    if (argc == 0) return PROTO_NONE;
    const proto::ProtoObject* target = args->getAt(ctx, 0);
    if (!target || target == PROTO_NONE) return target ? target : PROTO_NONE;

    for (int si = 1; si < argc; si++) {
        const proto::ProtoObject* src = args->getAt(ctx, si);
        if (!src || src == PROTO_NONE) continue;
        const proto::ProtoSparseList* own = src->getOwnAttributes(ctx);
        if (!own) continue;
        const proto::ProtoSparseListIterator* it = own->getIterator(ctx);
        while (it && it->hasNext(ctx)) {
            unsigned long rawKey = it->nextKey(ctx);
            const proto::ProtoObject* val = it->nextValue(ctx);
            it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
            const proto::ProtoString* propKey =
                reinterpret_cast<const proto::ProtoString*>(rawKey);
            if (!propKey) continue;
            if (isInternalKey(ctx, propKey)) continue;
            target = target->setAttribute(ctx, propKey, val ? val : PROTO_NONE);
        }
    }
    return target;
}

// Forward declaration — defined below after objectDefineProperty.
static const proto::ProtoObject* objectDefineProperty(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*);

// ---------------------------------------------------------------------------
// Object.create(proto[, propertiesObject]) → new object with [[Prototype]]=proto
// ---------------------------------------------------------------------------

static const proto::ProtoObject* objectCreate(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!args || args->getSize(ctx) == 0) return ctx->newObject(true);

    const proto::ProtoObject* protoArg = args->getAt(ctx, 0);
    const proto::ProtoObject* result;

    if (!protoArg || protoArg == PROTO_NONE || protoArg == getNullSentinel()) {
        // Object.create(null) → plain object with no prototype
        result = ctx->newObject(true);
    } else {
        // Object.create(proto) → child inheriting from proto
        result = protoArg->newChild(ctx, true);
    }

    // Second argument: property descriptors object
    if (args->getSize(ctx) >= 2) {
        const proto::ProtoObject* propsObj = args->getAt(ctx, 1);
        if (propsObj && propsObj != PROTO_NONE) {
            const proto::ProtoSparseList* own = propsObj->getOwnAttributes(ctx);
            if (own) {
                const proto::ProtoSparseListIterator* it = own->getIterator(ctx);
                while (it && it->hasNext(ctx)) {
                    unsigned long rawKey = it->nextKey(ctx);
                    const proto::ProtoObject* descObj = it->nextValue(ctx);
                    it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
                    const proto::ProtoString* propKey =
                        reinterpret_cast<const proto::ProtoString*>(rawKey);
                    if (!propKey) continue;
                    if (isInternalKey(ctx, propKey)) continue;
                    if (descObj && descObj != PROTO_NONE) {
                        std::string keyStr;
                        propKey->toUTF8String(ctx, keyStr);
                        if (keyStr != "length") {
                            const proto::ProtoList* dpArgs = ctx->newList();
                            dpArgs = dpArgs->appendLast(ctx, result);
                            dpArgs = dpArgs->appendLast(ctx, ctx->fromUTF8String(keyStr.c_str()));
                            dpArgs = dpArgs->appendLast(ctx, descObj);
                            const proto::ProtoObject* nr = objectDefineProperty(ctx, nullptr, nullptr, dpArgs, nullptr);
                            if (nr && nr != PROTO_NONE) result = nr;
                        }
                    }
                }
            }
        }
    }

    return result;
}

// ---------------------------------------------------------------------------
// Freeze / seal / extensibility state storage.
//
// IMPORTANT: We do NOT store these flags as ProtoObject attributes via
// setAttribute(). Calling protoCore type-interrogation methods (isString,
// isCell, etc.) on a ProtoObject after setAttribute() has been called on it
// causes infinite loops inside protoCore. This is a known protoCore bug.
//
// Instead, we track state in thread-local pointer sets. Using the raw pointer
// as the key is safe within a single script execution: frozen/sealed objects
// remain referenced (and thus not GC'd) for their entire observable lifetime.
// ---------------------------------------------------------------------------

// instead, we use markers added to the inheritance chain.
// ---------------------------------------------------------------------------

// Map from a JS object to its explicitly-overridden [[Prototype]], set by
// Object.setPrototypeOf(). protoCore objects are immutable so we cannot change
// the C++ parent pointer; we track the override out-of-band instead.
// Objects in this map are always reachable (the map itself holds the reference),
// so the GC will not reclaim them while the override is active.
static thread_local std::unordered_map<const proto::ProtoObject*,
                                       const proto::ProtoObject*> t_jsProtoMap;

// Returns true if obj is a JS primitive (not a plain object or array).
// JS null is represented as t_nullSentinel (a real ProtoObject cell), so we
// must check it explicitly.
//
// NOTE: We deliberately avoid calling obj->isString(ctx) here. Calling isString
// on a plain mutable ProtoObject cell causes an infinite loop inside protoCore.
static bool isPrimitive(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    if (!obj || obj == PROTO_NONE) return true;
    if (obj == getNullSentinel()) return true;   // JS null is a primitive
    // Fast tagged-pointer checks only; no isString (hangs on plain cells).
    return obj->isBoolean(ctx) || obj->isInteger(ctx) || obj->isDouble(ctx) ||
           obj->isFloat(ctx) || obj->isNone(ctx);
}

// ---------------------------------------------------------------------------
// Object.freeze(obj) — marks the object non-extensible and frozen.
// All current and future property writes will be rejected by OP_put_field.
// ---------------------------------------------------------------------------

static const proto::ProtoObject* objectFreeze(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!args || args->getSize(ctx) == 0) return PROTO_NONE;
    const proto::ProtoObject* obj = args->getAt(ctx, 0);
    if (!obj || obj == PROTO_NONE) return PROTO_NONE;
    if (isPrimitive(ctx, obj)) return obj;
    
    JSContextWrapper* wrapper = JSContextWrapper::current();
    if (wrapper) {
        obj->addParent(ctx, wrapper->getFrozenMarker());
        obj->addParent(ctx, wrapper->getNonExtensibleMarker());
    }
    return obj;
}

// ---------------------------------------------------------------------------
// Object.isFrozen(obj)
// ---------------------------------------------------------------------------

static const proto::ProtoObject* objectIsFrozen(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!args || args->getSize(ctx) == 0) return PROTO_TRUE; // no arg → undefined → frozen
    const proto::ProtoObject* obj = args->getAt(ctx, 0);
    if (!obj || obj == PROTO_NONE) return PROTO_TRUE; // undefined/null are frozen
    if (isPrimitive(ctx, obj)) return PROTO_TRUE; // primitives are frozen

    JSContextWrapper* wrapper = JSContextWrapper::current();
    if (wrapper && obj->hasParent(ctx, wrapper->getFrozenMarker())) {
        return PROTO_TRUE;
    }
    return PROTO_FALSE;
}

// ---------------------------------------------------------------------------
// Object.seal(obj) — marks the object non-extensible and sealed.
// Existing properties remain writable but cannot be deleted or reconfigured.
// ---------------------------------------------------------------------------

static const proto::ProtoObject* objectSeal(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!args || args->getSize(ctx) == 0) return PROTO_NONE;
    const proto::ProtoObject* obj = args->getAt(ctx, 0);
    if (!obj || obj == PROTO_NONE) return PROTO_NONE;
    if (isPrimitive(ctx, obj)) return obj;
    
    JSContextWrapper* wrapper = JSContextWrapper::current();
    if (wrapper) {
        obj->addParent(ctx, wrapper->getSealedMarker());
        obj->addParent(ctx, wrapper->getNonExtensibleMarker());
    }
    return obj;
}

// ---------------------------------------------------------------------------
// Object.isSealed(obj)
// ---------------------------------------------------------------------------

static const proto::ProtoObject* objectIsSealed(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!args || args->getSize(ctx) == 0) return PROTO_TRUE;
    const proto::ProtoObject* obj = args->getAt(ctx, 0);
    if (!obj || obj == PROTO_NONE) return PROTO_TRUE;
    if (isPrimitive(ctx, obj)) return PROTO_TRUE;

    JSContextWrapper* wrapper = JSContextWrapper::current();
    if (wrapper) {
        if (obj->hasParent(ctx, wrapper->getFrozenMarker())) return PROTO_TRUE;
        if (obj->hasParent(ctx, wrapper->getSealedMarker())) return PROTO_TRUE;
    }
    return PROTO_FALSE;
}

// ---------------------------------------------------------------------------
// Object.preventExtensions(obj) — marks the object non-extensible.
// New properties cannot be added, but existing ones remain writable.
// ---------------------------------------------------------------------------

static const proto::ProtoObject* objectPreventExtensions(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!args || args->getSize(ctx) == 0) return PROTO_NONE;
    const proto::ProtoObject* obj = args->getAt(ctx, 0);
    if (!obj || obj == PROTO_NONE) return PROTO_NONE;
    if (isPrimitive(ctx, obj)) return obj;

    JSContextWrapper* wrapper = JSContextWrapper::current();
    if (wrapper) {
        obj->addParent(ctx, wrapper->getNonExtensibleMarker());
    }
    return obj;
}

// ---------------------------------------------------------------------------
// Object.isExtensible(obj) — returns true unless prevented/sealed/frozen.
// ---------------------------------------------------------------------------

static const proto::ProtoObject* objectIsExtensible(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!args || args->getSize(ctx) == 0) return PROTO_FALSE;
    const proto::ProtoObject* obj = args->getAt(ctx, 0);
    if (!obj || obj == PROTO_NONE) return PROTO_FALSE;
    if (isPrimitive(ctx, obj)) return PROTO_FALSE;

    JSContextWrapper* wrapper = JSContextWrapper::current();
    if (wrapper && obj->hasParent(ctx, wrapper->getNonExtensibleMarker())) {
        return PROTO_FALSE;
    }
    return PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// Object.getOwnPropertyNames(obj) — returns ALL own string-keyed properties,
// including non-enumerable ones (unlike Object.keys which filters them out).
// ---------------------------------------------------------------------------

static const proto::ProtoObject* objectGetOwnPropertyNames(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    const proto::ProtoObject* obj = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0) : nullptr;

    // Pass includeNonEnumerable=true to collect all own string properties.
    std::vector<std::string> keys;
    collectOwnKeys(ctx, obj, keys, nullptr, /*includeNonEnumerable=*/true);

    const proto::ProtoObject* result = createNewArray(ctx, nullptr);
    const proto::ProtoString* lenKey  = JSSymbols::length(ctx);
    const proto::ProtoString* isArrKey2 = JSSymbols::isArray(ctx);
    for (size_t i = 0; i < keys.size(); i++) {
        const proto::ProtoString* ik = JSSymbols::indexKey(ctx, static_cast<uint32_t>(i));
        const proto::ProtoObject* kv = ctx->fromUTF8String(keys[i].c_str());
        if (ik && kv) result = result->setAttribute(ctx, ik, kv);
    }
    if (lenKey)   result = result->setAttribute(ctx, lenKey,   ctx->fromInteger(static_cast<long long>(keys.size())));
    if (isArrKey2) result = result->setAttribute(ctx, isArrKey2, ctx->fromInteger(1LL));
    return result;
}

// ---------------------------------------------------------------------------
// Object.getPrototypeOf(obj) → the [[Prototype]] of obj, or null
// ---------------------------------------------------------------------------

static const proto::ProtoObject* objectGetPrototypeOf(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!args || args->getSize(ctx) == 0) return PROTO_NONE;
    const proto::ProtoObject* obj = args->getAt(ctx, 0);
    if (!obj || obj == PROTO_NONE) return PROTO_NONE;
    // Check for an explicit JS prototype override first.
    {
        auto it = t_jsProtoMap.find(obj);
        if (it != t_jsProtoMap.end()) return it->second;
    }
    // Fall back to the C++ (protoCore) parent chain.
    const proto::ProtoObject* proto = obj->getPrototype(ctx);
    if (!proto || proto == PROTO_NONE) {
        // No prototype → return JS null sentinel
        return getNullSentinel();
    }
    return proto;
}

// ---------------------------------------------------------------------------
// Object.setPrototypeOf(obj, proto) — ES2015 §19.1.2.20
// Changes the [[Prototype]] of obj to proto.
// protoCore objects are immutable, so we track the override in a thread-local
// map rather than modifying the C++ parent pointer.
// ---------------------------------------------------------------------------

static const proto::ProtoObject* objectSetPrototypeOf(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!args || args->getSize(ctx) < 2) return PROTO_NONE;
    const proto::ProtoObject* obj   = args->getAt(ctx, 0);
    const proto::ProtoObject* proto = args->getAt(ctx, 1);
    if (!obj || obj == PROTO_NONE) return PROTO_NONE;
    // Per spec, non-extensible objects throw TypeError — skip that check for now.
    if (!proto || proto == PROTO_NONE || proto == getNullSentinel()) {
        // Setting proto to null removes any override.
        t_jsProtoMap.erase(obj);
    } else {
        t_jsProtoMap[obj] = proto;
    }
    // Spec: returns the modified object.
    return obj;
}

// ---------------------------------------------------------------------------
// coercePropNameToString — convert any JS value to a property name string
// per ECMAScript ToPropertyKey (simplified: no Symbol support).
// ---------------------------------------------------------------------------
static bool coercePropNameToString(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* nameObj,
    std::string& out)
{
    if (!nameObj || nameObj == PROTO_NONE) { out = "undefined"; return true; }
    if (nameObj->isString(ctx)) {
        const proto::ProtoString* ps = nameObj->asString(ctx);
        if (ps) { ps->toUTF8String(ctx, out); return true; }
    }
    if (nameObj->isInteger(ctx)) { out = std::to_string(nameObj->asLong(ctx)); return true; }
    if (nameObj->isDouble(ctx) || nameObj->isFloat(ctx)) {
        double d = nameObj->asDouble(ctx);
        if (d == (long long)d) out = std::to_string((long long)d);
        else {
            char buf[64]; snprintf(buf, sizeof(buf), "%g", d);
            out = buf;
        }
        return true;
    }
    if (nameObj->isBoolean(ctx)) { out = nameObj->asBoolean(ctx) ? "true" : "false"; return true; }
    if (nameObj == getNullSentinel()) { out = "null"; return true; }
    return false;
}

// ---------------------------------------------------------------------------
// Object.defineProperty(obj, propName, descriptor)
//
// Stores the property value and descriptor flags on the target object.
// Descriptor flags are encoded as a single integer (bits: 0=writable,
// 1=configurable, 2=enumerable) under the hidden key "__pd_<propName>__".
// A missing __pd__ key means all flags are true (default JS semantics).
// ---------------------------------------------------------------------------

static const proto::ProtoObject* objectDefineProperty(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!ctx || !args || args->getSize(ctx) < 3) return PROTO_NONE;

    const proto::ProtoObject* target = args->getAt(ctx, 0);
    // Per ES spec, Object.defineProperty throws TypeError on non-object first argument.
    {
        const proto::ProtoObject* nullSentinel = getNullSentinel();
        bool isNull = (target == nullSentinel);
        bool isUndefined = (!target || target == PROTO_NONE);
        if (isNull || isUndefined ||
            target->isBoolean(ctx) || target->isInteger(ctx) ||
            target->isDouble(ctx)  || target->isFloat(ctx)   ||
            target->isString(ctx)) {
            signalNativeException(makeNativeError(ctx, "TypeError",
                "Object.defineProperty called on non-object"));
            return PROTO_NONE;
        }
    }

    // Get property name string — coerce any JS value per spec (ToPropertyKey).
    const proto::ProtoObject* propNameObj = args->getAt(ctx, 1);
    std::string propName;
    if (!coercePropNameToString(ctx, propNameObj, propName) || propName.empty())
        return target;

    // Get descriptor object.
    const proto::ProtoObject* desc = args->getAt(ctx, 2);
    if (!desc || desc == PROTO_NONE) return target;

    // Per ES5 §8.12.9: if the property already exists and is non-configurable,
    // redefining it must throw a TypeError.
    // Sidecar bit layout: bit 0 = writable, bit 1 = configurable, bit 2 = enumerable.
    {
        const proto::ProtoObject* ko0 = ctx->fromUTF8String(propName.c_str());
        const proto::ProtoString* pk0 = ko0 ? ko0->asString(ctx) : nullptr;
        if (pk0) {
            const proto::ProtoObject* existingVal = target->getAttribute(ctx, pk0, false);
            if (existingVal != nullptr) {  // property exists (existingVal may be PROTO_NONE = undefined)
                // Check sidecar for configurable bit.
                std::string pdExKey = std::string("__pd_") + propName + "__";
                const proto::ProtoObject* pdeko = ctx->fromUTF8String(pdExKey.c_str());
                const proto::ProtoString* pdek = pdeko ? pdeko->asString(ctx) : nullptr;
                const proto::ProtoObject* existingBitsObj = pdek
                    ? target->getAttribute(ctx, pdek, false) : nullptr;
                // If no sidecar exists, the property was created by plain assignment and
                // has default flags {writable, enumerable, configurable} = true.
                // Treating it as configurable is correct — no TypeError needed.
                if (existingBitsObj && existingBitsObj != PROTO_NONE
                    && existingBitsObj->isInteger(ctx)) {
                    long existingBits = existingBitsObj->asLong(ctx);
                    // bit 1 (value 0x2) is the configurable flag.
                    bool isConfigurable = (existingBits & 0x2) != 0;
                    if (!isConfigurable) {
                        signalNativeException(makeNativeError(ctx, "TypeError",
                            "Cannot redefine property: property is non-configurable"));
                        return PROTO_NONE;
                    }
                }
            }
        }
    }

    // Extract flags from descriptor.
    // Per ES spec, Object.defineProperty defaults: writable=false, configurable=false, enumerable=false
    // when flags are not explicitly provided.
    auto getBoolProp = [&](const char* name, bool defaultVal) -> bool {
        const proto::ProtoObject* ko = ctx->fromUTF8String(name);
        const proto::ProtoString* k  = ko ? ko->asString(ctx) : nullptr;
        if (!k) return defaultVal;
        const proto::ProtoObject* v = desc->getAttribute(ctx, k, false);
        if (!v || v == PROTO_NONE) return defaultVal;
        return (v == PROTO_TRUE) || (v->isBoolean(ctx) && v->asBoolean(ctx));
    };

    bool writable     = getBoolProp("writable",     false);
    bool configurable = getBoolProp("configurable",  false);
    bool enumerable   = getBoolProp("enumerable",    false);

    // Accessor descriptor: extract get/set functions and store as __get_<name>__ / __set_<name>__.
    auto getFnProp = [&](const char* name) -> const proto::ProtoObject* {
        const proto::ProtoObject* ko = ctx->fromUTF8String(name);
        const proto::ProtoString* k  = ko ? ko->asString(ctx) : nullptr;
        if (!k) return nullptr;
        const proto::ProtoObject* v = desc->getAttribute(ctx, k, false);
        if (!v || v == PROTO_NONE || v->isNone(ctx)) return nullptr;
        return v;
    };
    const proto::ProtoObject* getter = getFnProp("get");
    const proto::ProtoObject* setter = getFnProp("set");
    bool isAccessor = getter || setter;

    // Per ES5 8.10.5 step 9: it is a TypeError to specify both accessor (get/set)
    // and data (value/writable) fields in the same descriptor.
    if (isAccessor) {
        auto descHasKey = [&](const char* name) -> bool {
            const proto::ProtoObject* ko2 = ctx->fromUTF8String(name);
            const proto::ProtoString* k2  = ko2 ? ko2->asString(ctx) : nullptr;
            if (!k2) return false;
            // getAttribute(..., false) returns nullptr when property is absent on this object;
            // returns PROTO_NONE or a real value when the key is present.
            const proto::ProtoObject* v = desc->getAttribute(ctx, k2, false);
            return v != nullptr;
        };
        if (descHasKey("value") || descHasKey("writable")) {
            signalNativeException(makeNativeError(ctx, "TypeError",
                "Invalid property descriptor. Cannot both specify accessors "
                "and a value or writable attribute"));
            return PROTO_NONE;
        }
    }

    if (getter) {
        std::string gkStr = "__get_" + propName + "__";
        const proto::ProtoObject* gko = ctx->fromUTF8String(gkStr.c_str());
        const proto::ProtoString* gk  = gko ? gko->asString(ctx) : nullptr;
        if (gk) target = target->setAttribute(ctx, gk, getter);
    }
    if (setter) {
        std::string skStr = "__set_" + propName + "__";
        const proto::ProtoObject* sko = ctx->fromUTF8String(skStr.c_str());
        const proto::ProtoString* sk  = sko ? sko->asString(ctx) : nullptr;
        if (sk) target = target->setAttribute(ctx, sk, setter);
    }
    if (isAccessor) {
        // When converting a data property to an accessor, remove the data key
        // entirely (nullptr, not PROTO_NONE).  Using nullptr makes setAttribute
        // call implRemoveAt so the key is absent: hasAttribute returns PROTO_FALSE.
        // This is essential for two reasons:
        //   1. arrLen / arrGet no longer see a stale data value instead of invoking
        //      the getter (or returning undefined for setter-only accessors).
        //   2. The global environment record's HasBinding returns false after the
        //      data key is removed, so strict-mode assignment correctly throws
        //      ReferenceError when the accessor is later deleted.
        const proto::ProtoObject* dko = ctx->fromUTF8String(propName.c_str());
        const proto::ProtoString* dk  = dko ? dko->asString(ctx) : nullptr;
        if (dk) target = target->setAttribute(ctx, dk, nullptr);
    }
    // Store the value for data descriptors.
    // Per ES5 §8.6.1: a data property always has a Value; default is undefined.
    // Must create the property even when "value" is absent so hasOwnProperty returns true.
    if (!isAccessor) {
        const proto::ProtoObject* valueKey = ctx->fromUTF8String("value");
        const proto::ProtoString* vkp = valueKey ? valueKey->asString(ctx) : nullptr;
        const proto::ProtoObject* val = (vkp) ? desc->getAttribute(ctx, vkp, false) : nullptr;
        // val == nullptr means "value" key was absent → default to PROTO_NONE (undefined).
        const proto::ProtoObject* storedVal = val ? val : PROTO_NONE;
        const proto::ProtoObject* ko = ctx->fromUTF8String(propName.c_str());
        const proto::ProtoString* pk = ko ? ko->asString(ctx) : nullptr;
        if (pk) target = target->setAttribute(ctx, pk, storedVal);
    }

    // Encode descriptor flags and store as sidecar attribute.
    uint8_t bits = (writable ? 0x1 : 0) | (configurable ? 0x2 : 0) | (enumerable ? 0x4 : 0);
    std::string pdKeyStr = "__pd_" + propName + "__";
    const proto::ProtoObject* pko = ctx->fromUTF8String(pdKeyStr.c_str());
    const proto::ProtoString* pdk = pko ? pko->asString(ctx) : nullptr;
    if (pdk) target = target->setAttribute(ctx, pdk, ctx->fromInteger((long long)bits));

    return target;
}

// ---------------------------------------------------------------------------
// Object.getOwnPropertyDescriptor(obj, propName)
// Returns a data or accessor descriptor for OWN properties only, or
// undefined (PROTO_NONE) if the property is absent or inherited.
// ---------------------------------------------------------------------------

static const proto::ProtoObject* objectGetOwnPropertyDescriptor(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!ctx || !args || args->getSize(ctx) < 2) return PROTO_NONE;
    const proto::ProtoObject* target = args->getAt(ctx, 0);
    const proto::ProtoObject* propNameObj = args->getAt(ctx, 1);
    if (!target || target == PROTO_NONE) return PROTO_NONE;

    std::string propName;
    if (!coercePropNameToString(ctx, propNameObj, propName) || propName.empty())
        return PROTO_NONE;

    const proto::ProtoObject* ko = ctx->fromUTF8String(propName.c_str());
    const proto::ProtoString* pk = ko ? ko->asString(ctx) : nullptr;
    if (!pk) return PROTO_NONE;

    // Helper: build result descriptor object.
    auto setAttr = [&](const proto::ProtoObject*& r, const char* name, const proto::ProtoObject* v) {
        const proto::ProtoObject* k = ctx->fromUTF8String(name);
        const proto::ProtoString* ks = k ? k->asString(ctx) : nullptr;
        if (ks) r = r->setAttribute(ctx, ks, v);
    };

    // Check for accessor descriptor first: __get_<prop>__ or __set_<prop>__ as OWN attributes.
    std::string gkStr = "__get_" + propName + "__";
    std::string skStr = "__set_" + propName + "__";
    const proto::ProtoObject* gko = ctx->fromUTF8String(gkStr.c_str());
    const proto::ProtoObject* sko = ctx->fromUTF8String(skStr.c_str());
    const proto::ProtoString* gk  = gko ? gko->asString(ctx) : nullptr;
    const proto::ProtoString* sk2 = sko ? sko->asString(ctx) : nullptr;

    const proto::ProtoObject* getter = (gk) ? target->hasOwnAttribute(ctx, gk) : nullptr;
    const proto::ProtoObject* setter = (sk2) ? target->hasOwnAttribute(ctx, sk2) : nullptr;

    if (getter == PROTO_TRUE || setter == PROTO_TRUE) {
        // Accessor descriptor.
        const proto::ProtoObject* getterFn = (gk && getter == PROTO_TRUE)
            ? target->getAttribute(ctx, gk, false) : PROTO_NONE;
        const proto::ProtoObject* setterFn = (sk2 && setter == PROTO_TRUE)
            ? target->getAttribute(ctx, sk2, false) : PROTO_NONE;

        // Get descriptor flags (configurable, enumerable only for accessors).
        std::string pdKeyStr = "__pd_" + propName + "__";
        const proto::ProtoObject* pdko = ctx->fromUTF8String(pdKeyStr.c_str());
        const proto::ProtoString* pdk  = pdko ? pdko->asString(ctx) : nullptr;
        const proto::ProtoObject* bitsObj = pdk ? target->getAttribute(ctx, pdk, false) : nullptr;
        uint8_t bits = 0x2; // default for accessors: configurable=true, enumerable=false
        if (bitsObj && bitsObj != PROTO_NONE && bitsObj->isInteger(ctx))
            bits = static_cast<uint8_t>(bitsObj->asLong(ctx));

        const proto::ProtoObject* result = ctx->newObject(true);
        setAttr(result, "get",          getterFn ? getterFn : PROTO_NONE);
        setAttr(result, "set",          setterFn ? setterFn : PROTO_NONE);
        setAttr(result, "enumerable",   (bits & 0x4) ? PROTO_TRUE : PROTO_FALSE);
        setAttr(result, "configurable", (bits & 0x2) ? PROTO_TRUE : PROTO_FALSE);
        return result;
    }

    // Check if the property is an OWN data property.
    const proto::ProtoObject* ownFlag = target->hasOwnAttribute(ctx, pk);
    if (ownFlag != PROTO_TRUE) return PROTO_NONE; // inherited or absent

    const proto::ProtoObject* val = target->getAttribute(ctx, pk, false);
    // val==nullptr means getAttribute returned null at C++ level (e.g. the attribute
    // stores PROTO_NONE = undefined).  The property DOES exist (ownFlag confirmed above),
    // so treat !val as PROTO_NONE rather than returning PROTO_NONE (absence).
    const proto::ProtoObject* storedVal = val ? val : PROTO_NONE;

    // Get descriptor flags.
    std::string pdKeyStr = "__pd_" + propName + "__";
    const proto::ProtoObject* pdko = ctx->fromUTF8String(pdKeyStr.c_str());
    const proto::ProtoString* pdk  = pdko ? pdko->asString(ctx) : nullptr;
    const proto::ProtoObject* bitsObj = pdk ? target->getAttribute(ctx, pdk, false) : nullptr;
    uint8_t bits = 0x7; // default: writable=true, configurable=true, enumerable=true
    if (bitsObj && bitsObj != PROTO_NONE && bitsObj->isInteger(ctx))
        bits = static_cast<uint8_t>(bitsObj->asLong(ctx));

    const proto::ProtoObject* result = ctx->newObject(true);
    setAttr(result, "value",        storedVal);
    setAttr(result, "writable",     (bits & 0x1) ? PROTO_TRUE : PROTO_FALSE);
    setAttr(result, "enumerable",   (bits & 0x4) ? PROTO_TRUE : PROTO_FALSE);
    setAttr(result, "configurable", (bits & 0x2) ? PROTO_TRUE : PROTO_FALSE);
    return result;
}

// ---------------------------------------------------------------------------
// Object.defineProperties(target, props) → apply a map of descriptors
// ---------------------------------------------------------------------------

static const proto::ProtoObject* objectDefineProperties(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!ctx || !args || args->getSize(ctx) < 2) return PROTO_NONE;
    const proto::ProtoObject* target = args->getAt(ctx, 0);
    const proto::ProtoObject* propsObj = args->getAt(ctx, 1);

    // Per spec: throw TypeError if first arg (O) is null/undefined or a primitive.
    {
        const proto::ProtoObject* nullSentinel = getNullSentinel();
        bool isNull = (target == nullSentinel);
        bool isUndefined = (!target || target == PROTO_NONE);
        if (isNull || isUndefined ||
            target->isBoolean(ctx) || target->isInteger(ctx) ||
            target->isDouble(ctx)  || target->isFloat(ctx)   ||
            target->isString(ctx)) {
            signalNativeException(makeNativeError(ctx, "TypeError",
                "Object.defineProperties called on non-object"));
            return PROTO_NONE;
        }
    }
    // Per spec: throw TypeError if Properties arg is null or undefined.
    {
        const proto::ProtoObject* nullSentinel = getNullSentinel();
        if (!propsObj || propsObj == PROTO_NONE || propsObj == nullSentinel) {
            signalNativeException(makeNativeError(ctx, "TypeError",
                "Cannot convert undefined or null to object"));
            return PROTO_NONE;
        }
    }

    const proto::ProtoSparseList* own = propsObj->getOwnAttributes(ctx);
    if (!own) return target;
    const proto::ProtoSparseListIterator* it = own->getIterator(ctx);
    while (it && it->hasNext(ctx)) {
        unsigned long rawKey = it->nextKey(ctx);
        const proto::ProtoObject* descObj = it->nextValue(ctx);
        it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
        const proto::ProtoString* propKey =
            reinterpret_cast<const proto::ProtoString*>(rawKey);
        if (!propKey) continue;
        if (isInternalKey(ctx, propKey)) continue;
        if (descObj && descObj != PROTO_NONE) {
            std::string keyStr;
            propKey->toUTF8String(ctx, keyStr);
            if (keyStr != "length") {
                const proto::ProtoList* dpArgs = ctx->newList();
                dpArgs = dpArgs->appendLast(ctx, target);
                dpArgs = dpArgs->appendLast(ctx, ctx->fromUTF8String(keyStr.c_str()));
                dpArgs = dpArgs->appendLast(ctx, descObj);
                const proto::ProtoObject* newTarget = objectDefineProperty(ctx, nullptr, nullptr, dpArgs, nullptr);
                if (newTarget && newTarget != PROTO_NONE) target = newTarget;
            }
        }
    }
    return target;
}

// ---------------------------------------------------------------------------
// Object.fromEntries(iterable) → object from [[key,val], ...] pairs
// ---------------------------------------------------------------------------

static const proto::ProtoObject* objectFromEntries(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    const proto::ProtoObject* result = ctx->newObject(true);
    if (!args || args->getSize(ctx) == 0) return result;
    const proto::ProtoObject* iterable = args->getAt(ctx, 0);
    if (!iterable || iterable == PROTO_NONE) return result;

    const proto::ProtoString* lenKey = JSSymbols::length(ctx);
    if (!lenKey) return result;
    const proto::ProtoObject* lenObj = iterable->getAttribute(ctx, lenKey, false);
    long long len = 0;
    if (lenObj && lenObj != PROTO_NONE) {
        if (lenObj->isInteger(ctx)) len = lenObj->asLong(ctx);
        else if (lenObj->isDouble(ctx)) len = static_cast<long long>(lenObj->asDouble(ctx));
    }

    const proto::ProtoString* k0Key = JSSymbols::indexKey(ctx, 0);
    const proto::ProtoString* k1Key = JSSymbols::indexKey(ctx, 1);

    for (long long i = 0; i < len; i++) {
        const proto::ProtoString* idxKey =
            JSSymbols::indexKey(ctx, static_cast<uint32_t>(i));
        if (!idxKey) continue;
        const proto::ProtoObject* pair = iterable->getAttribute(ctx, idxKey, false);
        if (!pair || pair == PROTO_NONE) continue;

        const proto::ProtoObject* keyObj = k0Key ? pair->getAttribute(ctx, k0Key, false) : nullptr;
        const proto::ProtoObject* valObj = k1Key ? pair->getAttribute(ctx, k1Key, false) : PROTO_NONE;
        if (!valObj) valObj = PROTO_NONE;

        if (!keyObj || keyObj == PROTO_NONE) continue;
        std::string keyStr;
        if (keyObj->isString(ctx)) {
            const proto::ProtoString* ps = keyObj->asString(ctx);
            if (ps) ps->toUTF8String(ctx, keyStr);
        } else if (keyObj->isInteger(ctx)) {
            keyStr = std::to_string(keyObj->asLong(ctx));
        }
        if (keyStr.empty()) continue;
        const proto::ProtoString* entryKey = ctx->fromUTF8String(keyStr.c_str())->asString(ctx);
        if (entryKey) result = result->setAttribute(ctx, entryKey, valObj);
    }
    return result;
}

// ---------------------------------------------------------------------------
// Object.hasOwn(obj, key) — static version of hasOwnProperty
// ---------------------------------------------------------------------------

static const proto::ProtoObject* objectHasOwn(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!args || args->getSize(ctx) < 2) return PROTO_FALSE;
    const proto::ProtoObject* obj = args->getAt(ctx, 0);
    const proto::ProtoObject* key = args->getAt(ctx, 1);
    if (!obj || obj == PROTO_NONE || !key || key == PROTO_NONE) return PROTO_FALSE;

    std::string keyStr;
    if (key->isString(ctx)) {
        const proto::ProtoString* ps = key->asString(ctx);
        if (ps) ps->toUTF8String(ctx, keyStr);
    } else if (key->isInteger(ctx)) {
        keyStr = std::to_string(key->asLong(ctx));
    }
    if (keyStr.empty()) return PROTO_FALSE;
    const proto::ProtoString* strKey = ctx->fromUTF8String(keyStr.c_str())->asString(ctx);
    if (!strKey) return PROTO_FALSE;
    // hasOwnAttribute returns PROTO_TRUE if own, PROTO_FALSE if inherited, nullptr if absent
    const proto::ProtoObject* own = obj->hasOwnAttribute(ctx, strKey);
    if (own == PROTO_TRUE) return PROTO_TRUE;
    // Also check accessor sidecars — accessor properties have no data key when
    // defined via Object.defineProperty (the data key is removed on creation).
    for (const char* prefix : {"__get_", "__set_"}) {
        std::string sk = std::string(prefix) + keyStr + "__";
        const proto::ProtoObject* sko = ctx->fromUTF8String(sk.c_str());
        const proto::ProtoString* sks = sko ? sko->asString(ctx) : nullptr;
        if (sks && obj->hasOwnAttribute(ctx, sks) == PROTO_TRUE) return PROTO_TRUE;
    }
    return PROTO_FALSE;
}

// ---------------------------------------------------------------------------
// Instance method: hasOwnProperty(key)
// ---------------------------------------------------------------------------

static const proto::ProtoObject* objectHasOwnProperty(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!self || self == PROTO_NONE) return PROTO_FALSE;
    if (!args || args->getSize(ctx) == 0) return PROTO_FALSE;
    const proto::ProtoObject* key = args->getAt(ctx, 0);
    if (!key || key == PROTO_NONE) return PROTO_FALSE;

    std::string keyStr;
    if (key->isString(ctx)) {
        const proto::ProtoString* ps = key->asString(ctx);
        if (ps) ps->toUTF8String(ctx, keyStr);
    } else if (key->isInteger(ctx)) {
        keyStr = std::to_string(key->asLong(ctx));
    }
    if (keyStr.empty()) return PROTO_FALSE;

    const proto::ProtoString* strKey = ctx->fromUTF8String(keyStr.c_str())->asString(ctx);
    if (!strKey) return PROTO_FALSE;
    const proto::ProtoObject* own = self->hasOwnAttribute(ctx, strKey);
    if (own == PROTO_TRUE) return PROTO_TRUE;
    // Also check accessor sidecars — accessor properties have no data key when
    // defined via Object.defineProperty (the data key is removed on creation).
    for (const char* prefix : {"__get_", "__set_"}) {
        std::string sk = std::string(prefix) + keyStr + "__";
        const proto::ProtoObject* sko = ctx->fromUTF8String(sk.c_str());
        const proto::ProtoString* sks = sko ? sko->asString(ctx) : nullptr;
        if (sks && self->hasOwnAttribute(ctx, sks) == PROTO_TRUE) return PROTO_TRUE;
    }
    return PROTO_FALSE;
}

// ---------------------------------------------------------------------------
// Instance method: propertyIsEnumerable(key) → true if own property exists
// ---------------------------------------------------------------------------

static const proto::ProtoObject* objectPropertyIsEnumerable(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!self || self == PROTO_NONE) return PROTO_FALSE;
    if (!args || args->getSize(ctx) == 0) return PROTO_FALSE;
    const proto::ProtoObject* key = args->getAt(ctx, 0);
    if (!key || key == PROTO_NONE) return PROTO_FALSE;

    std::string keyStr;
    if (key->isString(ctx)) {
        const proto::ProtoString* ps = key->asString(ctx);
        if (ps) ps->toUTF8String(ctx, keyStr);
    } else if (key->isInteger(ctx)) {
        keyStr = std::to_string(key->asLong(ctx));
    }
    if (keyStr.empty()) return PROTO_FALSE;

    const proto::ProtoString* strKey = ctx->fromUTF8String(keyStr.c_str())->asString(ctx);
    if (!strKey) return PROTO_FALSE;

    // Check if it's an own property
    bool exists = (self->hasOwnAttribute(ctx, strKey) == PROTO_TRUE);
    if (!exists) {
        // Also check accessors
        for (const char* prefix : {"__get_", "__set_"}) {
            std::string sk = std::string(prefix) + keyStr + "__";
            const proto::ProtoObject* sko = ctx->fromUTF8String(sk.c_str());
            const proto::ProtoString* sks = sko ? sko->asString(ctx) : nullptr;
            if (sks && self->hasOwnAttribute(ctx, sks) == PROTO_TRUE) {
                exists = true;
                break;
            }
        }
    }
    if (!exists) return PROTO_FALSE;

    // Check descriptor bit 0x4 (enumerable)
    std::string pdKeyStr = std::string("__pd_") + keyStr + "__";
    const proto::ProtoObject* pdko = ctx->fromUTF8String(pdKeyStr.c_str());
    const proto::ProtoString* pdks = pdko ? pdko->asString(ctx) : nullptr;
    if (pdks) {
        const proto::ProtoObject* pdAttr = self->getAttribute(ctx, pdks, true);
        if (pdAttr && pdAttr->isInteger(ctx)) {
            long long bits = pdAttr->asLong(ctx);
            return (bits & 0x4) ? PROTO_TRUE : PROTO_FALSE;
        }
    }

    // Default: properties are enumerable unless explicitly marked (bits 0x3 vs 0x7)
    return PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// Instance method: toString() → "[object Object]"
// ---------------------------------------------------------------------------

static const proto::ProtoObject* objectToString(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*,
    const proto::ProtoSparseList*)
{
    if (!self || self == PROTO_NONE || self->isNone(ctx))
        return ctx->fromUTF8String("[object Undefined]");

    // null sentinel → [object Null]
    const proto::ProtoObject* nullSentinel = getNullSentinel();
    if (nullSentinel && self == nullSentinel)
        return ctx->fromUTF8String("[object Null]");

    if (self->isBoolean(ctx))
        return ctx->fromUTF8String("[object Boolean]");
    if (self->isInteger(ctx) || self->isDouble(ctx) || self->isFloat(ctx))
        return ctx->fromUTF8String("[object Number]");
    if (self->isString(ctx))
        return ctx->fromUTF8String("[object String]");

    // Function: JS closure (__bytecode_id__), native ProtoMethod, or wrapped
    // native function (__native_fn__ holds a ProtoMethod pointer).
    {
        const proto::ProtoString* bcKey = JSSymbols::bytecodeId(ctx);
        if (bcKey) {
            const proto::ProtoObject* bcVal = self->getAttribute(ctx, bcKey, false);
            if (bcVal && bcVal != PROTO_NONE && bcVal->isInteger(ctx))
                return ctx->fromUTF8String("[object Function]");
        }
        if (self->isMethod(ctx))
            return ctx->fromUTF8String("[object Function]");
        const proto::ProtoString* nfKey = JSSymbols::nativeFn(ctx);
        if (nfKey) {
            const proto::ProtoObject* nfVal = self->getAttribute(ctx, nfKey, false);
            if (nfVal && nfVal != PROTO_NONE && nfVal->isMethod(ctx))
                return ctx->fromUTF8String("[object Function]");
        }
        // Bound function: has __bound_fn__ pointing to the original callable.
        const proto::ProtoString* bfKey = JSSymbols::boundFn(ctx);
        if (bfKey) {
            const proto::ProtoObject* bfVal = self->getAttribute(ctx, bfKey, false);
            if (bfVal && bfVal != PROTO_NONE)
                return ctx->fromUTF8String("[object Function]");
        }
    }

    // Array: has __is_array__ as an own attribute (moved from prototype in Phase 7).
    {
        const proto::ProtoString* iaKey = JSSymbols::isArray(ctx);
        if (iaKey) {
            const proto::ProtoObject* iaVal = self->getAttribute(ctx, iaKey, false);
            if (iaVal == PROTO_TRUE)
                return ctx->fromUTF8String("[object Array]");
        }
    }

    // Symbol.toStringTag lookup: check both the internal sidecar key "__toStringTag__"
    // (used by built-in class prototypes) and the WKS string key "Symbol.toStringTag"
    // (used by user code writing `obj[Symbol.toStringTag] = 'tag'`, because
    // Symbol.toStringTag evaluates to the string "Symbol.toStringTag" in this runtime).
    {
        // Helper lambda: extract a string tag value from a protoCore attribute lookup.
        auto tryTagKey = [&](const proto::ProtoString* key) -> std::string {
            if (!key) return {};
            const proto::ProtoObject* val = self->getAttribute(ctx, key, true);
            if (!val || val == PROTO_NONE || !val->isString(ctx)) return {};
            std::string tag;
            val->asString(ctx)->toUTF8String(ctx, tag);
            return tag;
        };

        // 1. Try internal sidecar key used by built-in prototypes.
        std::string tag = tryTagKey(JSSymbols::toStringTag(ctx));

        // 2. If not found, try the WKS string key "Symbol.toStringTag".
        if (tag.empty()) {
            const proto::ProtoObject* wksKeyObj = ctx->fromUTF8String("Symbol.toStringTag");
            const proto::ProtoString* wksKey = wksKeyObj ? wksKeyObj->asString(ctx) : nullptr;
            tag = tryTagKey(wksKey);
        }

        if (!tag.empty()) {
            std::string tagResult = "[object " + tag + "]";
            return ctx->fromUTF8String(tagResult.c_str());
        }
    }

    return ctx->fromUTF8String("[object Object]");
}

// ---------------------------------------------------------------------------
// Instance method: valueOf() → self
// ---------------------------------------------------------------------------

static const proto::ProtoObject* objectValueOf(
    proto::ProtoContext* /*ctx*/,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*,
    const proto::ProtoSparseList*)
{
    return self;
}

static const proto::ProtoObject* objectIsPrototypeOf(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!args || args->getSize(ctx) < 1) return PROTO_FALSE;
    const proto::ProtoObject* arg = args->getAt(ctx, 0);
    // If arg is not an object, return false
    if (!arg || arg == PROTO_NONE || arg->isNone(ctx) || arg->isInteger(ctx) || arg->isDouble(ctx) || arg->asString(ctx) || arg == PROTO_TRUE || arg == PROTO_FALSE)
        return PROTO_FALSE;

    // Walk the prototype chain of arg
    const proto::ProtoObject* curr = arg->getFirstParent(ctx);
    while (curr && curr != PROTO_NONE) {
        if (curr == self) return PROTO_TRUE;
        curr = curr->getFirstParent(ctx);
    }
    return PROTO_FALSE;
}

} // anonymous namespace

const proto::ProtoObject* installObjectInstanceMethods(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* base)
{
    if (!ctx || !base) return base;
    // Register a built-in method and mark it as {writable:true, configurable:true,
    // enumerable:false} per ECMAScript — bits 0x3 (0x1=writable, 0x2=configurable).
    auto reg = [&](const char* name, proto::ProtoMethod fn) {
        const proto::ProtoString* key =
            ctx->fromUTF8String(name) ? ctx->fromUTF8String(name)->asString(ctx) : nullptr;
        if (key) {
            base = base->setAttribute(ctx, key, ctx->fromMethod(nullptr, fn));
            std::string pdKeyStr = std::string("__pd_") + name + "__";
            const proto::ProtoObject* pko = ctx->fromUTF8String(pdKeyStr.c_str());
            const proto::ProtoString* pdk = pko ? pko->asString(ctx) : nullptr;
            if (pdk)
                base = base->setAttribute(ctx, pdk,
                    ctx->fromInteger(0x3LL)); // writable+configurable, not enumerable
        }
    };
    reg("hasOwnProperty",       objectHasOwnProperty);
    reg("isPrototypeOf",        objectIsPrototypeOf);
    reg("propertyIsEnumerable", objectPropertyIsEnumerable);
    reg("toLocaleString",       objectToString); // Alias for now
    reg("toString",             objectToString);
    reg("valueOf",              objectValueOf);
    return base;
}

void ensureObjectConstructor(proto::ProtoContext* ctx,
                             const proto::ProtoObject** globalRoot) {
    if (!ctx || !globalRoot || !*globalRoot) return;

    const proto::ProtoString* keyObject = JSSymbols::Object(ctx);
    if (!keyObject) return;

    const proto::ProtoObject* existing = (*globalRoot)->getAttribute(ctx, keyObject, false);
    if (existing && existing != PROTO_NONE) return;

    // Use objectPrototype directly as Object.prototype. Object literals (OP_object
    // in ProtoInterpreter.cpp) are created as children of objectPrototype, so
    // isInstanceOf correctly finds Object.prototype in their prototype chain.
    // Previously a new child was created here, which broke '{} instanceof Object'.
    const proto::ProtoObject* objProto = ctx->space ? ctx->space->objectPrototype : nullptr;
    const proto::ProtoObject* proto = objProto ? objProto : ctx->newObject(false);
    if (!proto) proto = ctx->newObject(false);

    // Methods are already inherited from space->objectPrototype via getAttribute(key, true).
    // No need to re-register them here — just keep the constructor object clean.

    // Build Object constructor object.
    const proto::ProtoObject* ctor = ctx->newObject(false);
    if (!ctor) return;

    auto reg = [&](const char* name, proto::ProtoMethod fn, long long length = 1) {
        const proto::ProtoString* key = ctx->fromUTF8String(name)->asString(ctx);
        if (key) {
            const proto::ProtoObject* wrapped = wrapNativeFunction(ctx, fn, name, length, globalRoot);
            if (wrapped && wrapped != PROTO_NONE)
                ctor = ctor->setAttribute(ctx, key, wrapped);
        }
    };

    reg("keys",                  objectKeys,                  1);
    reg("values",                objectValues,                1);
    reg("entries",               objectEntries,               1);
    reg("assign",                objectAssign,                2);
    reg("create",                objectCreate,                1);
    reg("freeze",                objectFreeze,                1);
    reg("isFrozen",              objectIsFrozen,              1);
    reg("seal",                  objectSeal,                  1);
    reg("isSealed",              objectIsSealed,              1);
    reg("preventExtensions",     objectPreventExtensions,     1);
    reg("isExtensible",          objectIsExtensible,          1);
    reg("getOwnPropertyNames",   objectGetOwnPropertyNames,   1);
    reg("getPrototypeOf",        objectGetPrototypeOf,        1);
    reg("setPrototypeOf",        objectSetPrototypeOf,        2);
    reg("fromEntries",           objectFromEntries,           1);
    reg("hasOwn",                objectHasOwn,                2);
    reg("defineProperty",           objectDefineProperty,        3);
    reg("defineProperties",         objectDefineProperties,      2);
    reg("getOwnPropertyDescriptor", objectGetOwnPropertyDescriptor, 2);

    const proto::ProtoString* protoKey = JSSymbols::prototype(ctx);
    if (protoKey) ctor = ctor->setAttribute(ctx, protoKey, proto);
    const proto::ProtoString* nameKey = JSSymbols::name(ctx);
    if (nameKey) ctor = ctor->setAttribute(ctx, nameKey, ctx->fromUTF8String("Object"));

    // Explicitly mark as a constructor for OP_call_constructor.
    const proto::ProtoString* isCtorKey = ctx->fromUTF8String("__is_constructor__")->asString(ctx);
    if (isCtorKey) ctor = ctor->setAttribute(ctx, isCtorKey, PROTO_TRUE);

    // Mark as callable so OP_typeof / OP_typeof_is_function return "function".
    // The __construct__ method also handles `new Object(value)` → coerce to object.
    static const proto::ProtoMethod objectCtorFn = [](
        proto::ProtoContext* ctx, const proto::ProtoObject* self,
        const proto::ParentLink*, const proto::ProtoList* args,
        const proto::ProtoSparseList*) -> const proto::ProtoObject* {
        // new Object() → return self (already a plain object child of Object.prototype)
        // new Object(value) → if value is an object, return it; otherwise return self
        if (!args || args->getSize(ctx) == 0) return self;
        const proto::ProtoObject* val = args->getAt(ctx, 0);
        if (!val || val == PROTO_NONE) return self;
        if (val->isBoolean(ctx) || val->isInteger(ctx) || val->isDouble(ctx) ||
            val->isString(ctx)) return self;
        return val;
    };
    const proto::ProtoObject* ctorMethodObj = ctx->fromMethod(nullptr, objectCtorFn);
    const proto::ProtoString* constructKey =
        ctx->fromUTF8String("__construct__") ? ctx->fromUTF8String("__construct__")->asString(ctx) : nullptr;
    if (constructKey && ctorMethodObj)
        ctor = ctor->setAttribute(ctx, constructKey, ctorMethodObj);

    *globalRoot = (*globalRoot)->setAttribute(ctx, keyObject, ctor);
}

const proto::ProtoObject* getJSProtoOverride(const proto::ProtoObject* obj)
{
    if (!obj) return nullptr;
    auto it = t_jsProtoMap.find(obj);
    return (it != t_jsProtoMap.end()) ? it->second : nullptr;
}

} // namespace protojs
