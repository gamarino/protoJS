#include "SetPrototype.h"
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
// Module-level storage for the JS Set prototype, set by BuildSetPrototype
// and retrieved by ensureSetConstructor.
// ---------------------------------------------------------------------------
static const proto::ProtoObject* s_setPrototype = nullptr;

namespace {

// ---------------------------------------------------------------------------
// SameValueZero equality (required for Set membership checks on doubles/NaN).
// ---------------------------------------------------------------------------
static bool setSVZ(proto::ProtoContext* ctx,
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

// Normalize -0 to +0 per SameValueZero spec.
static const proto::ProtoObject* normalizeSetVal(proto::ProtoContext* ctx,
                                                  const proto::ProtoObject* val)
{
    if (val && (val->isDouble(ctx) || val->isFloat(ctx))) {
        double d = val->asDouble(ctx);
        if (d == 0.0 && std::signbit(d))
            return ctx->fromInteger(0LL);
    }
    return val;
}

// Returns true if self is a valid Set receiver (has __set_order__ slot).
// Signals TypeError and returns false otherwise.
static bool requireSetThis(proto::ProtoContext* ctx, const proto::ProtoObject* self)
{
    if (!self || self == PROTO_NONE || self == PROTO_TRUE || self == PROTO_FALSE ||
        self->isInteger(ctx) || self->isDouble(ctx) || self->isFloat(ctx) ||
        self->isString(ctx)) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Set operation called on non-Set"));
        return false;
    }
    const proto::ProtoObject* ko = ctx->fromUTF8String("__set_order__");
    const proto::ProtoString* ks = ko ? ko->asString(ctx) : nullptr;
    const proto::ProtoObject* v  = ks ? self->getAttribute(ctx, ks, false) : nullptr;
    if (!v || v == PROTO_NONE) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Set operation called on non-Set"));
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Helper: retrieve backing structures from setObj.
// ---------------------------------------------------------------------------
static const proto::ProtoSet* getSetCore(proto::ProtoContext* ctx,
                                          const proto::ProtoObject* setObj)
{
    const proto::ProtoObject* ko = ctx->fromUTF8String("__set_core__");
    const proto::ProtoString* ks = ko ? ko->asString(ctx) : nullptr;
    if (!ks || !setObj || setObj == PROTO_NONE) return nullptr;
    const proto::ProtoObject* v = setObj->getAttribute(ctx, ks, false);
    return (v && v != PROTO_NONE) ? v->asSet(ctx) : nullptr;
}

static void setSetCoreInPlace(proto::ProtoContext* ctx,
                               const proto::ProtoObject* setObj,
                               const proto::ProtoSet* core)
{
    if (!core || !setObj || setObj == PROTO_NONE) return;
    const proto::ProtoObject* ko = ctx->fromUTF8String("__set_core__");
    const proto::ProtoString* ks = ko ? ko->asString(ctx) : nullptr;
    if (ks) setObj->setAttribute(ctx, ks, core->asObject(ctx));
}

static const proto::ProtoSparseList* getSetOrder(proto::ProtoContext* ctx,
                                                   const proto::ProtoObject* setObj)
{
    const proto::ProtoObject* ko = ctx->fromUTF8String("__set_order__");
    const proto::ProtoString* ks = ko ? ko->asString(ctx) : nullptr;
    if (!ks || !setObj || setObj == PROTO_NONE) return nullptr;
    const proto::ProtoObject* v = setObj->getAttribute(ctx, ks, false);
    return (v && v != PROTO_NONE) ? v->asSparseList(ctx) : nullptr;
}

static void setSetOrderInPlace(proto::ProtoContext* ctx,
                                const proto::ProtoObject* setObj,
                                const proto::ProtoSparseList* order)
{
    if (!order || !setObj || setObj == PROTO_NONE) return;
    const proto::ProtoObject* ko = ctx->fromUTF8String("__set_order__");
    const proto::ProtoString* ks = ko ? ko->asString(ctx) : nullptr;
    if (ks) setObj->setAttribute(ctx, ks, order->asObject(ctx));
}

static long getSetSize(proto::ProtoContext* ctx, const proto::ProtoObject* setObj) {
    const proto::ProtoObject* ko = ctx->fromUTF8String("__set_size__");
    const proto::ProtoString* ks = ko ? ko->asString(ctx) : nullptr;
    if (!ks || !setObj || setObj == PROTO_NONE) return 0L;
    const proto::ProtoObject* v = setObj->getAttribute(ctx, ks, false);
    return (v && v != PROTO_NONE && v->isInteger(ctx)) ? v->asLong(ctx) : 0L;
}

static void setSetSizeInPlace(proto::ProtoContext* ctx,
                               const proto::ProtoObject* setObj,
                               long sz)
{
    const proto::ProtoObject* ko = ctx->fromUTF8String("__set_size__");
    const proto::ProtoString* ks = ko ? ko->asString(ctx) : nullptr;
    if (ks && setObj && setObj != PROTO_NONE)
        setObj->setAttribute(ctx, ks, ctx->fromInteger(sz));
}

// ---------------------------------------------------------------------------
// Check Set membership combining ProtoSet::has with SameValueZero fallback
// (handles NaN and double edge cases that may not be interned by ProtoSet).
// ---------------------------------------------------------------------------
static bool setContains(proto::ProtoContext* ctx,
                         const proto::ProtoObject* setObj,
                         const proto::ProtoObject* val)
{
    const proto::ProtoSet* core = getSetCore(ctx, setObj);
    if (!core) return false;
    if (core->has(ctx, val) == PROTO_TRUE) return true;
    // Augment for double/NaN edge cases.
    if (val && (val->isDouble(ctx) || val->isFloat(ctx))) {
        const proto::ProtoSparseList* order = getSetOrder(ctx, setObj);
        if (!order) return false;
        const proto::ProtoSparseListIterator* it = order->getIterator(ctx);
        while (it && it->hasNext(ctx)) {
            const proto::ProtoObject* existing = it->nextValue(ctx);
            it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
            if (setSVZ(ctx, existing, val)) return true;
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// set.add(val) → set
// ---------------------------------------------------------------------------
static const proto::ProtoObject* setAdd(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    if (!requireSetThis(ctx, self)) return PROTO_NONE;
    const proto::ProtoObject* val = (args && args->getSize(ctx) > 0) ? args->getAt(ctx, 0) : PROTO_NONE;
    if (!val) val = PROTO_NONE;
    val = normalizeSetVal(ctx, val);

    if (setContains(ctx, self, val)) return self;

    const proto::ProtoSet* core  = getSetCore(ctx, self);
    const proto::ProtoSparseList* order = getSetOrder(ctx, self);
    long sz = getSetSize(ctx, self);
    if (core)  setSetCoreInPlace(ctx, self, core->add(ctx, val));
    if (order) setSetOrderInPlace(ctx, self,
                   order->setAt(ctx, static_cast<unsigned long>(sz), val));
    setSetSizeInPlace(ctx, self, sz + 1);
    return self;
}

// ---------------------------------------------------------------------------
// set.has(val) → boolean
// ---------------------------------------------------------------------------
static const proto::ProtoObject* setHas(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    if (!requireSetThis(ctx, self)) return PROTO_NONE;
    const proto::ProtoObject* val = (args && args->getSize(ctx) > 0) ? args->getAt(ctx, 0) : PROTO_NONE;
    if (!val) val = PROTO_NONE;
    val = normalizeSetVal(ctx, val);
    return setContains(ctx, self, val) ? PROTO_TRUE : PROTO_FALSE;
}

// ---------------------------------------------------------------------------
// set.delete(val) → boolean
// ---------------------------------------------------------------------------
static const proto::ProtoObject* setDeleteFn(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    if (!requireSetThis(ctx, self)) return PROTO_NONE;
    const proto::ProtoObject* val = (args && args->getSize(ctx) > 0) ? args->getAt(ctx, 0) : PROTO_NONE;
    if (!val) val = PROTO_NONE;
    val = normalizeSetVal(ctx, val);

    if (!setContains(ctx, self, val)) return PROTO_FALSE;

    const proto::ProtoSet* core  = getSetCore(ctx, self);
    const proto::ProtoSparseList* order = getSetOrder(ctx, self);
    long sz = getSetSize(ctx, self);

    if (core) setSetCoreInPlace(ctx, self, core->remove(ctx, val));

    // Remove from order list: find matching index via SameValueZero scan.
    if (order) {
        const proto::ProtoSparseListIterator* it = order->getIterator(ctx);
        while (it && it->hasNext(ctx)) {
            unsigned long idx = it->nextKey(ctx);
            const proto::ProtoObject* existing = it->nextValue(ctx);
            it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
            if (setSVZ(ctx, existing, val)) {
                setSetOrderInPlace(ctx, self, order->removeAt(ctx, idx));
                break;
            }
        }
    }

    setSetSizeInPlace(ctx, self, sz > 0 ? sz - 1 : 0);
    return PROTO_TRUE;
}

// ---------------------------------------------------------------------------
// set.clear() → undefined
// ---------------------------------------------------------------------------
static const proto::ProtoObject* setClear(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*)
{
    if (!requireSetThis(ctx, self)) return PROTO_NONE;
    setSetCoreInPlace(ctx, self, ctx->newSet());
    setSetOrderInPlace(ctx, self, ctx->newSparseList());
    setSetSizeInPlace(ctx, self, 0L);
    return PROTO_NONE;
}

// ---------------------------------------------------------------------------
// set.size getter
// ---------------------------------------------------------------------------
static const proto::ProtoObject* setSizeGetter(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*)
{
    if (!requireSetThis(ctx, self)) return PROTO_NONE;
    return ctx->fromInteger(static_cast<long long>(getSetSize(ctx, self)));
}

// ---------------------------------------------------------------------------
// set.forEach(callback, thisArg?)
// ---------------------------------------------------------------------------
static const proto::ProtoObject* setForEach(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    if (!requireSetThis(ctx, self)) return PROTO_NONE;
    if (!args || args->getSize(ctx) == 0) return PROTO_NONE;
    const proto::ProtoObject* callback = args->getAt(ctx, 0);
    if (!callback || callback == PROTO_NONE) return PROTO_NONE;
    const proto::ProtoObject* thisArg = (args->getSize(ctx) > 1) ? args->getAt(ctx, 1) : PROTO_NONE;
    if (!thisArg) thisArg = PROTO_NONE;

    const proto::ProtoSparseList* order = getSetOrder(ctx, self);
    if (!order) return PROTO_NONE;

    const proto::ProtoSparseListIterator* it = order->getIterator(ctx);
    while (it && it->hasNext(ctx)) {
        const proto::ProtoObject* v = it->nextValue(ctx);
        it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
        if (!v) v = PROTO_NONE;
        // Call callback(value, value, set) per spec.
        const proto::ProtoList* cbArgs = ctx->newList();
        cbArgs = cbArgs->appendLast(ctx, v);
        cbArgs = cbArgs->appendLast(ctx, v);
        cbArgs = cbArgs->appendLast(ctx, self);
        callJSFunction(ctx, callback, thisArg, cbArgs);
        if (hasCallException()) return PROTO_NONE;
    }
    return PROTO_NONE;
}

// ---------------------------------------------------------------------------
// Set iterator next() — advances through __set_order__ sparse list.
// ---------------------------------------------------------------------------
static const proto::ProtoObject* setIteratorNext(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*)
{
    auto makeDone = [&]() -> const proto::ProtoObject* {
        const proto::ProtoObject* r = ctx->newObject(true);
        const proto::ProtoString* vk = JSSymbols::value(ctx);
        const proto::ProtoString* dk = JSSymbols::done(ctx);
        if (vk) r = r->setAttribute(ctx, vk, PROTO_NONE);
        if (dk) r = r->setAttribute(ctx, dk, PROTO_TRUE);
        return r;
    };

    if (!self || self == PROTO_NONE) return makeDone();

    const proto::ProtoString* idxKey  = JSSymbols::iterIdx(ctx);
    const proto::ProtoString* arrKey2 = JSSymbols::iterArr(ctx);
    const proto::ProtoString* kindKey = JSSymbols::iterKind(ctx);
    if (!idxKey || !arrKey2 || !kindKey) return makeDone();

    const proto::ProtoObject* setObj  = self->getAttribute(ctx, arrKey2, false);
    const proto::ProtoObject* posObj  = self->getAttribute(ctx, idxKey,  false);
    const proto::ProtoObject* kindObj = self->getAttribute(ctx, kindKey, false);
    if (!setObj || setObj == PROTO_NONE) return makeDone();

    long long pos = (posObj && posObj != PROTO_NONE && posObj->isInteger(ctx))
                    ? posObj->asLong(ctx) : 0LL;
    std::string kind = "values";
    if (kindObj && kindObj != PROTO_NONE && kindObj->isString(ctx)) {
        const proto::ProtoString* ks2 = kindObj->asString(ctx);
        if (ks2) ks2->toUTF8String(ctx, kind);
    }

    const proto::ProtoSparseList* order = getSetOrder(ctx, setObj);
    if (!order) return makeDone();

    const proto::ProtoSparseListIterator* it = order->getIterator(ctx);
    while (it && it->hasNext(ctx)) {
        unsigned long slotIdx = it->nextKey(ctx);
        const proto::ProtoObject* v = it->nextValue(ctx);
        it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
        if (static_cast<long long>(slotIdx) < pos) continue;

        // Advance position past this slot (mutates iterator in place).
        self->setAttribute(ctx, idxKey, ctx->fromInteger(static_cast<long long>(slotIdx) + 1));

        if (!v) v = PROTO_NONE;
        const proto::ProtoObject* iterVal = PROTO_NONE;
        if (kind == "entries") {
            // Set entries: [value, value]
            const proto::ProtoObject* pair = createNewArray(ctx, nullptr);
            const proto::ProtoString* i0 = JSSymbols::indexKey(ctx, 0);
            const proto::ProtoString* i1 = JSSymbols::indexKey(ctx, 1);
            const proto::ProtoString* lk = JSSymbols::length(ctx);
            const proto::ProtoString* ia = JSSymbols::isArray(ctx);
            if (i0) pair = pair->setAttribute(ctx, i0, v);
            if (i1) pair = pair->setAttribute(ctx, i1, v);
            if (lk) pair = pair->setAttribute(ctx, lk, ctx->fromInteger(2LL));
            if (ia) pair = pair->setAttribute(ctx, ia, ctx->fromInteger(1LL));
            iterVal = pair;
        } else {
            iterVal = v; // "keys" or "values" both yield the value for Set
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

static const proto::ProtoObject* makeSetIterator(
    proto::ProtoContext* ctx, const proto::ProtoObject* setObj, const char* kind)
{
    const proto::ProtoObject* iter = ctx->newObject(true);
    const proto::ProtoString* idxKey  = JSSymbols::iterIdx(ctx);
    const proto::ProtoString* arrKey2 = JSSymbols::iterArr(ctx);
    const proto::ProtoString* kindKey = JSSymbols::iterKind(ctx);
    const proto::ProtoString* nextKey = JSSymbols::next(ctx);
    if (idxKey)  iter = iter->setAttribute(ctx, idxKey,  ctx->fromInteger(0LL));
    if (arrKey2) iter = iter->setAttribute(ctx, arrKey2, setObj ? setObj : PROTO_NONE);
    if (kindKey) iter = iter->setAttribute(ctx, kindKey, ctx->fromUTF8String(kind));
    if (nextKey) {
        const proto::ProtoObject* nextFn = ctx->fromMethod(nullptr, setIteratorNext);
        if (nextFn) iter = iter->setAttribute(ctx, nextKey, nextFn);
    }
    return iter;
}

static const proto::ProtoObject* setValues(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{
    if (!requireSetThis(ctx, self)) return PROTO_NONE;
    return makeSetIterator(ctx, self, "values");
}

static const proto::ProtoObject* setKeys(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{
    if (!requireSetThis(ctx, self)) return PROTO_NONE;
    return makeSetIterator(ctx, self, "values"); // Set.keys() === Set.values()
}

static const proto::ProtoObject* setEntries(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*)
{
    if (!requireSetThis(ctx, self)) return PROTO_NONE;
    return makeSetIterator(ctx, self, "entries");
}

// ---------------------------------------------------------------------------
// Set constructor: new Set(iterable?)
// ---------------------------------------------------------------------------
static const proto::ProtoObject* setConstruct(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    if (!self) return PROTO_NONE;

    setSetCoreInPlace(ctx, self, ctx->newSet());
    setSetOrderInPlace(ctx, self, ctx->newSparseList());
    setSetSizeInPlace(ctx, self, 0L);

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
                        const proto::ProtoObject* val = iterable->getAttribute(ctx, ik, true);
                        if (!val) val = PROTO_NONE;
                        val = normalizeSetVal(ctx, val);
                        if (!setContains(ctx, self, val)) {
                            const proto::ProtoSet* core  = getSetCore(ctx, self);
                            const proto::ProtoSparseList* order = getSetOrder(ctx, self);
                            long sz = getSetSize(ctx, self);
                            if (core)  setSetCoreInPlace(ctx, self, core->add(ctx, val));
                            if (order) setSetOrderInPlace(ctx, self,
                                           order->setAt(ctx, static_cast<unsigned long>(sz), val));
                            setSetSizeInPlace(ctx, self, sz + 1);
                        }
                    }
                }
            }
        }
    }
    return self;
}

// ---------------------------------------------------------------------------
// makeEmptySet — create a new empty Set inheriting from s_setPrototype.
// ---------------------------------------------------------------------------
static const proto::ProtoObject* makeEmptySet(proto::ProtoContext* ctx)
{
    const proto::ProtoObject* s = s_setPrototype
        ? s_setPrototype->newChild(ctx, true)
        : ctx->newObject(true);
    if (!s) return PROTO_NONE;
    setSetCoreInPlace(ctx, s, ctx->newSet());
    setSetOrderInPlace(ctx, s, ctx->newSparseList());
    setSetSizeInPlace(ctx, s, 0L);
    return s;
}

// Add a single value to a Set in place (with normalization, no-op if already present).
static void setAddValue(proto::ProtoContext* ctx,
                        const proto::ProtoObject* setObj,
                        const proto::ProtoObject* val)
{
    val = normalizeSetVal(ctx, val);
    if (setContains(ctx, setObj, val)) return;
    const proto::ProtoSet* core  = getSetCore(ctx, setObj);
    const proto::ProtoSparseList* order = getSetOrder(ctx, setObj);
    long sz = getSetSize(ctx, setObj);
    if (core)  setSetCoreInPlace(ctx, setObj, core->add(ctx, val));
    if (order) setSetOrderInPlace(ctx, setObj,
                   order->setAt(ctx, static_cast<unsigned long>(sz), val));
    setSetSizeInPlace(ctx, setObj, sz + 1);
}

// Iterate other (a Set or array-like) and add each element to setObj.
static void setAddAllFrom(proto::ProtoContext* ctx,
                          const proto::ProtoObject* setObj,
                          const proto::ProtoObject* other)
{
    if (!other || other == PROTO_NONE) return;
    // Prefer Set protocol (has __set_order__).
    const proto::ProtoSparseList* otherOrder = getSetOrder(ctx, other);
    if (otherOrder) {
        const proto::ProtoSparseListIterator* it = otherOrder->getIterator(ctx);
        while (it && it->hasNext(ctx)) {
            const proto::ProtoObject* v = it->nextValue(ctx);
            it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
            setAddValue(ctx, setObj, v ? v : PROTO_NONE);
        }
        return;
    }
    // Fall back to array-like length+index.
    const proto::ProtoString* lenKs = JSSymbols::length(ctx);
    if (!lenKs) return;
    const proto::ProtoObject* lenObj = other->getAttribute(ctx, lenKs, true);
    if (!lenObj || lenObj == PROTO_NONE || !lenObj->isInteger(ctx)) return;
    long len = lenObj->asLong(ctx);
    for (long i = 0; i < len; i++) {
        const proto::ProtoString* ik = JSSymbols::indexKey(ctx, static_cast<uint32_t>(i));
        if (!ik) continue;
        const proto::ProtoObject* v = other->getAttribute(ctx, ik, true);
        setAddValue(ctx, setObj, v ? v : PROTO_NONE);
    }
}

// ---------------------------------------------------------------------------
// ES2025 Set collection methods
// ---------------------------------------------------------------------------
static const proto::ProtoObject* setUnion(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    if (!requireSetThis(ctx, self)) return PROTO_NONE;
    const proto::ProtoObject* other = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0) : PROTO_NONE;
    if (!other || other == PROTO_NONE) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Set.prototype.union: argument must be a Set-like object"));
        return PROTO_NONE;
    }
    const proto::ProtoObject* result = makeEmptySet(ctx);
    if (!result || result == PROTO_NONE) return PROTO_NONE;
    setAddAllFrom(ctx, result, self);
    setAddAllFrom(ctx, result, other);
    return result;
}

static const proto::ProtoObject* setIntersection(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    if (!requireSetThis(ctx, self)) return PROTO_NONE;
    const proto::ProtoObject* other = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0) : PROTO_NONE;
    if (!other || other == PROTO_NONE) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Set.prototype.intersection: argument must be a Set-like object"));
        return PROTO_NONE;
    }
    const proto::ProtoObject* result = makeEmptySet(ctx);
    if (!result || result == PROTO_NONE) return PROTO_NONE;
    const proto::ProtoSparseList* order = getSetOrder(ctx, self);
    if (!order) return result;
    // Iterate self via iterator, add elements present in other.
    const proto::ProtoSparseListIterator* it = order->getIterator(ctx);
    while (it && it->hasNext(ctx)) {
        const proto::ProtoObject* v = it->nextValue(ctx);
        it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
        if (!v) v = PROTO_NONE;
        if (setContains(ctx, other, v))
            setAddValue(ctx, result, v);
    }
    return result;
}

static const proto::ProtoObject* setDifference(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    if (!requireSetThis(ctx, self)) return PROTO_NONE;
    const proto::ProtoObject* other = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0) : PROTO_NONE;
    if (!other || other == PROTO_NONE) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Set.prototype.difference: argument must be a Set-like object"));
        return PROTO_NONE;
    }
    const proto::ProtoObject* result = makeEmptySet(ctx);
    if (!result || result == PROTO_NONE) return PROTO_NONE;
    const proto::ProtoSparseList* order = getSetOrder(ctx, self);
    if (!order) return result;
    const proto::ProtoSparseListIterator* it = order->getIterator(ctx);
    while (it && it->hasNext(ctx)) {
        const proto::ProtoObject* v = it->nextValue(ctx);
        it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
        if (!v) v = PROTO_NONE;
        if (!setContains(ctx, other, v))
            setAddValue(ctx, result, v);
    }
    return result;
}

