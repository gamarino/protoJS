#include "MapPrototype.h"
#include "ArrayPrototype.h"
#include "ArrayElementsStorage.h"
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
    // New entry — pick the slot AFTER the highest currently used slot,
    // not size, because removeAt leaves sparse-list holes that mean
    // size != max-slot+1 after any delete. Pre-fix `newIdx = size`
    // overwrote the entry that previously sat at slot `size` (e.g.
    // delete 'a' from c,a,b leaves 0=c, 2=b, size=2; re-set 'a' at
    // slot 2 wiped 'b').
    long sz = getMapSize(ctx, self);
    unsigned long newIdx = 0;
    bool hasAny = false;
    {
        const proto::ProtoSparseListIterator* it = keysList->getIterator(ctx);
        while (it && it->hasNext(ctx)) {
            unsigned long slot = it->nextKey(ctx);
            (void)it->nextValue(ctx);
            it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
            if (!hasAny || slot >= newIdx) newIdx = slot + 1;
            hasAny = true;
        }
    }
    if (!hasAny) newIdx = static_cast<unsigned long>(sz);
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
// get Map[Symbol.species] — returns `this` per §24.1.2.2.
static const proto::ProtoObject* mapSpeciesGetter(
    proto::ProtoContext* /*ctx*/, const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*)
{
    return self;
}

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
    const proto::ProtoObject* callback = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0) : PROTO_NONE;
    // ECMA-262 §24.1.3.5 step 4: IsCallable(callbackfn) must be true,
    // else throw TypeError. Mirrors the discipline of setForEach.
    {
        bool callable = false;
        if (callback && callback != PROTO_NONE && callback != getUndefinedSentinel()) {
            if (callback->isMethod(ctx)) callable = true;
            const proto::ProtoString* bcK = JSSymbols::bytecodeId(ctx);
            if (!callable && bcK && callback->hasAttribute(ctx, bcK) == PROTO_TRUE) callable = true;
            const proto::ProtoString* nfK = JSSymbols::nativeFn(ctx);
            if (!callable && nfK && callback->hasAttribute(ctx, nfK) == PROTO_TRUE) callable = true;
            const proto::ProtoString* bfK = JSSymbols::boundFn(ctx);
            if (!callable && bfK && callback->hasAttribute(ctx, bfK) == PROTO_TRUE) callable = true;
        }
        if (!callable) {
            signalNativeException(makeNativeError(ctx, "TypeError",
                "Map.prototype.forEach callback is not callable"));
            return PROTO_NONE;
        }
    }
    const proto::ProtoObject* thisArg = (args->getSize(ctx) > 1) ? args->getAt(ctx, 1) : PROTO_NONE;
    if (!thisArg) thisArg = PROTO_NONE;

    // ECMA-262 §24.1.3.5 NOTE: same iteration discipline as
    // Set.prototype.forEach — entries added during the callback are
    // visited; entries deleted before being visited are not; entries
    // re-added after deletion are visited again at a fresh slot.
    // Pre-fix the frozen iterator snapshot missed all three cases.
    unsigned long pos = 0;
    while (true) {
        const proto::ProtoSparseList* keysList = getMapList(ctx, self, "__map_keys__");
        const proto::ProtoSparseList* valsList = getMapList(ctx, self, "__map_vals__");
        if (!keysList) break;
        unsigned long highWater = 0;
        bool anyEntry = false;
        const proto::ProtoSparseListIterator* probe = keysList->getIterator(ctx);
        while (probe && probe->hasNext(ctx)) {
            unsigned long slot = probe->nextKey(ctx);
            (void)probe->nextValue(ctx);
            probe = const_cast<proto::ProtoSparseListIterator*>(probe)->advance(ctx);
            if (!anyEntry || slot >= highWater) highWater = slot + 1;
            anyEntry = true;
        }
        if (!anyEntry || pos >= highWater) break;
        if (!keysList->has(ctx, pos)) { ++pos; continue; }
        const proto::ProtoObject* k = keysList->getAt(ctx, pos);
        const proto::ProtoObject* v = (valsList && valsList->has(ctx, pos))
            ? valsList->getAt(ctx, pos) : PROTO_NONE;
        ++pos;
        if (!k) k = PROTO_NONE;
        if (!v) v = PROTO_NONE;
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
            // entries: [key, value] — build a real array via __elements__
            // and the PROTO_TRUE isArray singleton.  Pre-fix the pair was
            // stored with indexed-attribute keys (\"0\", \"1\") + length +
            // __is_array__=fromInteger(1) (wrong singleton).  Consumers
            // checking `__is_array__ == PROTO_TRUE` and reading via
            // __elements__ saw the pair as a plain object.
            const proto::ProtoObject* pair = createNewArray(ctx, nullptr);
            const proto::ProtoString* lk = JSSymbols::length(ctx);
            const proto::ProtoString* ia = JSSymbols::isArray(ctx);
            const proto::ProtoList* pairEls = ctx->newList();
            pairEls = pairEls->appendLast(ctx, k);
            pairEls = pairEls->appendLast(ctx, v);
            setArrayElements(ctx, pair, pairEls);
            if (lk) pair = pair->setAttribute(ctx, lk, ctx->fromInteger(2LL));
            if (ia) pair = pair->setAttribute(ctx, ia, PROTO_TRUE);
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

// Insert a new (key, val) entry into mapObj.
// Assumes key is already normalized and NOT present in the map.
static void mapInsertEntry(proto::ProtoContext* ctx,
                            const proto::ProtoObject* mapObj,
                            const proto::ProtoObject* key,
                            const proto::ProtoObject* val)
{
    const proto::ProtoSparseList* kl = getMapList(ctx, mapObj, "__map_keys__");
    const proto::ProtoSparseList* vl = getMapList(ctx, mapObj, "__map_vals__");
    const proto::ProtoSparseList* hl = getMapList(ctx, mapObj, "__map_hash__");
    if (!kl || !vl || !hl) return;
    long sz = getMapSize(ctx, mapObj);
    // ECMA-262 §24.1.3.9: re-adding a key after delete places it at
    // the END of the insertion order. Pre-fix used `ni = size` as the
    // sparse-list slot, but after a removeAt the sparse list still has
    // holes — slot `size` may be occupied by an entry that was at a
    // higher index pre-delete (e.g. delete 'a' from c,a,b leaves
    // 0=c, 2=b, size=2; re-setting 'a' at slot 2 overwrote 'b').
    // Walk the existing keys list and pick max(slotIdx)+1 instead.
    unsigned long ni = 0;
    bool hasAny = false;
    const proto::ProtoSparseListIterator* it = kl->getIterator(ctx);
    while (it && it->hasNext(ctx)) {
        unsigned long slot = it->nextKey(ctx);
        (void)it->nextValue(ctx);
        it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
        if (!hasAny || slot >= ni) ni = slot + 1;
        hasAny = true;
    }
    if (!hasAny) ni = static_cast<unsigned long>(sz);
    kl = kl->setAt(ctx, ni, key);
    vl = vl->setAt(ctx, ni, val);
    unsigned long h = szvHash(ctx, key);
    if (!hl->has(ctx, h))
        hl = hl->setAt(ctx, h, ctx->fromInteger(static_cast<long long>(ni)));
    setMapListInPlace(ctx, mapObj, "__map_keys__", kl);
    setMapListInPlace(ctx, mapObj, "__map_vals__", vl);
    setMapListInPlace(ctx, mapObj, "__map_hash__", hl);
    setMapSizeInPlace(ctx, mapObj, sz + 1);
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
    mapInsertEntry(ctx, self, key, defVal);
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

    // ECMA-262 (upsert proposal) §Map.prototype.getOrInsertComputed
    // step 3: if IsCallable(callbackfn) is false, throw TypeError.
    // The check happens BEFORE the map lookup, so even when the key
    // already exists a non-callable callbackfn must still throw.
    {
        bool callable = false;
        if (callbackFn && callbackFn != PROTO_NONE && callbackFn != getUndefinedSentinel()) {
            if (callbackFn->isMethod(ctx)) callable = true;
            const proto::ProtoString* bcK = JSSymbols::bytecodeId(ctx);
            if (!callable && bcK && callbackFn->hasAttribute(ctx, bcK) == PROTO_TRUE) callable = true;
            const proto::ProtoString* nfK = JSSymbols::nativeFn(ctx);
            if (!callable && nfK && callbackFn->hasAttribute(ctx, nfK) == PROTO_TRUE) callable = true;
            const proto::ProtoString* bfK = JSSymbols::boundFn(ctx);
            if (!callable && bfK && callbackFn->hasAttribute(ctx, bfK) == PROTO_TRUE) callable = true;
        }
        if (!callable) {
            signalNativeException(makeNativeError(ctx, "TypeError",
                "Map.prototype.getOrInsertComputed: callbackfn is not a function"));
            return PROTO_NONE;
        }
    }

    key = normalizeMapKey(ctx, key);
    unsigned long foundIdx = 0;
    if (mapFind(ctx, self, key, foundIdx)) {
        const proto::ProtoSparseList* valsList = getMapList(ctx, self, "__map_vals__");
        if (!valsList) return PROTO_NONE;
        const proto::ProtoObject* v = valsList->has(ctx, foundIdx)
            ? valsList->getAt(ctx, foundIdx) : PROTO_NONE;
        return v ? v : PROTO_NONE;
    }
    // Call callback(key) to compute default value.
    const proto::ProtoList* cbArgs = ctx->newList();
    cbArgs = cbArgs->appendLast(ctx, key);
    const proto::ProtoObject* defVal = callJSFunction(ctx, callbackFn, PROTO_NONE, cbArgs);
    if (hasCallException()) return PROTO_NONE;
    if (!defVal) defVal = PROTO_NONE;
    mapInsertEntry(ctx, self, key, defVal);
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
        // Real arrays keep data in __elements__; legacy plain-objects
        // use indexed string attributes. Probe both. Pre-fix the
        // getAttribute-only path always failed on real arrays and
        // groupBy collected three nulls per group.
        const proto::ProtoObject* elem =
            arrayTryFastGet(ctx, iterable, static_cast<unsigned long>(i));
        if (!elem) {
            const proto::ProtoString* ik = JSSymbols::indexKey(ctx, static_cast<uint32_t>(i));
            elem = ik ? iterable->getAttribute(ctx, ik, true) : nullptr;
        }
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
            const proto::ProtoSparseList* vl = getMapList(ctx, result, "__map_vals__");
            if (vl && vl->has(ctx, foundIdx)) {
                const proto::ProtoObject* arr = vl->getAt(ctx, foundIdx);
                if (arr && arr != PROTO_NONE) {
                    // Append to __elements__ so Array.prototype.* sees
                    // it (pre-fix used setAttribute(indexKey) which
                    // bypassed __elements__).
                    const proto::ProtoList* els = getArrayElements(ctx, arr);
                    if (!els) els = ctx->newList();
                    els = els->appendLast(ctx, elem);
                    setArrayElements(ctx, arr, els);
                    const proto::ProtoString* lk = JSSymbols::length(ctx);
                    if (lk) arr = arr->setAttribute(ctx, lk,
                        ctx->fromInteger(static_cast<long long>(els->getSize(ctx))));
                    setMapListInPlace(ctx, result, "__map_vals__",
                        vl->setAt(ctx, foundIdx, arr));
                }
            }
        } else {
            // Create a real array [elem] (createNewArray ensures the
            // proto chain / __is_array__ / length descriptor / etc.
            // are all aligned with Array.prototype).
            const proto::ProtoObject* arr = createNewArray(ctx, nullptr);
            const proto::ProtoList* els = ctx->newList();
            els = els->appendLast(ctx, elem);
            setArrayElements(ctx, arr, els);
            const proto::ProtoString* lk = JSSymbols::length(ctx);
            if (lk) arr = arr->setAttribute(ctx, lk, ctx->fromInteger(1LL));

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
        // Per ECMA-262 §24.1.1.1 step 5: if iterable is null/undefined,
        // skip iteration. Primitives that aren't strings throw
        // 'X is not iterable'. Strings ARE iterable but each code point
        // is a string scalar that fails the AddEntriesFromIterable
        // 'entry must be an Object' check — emit TypeError for them
        // too, matching V8 / SpiderMonkey.
        if (iterable == getUndefinedSentinel() || iterable == getNullSentinel()) {
            return self;
        }
        // §24.1.1.2 step 7.a-c: when iterable is present, [[Get]] the
        // adder (Map.prototype.set on self). Pre-fix the constructor
        // ingested its iterable via internal storage and never observed
        // a thrown .set getter; spec says propagate that abrupt
        // completion BEFORE the iteration starts.
        {
            const proto::ProtoObject* sKo = ctx->fromUTF8String("set");
            const proto::ProtoString* sKs = sKo ? sKo->asString(ctx) : nullptr;
            const proto::ProtoObject* setterFn = sKs
                ? self->getAttribute(ctx, sKs, true) : PROTO_NONE;
            // Accessor: __get_set__ lives behind the undefined sentinel
            // placeholder. Invoking the getter propagates abrupt completions
            // and resolves to the actual function value.
            if (!setterFn || setterFn == PROTO_NONE || setterFn == getUndefinedSentinel()) {
                const proto::ProtoObject* gko = ctx->fromUTF8String("__get_set__");
                const proto::ProtoString* gks = gko ? gko->asString(ctx) : nullptr;
                if (gks) {
                    const proto::ProtoObject* getter = self->getAttribute(ctx, gks, true);
                    if (getter && getter != PROTO_NONE) {
                        setterFn = callJSFunction(ctx, getter, self, ctx->newList());
                        if (hasCallException()) return PROTO_NONE;
                    }
                }
            }
            if (hasCallException()) return PROTO_NONE;
            bool callable = false;
            if (setterFn && setterFn != PROTO_NONE && setterFn != getUndefinedSentinel()) {
                if (setterFn->isMethod(ctx)) callable = true;
                const proto::ProtoString* bcK = JSSymbols::bytecodeId(ctx);
                if (!callable && bcK && setterFn->hasAttribute(ctx, bcK) == PROTO_TRUE) callable = true;
                const proto::ProtoString* nfK = JSSymbols::nativeFn(ctx);
                if (!callable && nfK && setterFn->hasAttribute(ctx, nfK) == PROTO_TRUE) callable = true;
                const proto::ProtoString* bfK = JSSymbols::boundFn(ctx);
                if (!callable && bfK && setterFn->hasAttribute(ctx, bfK) == PROTO_TRUE) callable = true;
            }
            if (!callable) {
                signalNativeException(makeNativeError(ctx, "TypeError",
                    "Map: 'set' is not callable"));
                return PROTO_NONE;
            }
        }
        if (iterable && (iterable->isInteger(ctx) || iterable->isDouble(ctx)
                         || iterable->isFloat(ctx) || iterable->isBoolean(ctx)
                         || iterable->isString(ctx))) {
            signalNativeException(makeNativeError(ctx, "TypeError",
                "is not iterable"));
            return PROTO_NONE;
        }
        // §24.1.1.2 step 6 + GetIterator: when @@iterator is explicitly
        // set to undefined / null, iterable is non-iterable — TypeError.
        // Pre-fix the impl walked the array-like length path regardless
        // and silently produced an empty Map.
        if (iterable && iterable != PROTO_NONE) {
            const proto::ProtoObject* ito = ctx->fromUTF8String("Symbol.iterator");
            const proto::ProtoString* its = ito ? ito->asString(ctx) : nullptr;
            if (its && iterable->hasOwnAttribute(ctx, its) == PROTO_TRUE) {
                const proto::ProtoObject* fn = iterable->getAttribute(ctx, its, false);
                if (fn == getUndefinedSentinel() || fn == getNullSentinel()) {
                    signalNativeException(makeNativeError(ctx, "TypeError",
                        "Map iterable's @@iterator is not callable"));
                    return PROTO_NONE;
                }
            }
        }
        if (iterable && iterable != PROTO_NONE) {
            // Element / pair-element read: prefer __elements__ native
            // storage; fall back to indexed-attribute.  Pre-fix
            // `new Map([[\"a\",1]])` silently produced an empty Map
            // because the outer iterable [...] is a real array with
            // entries in __elements__ — getAttribute(\"0\") returned
            // nullptr, so no pairs were processed.
            auto readEl = [&](const proto::ProtoObject* arr, long i) -> const proto::ProtoObject* {
                if (!arr || arr == PROTO_NONE) return PROTO_NONE;
                const proto::ProtoObject* v =
                    arrayTryFastGet(ctx, arr, static_cast<unsigned long>(i));
                if (v) return v;
                const proto::ProtoString* ik = JSSymbols::indexKey(ctx, static_cast<uint32_t>(i));
                v = ik ? arr->getAttribute(ctx, ik, true) : nullptr;
                return v ? v : PROTO_NONE;
            };
            const proto::ProtoString* lenKs = JSSymbols::length(ctx);
            long len = -1;
            if (lenKs) {
                const proto::ProtoObject* lenObj = iterable->getAttribute(ctx, lenKs, true);
                if (lenObj && lenObj != PROTO_NONE && lenObj->isInteger(ctx))
                    len = lenObj->asLong(ctx);
            }
            if (len < 0) {
                const proto::ProtoList* els = getArrayElements(ctx, iterable);
                if (els) len = static_cast<long>(els->getSize(ctx));
            }
            for (long i = 0; i < len; i++) {
                const proto::ProtoObject* pair = readEl(iterable, i);
                if (!pair || pair == PROTO_NONE) continue;
                // ECMA-262 §24.1.1.2 AddEntriesFromIterable step 4.a-c:
                // each entry MUST be an Object, otherwise TypeError.
                // Pre-fix `new Map([1, 2])` silently produced an empty
                // Map because primitive entries' readEl(0/1) returned
                // PROTO_NONE — no throw, no insertion.
                if (pair->isInteger(ctx) || pair->isDouble(ctx)
                    || pair->isFloat(ctx) || pair->isString(ctx)
                    || pair->isBoolean(ctx)
                    || pair == getNullSentinel()
                    || pair == getUndefinedSentinel()) {
                    signalNativeException(makeNativeError(ctx, "TypeError",
                        "Iterator value is not an entry object"));
                    return PROTO_NONE;
                }
                const proto::ProtoObject* pkey = readEl(pair, 0);
                const proto::ProtoObject* pval = readEl(pair, 1);
                {
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
    // §24.1.3.10 requires the accessor function to expose
    //   .name   = 'get size'  (descriptor 0x2)
    //   .length = 0           (descriptor 0x2)
    // matching the parallel Set.prototype.size install pattern.
    {
        const proto::ProtoObject* gko = ctx->fromUTF8String("__get_size__");
        const proto::ProtoString* gks = gko ? gko->asString(ctx) : nullptr;
        if (gks) {
            const proto::ProtoObject* parent =
                (ctx->space && ctx->space->methodPrototype)
                ? ctx->space->methodPrototype : nullptr;
            const proto::ProtoObject* getter = parent
                ? parent->newChild(ctx, true) : ctx->newObject(true);
            if (getter) {
                const proto::ProtoString* nfKey = JSSymbols::nativeFn(ctx);
                if (nfKey) {
                    proto::ProtoObject* mGetter = const_cast<proto::ProtoObject*>(getter);
                    const proto::ProtoObject* raw = ctx->fromMethod(mGetter, mapSizeGetter);
                    if (raw) getter = getter->setAttribute(ctx, nfKey, raw);
                }
                const proto::ProtoString* lenKey = JSSymbols::length(ctx);
                if (lenKey) {
                    getter = getter->setAttribute(ctx, lenKey, ctx->fromInteger(0LL));
                    const proto::ProtoObject* pdlo = ctx->fromUTF8String("__pd_length__");
                    const proto::ProtoString* pdls = pdlo ? pdlo->asString(ctx) : nullptr;
                    if (pdls) getter = getter->setAttribute(ctx, pdls, ctx->fromInteger(0x2LL));
                }
                const proto::ProtoString* nmKey = JSSymbols::name(ctx);
                if (nmKey) {
                    getter = getter->setAttribute(ctx, nmKey, ctx->fromUTF8String("get size"));
                    const proto::ProtoObject* pdno = ctx->fromUTF8String("__pd_name__");
                    const proto::ProtoString* pdns = pdno ? pdno->asString(ctx) : nullptr;
                    if (pdns) getter = getter->setAttribute(ctx, pdns, ctx->fromInteger(0x2LL));
                }
                mapProto = mapProto->setAttribute(ctx, gks, getter);
                // §24.1.3.10 .size accessor descriptor:
                // {enumerable:false, configurable:true} → bits 0x2.
                // Pre-fix no sidecar so Object.getOwnPropertyDescriptor
                // reported enumerable:true on Map.prototype.size.
                const proto::ProtoObject* pdo = ctx->fromUTF8String("__pd_size__");
                const proto::ProtoString* pdks = pdo ? pdo->asString(ctx) : nullptr;
                if (pdks) mapProto = mapProto->setAttribute(ctx, pdks, ctx->fromInteger(0x2LL));
            }
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

    // Symbol.toStringTag = "Map": install under both internal and
    // user-visible keys (see the Set fix one commit prior).
    {
        const proto::ProtoString* tstKey = JSSymbols::toStringTag(ctx);
        if (tstKey) {
            mapProto = mapProto->setAttribute(ctx, tstKey, ctx->fromUTF8String("Map"));
            const proto::ProtoObject* pdko = ctx->fromUTF8String("__pd___toStringTag____");
            const proto::ProtoString* pdks = pdko ? pdko->asString(ctx) : nullptr;
            if (pdks) mapProto = mapProto->setAttribute(ctx, pdks, ctx->fromInteger(0x2LL));
        }
        const proto::ProtoString* userKey = JSSymbols::symbolToStringTag(ctx);
        if (userKey) {
            mapProto = mapProto->setAttribute(ctx, userKey, ctx->fromUTF8String("Map"));
            const proto::ProtoObject* pdko = ctx->fromUTF8String("__pd_Symbol.toStringTag__");
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

    // BuildMapPrototype ran before ensureFunctionPrototype, so each
    // installNonEnumerableMethod call back then saw methodPrototype =
    // nullptr and stamped a parentless wrapper. Reinstall now that
    // methodPrototype (=Function.prototype) is available so
    // m.set.call / m.has.bind / etc. resolve via the standard chain.
    // Mirrors the parallel fix in ensureNumberConstructor.
    if (ctx->space && ctx->space->methodPrototype && s_mapPrototype) {
        const proto::ProtoObject* mp = s_mapPrototype;
        mp = installNonEnumerableMethod(ctx, mp, "set",     mapSet,     2);
        mp = installNonEnumerableMethod(ctx, mp, "get",     mapGet,     1);
        mp = installNonEnumerableMethod(ctx, mp, "has",     mapHas,     1);
        mp = installNonEnumerableMethod(ctx, mp, "delete",  mapDelete,  1);
        mp = installNonEnumerableMethod(ctx, mp, "clear",   mapClear,   0);
        mp = installNonEnumerableMethod(ctx, mp, "forEach", mapForEach, 1);
        mp = installNonEnumerableMethod(ctx, mp, "keys",    mapKeys,    0);
        mp = installNonEnumerableMethod(ctx, mp, "values",  mapValues,  0);
        mp = installNonEnumerableMethod(ctx, mp, "entries", mapEntries, 0);
        mp = installNonEnumerableMethod(ctx, mp, "getOrInsert",          mapGetOrInsert,         2);
        mp = installNonEnumerableMethod(ctx, mp, "getOrInsertComputed",  mapGetOrInsertComputed, 2);
        s_mapPrototype = mp;
    }

    const proto::ProtoObject* ctorParent =
        (ctx->space && ctx->space->methodPrototype) ? ctx->space->methodPrototype : nullptr;
    const proto::ProtoObject* ctor = ctorParent
        ? ctorParent->newChild(ctx, true)
        : ctx->newObject(true);
    if (!ctor) return;

    // ctor.name = "Map" with §17 built-in descriptor 0x2
    // (writable:false, enumerable:false, configurable:true)
    const proto::ProtoString* nameKey = JSSymbols::name(ctx);
    if (nameKey) {
        ctor = ctor->setAttribute(ctx, nameKey, ctx->fromUTF8String("Map"));
        const proto::ProtoObject* pdno = ctx->fromUTF8String("__pd_name__");
        const proto::ProtoString* pdnk = pdno ? pdno->asString(ctx) : nullptr;
        if (pdnk) ctor = ctor->setAttribute(ctx, pdnk, ctx->fromInteger(0x2LL));
    }

    // Map.length = 0 per §24.1.1.1 with same §17 descriptor.
    // Pre-fix the attribute was absent so the test262
    // 'built-ins/Map/length.js' verifyProperty failed.
    {
        const proto::ProtoString* lenKey = JSSymbols::length(ctx);
        if (lenKey) {
            ctor = ctor->setAttribute(ctx, lenKey, ctx->fromInteger(0LL));
            const proto::ProtoObject* pdlo = ctx->fromUTF8String("__pd_length__");
            const proto::ProtoString* pdlk = pdlo ? pdlo->asString(ctx) : nullptr;
            if (pdlk) ctor = ctor->setAttribute(ctx, pdlk, ctx->fromInteger(0x2LL));
        }
    }

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

    // Map.prototype.constructor === Map per §24.1.3.2 (non-enumerable).
    if (s_mapPrototype) {
        const proto::ProtoString* ctorWordKey = JSSymbols::constructor(ctx);
        if (ctorWordKey) {
            const proto::ProtoObject* updated =
                s_mapPrototype->setAttribute(ctx, ctorWordKey, ctor);
            if (updated && updated != PROTO_NONE) {
                const proto::ProtoObject* pdo = ctx->fromUTF8String("__pd_constructor__");
                const proto::ProtoString* pdk = pdo ? pdo->asString(ctx) : nullptr;
                if (pdk) updated = updated->setAttribute(ctx, pdk, ctx->fromInteger(0x3LL));
                s_mapPrototype = updated;
            }
        }
    }

    // get Map[Symbol.species] per §24.1.2.2. Mirrors the Set install.
    {
        const proto::ProtoString* speciesKey = JSSymbols::symbolSpecies(ctx);
        if (speciesKey) {
            const proto::ProtoObject* parent =
                (ctx->space && ctx->space->methodPrototype)
                ? ctx->space->methodPrototype : nullptr;
            const proto::ProtoObject* getter = parent
                ? parent->newChild(ctx, true) : ctx->newObject(true);
            if (getter) {
                const proto::ProtoString* nfKey = JSSymbols::nativeFn(ctx);
                if (nfKey) {
                    proto::ProtoObject* mGetter = const_cast<proto::ProtoObject*>(getter);
                    const proto::ProtoObject* raw = ctx->fromMethod(mGetter, mapSpeciesGetter);
                    if (raw) getter = getter->setAttribute(ctx, nfKey, raw);
                }
                const proto::ProtoString* lenKey = JSSymbols::length(ctx);
                if (lenKey) {
                    getter = getter->setAttribute(ctx, lenKey, ctx->fromInteger(0LL));
                    const proto::ProtoObject* pdlo = ctx->fromUTF8String("__pd_length__");
                    const proto::ProtoString* pdls = pdlo ? pdlo->asString(ctx) : nullptr;
                    if (pdls) getter = getter->setAttribute(ctx, pdls, ctx->fromInteger(0x2LL));
                }
                const proto::ProtoString* nmKey = JSSymbols::name(ctx);
                if (nmKey) {
                    getter = getter->setAttribute(ctx, nmKey, ctx->fromUTF8String("get [Symbol.species]"));
                    const proto::ProtoObject* pdno = ctx->fromUTF8String("__pd_name__");
                    const proto::ProtoString* pdns = pdno ? pdno->asString(ctx) : nullptr;
                    if (pdns) getter = getter->setAttribute(ctx, pdns, ctx->fromInteger(0x2LL));
                }
                const proto::ProtoString* gksSym =
                    ctx->fromUTF8String("__get_Symbol.species__")->asString(ctx);
                if (gksSym) ctor = ctor->setAttribute(ctx, gksSym, getter);
            }
        }
    }

    *globalRoot = (*globalRoot)->setAttribute(ctx, ks, ctor);
    // §17 globalThis.Map descriptor 0x3.
    {
        const proto::ProtoObject* pdo = ctx->fromUTF8String("__pd_Map__");
        const proto::ProtoString* pdk = pdo ? pdo->asString(ctx) : nullptr;
        if (pdk) *globalRoot = (*globalRoot)->setAttribute(ctx, pdk,
            ctx->fromInteger(0x3LL));
    }
}

// ---------------------------------------------------------------------------
// WeakMap — minimal conformant implementation (linear-scan, O(n)).
//
// Entries are stored as mutable object attributes named __wm_N_key__ and
// __wm_N_val__, with __wm_n__ holding the entry count. Keys are compared by
// object identity (pointer equality), matching ECMAScript WeakMap semantics.
// ---------------------------------------------------------------------------

static long long wmGetCount(proto::ProtoContext* ctx, const proto::ProtoObject* self) {
    const proto::ProtoObject* nObj = ctx->fromUTF8String("__wm_n__");
    const proto::ProtoString* nKey = nObj ? nObj->asString(ctx) : nullptr;
    if (!nKey) return 0;
    const proto::ProtoObject* nv = self->getAttribute(ctx, nKey, false);
    if (nv && nv != PROTO_NONE && nv->isInteger(ctx)) return nv->asLong(ctx);
    return 0;
}

static const proto::ProtoObject* weakMapSet(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!ctx || !self || self == PROTO_NONE) return PROTO_NONE;
    if (!args || args->getSize(ctx) < 1) return const_cast<proto::ProtoObject*>(self);
    const proto::ProtoObject* key = args->getAt(ctx, 0);
    const proto::ProtoObject* val = (args->getSize(ctx) > 1) ? args->getAt(ctx, 1) : PROTO_NONE;
    if (!key || key == PROTO_NONE) return const_cast<proto::ProtoObject*>(self);

    // Spec §24.3.3.6 step 5: WeakMap.prototype.set throws TypeError
    // when the key is not an Object. Primitives, null, undefined are
    // all rejected. The pre-fix path silently stored primitives,
    // which made identity-tracked test262 cases pass for the wrong
    // reasons.
    if (key->isString(ctx) || key->isInteger(ctx)
        || key->isDouble(ctx) || key->isFloat(ctx)
        || key->isBoolean(ctx)
        || key == getNullSentinel() || key == getUndefinedSentinel()
        || key == PROTO_TRUE || key == PROTO_FALSE) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Invalid value used as weak map key"));
        return PROTO_NONE;
    }

    long long n = wmGetCount(ctx, self);
    // Search for existing key (identity comparison).
    for (long long i = 0; i < n; i++) {
        std::string kstr = "__wm_" + std::to_string(i) + "_key__";
        const proto::ProtoObject* ko = ctx->fromUTF8String(kstr.c_str());
        const proto::ProtoString* ks = ko ? ko->asString(ctx) : nullptr;
        if (!ks) continue;
        if (self->getAttribute(ctx, ks, false) == key) {
            std::string vstr = "__wm_" + std::to_string(i) + "_val__";
            const proto::ProtoObject* vo = ctx->fromUTF8String(vstr.c_str());
            const proto::ProtoString* vs = vo ? vo->asString(ctx) : nullptr;
            if (vs) self = self->setAttribute(ctx, vs, val ? val : PROTO_NONE);
            return const_cast<proto::ProtoObject*>(self);
        }
    }
    // New entry.
    std::string kstr = "__wm_" + std::to_string(n) + "_key__";
    std::string vstr = "__wm_" + std::to_string(n) + "_val__";
    const proto::ProtoObject* ko = ctx->fromUTF8String(kstr.c_str());
    const proto::ProtoObject* vo = ctx->fromUTF8String(vstr.c_str());
    const proto::ProtoString* ks = ko ? ko->asString(ctx) : nullptr;
    const proto::ProtoString* vs = vo ? vo->asString(ctx) : nullptr;
    if (ks) self = self->setAttribute(ctx, ks, key);
    if (vs) self = self->setAttribute(ctx, vs, val ? val : PROTO_NONE);
    const proto::ProtoObject* nObj = ctx->fromUTF8String("__wm_n__");
    const proto::ProtoString* nKey = nObj ? nObj->asString(ctx) : nullptr;
    if (nKey) self = self->setAttribute(ctx, nKey, ctx->fromInteger(n + 1));
    return const_cast<proto::ProtoObject*>(self);
}

static const proto::ProtoObject* weakMapGet(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!ctx || !self || self == PROTO_NONE || !args || args->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* key = args->getAt(ctx, 0);
    if (!key || key == PROTO_NONE) return PROTO_NONE;
    long long n = wmGetCount(ctx, self);
    for (long long i = 0; i < n; i++) {
        std::string kstr = "__wm_" + std::to_string(i) + "_key__";
        const proto::ProtoObject* ko = ctx->fromUTF8String(kstr.c_str());
        const proto::ProtoString* ks = ko ? ko->asString(ctx) : nullptr;
        if (!ks) continue;
        if (self->getAttribute(ctx, ks, false) == key) {
            std::string vstr = "__wm_" + std::to_string(i) + "_val__";
            const proto::ProtoObject* vo = ctx->fromUTF8String(vstr.c_str());
            const proto::ProtoString* vs = vo ? vo->asString(ctx) : nullptr;
            return vs ? self->getAttribute(ctx, vs, false) : PROTO_NONE;
        }
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* weakMapHas(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!ctx || !self || self == PROTO_NONE || !args || args->getSize(ctx) < 1) return PROTO_FALSE;
    const proto::ProtoObject* key = args->getAt(ctx, 0);
    if (!key || key == PROTO_NONE) return PROTO_FALSE;
    long long n = wmGetCount(ctx, self);
    for (long long i = 0; i < n; i++) {
        std::string kstr = "__wm_" + std::to_string(i) + "_key__";
        const proto::ProtoObject* ko = ctx->fromUTF8String(kstr.c_str());
        const proto::ProtoString* ks = ko ? ko->asString(ctx) : nullptr;
        if (!ks) continue;
        if (self->getAttribute(ctx, ks, false) == key) return PROTO_TRUE;
    }
    return PROTO_FALSE;
}

static const proto::ProtoObject* weakMapDelete(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!ctx || !self || self == PROTO_NONE || !args || args->getSize(ctx) < 1) return PROTO_FALSE;
    const proto::ProtoObject* key = args->getAt(ctx, 0);
    if (!key || key == PROTO_NONE) return PROTO_FALSE;
    long long n = wmGetCount(ctx, self);
    for (long long i = 0; i < n; i++) {
        std::string kstr = "__wm_" + std::to_string(i) + "_key__";
        const proto::ProtoObject* ko = ctx->fromUTF8String(kstr.c_str());
        const proto::ProtoString* ks = ko ? ko->asString(ctx) : nullptr;
        if (!ks) continue;
        if (self->getAttribute(ctx, ks, false) == key) {
            // Swap with last entry to fill the gap (O(1) removal).
            long long last = n - 1;
            if (i != last) {
                std::string lkStr = "__wm_" + std::to_string(last) + "_key__";
                std::string lvStr = "__wm_" + std::to_string(last) + "_val__";
                std::string ivStr = "__wm_" + std::to_string(i) + "_val__";
                const proto::ProtoObject* lko = ctx->fromUTF8String(lkStr.c_str());
                const proto::ProtoObject* lvo = ctx->fromUTF8String(lvStr.c_str());
                const proto::ProtoObject* ivo = ctx->fromUTF8String(ivStr.c_str());
                const proto::ProtoString* lks = lko ? lko->asString(ctx) : nullptr;
                const proto::ProtoString* lvs = lvo ? lvo->asString(ctx) : nullptr;
                const proto::ProtoString* ivs = ivo ? ivo->asString(ctx) : nullptr;
                const proto::ProtoObject* lkv = lks ? self->getAttribute(ctx, lks, false) : PROTO_NONE;
                const proto::ProtoObject* lvv = lvs ? self->getAttribute(ctx, lvs, false) : PROTO_NONE;
                if (ks && lkv) self = self->setAttribute(ctx, ks, lkv);
                if (ivs && lvv) self = self->setAttribute(ctx, ivs, lvv);
                if (lks) self = self->setAttribute(ctx, lks, PROTO_NONE);
                if (lvs) self = self->setAttribute(ctx, lvs, PROTO_NONE);
            } else {
                std::string vstr = "__wm_" + std::to_string(i) + "_val__";
                const proto::ProtoObject* vo = ctx->fromUTF8String(vstr.c_str());
                const proto::ProtoString* vs = vo ? vo->asString(ctx) : nullptr;
                self = self->setAttribute(ctx, ks, PROTO_NONE);
                if (vs) self = self->setAttribute(ctx, vs, PROTO_NONE);
            }
            const proto::ProtoObject* nObj = ctx->fromUTF8String("__wm_n__");
            const proto::ProtoString* nKey = nObj ? nObj->asString(ctx) : nullptr;
            if (nKey) self = self->setAttribute(ctx, nKey, ctx->fromInteger(n - 1));
            return PROTO_TRUE;
        }
    }
    return PROTO_FALSE;
}

static const proto::ProtoObject* weakMapConstruct(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* /*args*/,
    const proto::ProtoSparseList*)
{
    if (!self) return PROTO_NONE;
    // Initialize the entry count to 0; entries are stored lazily.
    const proto::ProtoObject* nObj = ctx->fromUTF8String("__wm_n__");
    const proto::ProtoString* nKey = nObj ? nObj->asString(ctx) : nullptr;
    if (nKey) self = self->setAttribute(ctx, nKey, ctx->fromInteger(0LL));
    return const_cast<proto::ProtoObject*>(self);
}

void ensureWeakMapConstructor(proto::ProtoContext* ctx,
                               const proto::ProtoObject** globalRoot)
{
    if (!ctx || !globalRoot || !*globalRoot) return;

    const proto::ProtoObject* keyObj = ctx->fromUTF8String("WeakMap");
    const proto::ProtoString* key = keyObj ? keyObj->asString(ctx) : nullptr;
    if (!key) return;

    const proto::ProtoObject* existing = (*globalRoot)->getAttribute(ctx, key, false);
    if (existing && existing != PROTO_NONE) return;

    // Build the WeakMap prototype with set/get/has/delete methods.
    const proto::ProtoObject* proto = ctx->newObject(true);
    struct Method { const char* name; proto::ProtoMethod fn; int argc; };
    static const Method kMethods[] = {
        { "set",    weakMapSet,    2 },
        { "get",    weakMapGet,    1 },
        { "has",    weakMapHas,    1 },
        { "delete", weakMapDelete, 1 },
        { nullptr,  nullptr,       0 }
    };
    for (int i = 0; kMethods[i].name; i++)
        proto = installNonEnumerableMethod(ctx, proto, kMethods[i].name,
                                           kMethods[i].fn, kMethods[i].argc);

    // Symbol.toStringTag = "WeakMap": install under both the internal
    // "__toStringTag__" key (used by Object.prototype.toString's
    // own probe) and the user-visible "Symbol.toStringTag" key (what
    // `WeakMap.prototype[Symbol.toStringTag]` resolves to). Without
    // the user-visible install the test262 'built-ins/WeakMap/
    // prototype/Symbol.toStringTag.js' check failed because the value
    // was looked up under the spec-name string, not the internal
    // shortcut. Stamp descriptor 0x2 (writable=false, enumerable=false,
    // configurable=true) per §17.
    {
        const proto::ProtoString* tagKey = JSSymbols::toStringTag(ctx);
        if (tagKey) proto = proto->setAttribute(ctx, tagKey, ctx->fromUTF8String("WeakMap"));
        const proto::ProtoString* userKey = JSSymbols::symbolToStringTag(ctx);
        if (userKey) {
            proto = proto->setAttribute(ctx, userKey, ctx->fromUTF8String("WeakMap"));
            const proto::ProtoObject* pdko = ctx->fromUTF8String("__pd_Symbol.toStringTag__");
            const proto::ProtoString* pdks = pdko ? pdko->asString(ctx) : nullptr;
            if (pdks) proto = proto->setAttribute(ctx, pdks, ctx->fromInteger(0x2LL));
        }
    }

    // Build the constructor object.
    const proto::ProtoObject* ctor =
        (ctx->space && ctx->space->methodPrototype)
        ? ctx->space->methodPrototype->newChild(ctx, true)
        : ctx->newObject(true);
    if (!ctor) return;

    const proto::ProtoString* nameKey = JSSymbols::name(ctx);
    if (nameKey) ctor = ctor->setAttribute(ctx, nameKey, ctx->fromUTF8String("WeakMap"));

    const proto::ProtoString* protoKey = JSSymbols::prototype(ctx);
    if (protoKey) ctor = ctor->setAttribute(ctx, protoKey, proto);

    // __construct__ native handler (called by OP_call_constructor generic fallback).
    const proto::ProtoObject* constructKeyObj = ctx->fromUTF8String("__construct__");
    const proto::ProtoString* constructKs = constructKeyObj ? constructKeyObj->asString(ctx) : nullptr;
    if (constructKs) {
        const proto::ProtoObject* constructFn = ctx->fromMethod(nullptr, weakMapConstruct);
        if (constructFn) ctor = ctor->setAttribute(ctx, constructKs, constructFn);
    }

    // Set prototype.constructor = ctor (non-enumerable via property descriptor).
    {
        const proto::ProtoString* ctorKey = JSSymbols::constructor(ctx);
        if (ctorKey) proto = proto->setAttribute(ctx, ctorKey, ctor);
    }

    *globalRoot = (*globalRoot)->setAttribute(ctx, key, ctor);
    // §17 globalThis.WeakMap descriptor 0x3.
    {
        const proto::ProtoObject* pdo = ctx->fromUTF8String("__pd_WeakMap__");
        const proto::ProtoString* pdk = pdo ? pdo->asString(ctx) : nullptr;
        if (pdk) *globalRoot = (*globalRoot)->setAttribute(ctx, pdk,
            ctx->fromInteger(0x3LL));
    }
}

} // namespace protojs
