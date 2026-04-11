#include "ObjectPrototype.h"
#include "ArrayPrototype.h"
#include "JSSymbols.h"
#include "headers/protoCore.h"
#include "runtime/ProtoInterpreter.h"
#include <cstring>
#include <string>
#include <unordered_set>
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
// collectOwnKeys — fills `keys` and `vals` with the JS-visible own
// enumerable string properties of `obj`.  Internal (__*__) keys and the
// "length" property of arrays are excluded.
// ---------------------------------------------------------------------------
static void collectOwnKeys(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* obj,
    std::vector<std::string>&       keys,
    std::vector<const proto::ProtoObject*>* vals)
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

// ---------------------------------------------------------------------------
// Object.create(proto) → new mutable empty object
// ---------------------------------------------------------------------------

static const proto::ProtoObject* objectCreate(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* /*args*/,
    const proto::ProtoSparseList*)
{
    return ctx->newObject(true);
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

static thread_local std::unordered_set<const proto::ProtoObject*> t_frozenObjects;
static thread_local std::unordered_set<const proto::ProtoObject*> t_sealedObjects;
static thread_local std::unordered_set<const proto::ProtoObject*> t_nonExtensibleObjects;

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
    // Primitives: spec says freeze is a no-op and returns the value unchanged.
    if (isPrimitive(ctx, obj)) return obj;
    t_frozenObjects.insert(obj);
    t_nonExtensibleObjects.insert(obj);
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
    return t_frozenObjects.count(obj) ? PROTO_TRUE : PROTO_FALSE;
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
    t_sealedObjects.insert(obj);
    t_nonExtensibleObjects.insert(obj);
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
    // Frozen objects are also sealed.
    if (t_frozenObjects.count(obj) || t_sealedObjects.count(obj)) return PROTO_TRUE;
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
    t_nonExtensibleObjects.insert(obj);
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
    return t_nonExtensibleObjects.count(obj) ? PROTO_FALSE : PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// Object.getOwnPropertyNames(obj) — same stub as keys
// ---------------------------------------------------------------------------

static const proto::ProtoObject* objectGetOwnPropertyNames(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* pl,
    const proto::ProtoList* args,
    const proto::ProtoSparseList* kw)
{
    return objectKeys(ctx, self, pl, args, kw);
}

// ---------------------------------------------------------------------------
// Object.getPrototypeOf(obj) → PROTO_NONE (prototype chain not tracked)
// ---------------------------------------------------------------------------

static const proto::ProtoObject* objectGetPrototypeOf(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* /*args*/,
    const proto::ProtoSparseList*)
{
    (void)ctx;
    return PROTO_NONE;
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
    if (!target || target == PROTO_NONE) return PROTO_NONE;

    // Get property name string.
    const proto::ProtoObject* propNameObj = args->getAt(ctx, 1);
    if (!propNameObj || propNameObj == PROTO_NONE) return target;
    std::string propName;
    const proto::ProtoString* ps = propNameObj->asString(ctx);
    if (ps) ps->toUTF8String(ctx, propName);
    else if (propNameObj->isInteger(ctx))
        propName = std::to_string(propNameObj->asLong(ctx));
    if (propName.empty()) return target;

    // Get descriptor object.
    const proto::ProtoObject* desc = args->getAt(ctx, 2);
    if (!desc || desc == PROTO_NONE) return target;

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

    // Store the value if present in the descriptor.
    const proto::ProtoObject* valueKey = ctx->fromUTF8String("value");
    const proto::ProtoString* vkp = valueKey ? valueKey->asString(ctx) : nullptr;
    if (vkp) {
        const proto::ProtoObject* val = desc->getAttribute(ctx, vkp, false);
        if (val) { // val may be PROTO_NONE (explicit undefined)
            const proto::ProtoObject* ko = ctx->fromUTF8String(propName.c_str());
            const proto::ProtoString* pk = ko ? ko->asString(ctx) : nullptr;
            if (pk) target = target->setAttribute(ctx, pk, val);
        }
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
// Object.getOwnPropertyDescriptor(obj, propName) → descriptor object
// Returns {value, writable, configurable, enumerable} or undefined if absent.
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
    if (!target || target == PROTO_NONE || !propNameObj || propNameObj == PROTO_NONE)
        return PROTO_NONE;

    std::string propName;
    const proto::ProtoString* ps = propNameObj->asString(ctx);
    if (ps) ps->toUTF8String(ctx, propName);
    else if (propNameObj->isInteger(ctx)) propName = std::to_string(propNameObj->asLong(ctx));
    if (propName.empty()) return PROTO_NONE;

    // Get the value.
    const proto::ProtoObject* ko = ctx->fromUTF8String(propName.c_str());
    const proto::ProtoString* pk = ko ? ko->asString(ctx) : nullptr;
    const proto::ProtoObject* val = pk ? target->getAttribute(ctx, pk, false) : nullptr;
    if (!val) return PROTO_NONE; // property doesn't exist

    // Get the descriptor flags.
    std::string pdKeyStr = "__pd_" + propName + "__";
    const proto::ProtoObject* pdko = ctx->fromUTF8String(pdKeyStr.c_str());
    const proto::ProtoString* pdk  = pdko ? pdko->asString(ctx) : nullptr;
    const proto::ProtoObject* bitsObj = pdk ? target->getAttribute(ctx, pdk, false) : nullptr;
    uint8_t bits = 0x7; // default: all true (writable + configurable + enumerable)
    if (bitsObj && bitsObj != PROTO_NONE && bitsObj->isInteger(ctx))
        bits = static_cast<uint8_t>(bitsObj->asLong(ctx));

    const proto::ProtoObject* result = ctx->newObject(true);
    auto setAttr = [&](const char* name, const proto::ProtoObject* v) {
        const proto::ProtoObject* k = ctx->fromUTF8String(name);
        const proto::ProtoString* ks = k ? k->asString(ctx) : nullptr;
        if (ks) result = result->setAttribute(ctx, ks, v);
    };
    setAttr("value",        val);
    setAttr("writable",     (bits & 0x1) ? PROTO_TRUE : PROTO_FALSE);
    setAttr("configurable", (bits & 0x2) ? PROTO_TRUE : PROTO_FALSE);
    setAttr("enumerable",   (bits & 0x4) ? PROTO_TRUE : PROTO_FALSE);
    return result;
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
    return (own == PROTO_TRUE) ? PROTO_TRUE : PROTO_FALSE;
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
    return (own == PROTO_TRUE) ? PROTO_TRUE : PROTO_FALSE;
}

// ---------------------------------------------------------------------------
// Instance method: propertyIsEnumerable(key) → true if own property exists
// ---------------------------------------------------------------------------

static const proto::ProtoObject* objectPropertyIsEnumerable(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* pl,
    const proto::ProtoList* args,
    const proto::ProtoSparseList* kw)
{
    return objectHasOwnProperty(ctx, self, pl, args, kw);
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
    if (self->isBoolean(ctx))
        return ctx->fromUTF8String("[object Boolean]");
    if (self->isInteger(ctx) || self->isDouble(ctx) || self->isFloat(ctx))
        return ctx->fromUTF8String("[object Number]");
    if (self->isString(ctx))
        return ctx->fromUTF8String("[object String]");
    // Function: has __bytecode_id__ (JS closure) or is a native ProtoMethod.
    {
        const proto::ProtoString* bcKey = JSSymbols::bytecodeId(ctx);
        if (bcKey) {
            const proto::ProtoObject* bcVal = self->getAttribute(ctx, bcKey, false);
            if (bcVal && bcVal != PROTO_NONE && bcVal->isInteger(ctx))
                return ctx->fromUTF8String("[object Function]");
        }
        if (self->isMethod(ctx))
            return ctx->fromUTF8String("[object Function]");
    }
    // Array: has __is_array__ in prototype chain (set on Array.prototype).
    {
        const proto::ProtoString* iaKey = JSSymbols::isArray(ctx);
        if (iaKey) {
            const proto::ProtoObject* iaVal = self->getAttribute(ctx, iaKey, true);
            if (iaVal == PROTO_TRUE)
                return ctx->fromUTF8String("[object Array]");
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

} // anonymous namespace

const proto::ProtoObject* installObjectInstanceMethods(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* base)
{
    if (!ctx || !base) return base;
    auto reg = [&](const char* name, proto::ProtoMethod fn) {
        const proto::ProtoString* key = ctx->fromUTF8String(name) ? ctx->fromUTF8String(name)->asString(ctx) : nullptr;
        if (key) base = base->setAttribute(ctx, key, ctx->fromMethod(nullptr, fn));
    };
    reg("hasOwnProperty",       objectHasOwnProperty);
    reg("propertyIsEnumerable", objectPropertyIsEnumerable);
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

    auto reg = [&](const char* name, proto::ProtoMethod fn) {
        const proto::ProtoString* key = ctx->fromUTF8String(name)->asString(ctx);
        if (key) ctor = ctor->setAttribute(ctx, key, ctx->fromMethod(nullptr, fn));
    };

    reg("keys",                  objectKeys);
    reg("values",                objectValues);
    reg("entries",               objectEntries);
    reg("assign",                objectAssign);
    reg("create",                objectCreate);
    reg("freeze",                objectFreeze);
    reg("isFrozen",              objectIsFrozen);
    reg("seal",                  objectSeal);
    reg("isSealed",              objectIsSealed);
    reg("preventExtensions",     objectPreventExtensions);
    reg("isExtensible",          objectIsExtensible);
    reg("getOwnPropertyNames",   objectGetOwnPropertyNames);
    reg("getPrototypeOf",        objectGetPrototypeOf);
    reg("setPrototypeOf",        objectGetPrototypeOf); // stub: same as getPrototypeOf
    reg("fromEntries",           objectFromEntries);
    reg("hasOwn",                objectHasOwn);
    reg("defineProperty",           objectDefineProperty);
    reg("getOwnPropertyDescriptor", objectGetOwnPropertyDescriptor);

    const proto::ProtoString* protoKey = JSSymbols::prototype(ctx);
    if (protoKey) ctor = ctor->setAttribute(ctx, protoKey, proto);
    const proto::ProtoString* nameKey = JSSymbols::name(ctx);
    if (nameKey) ctor = ctor->setAttribute(ctx, nameKey, ctx->fromUTF8String("Object"));

    *globalRoot = (*globalRoot)->setAttribute(ctx, keyObject, ctor);
}

} // namespace protojs