static const proto::ProtoObject* setSymmetricDifference(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    if (!requireSetThis(ctx, self)) return PROTO_NONE;
    const proto::ProtoObject* other = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0) : PROTO_NONE;
    if (!other || other == PROTO_NONE) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Set.prototype.symmetricDifference: argument must be a Set-like object"));
        return PROTO_NONE;
    }
    const proto::ProtoObject* result = makeEmptySet(ctx);
    if (!result || result == PROTO_NONE) return PROTO_NONE;
    // Elements in self but not in other.
    {
        const proto::ProtoSparseList* order = getSetOrder(ctx, self);
        if (order) {
            const proto::ProtoSparseListIterator* it = order->getIterator(ctx);
            while (it && it->hasNext(ctx)) {
                const proto::ProtoObject* v = it->nextValue(ctx);
                it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
                if (!v) v = PROTO_NONE;
                if (!setContains(ctx, other, v))
                    setAddValue(ctx, result, v);
            }
        }
    }
    // Elements in other but not in self.
    {
        const proto::ProtoSparseList* order = getSetOrder(ctx, other);
        if (order) {
            const proto::ProtoSparseListIterator* it = order->getIterator(ctx);
            while (it && it->hasNext(ctx)) {
                const proto::ProtoObject* v = it->nextValue(ctx);
                it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
                if (!v) v = PROTO_NONE;
                if (!setContains(ctx, self, v))
                    setAddValue(ctx, result, v);
            }
        }
    }
    return result;
}

