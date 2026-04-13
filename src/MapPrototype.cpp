#include "MapPrototype.h"
#include "ArrayPrototype.h"
#include "JSSymbols.h"
#include "PrototypeUtils.h"
#include "runtime/ProtoInterpreter.h"
#include "headers/protoCore.h"
#include <cmath>
#include <cstring>
#include <string>

namespace protojs {

// ---------------------------------------------------------------------------
// Module-level storage for the JS Map prototype, set by BuildMapPrototype
// and retrieved by ensureMapConstructor.
// ---------------------------------------------------------------------------
static const proto::ProtoObject* s_mapPrototype = nullptr;

namespace {

// ---------------------------------------------------------------------------
// SameValueZero hash — used as bucket index in __map_hash__ ProtoSparseList.
// ---------------------------------------------------------------------------
static unsigned long szvHash(proto::ProtoContext* ctx, const proto::ProtoObject* key) {
    if (!key || key == PROTO_NONE)          return 0UL;
    if (key == PROTO_TRUE)                  return 1UL;
    if (key == PROTO_FALSE)                 return 2UL;
    if (key->isInteger(ctx))
        return static_cast<unsigned long>(key->asLong(ctx) & 0x7FFFFFFF);
    if (key->isDouble(ctx) || key->isFloat(ctx)) {
        double d = key->asDouble(ctx);
        if (std::isnan(d))                  return 3UL;
        uint64_t bits; memcpy(&bits, &d, 8);
        return static_cast<unsigned long>(bits ^ (bits >> 32));
    }
    if (key->isString(ctx)) {
        const proto::ProtoString* ps = key->asString(ctx);
        return ps ? static_cast<unsigned long>(ps->getHash(ctx)) : 4UL;
    }
    // Object / other: pointer identity.
    return static_cast<unsigned long>(reinterpret_cast<uintptr_t>(key) >> 3);
}

// ---------------------------------------------------------------------------
// SameValueZero equality.
// ---------------------------------------------------------------------------
static bool sameValueZero(proto::ProtoContext* ctx,
                           const proto::ProtoObject* a,
                           const proto::ProtoObject* b)
{
    if (a == b) return true;
    if (!a || !b) return false;
    if ((a->isDouble(ctx) || a->isFloat(ctx)) &&
        (b->isDouble(ctx) || b->isFloat(ctx))) {
        double da = a->asDouble(ctx), db = b->asDouble(ctx);
        if (std::isnan(da) && std::isnan(db)) return true;
        return da == db;
    }
    if (a->isInteger(ctx) && b->isInteger(ctx))
        return a->asLong(ctx) == b->asLong(ctx);
    if (a->isString(ctx) && b->isString(ctx)) {
        std::string sa, sb;
        a->asString(ctx)->toUTF8String(ctx, sa);
        b->asString(ctx)->toUTF8String(ctx, sb);
        return sa == sb;
    }
    return false;
}

// ---------------------------------------------------------------------------
// Normalize -0 to +0 per SameValueZero spec (Map keys treat -0 as +0).
// ---------------------------------------------------------------------------
static const proto::ProtoObject* normalizeMapKey(proto::ProtoContext* ctx,
                                                  const proto::ProtoObject* key)
{
    if (key && (key->isDouble(ctx) || key->isFloat(ctx))) {
        double d = key->asDouble(ctx);
        if (d == 0.0 && std::signbit(d))
            return ctx->fromInteger(0LL);
    }
    return key;
}

// Returns true if self is a valid Map receiver (has __map_keys__ slot).
// Signals TypeError and returns false otherwise.
static bool requireMapThis(proto::ProtoContext* ctx, const proto::ProtoObject* self)
{
    if (!self || self == PROTO_NONE || self == PROTO_TRUE || self == PROTO_FALSE ||
        self->isInteger(ctx) || self->isDouble(ctx) || self->isFloat(ctx) ||
        self->isString(ctx)) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Map operation called on non-Map"));
        return false;
    }
    const proto::ProtoObject* ko = ctx->fromUTF8String("__map_keys__");
    const proto::ProtoString* ks = ko ? ko->asString(ctx) : nullptr;
    const proto::ProtoObject* v  = ks ? self->getAttribute(ctx, ks, false) : nullptr;
    if (!v || v == PROTO_NONE) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Map operation called on non-Map"));
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Retrieve one of the map's hidden ProtoSparseList backing attributes.
// ---------------------------------------------------------------------------
static const proto::ProtoSparseList* getMapList(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* mapObj,
    const char* attrName)
{
    const proto::ProtoObject* ko = ctx->fromUTF8String(attrName);
    const proto::ProtoString* ks = ko ? ko->asString(ctx) : nullptr;
    if (!ks || !mapObj || mapObj == PROTO_NONE) return nullptr;
    const proto::ProtoObject* v = mapObj->getAttribute(ctx, ks, false);
    return (v && v != PROTO_NONE) ? v->asSparseList(ctx) : nullptr;
}

// Update one of the map's hidden ProtoSparseList backing attributes (mutates mapObj in place).
static void setMapListInPlace(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* mapObj,
    const char* attrName,
    const proto::ProtoSparseList* list)
{
    if (!list || !mapObj || mapObj == PROTO_NONE) return;
    const proto::ProtoObject* ko = ctx->fromUTF8String(attrName);
    const proto::ProtoString* ks = ko ? ko->asString(ctx) : nullptr;
    if (ks) mapObj->setAttribute(ctx, ks, list->asObject(ctx));
}

static long getMapSize(proto::ProtoContext* ctx, const proto::ProtoObject* mapObj) {
    const proto::ProtoObject* ko = ctx->fromUTF8String("__map_size__");
    const proto::ProtoString* ks = ko ? ko->asString(ctx) : nullptr;
    if (!ks || !mapObj || mapObj == PROTO_NONE) return 0L;
    const proto::ProtoObject* v = mapObj->getAttribute(ctx, ks, false);
    return (v && v != PROTO_NONE && v->isInteger(ctx)) ? v->asLong(ctx) : 0L;
}

static void setMapSizeInPlace(proto::ProtoContext* ctx,
                               const proto::ProtoObject* mapObj,
                               long sz)
{
    const proto::ProtoObject* ko = ctx->fromUTF8String("__map_size__");
    const proto::ProtoString* ks = ko ? ko->asString(ctx) : nullptr;
    if (ks && mapObj && mapObj != PROTO_NONE)
        mapObj->setAttribute(ctx, ks, ctx->fromInteger(sz));
}

// ---------------------------------------------------------------------------
// mapFind: hash lookup + linear scan for SameValueZero equality.
// Returns true if found, sets foundIdx to the SparseList index.
// ---------------------------------------------------------------------------
static bool mapFind(proto::ProtoContext* ctx,
                    const proto::ProtoObject* mapObj,
                    const proto::ProtoObject* key,
                    unsigned long& foundIdx)
{
    key = normalizeMapKey(ctx, key);
    const proto::ProtoSparseList* keysList = getMapList(ctx, mapObj, "__map_keys__");
    const proto::ProtoSparseList* hashList = getMapList(ctx, mapObj, "__map_hash__");
    if (!keysList || !hashList) return false;

    // Try hash slot first.
    unsigned long h = szvHash(ctx, key);
    if (hashList->has(ctx, h)) {
        const proto::ProtoObject* slotObj = hashList->getAt(ctx, h);
        if (slotObj && slotObj != PROTO_NONE && slotObj->isInteger(ctx)) {
            unsigned long idx = static_cast<unsigned long>(slotObj->asLong(ctx));
            if (keysList->has(ctx, idx)) {
                const proto::ProtoObject* k = keysList->getAt(ctx, idx);
                if (sameValueZero(ctx, k, key)) { foundIdx = idx; return true; }
            }
        }
    }
    // Fall back to linear scan for hash collisions.
    const proto::ProtoSparseListIterator* it = keysList->getIterator(ctx);
    while (it && it->hasNext(ctx)) {
        unsigned long idx = it->nextKey(ctx);
        const proto::ProtoObject* k = it->nextValue(ctx);
        it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
        if (sameValueZero(ctx, k, key)) { foundIdx = idx; return true; }
    }
    return false;
}

// ---------------------------------------------------------------------------
// map.set(key, val) → map
// ---------------------------------------------------------------------------
static const proto::ProtoObject* mapSet(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    if (!requireMapThis(ctx, self)) return PROTO_NONE;
    int argc = args ? static_cast<int>(args->getSize(ctx)) : 0;
    const proto::ProtoObject* key = (argc > 0) ? args->getAt(ctx, 0) : PROTO_NONE;
    const proto::ProtoObject* val = (argc > 1) ? args->getAt(ctx, 1) : PROTO_NONE;
    if (!key) key = PROTO_NONE;
    if (!val) val = PROTO_NONE;
    key = normalizeMapKey(ctx, key);

    const proto::ProtoSparseList* keysList = getMapList(ctx, self, "__map_keys__");
    const proto::ProtoSparseList* valsList = getMapList(ctx, self, "__map_vals__");
    const proto::ProtoSparseList* hashList = getMapList(ctx, self, "__map_hash__");
    if (!keysList || !valsList || !hashList) return self;

    unsigned long existingIdx = 0;
    if (mapFind(ctx, self, key, existingIdx)) {
        // Update value in place.
        setMapListInPlace(ctx, self, "__map_vals__", valsList->setAt(ctx, existingIdx, val));
        return self;
    }
    // New entry.
    long sz = getMapSize(ctx, self);
    unsigned long newIdx = static_cast<unsigned long>(sz);
    keysList = keysList->setAt(ctx, newIdx, key);
    valsList = valsList->setAt(ctx, newIdx, val);
    unsigned long h = szvHash(ctx, key);
    if (!hashList->has(ctx, h))
        hashList = hashList->setAt(ctx, h, ctx->fromInteger(static_cast<long long>(newIdx)));
    setMapListInPlace(ctx, self, "__map_keys__", keysList);
    setMapListInPlace(ctx, self, "__map_vals__", valsList);
    setMapListInPlace(ctx, self, "__map_hash__", hashList);
    setMapSizeInPlace(ctx, self, sz + 1);
    return self;
}

// ---------------------------------------------------------------------------
// map.get(key) → value or undefined
// ---------------------------------------------------------------------------
static const proto::ProtoObject* mapGet(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    if (!requireMapThis(ctx, self)) return PROTO_NONE;
    const proto::ProtoObject* key = (args && args->getSize(ctx) > 0) ? args->getAt(ctx, 0) : PROTO_NONE;
    if (!key) key = PROTO_NONE;
    unsigned long foundIdx = 0;
    if (!mapFind(ctx, self, key, foundIdx)) return PROTO_NONE;
    const proto::ProtoSparseList* valsList = getMapList(ctx, self, "__map_vals__");
    if (!valsList) return PROTO_NONE;
    const proto::ProtoObject* v = valsList->has(ctx, foundIdx) ? valsList->getAt(ctx, foundIdx) : PROTO_NONE;
    return v ? v : PROTO_NONE;
}

// ---------------------------------------------------------------------------
// map.has(key) → boolean
// ---------------------------------------------------------------------------
static const proto::ProtoObject* mapHas(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    if (!requireMapThis(ctx, self)) return PROTO_NONE;
    const proto::ProtoObject* key = (args && args->getSize(ctx) > 0) ? args->getAt(ctx, 0) : PROTO_NONE;
    if (!key) key = PROTO_NONE;
    unsigned long foundIdx = 0;
    return mapFind(ctx, self, key, foundIdx) ? PROTO_TRUE : PROTO_FALSE;
}

// ---------------------------------------------------------------------------
// map.delete(key) → boolean
// ---------------------------------------------------------------------------
static const proto::ProtoObject* mapDelete(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    if (!requireMapThis(ctx, self)) return PROTO_NONE;
    const proto::ProtoObject* key = (args && args->getSize(ctx) > 0) ? args->getAt(ctx, 0) : PROTO_NONE;
    if (!key) key = PROTO_NONE;

    unsigned long foundIdx = 0;
    if (!mapFind(ctx, self, key, foundIdx)) return PROTO_FALSE;

    const proto::ProtoSparseList* keysList = getMapList(ctx, self, "__map_keys__");
    const proto::ProtoSparseList* valsList = getMapList(ctx, self, "__map_vals__");
    const proto::ProtoSparseList* hashList = getMapList(ctx, self, "__map_hash__");
    if (!keysList || !valsList || !hashList) return PROTO_FALSE;

    keysList = keysList->removeAt(ctx, foundIdx);
    valsList = valsList->removeAt(ctx, foundIdx);

    // Remove hash slot if it points to this index.
    unsigned long h = szvHash(ctx, key);
    if (hashList->has(ctx, h)) {
        const proto::ProtoObject* slotObj = hashList->getAt(ctx, h);
        if (slotObj && slotObj->isInteger(ctx) &&
            static_cast<unsigned long>(slotObj->asLong(ctx)) == foundIdx)
            hashList = hashList->removeAt(ctx, h);
    }

    long sz = getMapSize(ctx, self);
    setMapListInPlace(ctx, self, "__map_keys__", keysList);
    setMapListInPlace(ctx, self, "__map_vals__", valsList);
    setMapListInPlace(ctx, self, "__map_hash__", hashList);
    setMapSizeInPlace(ctx, self, sz > 0 ? sz - 1 : 0);
    return PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// map.clear() → undefined
// ---------------------------------------------------------------------------
static const proto::ProtoObject* mapClear(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*)
{
    if (!requireMapThis(ctx, self)) return PROTO_NONE;
    const proto::ProtoSparseList* empty = ctx->newSparseList();
    setMapListInPlace(ctx, self, "__map_keys__", empty);
    setMapListInPlace(ctx, self, "__map_vals__", empty);
    setMapListInPlace(ctx, self, "__map_hash__", empty);
    setMapSizeInPlace(ctx, self, 0L);
    return PROTO_NONE;
}

// ---------------------------------------------------------------------------
// map.size getter
// ---------------------------------------------------------------------------
static const proto::ProtoObject* mapSizeGetter(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*)
{
    if (!requireMapThis(ctx, self)) return PROTO_NONE;
    return ctx->fromInteger(static_cast<long long>(getMapSize(ctx, self)));
}

// ---------------------------------------------------------------------------
// map.forEach(callback, thisArg?)
// ---------------------------------------------------------------------------
static const proto::ProtoObject* mapForEach(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    if (!requireMapThis(ctx, self)) return PROTO_NONE;
    if (!args || args->getSize(ctx) == 0) return PROTO_NONE;
    const proto::ProtoObject* callback = args->getAt(ctx, 0);
    if (!callback || callback == PROTO_NONE) return PROTO_NONE;
    const proto::ProtoObject* thisArg = (args->getSize(ctx) > 1) ? args->getAt(ctx, 1) : PROTO_NONE;
    if (!thisArg) thisArg = PROTO_NONE;

    const proto::ProtoSparseList* keysList = getMapList(ctx, self, "__map_keys__");
    const proto::ProtoSparseList* valsList = getMapList(ctx, self, "__map_vals__");
    if (!keysList) return PROTO_NONE;

    const proto::ProtoSparseListIterator* it = keysList->getIterator(ctx);
    while (it && it->hasNext(ctx)) {
        unsigned long idx = it->nextKey(ctx);
        const proto::ProtoObject* k = it->nextValue(ctx);
        it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
        const proto::ProtoObject* v = (valsList && valsList->has(ctx, idx))
            ? valsList->getAt(ctx, idx) : PROTO_NONE;
        if (!k) k = PROTO_NONE;
        if (!v) v = PROTO_NONE;
        // Call callback(value, key, map) per spec.
        const proto::ProtoList* cbArgs = ctx->newList();
        cbArgs = cbArgs->appendLast(ctx, v);
        cbArgs = cbArgs->appendLast(ctx, k);
        cbArgs = cbArgs->appendLast(ctx, self);
        callJSFunction(ctx, callback, thisArg, cbArgs);
        if (hasCallException()) return PROTO_NONE;
    }
    return PROTO_NONE;
}

// ---------------------------------------------------------------------------
// Map iterator next() — advances through __map_keys__ sparse list.
// The iterator object (self) is mutable; __iter_idx__ is updated in place.
// ---------------------------------------------------------------------------
static const proto::ProtoObject* mapIteratorNext(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*)
{
    // Build {value: undefined, done: true} result.
    auto makeDone = [&]() -> const proto::ProtoObject* {
        const proto::ProtoObject* r = ctx->newObject(true);
        const proto::ProtoString* vk = JSSymbols::value(ctx);
        const proto::ProtoString* dk = JSSymbols::done(ctx);
        if (vk) r = r->setAttribute(ctx, vk, PROTO_NONE);
        if (dk) r = r->setAttribute(ctx, dk, PROTO_TRUE);
        return r;
    };

    if (!self || self == PROTO_NONE) return makeDone();

    // Read current position and kind from iterator.
    const proto::ProtoString* idxKey  = JSSymbols::iterIdx(ctx);
    const proto::ProtoString* arrKey2 = JSSymbols::iterArr(ctx);
    const proto::ProtoString* kindKey = JSSymbols::iterKind(ctx);
    if (!idxKey || !arrKey2 || !kindKey) return makeDone();

    const proto::ProtoObject* mapObj  = self->getAttribute(ctx, arrKey2, false);
    const proto::ProtoObject* posObj  = self->getAttribute(ctx, idxKey,  false);
    const proto::ProtoObject* kindObj = self->getAttribute(ctx, kindKey, false);
    if (!mapObj || mapObj == PROTO_NONE) return makeDone();

    long long pos = (posObj && posObj != PROTO_NONE && posObj->isInteger(ctx))
                    ? posObj->asLong(ctx) : 0LL;
    std::string kind = "entries";
    if (kindObj && kindObj != PROTO_NONE && kindObj->isString(ctx)) {
        const proto::ProtoString* ks2 = kindObj->asString(ctx);
        if (ks2) ks2->toUTF8String(ctx, kind);
    }

    const proto::ProtoSparseList* keysList = getMapList(ctx, mapObj, "__map_keys__");
    const proto::ProtoSparseList* valsList = getMapList(ctx, mapObj, "__map_vals__");
    if (!keysList) return makeDone();

    // Find the entry with the smallest SparseList index >= pos.
    const proto::ProtoSparseListIterator* it = keysList->getIterator(ctx);
    while (it && it->hasNext(ctx)) {
        unsigned long slotIdx = it->nextKey(ctx);
        const proto::ProtoObject* k = it->nextValue(ctx);
        it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
        if (static_cast<long long>(slotIdx) < pos) continue;

        // Advance iterator position past this slot (mutate in place).
        self->setAttribute(ctx, idxKey, ctx->fromInteger(static_cast<long long>(slotIdx) + 1));

        const proto::ProtoObject* v = (valsList && valsList->has(ctx, slotIdx))
            ? valsList->getAt(ctx, slotIdx) : PROTO_NONE;
        if (!k) k = PROTO_NONE;
        if (!v) v = PROTO_NONE;

        const proto::ProtoObject* iterVal = PROTO_NONE;
        if (kind == "keys") {
            iterVal = k;
        } else if (kind == "values") {
            iterVal = v;
        } else {
            // entries: [key, value]
            const proto::ProtoObject* pair = createNewArray(ctx, nullptr);
            const proto::ProtoString* i0 = JSSymbols::indexKey(ctx, 0);
            const proto::ProtoString* i1 = JSSymbols::indexKey(ctx, 1);
            const proto::ProtoString* lk = JSSymbols::length(ctx);
            const proto::ProtoString* ia = JSSymbols::isArray(ctx);
            if (i0) pair = pair->setAttribute(ctx, i0, k);
            if (i1) pair = pair->setAttribute(ctx, i1, v);
            if (lk) pair = pair->setAttribute(ctx, lk, ctx->fromInteger(2LL));
            if (ia) pair = pair->setAttribute(ctx, ia, ctx->fromInteger(1LL));
            iterVal = pair;
        }

        const proto::ProtoObject* r = ctx->newObject(true);
        const proto::ProtoString* vk = JSSymbols::value(ctx);
        const proto::ProtoString* dk = JSSymbols::done(ctx);
        if (vk) r = r->setAttribute(ctx, vk, iterVal);
        if (dk) r = r->setAttribute(ctx, dk, PROTO_FALSE);
        return r;
    }
    return makeDone();
}

// Create a Map iterator object for the given kind.
static const proto::ProtoObject* makeMapIterator(
    proto::ProtoContext* ctx, const proto::ProtoObject* mapObj, const char* kind)
{
    const proto::ProtoObject* iter = ctx->newObject(true); // mutable for next() to advance idx
    const proto::ProtoString* idxKey  = JSSymbols::iterIdx(ctx);
    const proto::ProtoString* arrKey2 = JSSymbols::iterArr(ctx);
    const proto::ProtoString* kindKey = JSSymbols::iterKind(ctx);
    const proto::ProtoString* nextKey = JSSymbols::next(ctx);
    if (idxKey)  iter = iter->setAttribute(ctx, idxKey,  ctx->fromInteger(0LL));
    if (arrKey2) iter = iter->setAttribute(ctx, arrKey2, mapObj ? mapObj : PROTO_NONE);
    if (kindKey) iter = iter->setAttribute(ctx, kindKey, ctx->fromUTF8String(kind));
    if (nextKey) {
        const proto::ProtoObject* nextFn = ctx->fromMethod(nullptr, mapIteratorNext);
        if (nextFn) iter = iter->setAttribute(ctx, nextKey, nextFn);
    }
    return iter;
}

static const proto::ProtoObject* mapKeys(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{
    if (!requireMapThis(ctx, self)) return PROTO_NONE;
    return makeMapIterator(ctx, self, "keys");
}

static const proto::ProtoObject* mapValues(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{
    if (!requireMapThis(ctx, self)) return PROTO_NONE;
    return makeMapIterator(ctx, self, "values");
}

static const proto::ProtoObject* mapEntries(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{
    if (!requireMapThis(ctx, self)) return PROTO_NONE;
    return makeMapIterator(ctx, self, "entries");
}

// ---------------------------------------------------------------------------
// map.getOrInsert(key, defaultValue) → value
// Returns existing value for key; if absent, inserts defaultValue and returns it.
// ---------------------------------------------------------------------------
static const proto::ProtoObject* mapGetOrInsert(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    if (!requireMapThis(ctx, self)) return PROTO_NONE;
    int argc = args ? static_cast<int>(args->getSize(ctx)) : 0;
    const proto::ProtoObject* key = (argc > 0) ? args->getAt(ctx, 0) : PROTO_NONE;
    const proto::ProtoObject* defVal = (argc > 1) ? args->getAt(ctx, 1) : PROTO_NONE;
    if (!key) key = PROTO_NONE;
    if (!defVal) defVal = PROTO_NONE;
    key = normalizeMapKey(ctx, key);

    unsigned long foundIdx = 0;
    if (mapFind(ctx, self, key, foundIdx)) {
        const proto::ProtoSparseList* valsList = getMapList(ctx, self, "__map_vals__");
        if (!valsList) return PROTO_NONE;
        const proto::ProtoObject* v = valsList->has(ctx, foundIdx)
            ? valsList->getAt(ctx, foundIdx) : PROTO_NONE;
        return v ? v : PROTO_NONE;
    }
    // Insert new entry.
    const proto::ProtoSparseList* kl = getMapList(ctx, self, "__map_keys__");
    const proto::ProtoSparseList* vl = getMapList(ctx, self, "__map_vals__");
    const proto::ProtoSparseList* hl = getMapList(ctx, self, "__map_hash__");
    if (!kl || !vl || !hl) return PROTO_NONE;
    long sz = getMapSize(ctx, self);
    unsigned long ni = static_cast<unsigned long>(sz);
    kl = kl->setAt(ctx, ni, key);
    vl = vl->setAt(ctx, ni, defVal);
    unsigned long h = szvHash(ctx, key);
    if (!hl->has(ctx, h))
        hl = hl->setAt(ctx, h, ctx->fromInteger(static_cast<long long>(ni)));
    setMapListInPlace(ctx, self, "__map_keys__", kl);
    setMapListInPlace(ctx, self, "__map_vals__", vl);
    setMapListInPlace(ctx, self, "__map_hash__", hl);
    setMapSizeInPlace(ctx, self, sz + 1);
    return defVal;
}

// ---------------------------------------------------------------------------
// map.getOrInsertComputed(key, callbackFn) → value
// Returns existing value; if absent, calls callbackFn() for the default and inserts it.
// ---------------------------------------------------------------------------
static const proto::ProtoObject* mapGetOrInsertComputed(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    if (!requireMapThis(ctx, self)) return PROTO_NONE;
    int argc = args ? static_cast<int>(args->getSize(ctx)) : 0;
    const proto::ProtoObject* key = (argc > 0) ? args->getAt(ctx, 0) : PROTO_NONE;
    const proto::ProtoObject* callbackFn = (argc > 1) ? args->getAt(ctx, 1) : PROTO_NONE;
    if (!key) key = PROTO_NONE;
    key = normalizeMapKey(ctx, key);

    unsigned long foundIdx = 0;
    if (mapFind(ctx, self, key, foundIdx)) {
        const proto::ProtoSparseList* valsList = getMapList(ctx, self, "__map_vals__");
        if (!valsList) return PROTO_NONE;
        const proto::ProtoObject* v = valsList->has(ctx, foundIdx)
            ? valsList->getAt(ctx, foundIdx) : PROTO_NONE;
        return v ? v : PROTO_NONE;
    }
    // Call callback() to compute default value.
    if (!callbackFn || callbackFn == PROTO_NONE) return PROTO_NONE;
    const proto::ProtoList* cbArgs = ctx->newList();
    const proto::ProtoObject* defVal = callJSFunction(ctx, callbackFn, PROTO_NONE, cbArgs);
    if (hasCallException()) return PROTO_NONE;
    if (!defVal) defVal = PROTO_NONE;
    // Insert new entry.
    const proto::ProtoSparseList* kl = getMapList(ctx, self, "__map_keys__");
    const proto::ProtoSparseList* vl = getMapList(ctx, self, "__map_vals__");
    const proto::ProtoSparseList* hl = getMapList(ctx, self, "__map_hash__");
    if (!kl || !vl || !hl) return PROTO_NONE;
    long sz = getMapSize(ctx, self);
    unsigned long ni = static_cast<unsigned long>(sz);
    kl = kl->setAt(ctx, ni, key);
    vl = vl->setAt(ctx, ni, defVal);
    unsigned long h = szvHash(ctx, key);
    if (!hl->has(ctx, h))
        hl = hl->setAt(ctx, h, ctx->fromInteger(static_cast<long long>(ni)));
    setMapListInPlace(ctx, self, "__map_keys__", kl);
    setMapListInPlace(ctx, self, "__map_vals__", vl);
    setMapListInPlace(ctx, self, "__map_hash__", hl);
    setMapSizeInPlace(ctx, self, sz + 1);
    return defVal;
}

// ---------------------------------------------------------------------------
// Map.groupBy(iterable, keyFn) → Map  (static method on constructor)
// Groups elements of iterable by keyFn(element, index); returns a Map of arrays.
// ---------------------------------------------------------------------------
static const proto::ProtoObject* mapGroupBy(
    proto::ProtoContext* ctx, const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    int argc = args ? static_cast<int>(args->getSize(ctx)) : 0;
    const proto::ProtoObject* iterable = (argc > 0) ? args->getAt(ctx, 0) : PROTO_NONE;
    const proto::ProtoObject* keyFn    = (argc > 1) ? args->getAt(ctx, 1) : PROTO_NONE;
    if (!iterable) iterable = PROTO_NONE;
    if (!keyFn)    keyFn    = PROTO_NONE;

    // Create a result Map.
    const proto::ProtoObject* result = s_mapPrototype
        ? s_mapPrototype->newChild(ctx, true) : ctx->newObject(true);
    if (!result) return PROTO_NONE;
    {
        const proto::ProtoSparseList* empty = ctx->newSparseList();
        setMapListInPlace(ctx, result, "__map_keys__", empty);
        setMapListInPlace(ctx, result, "__map_vals__", empty);
        setMapListInPlace(ctx, result, "__map_hash__", empty);
        setMapSizeInPlace(ctx, result, 0L);
    }

    // Iterate the iterable using array-like length+index protocol.
    if (iterable == PROTO_NONE) return result;
    const proto::ProtoString* lenKs = JSSymbols::length(ctx);
    if (!lenKs) return result;
    const proto::ProtoObject* lenObj = iterable->getAttribute(ctx, lenKs, true);
    if (!lenObj || lenObj == PROTO_NONE || !lenObj->isInteger(ctx)) return result;
    long len = lenObj->asLong(ctx);

    for (long i = 0; i < len; i++) {
        const proto::ProtoString* ik = JSSymbols::indexKey(ctx, static_cast<uint32_t>(i));
        if (!ik) continue;
        const proto::ProtoObject* elem = iterable->getAttribute(ctx, ik, true);
        if (!elem) elem = PROTO_NONE;

        // Call keyFn(element, index).
        const proto::ProtoList* cbArgs = ctx->newList();
        cbArgs = cbArgs->appendLast(ctx, elem);
        cbArgs = cbArgs->appendLast(ctx, ctx->fromInteger(static_cast<long long>(i)));
        const proto::ProtoObject* groupKey = callJSFunction(ctx, keyFn, PROTO_NONE, cbArgs);
        if (hasCallException()) return PROTO_NONE;
        if (!groupKey) groupKey = PROTO_NONE;
        groupKey = normalizeMapKey(ctx, groupKey);

        // Find or create the group array for this key.
        unsigned long foundIdx = 0;
        if (mapFind(ctx, result, groupKey, foundIdx)) {
            // Append elem to existing array.
            const proto::ProtoSparseList* vl = getMapList(ctx, result, "__map_vals__");
            if (vl && vl->has(ctx, foundIdx)) {
                const proto::ProtoObject* arr = vl->getAt(ctx, foundIdx);
                if (arr && arr != PROTO_NONE) {
                    const proto::ProtoString* lk = JSSymbols::length(ctx);
                    const proto::ProtoObject* arrLen = lk ? arr->getAttribute(ctx, lk, false) : nullptr;
                    long al = (arrLen && arrLen->isInteger(ctx)) ? arrLen->asLong(ctx) : 0L;
                    const proto::ProtoString* newIdx = JSSymbols::indexKey(ctx, static_cast<uint32_t>(al));
                    if (newIdx) arr = arr->setAttribute(ctx, newIdx, elem);
                    if (lk) arr = arr->setAttribute(ctx, lk, ctx->fromInteger(al + 1));
                    setMapListInPlace(ctx, result, "__map_vals__",
                        vl->setAt(ctx, foundIdx, arr));
                }
            }
        } else {
            // Create new array [elem] and insert into result map.
            const proto::ProtoObject* arr = createNewArray(ctx, nullptr);
            const proto::ProtoString* k0  = JSSymbols::indexKey(ctx, 0);
            const proto::ProtoString* lk  = JSSymbols::length(ctx);
            const proto::ProtoString* ia  = JSSymbols::isArray(ctx);
            if (k0) arr = arr->setAttribute(ctx, k0, elem);
            if (lk) arr = arr->setAttribute(ctx, lk, ctx->fromInteger(1LL));
            if (ia) arr = arr->setAttribute(ctx, ia, ctx->fromInteger(1LL));

            const proto::ProtoSparseList* kl = getMapList(ctx, result, "__map_keys__");
            const proto::ProtoSparseList* vl = getMapList(ctx, result, "__map_vals__");
            const proto::ProtoSparseList* hl = getMapList(ctx, result, "__map_hash__");
            if (!kl || !vl || !hl) continue;
            long sz = getMapSize(ctx, result);
            unsigned long ni = static_cast<unsigned long>(sz);
            kl = kl->setAt(ctx, ni, groupKey);
            vl = vl->setAt(ctx, ni, arr);
            unsigned long h = szvHash(ctx, groupKey);
            if (!hl->has(ctx, h))
                hl = hl->setAt(ctx, h, ctx->fromInteger(static_cast<long long>(ni)));
            setMapListInPlace(ctx, result, "__map_keys__", kl);
            setMapListInPlace(ctx, result, "__map_vals__", vl);
            setMapListInPlace(ctx, result, "__map_hash__", hl);
            setMapSizeInPlace(ctx, result, sz + 1);
        }
    }
    return result;
}

// ---------------------------------------------------------------------------
// Map constructor: new Map(iterable?)
// The constructor receives `self` as the newly-allocated Map object.
// ---------------------------------------------------------------------------
static const proto::ProtoObject* mapConstruct(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    if (!self) return PROTO_NONE;

    // Initialize empty backing storage (mutates self in place — mutable object).
    const proto::ProtoSparseList* emptyList = ctx->newSparseList();
    setMapListInPlace(ctx, self, "__map_keys__", emptyList);
    setMapListInPlace(ctx, self, "__map_vals__", emptyList);
    setMapListInPlace(ctx, self, "__map_hash__", emptyList);
    setMapSizeInPlace(ctx, self, 0L);

    // If iterable argument provided, call map.set for each [key, value] pair.
    int argc = args ? static_cast<int>(args->getSize(ctx)) : 0;
    if (argc > 0) {
        const proto::ProtoObject* iterable = args->getAt(ctx, 0);
        if (iterable && iterable != PROTO_NONE) {
            const proto::ProtoString* lenKs = JSSymbols::length(ctx);
            if (lenKs) {
                const proto::ProtoObject* lenObj = iterable->getAttribute(ctx, lenKs, true);
                if (lenObj && lenObj != PROTO_NONE && lenObj->isInteger(ctx)) {
                    long len = lenObj->asLong(ctx);
                    for (long i = 0; i < len; i++) {
                        const proto::ProtoString* ik = JSSymbols::indexKey(ctx, static_cast<uint32_t>(i));
                        if (!ik) continue;
                        const proto::ProtoObject* pair = iterable->getAttribute(ctx, ik, true);
                        if (!pair || pair == PROTO_NONE) continue;
                        const proto::ProtoString* k0 = JSSymbols::indexKey(ctx, 0);
                        const proto::ProtoString* k1 = JSSymbols::indexKey(ctx, 1);
                        if (!k0 || !k1) continue;
                        const proto::ProtoObject* pkey = pair->getAttribute(ctx, k0, true);
                        const proto::ProtoObject* pval = pair->getAttribute(ctx, k1, true);
                        if (!pkey) pkey = PROTO_NONE;
                        if (!pval) pval = PROTO_NONE;
                        // Inline map.set(pkey, pval).
                        unsigned long existingIdx = 0;
                        const proto::ProtoSparseList* kl = getMapList(ctx, self, "__map_keys__");
                        const proto::ProtoSparseList* vl = getMapList(ctx, self, "__map_vals__");
                        const proto::ProtoSparseList* hl = getMapList(ctx, self, "__map_hash__");
                        long sz = getMapSize(ctx, self);
                        if (kl && vl && hl) {
                            if (mapFind(ctx, self, pkey, existingIdx)) {
                                setMapListInPlace(ctx, self, "__map_vals__",
                                    vl->setAt(ctx, existingIdx, pval));
                            } else {
                                unsigned long ni = static_cast<unsigned long>(sz);
                                kl = kl->setAt(ctx, ni, pkey);
                                vl = vl->setAt(ctx, ni, pval);
                                unsigned long h = szvHash(ctx, pkey);
                                if (!hl->has(ctx, h))
                                    hl = hl->setAt(ctx, h, ctx->fromInteger(static_cast<long long>(ni)));
                                setMapListInPlace(ctx, self, "__map_keys__", kl);
                                setMapListInPlace(ctx, self, "__map_vals__", vl);
                                setMapListInPlace(ctx, self, "__map_hash__", hl);
                                setMapSizeInPlace(ctx, self, sz + 1);
                            }
                        }
                    }
                }
            }
        }
    }
    return self;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// BuildMapPrototype
// ---------------------------------------------------------------------------
void BuildMapPrototype(proto::ProtoSpace* space, proto::ProtoContext* ctx,
                       const proto::ProtoObject* objectProto)
{
    if (!space || !ctx || !objectProto) return;

    const proto::ProtoObject* mapProto = objectProto->newChild(ctx, true); // mutable
    if (!mapProto) return;

    mapProto = installNonEnumerableMethod(ctx, mapProto, "set",     mapSet,     2);
    mapProto = installNonEnumerableMethod(ctx, mapProto, "get",     mapGet,     1);
    mapProto = installNonEnumerableMethod(ctx, mapProto, "has",     mapHas,     1);
    mapProto = installNonEnumerableMethod(ctx, mapProto, "delete",  mapDelete,  1);
    mapProto = installNonEnumerableMethod(ctx, mapProto, "clear",   mapClear,   0);
    mapProto = installNonEnumerableMethod(ctx, mapProto, "forEach", mapForEach, 1);
    mapProto = installNonEnumerableMethod(ctx, mapProto, "keys",    mapKeys,    0);
    mapProto = installNonEnumerableMethod(ctx, mapProto, "values",  mapValues,  0);
    mapProto = installNonEnumerableMethod(ctx, mapProto, "entries",             mapEntries,             0);
    mapProto = installNonEnumerableMethod(ctx, mapProto, "getOrInsert",         mapGetOrInsert,         2);
    mapProto = installNonEnumerableMethod(ctx, mapProto, "getOrInsertComputed",  mapGetOrInsertComputed, 2);

    // Install 'size' as a getter via __get_size__ (accessed by OP_get_field accessor protocol).
    {
        const proto::ProtoObject* gko = ctx->fromUTF8String("__get_size__");
        const proto::ProtoString* gks = gko ? gko->asString(ctx) : nullptr;
        if (gks) {
            const proto::ProtoObject* getter = ctx->fromMethod(nullptr, mapSizeGetter);
            if (getter) mapProto = mapProto->setAttribute(ctx, gks, getter);
        }
    }

    // Symbol.iterator = entries (Map iterates as [key, value] pairs)
    {
        const proto::ProtoString* symIterKey = JSSymbols::symbolIterator(ctx);
        if (symIterKey) {
            const proto::ProtoObject* entriesKeyObj = ctx->fromUTF8String("entries");
            const proto::ProtoString* entriesKey = entriesKeyObj ? entriesKeyObj->asString(ctx) : nullptr;
            const proto::ProtoObject* entriesFn = entriesKey
                ? mapProto->getAttribute(ctx, entriesKey, false) : nullptr;
            if (entriesFn && entriesFn != PROTO_NONE) {
                mapProto = mapProto->setAttribute(ctx, symIterKey, entriesFn);
                const proto::ProtoObject* pdko = ctx->fromUTF8String("__pd_Symbol.iterator__");
                const proto::ProtoString* pdks = pdko ? pdko->asString(ctx) : nullptr;
                if (pdks) mapProto = mapProto->setAttribute(ctx, pdks, ctx->fromInteger(0x3LL));
            }
        }
    }

    // Symbol.toStringTag = "Map": {writable:false, enumerable:false, configurable:true}
    // bit1=configurable=true → bits = 0x2
    {
        const proto::ProtoString* tstKey = JSSymbols::toStringTag(ctx);
        if (tstKey) {
            mapProto = mapProto->setAttribute(ctx, tstKey, ctx->fromUTF8String("Map"));
            const proto::ProtoObject* pdko = ctx->fromUTF8String("__pd___toStringTag____");
            const proto::ProtoString* pdks = pdko ? pdko->asString(ctx) : nullptr;
            if (pdks) mapProto = mapProto->setAttribute(ctx, pdks, ctx->fromInteger(0x2LL));
        }
    }

    // Store for ensureMapConstructor retrieval.
    s_mapPrototype = mapProto;
}

// ---------------------------------------------------------------------------
// ensureMapConstructor
// ---------------------------------------------------------------------------
void ensureMapConstructor(proto::ProtoContext* ctx,
                          const proto::ProtoObject** globalRoot)
{
    if (!ctx || !globalRoot || !*globalRoot) return;

    const proto::ProtoObject* ko = ctx->fromUTF8String("Map");
    const proto::ProtoString* ks = ko ? ko->asString(ctx) : nullptr;
    if (!ks) return;

    const proto::ProtoObject* existing = (*globalRoot)->getAttribute(ctx, ks, false);
    if (existing && existing != PROTO_NONE) return; // already installed

    const proto::ProtoObject* ctorParent =
        (ctx->space && ctx->space->methodPrototype) ? ctx->space->methodPrototype : nullptr;
    const proto::ProtoObject* ctor = ctorParent
        ? ctorParent->newChild(ctx, true)
        : ctx->newObject(true);
    if (!ctor) return;

    // ctor.name = "Map"
    const proto::ProtoString* nameKey = JSSymbols::name(ctx);
    if (nameKey) ctor = ctor->setAttribute(ctx, nameKey, ctx->fromUTF8String("Map"));

    // ctor.prototype = Map.prototype
    const proto::ProtoString* protoKey = JSSymbols::prototype(ctx);
    if (protoKey && s_mapPrototype)
        ctor = ctor->setAttribute(ctx, protoKey, s_mapPrototype);

    // __construct__ — MUST use ctx->fromMethod (NOT wrapNativeFunction).
    // OP_call_constructor checks isMethod() on the __construct__ value.
    const proto::ProtoObject* constructKey = ctx->fromUTF8String("__construct__");
    const proto::ProtoString* constructKs  = constructKey ? constructKey->asString(ctx) : nullptr;
    if (constructKs) {
        const proto::ProtoObject* constructFn = ctx->fromMethod(nullptr, mapConstruct);
        if (constructFn) ctor = ctor->setAttribute(ctx, constructKs, constructFn);
    }

    // Map.groupBy static method
    {
        const proto::ProtoObject* gbWrapper = ctx->space->methodPrototype
            ? ctx->space->methodPrototype->newChild(ctx, true)
            : ctx->newObject(true);
        const proto::ProtoString* nfk = JSSymbols::nativeFn(ctx);
        if (nfk) gbWrapper = gbWrapper->setAttribute(ctx, nfk, ctx->fromMethod(nullptr, mapGroupBy));
        const proto::ProtoString* lenk = JSSymbols::length(ctx);
        if (lenk) {
            gbWrapper = gbWrapper->setAttribute(ctx, lenk, ctx->fromInteger(2LL));
            const proto::ProtoObject* pdl = ctx->fromUTF8String("__pd_length__");
            const proto::ProtoString* pdlk = pdl ? pdl->asString(ctx) : nullptr;
            if (pdlk) gbWrapper = gbWrapper->setAttribute(ctx, pdlk, ctx->fromInteger(0x2LL));
        }
        const proto::ProtoString* nmk = JSSymbols::name(ctx);
        if (nmk) {
            gbWrapper = gbWrapper->setAttribute(ctx, nmk, ctx->fromUTF8String("groupBy"));
            const proto::ProtoObject* pdn = ctx->fromUTF8String("__pd_name__");
            const proto::ProtoString* pdnk = pdn ? pdn->asString(ctx) : nullptr;
            if (pdnk) gbWrapper = gbWrapper->setAttribute(ctx, pdnk, ctx->fromInteger(0x2LL));
        }
        const proto::ProtoObject* gbko = ctx->fromUTF8String("groupBy");
        const proto::ProtoString* gbks = gbko ? gbko->asString(ctx) : nullptr;
        if (gbks) {
            ctor = ctor->setAttribute(ctx, gbks, gbWrapper);
            const proto::ProtoObject* pdgb = ctx->fromUTF8String("__pd_groupBy__");
            const proto::ProtoString* pdgbk = pdgb ? pdgb->asString(ctx) : nullptr;
            if (pdgbk) ctor = ctor->setAttribute(ctx, pdgbk, ctx->fromInteger(0x3LL));
        }
    }

    *globalRoot = (*globalRoot)->setAttribute(ctx, ks, ctor);
}

} // namespace protojs
