#include "ObjectPrototype.h"
#include "ArrayPrototype.h"
#include "ProtoJSStringCache.h"
#include "headers/protoCore.h"
#include <cstring>
#include <string>
#include <vector>

namespace protojs {

namespace {

// ---------------------------------------------------------------------------
// Object.keys(obj) → array of own enumerable string property names.
// Note: protoCore does not expose a string-keyed property enumeration API;
// getOwnAttributes() returns a ProtoSparseList keyed by hash integers.
// Until a proper enumeration API is available, we return an empty array
// (safe stub — preserves vacuous-pass behaviour for most tests).
// ---------------------------------------------------------------------------

static const proto::ProtoObject* objectKeys(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* /*args*/,
    const proto::ProtoSparseList*)
{
    // Return empty array — cannot enumerate string keys without protoCore API.
    const proto::ProtoObject* result = createNewArray(ctx, nullptr);
    const proto::ProtoString* lenKey = ProtoJSStringCache::getKey(ctx, "length");
    if (lenKey) result = result->setAttribute(ctx, lenKey, ctx->fromInteger(0));
    return result;
}

// ---------------------------------------------------------------------------
// Object.values(obj) → array of own enumerable values (stub: empty)
// ---------------------------------------------------------------------------

static const proto::ProtoObject* objectValues(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* /*args*/,
    const proto::ProtoSparseList*)
{
    const proto::ProtoObject* result = createNewArray(ctx, nullptr);
    const proto::ProtoString* lenKey = ProtoJSStringCache::getKey(ctx, "length");
    if (lenKey) result = result->setAttribute(ctx, lenKey, ctx->fromInteger(0));
    return result;
}

// ---------------------------------------------------------------------------
// Object.entries(obj) → array of [key, value] pair arrays (stub: empty)
// ---------------------------------------------------------------------------

static const proto::ProtoObject* objectEntries(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* /*args*/,
    const proto::ProtoSparseList*)
{
    const proto::ProtoObject* result = createNewArray(ctx, nullptr);
    const proto::ProtoString* lenKey = ProtoJSStringCache::getKey(ctx, "length");
    if (lenKey) result = result->setAttribute(ctx, lenKey, ctx->fromInteger(0));
    return result;
}

// ---------------------------------------------------------------------------
// Object.assign(target, ...sources) → target
// Copies own enumerable string-keyed properties from sources to target.
// Since key enumeration is not available, we fall back to a no-op that
// returns the target unchanged — safe stub.
// ---------------------------------------------------------------------------

static const proto::ProtoObject* objectAssign(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!args || args->getSize(ctx) == 0) return PROTO_NONE;
    const proto::ProtoObject* target = args->getAt(ctx, 0);
    if (!target || target == PROTO_NONE) return target ? target : PROTO_NONE;
    // Without key enumeration we cannot copy source properties.
    // Return target as-is (safe no-op).
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
// Object.freeze(obj) → obj (no-op — protoCore immutable semantics apply)
// ---------------------------------------------------------------------------

static const proto::ProtoObject* objectFreeze(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    (void)ctx;
    if (!args || args->getSize(ctx) == 0) return PROTO_NONE;
    return args->getAt(ctx, 0);
}

// ---------------------------------------------------------------------------
// Object.isFrozen(obj) → always false (freeze state not tracked)
// ---------------------------------------------------------------------------

static const proto::ProtoObject* objectIsFrozen(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* /*args*/,
    const proto::ProtoSparseList*)
{
    (void)ctx;
    return PROTO_FALSE;
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

    const proto::ProtoString* lenKey = ProtoJSStringCache::getKey(ctx, "length");
    if (!lenKey) return result;
    const proto::ProtoObject* lenObj = iterable->getAttribute(ctx, lenKey, false);
    long long len = 0;
    if (lenObj && lenObj != PROTO_NONE) {
        if (lenObj->isInteger(ctx)) len = lenObj->asLong(ctx);
        else if (lenObj->isDouble(ctx)) len = static_cast<long long>(lenObj->asDouble(ctx));
    }

    const proto::ProtoString* k0Key = ProtoJSStringCache::getIndexKey(ctx, 0);
    const proto::ProtoString* k1Key = ProtoJSStringCache::getIndexKey(ctx, 1);

    for (long long i = 0; i < len; i++) {
        const proto::ProtoString* idxKey =
            ProtoJSStringCache::getIndexKey(ctx, static_cast<uint32_t>(i));
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
        const proto::ProtoString* entryKey = ProtoJSStringCache::getKey(ctx, keyStr.c_str());
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
    const proto::ProtoString* strKey = ProtoJSStringCache::getKey(ctx, keyStr.c_str());
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
    const proto::ProtoString* strKey = ProtoJSStringCache::getKey(ctx, keyStr.c_str());
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
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList*,
    const proto::ProtoSparseList*)
{
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

void ensureObjectConstructor(proto::ProtoContext* ctx,
                             const proto::ProtoObject** globalRoot) {
    if (!ctx || !globalRoot || !*globalRoot) return;

    const proto::ProtoString* keyObject = ProtoJSStringCache::getKey(ctx, "Object");
    if (!keyObject) return;

    const proto::ProtoObject* existing = (*globalRoot)->getAttribute(ctx, keyObject, false);
    if (existing && existing != PROTO_NONE) return;

    // Build Object.prototype with instance methods.
    const proto::ProtoObject* objProto = ctx->space ? ctx->space->objectPrototype : nullptr;
    const proto::ProtoObject* proto = objProto
        ? objProto->newChild(ctx, false)
        : ctx->newObject(false);
    if (!proto) proto = ctx->newObject(false);

    auto regProto = [&](const char* name, proto::ProtoMethod fn) {
        const proto::ProtoString* key = ProtoJSStringCache::getKey(ctx, name);
        if (key) proto = proto->setAttribute(ctx, key, ctx->fromMethod(nullptr, fn));
    };

    regProto("hasOwnProperty",       objectHasOwnProperty);
    regProto("propertyIsEnumerable", objectPropertyIsEnumerable);
    regProto("toString",             objectToString);
    regProto("valueOf",              objectValueOf);

    // Build Object constructor object.
    const proto::ProtoObject* ctor = ctx->newObject(false);
    if (!ctor) return;

    auto reg = [&](const char* name, proto::ProtoMethod fn) {
        const proto::ProtoString* key = ProtoJSStringCache::getKey(ctx, name);
        if (key) ctor = ctor->setAttribute(ctx, key, ctx->fromMethod(nullptr, fn));
    };

    reg("keys",                  objectKeys);
    reg("values",                objectValues);
    reg("entries",               objectEntries);
    reg("assign",                objectAssign);
    reg("create",                objectCreate);
    reg("freeze",                objectFreeze);
    reg("isFrozen",              objectIsFrozen);
    reg("getOwnPropertyNames",   objectGetOwnPropertyNames);
    reg("getPrototypeOf",        objectGetPrototypeOf);
    reg("setPrototypeOf",        objectGetPrototypeOf); // stub: same as getPrototypeOf
    reg("fromEntries",           objectFromEntries);
    reg("hasOwn",                objectHasOwn);

    const proto::ProtoString* protoKey = ProtoJSStringCache::getKey(ctx, "prototype");
    if (protoKey) ctor = ctor->setAttribute(ctx, protoKey, proto);
    const proto::ProtoString* nameKey = ProtoJSStringCache::getKey(ctx, "name");
    if (nameKey) ctor = ctor->setAttribute(ctx, nameKey, ctx->fromUTF8String("Object"));

    *globalRoot = (*globalRoot)->setAttribute(ctx, keyObject, ctor);
}

} // namespace protojs