static const proto::ProtoObject* setIsSubsetOf(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    if (!requireSetThis(ctx, self)) return PROTO_NONE;
    const proto::ProtoObject* other = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0) : PROTO_NONE;
    if (!other || other == PROTO_NONE) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Set.prototype.isSubsetOf: argument must be a Set-like object"));
        return PROTO_NONE;
    }
    const proto::ProtoSparseList* order = getSetOrder(ctx, self);
    if (!order) return PROTO_TRUE;
    const proto::ProtoSparseListIterator* it = order->getIterator(ctx);
    while (it && it->hasNext(ctx)) {
        const proto::ProtoObject* v = it->nextValue(ctx);
        it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
        if (!v) v = PROTO_NONE;
        if (!setContains(ctx, other, v)) return PROTO_FALSE;
    }
    return PROTO_TRUE;
}

static const proto::ProtoObject* setIsSupersetOf(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    if (!requireSetThis(ctx, self)) return PROTO_NONE;
    const proto::ProtoObject* other = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0) : PROTO_NONE;
    if (!other || other == PROTO_NONE) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Set.prototype.isSupersetOf: argument must be a Set-like object"));
        return PROTO_NONE;
    }
    const proto::ProtoSparseList* order = getSetOrder(ctx, other);
    if (!order) return PROTO_TRUE;
    const proto::ProtoSparseListIterator* it = order->getIterator(ctx);
    while (it && it->hasNext(ctx)) {
        const proto::ProtoObject* v = it->nextValue(ctx);
        it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
        if (!v) v = PROTO_NONE;
        if (!setContains(ctx, self, v)) return PROTO_FALSE;
    }
    return PROTO_TRUE;
}

static const proto::ProtoObject* setIsDisjointFrom(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    if (!requireSetThis(ctx, self)) return PROTO_NONE;
    const proto::ProtoObject* other = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0) : PROTO_NONE;
    if (!other || other == PROTO_NONE) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Set.prototype.isDisjointFrom: argument must be a Set-like object"));
        return PROTO_NONE;
    }
    const proto::ProtoSparseList* order = getSetOrder(ctx, self);
    if (!order) return PROTO_TRUE;
    const proto::ProtoSparseListIterator* it = order->getIterator(ctx);
    while (it && it->hasNext(ctx)) {
        const proto::ProtoObject* v = it->nextValue(ctx);
        it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
        if (!v) v = PROTO_NONE;
        if (setContains(ctx, other, v)) return PROTO_FALSE;
    }
    return PROTO_TRUE;
}

} // anonymous namespace

// ---------------------------------------------------------------------------
// BuildSetPrototype
// ---------------------------------------------------------------------------
void BuildSetPrototype(proto::ProtoSpace* space, proto::ProtoContext* ctx,
                       const proto::ProtoObject* objectProto)
{
    if (!space || !ctx || !objectProto) return;

    const proto::ProtoObject* setProto = objectProto->newChild(ctx, true);
    if (!setProto) return;

    setProto = installNonEnumerableMethod(ctx, setProto, "add",     setAdd,      1);
    setProto = installNonEnumerableMethod(ctx, setProto, "has",     setHas,      1);
    setProto = installNonEnumerableMethod(ctx, setProto, "delete",  setDeleteFn, 1);
    setProto = installNonEnumerableMethod(ctx, setProto, "clear",   setClear,    0);
    setProto = installNonEnumerableMethod(ctx, setProto, "forEach", setForEach,  1);
    setProto = installNonEnumerableMethod(ctx, setProto, "values",  setValues,   0);
    setProto = installNonEnumerableMethod(ctx, setProto, "keys",    setKeys,     0);
    setProto = installNonEnumerableMethod(ctx, setProto, "entries", setEntries,  0);
    // ES2025 collection methods
    setProto = installNonEnumerableMethod(ctx, setProto, "union",               setUnion,               1);
    setProto = installNonEnumerableMethod(ctx, setProto, "intersection",        setIntersection,        1);
    setProto = installNonEnumerableMethod(ctx, setProto, "difference",          setDifference,          1);
    setProto = installNonEnumerableMethod(ctx, setProto, "symmetricDifference", setSymmetricDifference, 1);
    setProto = installNonEnumerableMethod(ctx, setProto, "isSubsetOf",          setIsSubsetOf,          1);
    setProto = installNonEnumerableMethod(ctx, setProto, "isSupersetOf",        setIsSupersetOf,        1);
    setProto = installNonEnumerableMethod(ctx, setProto, "isDisjointFrom",      setIsDisjointFrom,      1);

    // Symbol.iterator = values (Set iterates values)
    {
        const proto::ProtoString* symIterKey = JSSymbols::symbolIterator(ctx);
        if (symIterKey) {
            const proto::ProtoObject* valuesKeyObj = ctx->fromUTF8String("values");
            const proto::ProtoString* valuesKey = valuesKeyObj ? valuesKeyObj->asString(ctx) : nullptr;
            const proto::ProtoObject* valuesFn = valuesKey
                ? setProto->getAttribute(ctx, valuesKey, false) : nullptr;
            if (valuesFn && valuesFn != PROTO_NONE) {
                setProto = setProto->setAttribute(ctx, symIterKey, valuesFn);
                const proto::ProtoObject* pdko = ctx->fromUTF8String("__pd_Symbol.iterator__");
                const proto::ProtoString* pdks = pdko ? pdko->asString(ctx) : nullptr;
                if (pdks) setProto = setProto->setAttribute(ctx, pdks, ctx->fromInteger(0x3LL));
            }
        }
    }

    // Symbol.toStringTag = "Set": {writable:false, enumerable:false, configurable:true}
    // bit1=configurable=true → bits = 0x2
    {
        const proto::ProtoString* tstKey = JSSymbols::toStringTag(ctx);
        if (tstKey) {
            setProto = setProto->setAttribute(ctx, tstKey, ctx->fromUTF8String("Set"));
            const proto::ProtoObject* pdko = ctx->fromUTF8String("__pd___toStringTag____");
            const proto::ProtoString* pdks = pdko ? pdko->asString(ctx) : nullptr;
            if (pdks) setProto = setProto->setAttribute(ctx, pdks, ctx->fromInteger(0x2LL));
        }
    }

    // size getter via __get_size__
    {
        const proto::ProtoObject* gko = ctx->fromUTF8String("__get_size__");
        const proto::ProtoString* gks = gko ? gko->asString(ctx) : nullptr;
        if (gks) {
            const proto::ProtoObject* getter = ctx->fromMethod(nullptr, setSizeGetter);
            if (getter) setProto = setProto->setAttribute(ctx, gks, getter);
        }
    }

    s_setPrototype = setProto;
}

// ---------------------------------------------------------------------------
// ensureSetConstructor
// ---------------------------------------------------------------------------
void ensureSetConstructor(proto::ProtoContext* ctx,
                          const proto::ProtoObject** globalRoot)
{
    if (!ctx || !globalRoot || !*globalRoot) return;

    const proto::ProtoObject* ko = ctx->fromUTF8String("Set");
    const proto::ProtoString* ks = ko ? ko->asString(ctx) : nullptr;
    if (!ks) return;

    const proto::ProtoObject* existing = (*globalRoot)->getAttribute(ctx, ks, false);
    if (existing && existing != PROTO_NONE) return;

    const proto::ProtoObject* ctorParent =
        (ctx->space && ctx->space->methodPrototype) ? ctx->space->methodPrototype : nullptr;
    const proto::ProtoObject* ctor = ctorParent
        ? ctorParent->newChild(ctx, true)
        : ctx->newObject(true);
    if (!ctor) return;

    const proto::ProtoString* nameKey = JSSymbols::name(ctx);
    if (nameKey) ctor = ctor->setAttribute(ctx, nameKey, ctx->fromUTF8String("Set"));

    const proto::ProtoString* protoKey = JSSymbols::prototype(ctx);
    if (protoKey && s_setPrototype)
        ctor = ctor->setAttribute(ctx, protoKey, s_setPrototype);

    const proto::ProtoObject* constructKey = ctx->fromUTF8String("__construct__");
    const proto::ProtoString* constructKs  = constructKey ? constructKey->asString(ctx) : nullptr;
    if (constructKs) {
        const proto::ProtoObject* constructFn = ctx->fromMethod(nullptr, setConstruct);
        if (constructFn) ctor = ctor->setAttribute(ctx, constructKs, constructFn);
    }

    *globalRoot = (*globalRoot)->setAttribute(ctx, ks, ctor);
}

} // namespace protojs
