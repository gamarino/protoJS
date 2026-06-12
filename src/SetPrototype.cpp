#include "SetPrototype.h"
#include "ArrayPrototype.h"
#include "ArrayElementsStorage.h"
#include "IteratorPrototype.h"
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
    val = normalizeSetVal(ctx, val);
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

// setLikeHas — membership test for the seven Set collection methods'
// `other` argument. When other is a real native Set, use the fast
// path (setContains via the __set_core__ slot). Otherwise consult
// the spec's Set-like protocol by calling other.has(val).
// getSetRecord has already validated other.has is callable, so the
// call here is safe; we only have to coerce the result to a bool.
// Pre-fix the set ops only consulted setContains, which always
// returned false for any non-native Set, so passing a Set-like
// {size, has, keys} object produced empty (intersection) / full
// (difference) / wrong (disjoint/subset) results.
static bool setLikeHas(proto::ProtoContext* ctx,
                       const proto::ProtoObject* other,
                       const proto::ProtoObject* val)
{
    if (getSetOrder(ctx, other)) return setContains(ctx, other, val);
    const proto::ProtoObject* hasKo = ctx->fromUTF8String("has");
    const proto::ProtoString* hasKs = hasKo ? hasKo->asString(ctx) : nullptr;
    if (!hasKs) return false;
    const proto::ProtoObject* hasFn = other->getAttribute(ctx, hasKs, true);
    if (!hasFn || hasFn == PROTO_NONE) return false;
    const proto::ProtoList* a = ctx->newList();
    a = a->appendLast(ctx, val ? val : PROTO_NONE);
    const proto::ProtoObject* r = callJSFunction(ctx, hasFn, other, a);
    if (hasCallException()) return false;
    if (r == PROTO_TRUE) return true;
    if (r == PROTO_FALSE) return false;
    if (!r || r == PROTO_NONE || r == getUndefinedSentinel()) return false;
    if (r->isBoolean(ctx)) return r->asBoolean(ctx);
    // Truthy coercion fallback — non-empty string / non-zero number.
    if (r->isInteger(ctx)) return r->asLong(ctx) != 0;
    if (r->isDouble(ctx) || r->isFloat(ctx)) {
        double d = r->asDouble(ctx);
        return d != 0.0 && !std::isnan(d);
    }
    if (r->isString(ctx)) {
        std::string s;
        r->asString(ctx)->toUTF8String(ctx, s);
        return !s.empty();
    }
    return true;  // any other object → truthy
}

// iterateSetLikeKeys — drive the Set-like iterator protocol over
// other.keys() and invoke `emit(value)` for each yielded value.
// Returns false on abrupt completion. Pre-fix Set ops only iterated
// real native Sets through __set_order__, so union / symmetricDifference
// / isSupersetOf produced incomplete results for a class-style or
// plain Set-like.
template <typename Emit>
static bool iterateSetLikeKeys(proto::ProtoContext* ctx,
                                const proto::ProtoObject* other,
                                Emit emit)
{
    // Real native Sets — fast path over __set_order__.
    if (const proto::ProtoSparseList* order = getSetOrder(ctx, other)) {
        const proto::ProtoSparseListIterator* it = order->getIterator(ctx);
        while (it && it->hasNext(ctx)) {
            const proto::ProtoObject* v = it->nextValue(ctx);
            it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
            if (!emit(v ? v : PROTO_NONE)) return false;
        }
        return true;
    }
    // Set-like: call other.keys() to obtain an iterator, then loop
    // calling .next() and reading the {value, done} record.
    const proto::ProtoObject* keysKo = ctx->fromUTF8String("keys");
    const proto::ProtoString* keysKs = keysKo ? keysKo->asString(ctx) : nullptr;
    if (!keysKs) return true;
    const proto::ProtoObject* keysFn = other->getAttribute(ctx, keysKs, true);
    if (!keysFn || keysFn == PROTO_NONE) return true;
    const proto::ProtoObject* iter = callJSFunction(ctx, keysFn, other, ctx->newList());
    if (hasCallException()) return false;
    if (!iter || iter == PROTO_NONE) return true;
    const proto::ProtoObject* nextKo = ctx->fromUTF8String("next");
    const proto::ProtoString* nextKs = nextKo ? nextKo->asString(ctx) : nullptr;
    if (!nextKs) return true;
    const proto::ProtoString* valueKs = JSSymbols::value(ctx);
    const proto::ProtoString* doneKs  = JSSymbols::done(ctx);
    for (int safety = 0; safety < 1000000; ++safety) {
        const proto::ProtoObject* nextFn = iter->getAttribute(ctx, nextKs, true);
        if (!nextFn || nextFn == PROTO_NONE) break;
        const proto::ProtoObject* step = callJSFunction(ctx, nextFn, iter, ctx->newList());
        if (hasCallException()) return false;
        if (!step || step == PROTO_NONE) break;
        const proto::ProtoObject* d = doneKs ? step->getAttribute(ctx, doneKs, false) : nullptr;
        bool isDone = (d == PROTO_TRUE);
        if (!isDone && d && d != PROTO_NONE && d->isBoolean(ctx) && d->asBoolean(ctx)) isDone = true;
        if (isDone) break;
        const proto::ProtoObject* val = valueKs ? step->getAttribute(ctx, valueKs, false) : nullptr;
        // §24.2.1.2 (GetSetRecord-driven loops) step 7.b.ii: if nextValue
        // is -0𝔽, set nextValue to +0𝔽 BEFORE handing it to the operation.
        // Without this, difference/intersection/etc. treat the set-like
        // key "-0" as distinct from the receiver's stored "+0".
        if (val) val = normalizeSetVal(ctx, val);
        if (!emit(val ? val : PROTO_NONE)) return false;
    }
    return true;
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
    if (order) {
        // Pick max(slot)+1, not size — ProtoSparseList::removeAt
        // leaves holes after Set.delete, so 'size' may already be
        // occupied. Pre-fix `add` after a `delete` of a middle entry
        // wiped the entry that previously sat at slot `size`.
        unsigned long newIdx = 0;
        bool hasAny = false;
        const proto::ProtoSparseListIterator* it = order->getIterator(ctx);
        while (it && it->hasNext(ctx)) {
            unsigned long slot = it->nextKey(ctx);
            (void)it->nextValue(ctx);
            it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
            if (!hasAny || slot >= newIdx) newIdx = slot + 1;
            hasAny = true;
        }
        if (!hasAny) newIdx = static_cast<unsigned long>(sz);
        setSetOrderInPlace(ctx, self, order->setAt(ctx, newIdx, val));
    }
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
// get Set[Symbol.species] — returns `this` per §24.2.2.2.
// Used by ArraySpeciesCreate-style derivations in the spec; the
// presence of the descriptor is what 'built-ins/Set/Symbol.species/*'
// asserts.
static const proto::ProtoObject* setSpeciesGetter(
    proto::ProtoContext* /*ctx*/, const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*)
{
    return self;
}

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
    const proto::ProtoObject* callback = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0) : PROTO_NONE;
    // ECMA-262 §24.2.3.6 step 4: if IsCallable(callbackfn) is false,
    // throw TypeError. Pre-fix any non-callable value silently no-op'd
    // (boolean, number, null, undefined, string, plain object). The
    // test262 'Set/prototype/forEach/callback-not-callable-*.js' tests
    // each pass a different primitive and expect a TypeError throw.
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
                "Set.prototype.forEach callback is not callable"));
            return PROTO_NONE;
        }
    }
    const proto::ProtoObject* thisArg = (args->getSize(ctx) > 1) ? args->getAt(ctx, 1) : PROTO_NONE;
    if (!thisArg) thisArg = PROTO_NONE;

    // ECMA-262 §24.2.3.6 NOTE: "New values added after the call to
    // forEach begins are visited." A frozen iterator snapshot misses
    // them, so instead we walk the order sparse list by slot index
    // and on every step recompute the high-water mark of occupied
    // slots — additions always land at max(slot)+1 (see setAdd) so a
    // value inserted from inside the callback shows up as a fresh
    // slot above pos and the loop discovers it on the next iteration.
    // Deletions hole-punch the slot (ProtoSparseList::removeAt) so
    // order->has(pos) yields false and we skip them.
    unsigned long pos = 0;
    while (true) {
        const proto::ProtoSparseList* order = getSetOrder(ctx, self);
        if (!order) break;
        unsigned long highWater = 0;
        bool anyEntry = false;
        const proto::ProtoSparseListIterator* probe = order->getIterator(ctx);
        while (probe && probe->hasNext(ctx)) {
            unsigned long slot = probe->nextKey(ctx);
            (void)probe->nextValue(ctx);
            probe = const_cast<proto::ProtoSparseListIterator*>(probe)->advance(ctx);
            if (!anyEntry || slot >= highWater) highWater = slot + 1;
            anyEntry = true;
        }
        if (!anyEntry || pos >= highWater) break;
        if (!order->has(ctx, pos)) { ++pos; continue; }
        const proto::ProtoObject* v = order->getAt(ctx, pos);
        ++pos;
        if (!v) v = PROTO_NONE;
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

    // Sticky-done check: per ECMA-262 §24.2.5.2.1, once a Set iterator
    // has yielded {done: true} it MUST keep doing so even if entries
    // are later added. Pre-fix the iterator only tracked __iter_idx__
    // and would happily resume yielding new entries past the original
    // 'done' return — set.add(4) after iteration completes would be
    // visited on the next call.
    const proto::ProtoObject* doneKo = ctx->fromUTF8String("__iter_done__");
    const proto::ProtoString* doneKs = doneKo ? doneKo->asString(ctx) : nullptr;
    if (doneKs) {
        const proto::ProtoObject* d = self->getAttribute(ctx, doneKs, false);
        if (d == PROTO_TRUE) return makeDone();
    }

    auto markDone = [&]() -> const proto::ProtoObject* {
        if (doneKs) self->setAttribute(ctx, doneKs, PROTO_TRUE);
        return makeDone();
    };

    const proto::ProtoObject* setObj  = self->getAttribute(ctx, arrKey2, false);
    const proto::ProtoObject* posObj  = self->getAttribute(ctx, idxKey,  false);
    const proto::ProtoObject* kindObj = self->getAttribute(ctx, kindKey, false);
    if (!setObj || setObj == PROTO_NONE) return markDone();

    long long pos = (posObj && posObj != PROTO_NONE && posObj->isInteger(ctx))
                    ? posObj->asLong(ctx) : 0LL;
    std::string kind = "values";
    if (kindObj && kindObj != PROTO_NONE && kindObj->isString(ctx)) {
        const proto::ProtoString* ks2 = kindObj->asString(ctx);
        if (ks2) ks2->toUTF8String(ctx, kind);
    }

    const proto::ProtoSparseList* order = getSetOrder(ctx, setObj);
    if (!order) return markDone();

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
            // Set entries: [value, value] — build a real array via
            // __elements__ and the PROTO_TRUE isArray singleton (see
            // MapPrototype::mapIteratorNext for the rationale).
            const proto::ProtoObject* pair = createNewArray(ctx, nullptr);
            const proto::ProtoString* lk = JSSymbols::length(ctx);
            const proto::ProtoString* ia = JSSymbols::isArray(ctx);
            const proto::ProtoList* pairEls = ctx->newList();
            pairEls = pairEls->appendLast(ctx, v);
            pairEls = pairEls->appendLast(ctx, v);
            setArrayElements(ctx, pair, pairEls);
            if (lk) pair = pair->setAttribute(ctx, lk, ctx->fromInteger(2LL));
            if (ia) pair = pair->setAttribute(ctx, ia, PROTO_TRUE);
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
    return markDone();
}

// %SetIteratorPrototype% per §24.2.5.2 — shared with @@toStringTag =
// "Set Iterator" and a shared next, chained to %IteratorPrototype%.
static const proto::ProtoObject* s_setIteratorProto = nullptr;

static const proto::ProtoObject* getSetIteratorProto(proto::ProtoContext* ctx) {
    if (s_setIteratorProto) return s_setIteratorProto;
    const proto::ProtoObject* iterProto = protojs::getIteratorPrototype(ctx);
    const proto::ProtoObject* parent = iterProto ? iterProto
        : (ctx->space ? ctx->space->objectPrototype : nullptr);
    const proto::ProtoObject* proto = parent
        ? parent->newChild(ctx, true) : ctx->newObject(true);
    if (!proto) return nullptr;

    const proto::ProtoString* nextKey = JSSymbols::next(ctx);
    if (nextKey && ctx->space && ctx->space->methodPrototype) {
        const proto::ProtoObject* wrapper =
            ctx->space->methodPrototype->newChild(ctx, true);
        if (wrapper) {
            const proto::ProtoString* nfk = JSSymbols::nativeFn(ctx);
            if (nfk) wrapper = wrapper->setAttribute(ctx, nfk,
                ctx->fromMethod(nullptr, setIteratorNext));
            const proto::ProtoString* lk = JSSymbols::length(ctx);
            if (lk) {
                wrapper = wrapper->setAttribute(ctx, lk, ctx->fromInteger(0LL));
                const proto::ProtoString* pdlk = JSSymbols::pdLength(ctx);
                if (pdlk) wrapper = wrapper->setAttribute(ctx, pdlk, ctx->fromInteger(0x2LL));
            }
            const proto::ProtoString* nk = JSSymbols::name(ctx);
            if (nk) {
                wrapper = wrapper->setAttribute(ctx, nk, ctx->fromUTF8String("next"));
                const proto::ProtoString* pdnk = JSSymbols::pdName(ctx);
                if (pdnk) wrapper = wrapper->setAttribute(ctx, pdnk, ctx->fromInteger(0x2LL));
            }
            proto = proto->setAttribute(ctx, nextKey, wrapper);
            const proto::ProtoObject* pdno = ctx->fromUTF8String("__pd_next__");
            const proto::ProtoString* pdnk = pdno ? pdno->asString(ctx) : nullptr;
            if (pdnk) proto = proto->setAttribute(ctx, pdnk, ctx->fromInteger(0x3LL));
        }
    }

    const proto::ProtoString* tagUser = JSSymbols::symbolToStringTag(ctx);
    if (tagUser) {
        proto = proto->setAttribute(ctx, tagUser,
            ctx->fromUTF8String("Set Iterator"));
        const proto::ProtoObject* pdo = ctx->fromUTF8String("__pd_Symbol.toStringTag__");
        const proto::ProtoString* pdk = pdo ? pdo->asString(ctx) : nullptr;
        if (pdk) proto = proto->setAttribute(ctx, pdk, ctx->fromInteger(0x2LL));
    }
    const proto::ProtoString* hnwK = JSSymbols::hasNonWritableProps(ctx);
    if (hnwK) proto = proto->setAttribute(ctx, hnwK, PROTO_TRUE);

    s_setIteratorProto = proto;
    return proto;
}

static const proto::ProtoObject* makeSetIterator(
    proto::ProtoContext* ctx, const proto::ProtoObject* setObj, const char* kind)
{
    const proto::ProtoObject* protoParent = getSetIteratorProto(ctx);
    const proto::ProtoObject* iter = protoParent
        ? protoParent->newChild(ctx, true) : ctx->newObject(true);
    const proto::ProtoString* idxKey  = JSSymbols::iterIdx(ctx);
    const proto::ProtoString* arrKey2 = JSSymbols::iterArr(ctx);
    const proto::ProtoString* kindKey = JSSymbols::iterKind(ctx);
    if (idxKey)  iter = iter->setAttribute(ctx, idxKey,  ctx->fromInteger(0LL));
    if (arrKey2) iter = iter->setAttribute(ctx, arrKey2, setObj ? setObj : PROTO_NONE);
    if (kindKey) iter = iter->setAttribute(ctx, kindKey, ctx->fromUTF8String(kind));
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
        // Per ECMA-262 §24.2.1.1 step 5: if iterable is null/undefined,
        // skip iteration entirely. Primitives (number / boolean) are
        // not iterable → TypeError. Strings ARE iterable per code unit
        // (handled via the length+charAt path below).
        if (iterable == getUndefinedSentinel() || iterable == getNullSentinel()) {
            return self;
        }
        // §24.2.1.1 step 7.a-c: when iterable is present, Get(set, "add")
        // and throw TypeError if IsCallable(adder) is false. Pre-fix the
        // constructor went straight to its internal fast path so users
        // assigning Set.prototype.add = null saw the iterable silently
        // ingested without the throw the spec mandates. The check has
        // to happen before any iteration begins.
        const proto::ProtoObject* adder = PROTO_NONE;
        {
            const proto::ProtoObject* addKo = ctx->fromUTF8String("add");
            const proto::ProtoString* addKs = addKo ? addKo->asString(ctx) : nullptr;
            // §24.2.1.1 step 7.a: GetMethod fires accessor getters
            // and propagates their abrupt completion.  Probe the
            // `__get_add__` accessor sidecar BEFORE falling back to
            // the data slot — pre-fix only the data slot was read,
            // so `Object.defineProperty(Set.prototype, 'add', {get: throws})`
            // silently surfaced PROTO_NONE and the constructor threw
            // its own "is not callable" TypeError instead of the
            // accessor's abrupt (test262 set-get-add-method-failure).
            {
                const proto::ProtoObject* gko = ctx->fromUTF8String("__get_add__");
                const proto::ProtoString* gks = gko ? gko->asString(ctx) : nullptr;
                if (gks) {
                    const proto::ProtoObject* getter = self->getAttribute(ctx, gks, true);
                    if (getter && getter != PROTO_NONE) {
                        adder = callJSFunction(ctx, getter, self, ctx->newList());
                        if (hasCallException()) return PROTO_NONE;
                    }
                }
            }
            if (!adder || adder == PROTO_NONE) {
                adder = addKs
                    ? self->getAttribute(ctx, addKs, true) : PROTO_NONE;
            }
            bool callable = false;
            if (adder && adder != PROTO_NONE && adder != getUndefinedSentinel()) {
                if (adder->isMethod(ctx)) callable = true;
                const proto::ProtoString* bcK = JSSymbols::bytecodeId(ctx);
                if (!callable && bcK && adder->hasAttribute(ctx, bcK) == PROTO_TRUE) callable = true;
                const proto::ProtoString* nfK = JSSymbols::nativeFn(ctx);
                if (!callable && nfK && adder->hasAttribute(ctx, nfK) == PROTO_TRUE) callable = true;
                const proto::ProtoString* bfK = JSSymbols::boundFn(ctx);
                if (!callable && bfK && adder->hasAttribute(ctx, bfK) == PROTO_TRUE) callable = true;
            }
            if (!callable) {
                signalNativeException(makeNativeError(ctx, "TypeError",
                    "Set: 'add' is not callable"));
                return PROTO_NONE;
            }
        }
        if (iterable && (iterable->isInteger(ctx) || iterable->isDouble(ctx)
                         || iterable->isFloat(ctx) || iterable->isBoolean(ctx))) {
            signalNativeException(makeNativeError(ctx, "TypeError",
                "is not iterable"));
            return PROTO_NONE;
        }
        // §24.2.1.1 step 6 + GetIterator: explicit @@iterator =
        // undefined / null is a TypeError, not a silent skip.
        if (iterable && iterable != PROTO_NONE && !iterable->isString(ctx)) {
            const proto::ProtoObject* ito = ctx->fromUTF8String("Symbol.iterator");
            const proto::ProtoString* its = ito ? ito->asString(ctx) : nullptr;
            if (its && iterable->hasOwnAttribute(ctx, its) == PROTO_TRUE) {
                const proto::ProtoObject* fn = iterable->getAttribute(ctx, its, false);
                if (fn == getUndefinedSentinel() || fn == getNullSentinel()) {
                    signalNativeException(makeNativeError(ctx, "TypeError",
                        "Set iterable's @@iterator is not callable"));
                    return PROTO_NONE;
                }
            }
        }
        if (iterable && iterable != PROTO_NONE) {
            // Strings iterate per code unit: read length from the
            // ProtoString and emit one single-char string per step.
            if (iterable->isString(ctx)) {
                if (const proto::ProtoString* ps = iterable->asString(ctx)) {
                    std::string utf8;
                    ps->toUTF8String(ctx, utf8);
                    size_t i = 0;
                    while (i < utf8.size()) {
                        unsigned char c = static_cast<unsigned char>(utf8[i]);
                        size_t len = (c < 0x80) ? 1 : (c < 0xE0) ? 2
                                  : (c < 0xF0) ? 3 : 4;
                        if (i + len > utf8.size()) break;
                        std::string single = utf8.substr(i, len);
                        const proto::ProtoObject* val =
                            ctx->fromUTF8String(single.c_str());
                        val = normalizeSetVal(ctx, val);
                        if (!setContains(ctx, self, val)) {
                            const proto::ProtoSet* core = getSetCore(ctx, self);
                            const proto::ProtoSparseList* order = getSetOrder(ctx, self);
                            long sz = getSetSize(ctx, self);
                            if (core) setSetCoreInPlace(ctx, self, core->add(ctx, val));
                            if (order) setSetOrderInPlace(ctx, self,
                                order->setAt(ctx, static_cast<unsigned long>(sz), val));
                            setSetSizeInPlace(ctx, self, sz + 1);
                        }
                        i += len;
                    }
                }
                return self;
            }
            // Element read: prefer the __elements__ native storage for
            // real arrays.  Pre-fix the constructor only read via
            // getAttribute(indexKey, true), so `new Set([1,2,3])`
            // silently produced an empty set — arrays now keep their
            // elements in __elements__, and getAttribute(\"0\") returns
            // nullptr.
            auto readIndex = [&](long i) -> const proto::ProtoObject* {
                const proto::ProtoObject* v =
                    arrayTryFastGet(ctx, iterable, static_cast<unsigned long>(i));
                if (v) return v;
                const proto::ProtoString* ik = JSSymbols::indexKey(ctx, static_cast<uint32_t>(i));
                v = ik ? iterable->getAttribute(ctx, ik, true) : nullptr;
                return v ? v : PROTO_NONE;
            };
            const proto::ProtoString* lenKs = JSSymbols::length(ctx);
            long len = -1;
            if (lenKs) {
                const proto::ProtoObject* lenObj = iterable->getAttribute(ctx, lenKs, true);
                if (lenObj && lenObj != PROTO_NONE && lenObj->isInteger(ctx))
                    len = lenObj->asLong(ctx);
            }
            // If __elements__ exists but .length attribute doesn't (some
            // built-ins set elements without writing length), fall back
            // to the elements list size.
            if (len < 0) {
                const proto::ProtoList* els = getArrayElements(ctx, iterable);
                if (els) len = static_cast<long>(els->getSize(ctx));
            }
            for (long i = 0; i < len; i++) {
                const proto::ProtoObject* val = readIndex(i);
                // §24.2.1.1 step 9.f: Call(adder, set, [nextValue]) —
                // dispatch through the resolved adder so user overrides
                // of Set.prototype.add are observable (test262 set-
                // iterable-calls-add).
                const proto::ProtoList* addArgs = ctx->newList();
                addArgs = addArgs->appendLast(ctx, val);
                callJSFunction(ctx, adder, self, addArgs);
                if (hasCallException()) return PROTO_NONE;
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
// ES2025 GetSetRecord(obj) validator (spec §24.2.1.2).
//
// Per ECMA-262, every Set.prototype.{union, intersection, difference,
// symmetricDifference, isSubsetOf, isSupersetOf, isDisjointFrom}
// begins with GetSetRecord(other) which throws when:
//   - other is not an Object             -> TypeError
//   - other.size is not Number / NaN     -> TypeError
//   - ToIntegerOrInfinity(size) < 0      -> RangeError
//   - other.has is not callable          -> TypeError
//   - other.keys is not callable         -> TypeError
//
// We perform only the validation here; the existing fast paths that
// follow continue to use getSetOrder() against a real native Set, so
// passing a real Set is unchanged. Failing the validation throws the
// proper error type, fixing the test262 cases under
// built-ins/Set/prototype/<method>/{array-throws, called-with-object,
// has-is-callable, keys-is-callable, size-is-a-number,
// require-internal-slot, builtins}.js — applied to all 7 methods.
// ---------------------------------------------------------------------------
static bool isCallableValue(proto::ProtoContext* ctx,
                            const proto::ProtoObject* v)
{
    if (!v || v == PROTO_NONE || v == getUndefinedSentinel()) return false;
    if (v->isMethod(ctx)) return true;
    const proto::ProtoString* bcK = JSSymbols::bytecodeId(ctx);
    if (bcK && v->hasAttribute(ctx, bcK) == PROTO_TRUE) return true;
    const proto::ProtoString* nfK = JSSymbols::nativeFn(ctx);
    if (nfK && v->hasAttribute(ctx, nfK) == PROTO_TRUE) return true;
    const proto::ProtoString* bfK = JSSymbols::boundFn(ctx);
    if (bfK && v->hasAttribute(ctx, bfK) == PROTO_TRUE) return true;
    return false;
}

static bool getSetRecord(proto::ProtoContext* ctx,
                         const proto::ProtoObject* obj,
                         const char* methodName,
                         double* outSize = nullptr)
{
    if (!obj || obj == PROTO_NONE || obj == getUndefinedSentinel() ||
        obj == PROTO_TRUE || obj == PROTO_FALSE ||
        obj->isInteger(ctx) || obj->isDouble(ctx) || obj->isFloat(ctx) ||
        obj->isString(ctx)) {
        std::string msg = std::string("Set.prototype.") + methodName +
            ": argument must be an Object";
        signalNativeException(makeNativeError(ctx, "TypeError", msg.c_str()));
        return false;
    }
    // Real native Sets store .size behind the __get_size__ accessor;
    // looking it up via the public "size" key returns nothing. Treat
    // the presence of a __set_order__ slot as proof that obj is a Set
    // and skip the rest of the Set-like protocol validation. The spec
    // (§24.2.1.2 GetSetRecord) would otherwise call the size getter
    // via [[Get]], but for our native Sets the receiver invariants are
    // already satisfied.
    if (getSetOrder(ctx, obj)) {
        if (outSize) *outSize = static_cast<double>(getSetSize(ctx, obj));
        return true;
    }
    const proto::ProtoObject* sizeKo = ctx->fromUTF8String("size");
    const proto::ProtoString* sizeKs = sizeKo ? sizeKo->asString(ctx) : nullptr;
    const proto::ProtoObject* sizeV = sizeKs
        ? obj->getAttribute(ctx, sizeKs, true) : PROTO_NONE;
    // Class-defined .size getters install under __get_size__ on the
    // prototype with the undefined sentinel as the data placeholder.
    // Pre-fix the validator only consulted the data slot and rejected
    // any class-style Set-like with "argument lacks a numeric .size".
    if (!sizeV || sizeV == PROTO_NONE || sizeV == getUndefinedSentinel()) {
        const proto::ProtoObject* gko = ctx->fromUTF8String("__get_size__");
        const proto::ProtoString* gks = gko ? gko->asString(ctx) : nullptr;
        if (gks) {
            const proto::ProtoObject* getter = obj->getAttribute(ctx, gks, true);
            if (getter && getter != PROTO_NONE) {
                sizeV = callJSFunction(ctx, getter, obj, ctx->newList());
                if (hasCallException()) return false;
            }
        }
    }
    if (!sizeV || sizeV == PROTO_NONE || sizeV == getUndefinedSentinel()) {
        std::string msg = std::string("Set.prototype.") + methodName +
            ": argument lacks a numeric .size";
        signalNativeException(makeNativeError(ctx, "TypeError", msg.c_str()));
        return false;
    }
    // §24.2.1.2 step 3: ToNumber(rawSize) — invokes valueOf /
    // Symbol.toPrimitive on objects, rejects BigInt with TypeError,
    // coerces strings via parsing.  Pre-fix only int / double cells
    // were accepted (test262 difference/size-is-a-number variations).
    bool sizeOk = false;
    double sizeNum = 0.0;
    // BigInt → TypeError per ToNumber.
    {
        const proto::ProtoString* bigK = JSSymbols::isBigInt(ctx);
        if (bigK && sizeV->getAttribute(ctx, bigK, true) == PROTO_TRUE) {
            std::string msg = std::string("Set.prototype.") + methodName +
                ": cannot convert BigInt .size to Number";
            signalNativeException(makeNativeError(ctx, "TypeError", msg.c_str()));
            return false;
        }
    }
    if (sizeV->isInteger(ctx)) {
        sizeOk = true;
        sizeNum = static_cast<double>(sizeV->asLong(ctx));
    } else if (sizeV->isDouble(ctx) || sizeV->isFloat(ctx)) {
        sizeNum = sizeV->asDouble(ctx);
        if (!std::isnan(sizeNum)) sizeOk = true;
    } else {
        const proto::ProtoObject* numV = jsToNumber(ctx, sizeV);
        if (hasCallException()) return false;
        if (numV && numV != PROTO_NONE) {
            if (numV->isInteger(ctx)) {
                sizeOk = true;
                sizeNum = static_cast<double>(numV->asLong(ctx));
            } else if (numV->isDouble(ctx) || numV->isFloat(ctx)) {
                sizeNum = numV->asDouble(ctx);
                if (!std::isnan(sizeNum)) sizeOk = true;
            }
        }
    }
    if (!sizeOk) {
        std::string msg = std::string("Set.prototype.") + methodName +
            ": .size is not a number";
        signalNativeException(makeNativeError(ctx, "TypeError", msg.c_str()));
        return false;
    }
    if (sizeNum < 0.0) {
        std::string msg = std::string("Set.prototype.") + methodName +
            ": .size cannot be negative";
        signalNativeException(makeNativeError(ctx, "RangeError", msg.c_str()));
        return false;
    }
    if (outSize) *outSize = sizeNum;
    // Resolve .has — accessor descriptors install the getter at
    // __get_has__ with the undefined sentinel as the data placeholder.
    // Pre-fix the validator only consulted the data slot and rejected
    // any class-style Set-like whose .has is a getter.
    auto resolveAttr = [&](const char* key) -> const proto::ProtoObject* {
        const proto::ProtoObject* ko = ctx->fromUTF8String(key);
        const proto::ProtoString* ks = ko ? ko->asString(ctx) : nullptr;
        if (!ks) return PROTO_NONE;
        const proto::ProtoObject* v = obj->getAttribute(ctx, ks, true);
        if (!v || v == PROTO_NONE || v == getUndefinedSentinel()) {
            std::string gkStr = std::string("__get_") + key + "__";
            const proto::ProtoObject* gko = ctx->fromUTF8String(gkStr.c_str());
            const proto::ProtoString* gks = gko ? gko->asString(ctx) : nullptr;
            if (gks) {
                const proto::ProtoObject* getter = obj->getAttribute(ctx, gks, true);
                if (getter && getter != PROTO_NONE) {
                    v = callJSFunction(ctx, getter, obj, ctx->newList());
                    if (hasCallException()) return nullptr;
                }
            }
        }
        return v ? v : PROTO_NONE;
    };
    const proto::ProtoObject* hasV = resolveAttr("has");
    if (!hasV) return false;  // abrupt completion
    if (!isCallableValue(ctx, hasV)) {
        std::string msg = std::string("Set.prototype.") + methodName +
            ": .has is not callable";
        signalNativeException(makeNativeError(ctx, "TypeError", msg.c_str()));
        return false;
    }
    const proto::ProtoObject* keysV = resolveAttr("keys");
    if (!keysV) return false;
    if (!isCallableValue(ctx, keysV)) {
        std::string msg = std::string("Set.prototype.") + methodName +
            ": .keys is not callable";
        signalNativeException(makeNativeError(ctx, "TypeError", msg.c_str()));
        return false;
    }
    return true;
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
    if (!getSetRecord(ctx, other, "union")) return PROTO_NONE;
    const proto::ProtoObject* result = makeEmptySet(ctx);
    if (!result || result == PROTO_NONE) return PROTO_NONE;
    setAddAllFrom(ctx, result, self);
    // Iterate other via the Set-like keys() protocol so class-style
    // Set-likes contribute their elements. Real native Sets go through
    // the __set_order__ fast path inside iterateSetLikeKeys.
    bool ok = iterateSetLikeKeys(ctx, other,
        [&](const proto::ProtoObject* v) -> bool {
            setAddValue(ctx, result, v);
            return true;
        });
    if (!ok) return PROTO_NONE;
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
    if (!getSetRecord(ctx, other, "intersection")) return PROTO_NONE;
    const proto::ProtoObject* result = makeEmptySet(ctx);
    if (!result || result == PROTO_NONE) return PROTO_NONE;
    // Spec §24.2.3.10: when this.size > other.size, iterate other and
    // keep elements that self contains — preserving other's order in
    // the result. Pre-fix this always iterated self, so the result
    // ordering came from the larger collection instead of the smaller.
    long selfSize = getSetSize(ctx, self);
    long otherSize = -1;
    if (getSetOrder(ctx, other)) {
        otherSize = getSetSize(ctx, other);
    } else {
        const proto::ProtoObject* sizeKo = ctx->fromUTF8String("size");
        const proto::ProtoString* sizeKs = sizeKo ? sizeKo->asString(ctx) : nullptr;
        if (sizeKs) {
            const proto::ProtoObject* sv = other->getAttribute(ctx, sizeKs, true);
            if (!sv || sv == PROTO_NONE || sv == getUndefinedSentinel()) {
                const proto::ProtoObject* gko = ctx->fromUTF8String("__get_size__");
                const proto::ProtoString* gks = gko ? gko->asString(ctx) : nullptr;
                if (gks) {
                    const proto::ProtoObject* getter = other->getAttribute(ctx, gks, true);
                    if (getter && getter != PROTO_NONE) {
                        sv = callJSFunction(ctx, getter, other, ctx->newList());
                        if (hasCallException()) return PROTO_NONE;
                    }
                }
            }
            if (sv && sv->isInteger(ctx)) otherSize = sv->asLong(ctx);
            else if (sv && (sv->isDouble(ctx) || sv->isFloat(ctx))) {
                double d = sv->asDouble(ctx);
                if (!std::isnan(d) && d >= 0) otherSize = static_cast<long>(d);
            }
        }
    }
    if (otherSize >= 0 && selfSize > otherSize) {
        // Iterate other via the keys() protocol; add elements self has.
        bool ok = iterateSetLikeKeys(ctx, other,
            [&](const proto::ProtoObject* v) -> bool {
                if (setContains(ctx, self, v)) setAddValue(ctx, result, v);
                return true;
            });
        if (!ok) return PROTO_NONE;
        return result;
    }
    const proto::ProtoSparseList* order = getSetOrder(ctx, self);
    if (!order) return result;
    // Iterate self via iterator, add elements present in other.
    const proto::ProtoSparseListIterator* it = order->getIterator(ctx);
    while (it && it->hasNext(ctx)) {
        const proto::ProtoObject* v = it->nextValue(ctx);
        it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
        if (!v) v = PROTO_NONE;
        if (setLikeHas(ctx, other, v))
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
    double otherSize = 0.0;
    if (!getSetRecord(ctx, other, "difference", &otherSize)) return PROTO_NONE;
    const proto::ProtoObject* result = makeEmptySet(ctx);
    if (!result || result == PROTO_NONE) return PROTO_NONE;
    const proto::ProtoSparseList* order = getSetOrder(ctx, self);
    if (!order) return result;
    long thisSize = getSetSize(ctx, self);
    // §24.2.4.5 step 4: pick the cheaper path. If thisSize ≤ otherSize,
    // iterate self and probe other.has. Otherwise, start with a copy of
    // self and iterate other.keys() removing each value. This matters
    // for both performance AND observable side effects — the tests
    // assert other.has is NOT called when thisSize > otherSize.
    if (static_cast<double>(thisSize) <= otherSize) {
        const proto::ProtoSparseListIterator* it = order->getIterator(ctx);
        while (it && it->hasNext(ctx)) {
            const proto::ProtoObject* v = it->nextValue(ctx);
            it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
            if (!v) v = PROTO_NONE;
            if (!setLikeHas(ctx, other, v))
                setAddValue(ctx, result, v);
        }
        return result;
    }
    setAddAllFrom(ctx, result, self);
    bool ok = iterateSetLikeKeys(ctx, other,
        [&](const proto::ProtoObject* v) -> bool {
            if (setContains(ctx, result, v)) {
                const proto::ProtoList* delArgs = ctx->newList();
                delArgs = delArgs->appendLast(ctx, v);
                setDeleteFn(ctx, result, nullptr, delArgs, nullptr);
            }
            return true;
        });
    if (!ok) return PROTO_NONE;
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
    if (!getSetRecord(ctx, other, "symmetricDifference")) return PROTO_NONE;
    // §24.2.4.13: start with a copy of self, then iterate other's keys
    // and TOGGLE membership in the result.  Pre-fix the implementation
    // called other.has during the self-iteration, violating the
    // "should not invoke .has" assertion in the set-like-* tests.
    const proto::ProtoObject* result = makeEmptySet(ctx);
    if (!result || result == PROTO_NONE) return PROTO_NONE;
    {
        const proto::ProtoSparseList* order = getSetOrder(ctx, self);
        if (order) {
            const proto::ProtoSparseListIterator* it = order->getIterator(ctx);
            while (it && it->hasNext(ctx)) {
                const proto::ProtoObject* v = it->nextValue(ctx);
                it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
                if (!v) v = PROTO_NONE;
                setAddValue(ctx, result, v);
            }
        }
    }
    bool ok = iterateSetLikeKeys(ctx, other,
        [&](const proto::ProtoObject* v) -> bool {
            // Toggle: if result already has v, remove it; otherwise add.
            if (setContains(ctx, result, v)) {
                const proto::ProtoList* delArgs = ctx->newList();
                delArgs = delArgs->appendLast(ctx, v);
                setDeleteFn(ctx, result, nullptr, delArgs, nullptr);
            } else {
                setAddValue(ctx, result, v);
            }
            return true;
        });
    if (!ok) return PROTO_NONE;
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
    if (!getSetRecord(ctx, other, "isSubsetOf")) return PROTO_NONE;
    const proto::ProtoSparseList* order = getSetOrder(ctx, self);
    if (!order) return PROTO_TRUE;
    const proto::ProtoSparseListIterator* it = order->getIterator(ctx);
    while (it && it->hasNext(ctx)) {
        const proto::ProtoObject* v = it->nextValue(ctx);
        it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
        if (!v) v = PROTO_NONE;
        if (!setLikeHas(ctx, other, v)) return PROTO_FALSE;
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
    double otherSize = 0.0;
    if (!getSetRecord(ctx, other, "isSupersetOf", &otherSize)) return PROTO_NONE;
    // §24.2.4.7 step 3: if SetDataSize(this) < other.size, return false.
    // Pre-fix the impl iterated other.keys() unconditionally, calling
    // .keys before short-circuiting — the test set-like-array fixture
    // installs a throwing .keys to prove the early bail-out fires.
    long thisSize = getSetSize(ctx, self);
    if (static_cast<double>(thisSize) < otherSize) return PROTO_FALSE;
    // Iterate other's keys() via the Set-like protocol and bail with
    // false the first time self doesn't contain a value.
    bool isSuperset = true;
    bool ok = iterateSetLikeKeys(ctx, other,
        [&](const proto::ProtoObject* v) -> bool {
            if (!setContains(ctx, self, v)) { isSuperset = false; return false; }
            return true;
        });
    if (!ok && !isSuperset) return PROTO_FALSE;
    if (!ok) return PROTO_NONE;
    return isSuperset ? PROTO_TRUE : PROTO_FALSE;
}

static const proto::ProtoObject* setIsDisjointFrom(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*)
{
    if (!requireSetThis(ctx, self)) return PROTO_NONE;
    const proto::ProtoObject* other = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0) : PROTO_NONE;
    double otherSize = 0.0;
    if (!getSetRecord(ctx, other, "isDisjointFrom", &otherSize)) return PROTO_NONE;
    const proto::ProtoSparseList* order = getSetOrder(ctx, self);
    if (!order) return PROTO_TRUE;
    long thisSize = getSetSize(ctx, self);
    // §24.2.4.10 step 4: when this.size <= other.size, iterate self and
    // probe other.has; otherwise iterate other.keys() and probe
    // self.has. The branch matters because the tests assert other.has
    // is NOT called when this.size > other.size.
    if (static_cast<double>(thisSize) <= otherSize) {
        const proto::ProtoSparseListIterator* it = order->getIterator(ctx);
        while (it && it->hasNext(ctx)) {
            const proto::ProtoObject* v = it->nextValue(ctx);
            it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
            if (!v) v = PROTO_NONE;
            if (setLikeHas(ctx, other, v)) return PROTO_FALSE;
        }
        return PROTO_TRUE;
    }
    bool disjoint = true;
    bool ok = iterateSetLikeKeys(ctx, other,
        [&](const proto::ProtoObject* v) -> bool {
            if (setContains(ctx, self, v)) { disjoint = false; return false; }
            return true;
        });
    if (!ok && !disjoint == false) {
        // ok=false from abrupt completion; propagate.
        if (hasCallException()) return PROTO_NONE;
    }
    return disjoint ? PROTO_TRUE : PROTO_FALSE;
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
    // bit1=configurable=true → bits = 0x2.
    // Install under BOTH the internal sidecar (__toStringTag__, used by
    // Object.prototype.toString's tag probe) AND the user-visible key
    // ("Symbol.toStringTag", what `Set.prototype[Symbol.toStringTag]`
    // resolves to in this runtime). Pre-fix only the sidecar was set, so
    // the test262 'built-ins/Set/prototype/Symbol.toStringTag.js' check
    // (which reads via the user form) returned undefined.
    {
        const proto::ProtoString* tstKey = JSSymbols::toStringTag(ctx);
        if (tstKey) {
            setProto = setProto->setAttribute(ctx, tstKey, ctx->fromUTF8String("Set"));
            const proto::ProtoObject* pdko = ctx->fromUTF8String("__pd___toStringTag____");
            const proto::ProtoString* pdks = pdko ? pdko->asString(ctx) : nullptr;
            if (pdks) setProto = setProto->setAttribute(ctx, pdks, ctx->fromInteger(0x2LL));
        }
        const proto::ProtoString* userKey = JSSymbols::symbolToStringTag(ctx);
        if (userKey) {
            setProto = setProto->setAttribute(ctx, userKey, ctx->fromUTF8String("Set"));
            const proto::ProtoObject* pdko = ctx->fromUTF8String("__pd_Symbol.toStringTag__");
            const proto::ProtoString* pdks = pdko ? pdko->asString(ctx) : nullptr;
            if (pdks) setProto = setProto->setAttribute(ctx, pdks, ctx->fromInteger(0x2LL));
            // resolvePutFieldOOP gates writability on __has_nonwritable_props__
            // so `Set.prototype[@@toStringTag] = "X"` would otherwise silently
            // succeed (test262 Symbol.toStringTag verifyProperty fails).
            const proto::ProtoString* hnwK = JSSymbols::hasNonWritableProps(ctx);
            if (hnwK) setProto = setProto->setAttribute(ctx, hnwK, PROTO_TRUE);
        }
    }

    // size getter via __get_size__
    {
        const proto::ProtoObject* gko = ctx->fromUTF8String("__get_size__");
        const proto::ProtoString* gks = gko ? gko->asString(ctx) : nullptr;
        if (gks) {
            // The getter must be a real Function object with the
            // standard §17 .length and .name data properties so
            // tests like Object.getOwnPropertyDescriptor(Set.prototype,
            // "size").get.name === "get size" pass. Wrap via the same
            // methodPrototype chain installNonEnumerableMethod uses.
            const proto::ProtoObject* parent =
                (ctx->space && ctx->space->methodPrototype)
                ? ctx->space->methodPrototype : nullptr;
            const proto::ProtoObject* getter = parent
                ? parent->newChild(ctx, true) : ctx->newObject(true);
            if (getter) {
                const proto::ProtoString* nfKey = JSSymbols::nativeFn(ctx);
                if (nfKey) {
                    proto::ProtoObject* mGetter = const_cast<proto::ProtoObject*>(getter);
                    const proto::ProtoObject* raw = ctx->fromMethod(mGetter, setSizeGetter);
                    if (raw) getter = getter->setAttribute(ctx, nfKey, raw);
                }
                // length = 0  (§17 descriptor 0x2)
                const proto::ProtoString* lenKey = JSSymbols::length(ctx);
                if (lenKey) {
                    getter = getter->setAttribute(ctx, lenKey, ctx->fromInteger(0LL));
                    const proto::ProtoString* pdls = JSSymbols::pdLength(ctx);
                    if (pdls) getter = getter->setAttribute(ctx, pdls, ctx->fromInteger(0x2LL));
                }
                // name = "get size"  (§17 descriptor 0x2)
                const proto::ProtoString* nmKey = JSSymbols::name(ctx);
                if (nmKey) {
                    getter = getter->setAttribute(ctx, nmKey, ctx->fromUTF8String("get size"));
                    const proto::ProtoString* pdns = JSSymbols::pdName(ctx);
                    if (pdns) getter = getter->setAttribute(ctx, pdns, ctx->fromInteger(0x2LL));
                }
                // Hot-path hint mirroring the Round 12/13 sweep.
                const proto::ProtoString* hnwG = JSSymbols::hasNonWritableProps(ctx);
                if (hnwG) getter = getter->setAttribute(ctx, hnwG, PROTO_TRUE);
                setProto = setProto->setAttribute(ctx, gks, getter);
                // Read-path hint: stamp Set.prototype with
                // __has_accessor_props__ so mySet.size invokes the
                // getter (chain-inherited from Set.prototype).
                {
                    const proto::ProtoString* hap = JSSymbols::hasAccessorProps(ctx);
                    if (hap) setProto = setProto->setAttribute(ctx, hap, PROTO_TRUE);
                }
                // §24.2.3.10 accessor descriptor 0x2 (non-enumerable,
                // configurable) on the size slot itself.
                const proto::ProtoString* pdks = JSSymbols::pdSize(ctx);
                if (pdks) setProto = setProto->setAttribute(ctx, pdks, ctx->fromInteger(0x2LL));
            }
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

    // BuildSetPrototype ran before ensureFunctionPrototype, so each
    // installNonEnumerableMethod call back then saw methodPrototype =
    // nullptr and stamped a parentless wrapper. Reinstall now that
    // methodPrototype (=Function.prototype) is available so
    // s.add.call / s.has.bind / etc. resolve via the standard chain.
    if (ctx->space && ctx->space->methodPrototype && s_setPrototype) {
        const proto::ProtoObject* sp = s_setPrototype;
        sp = installNonEnumerableMethod(ctx, sp, "add",     setAdd,      1);
        sp = installNonEnumerableMethod(ctx, sp, "has",     setHas,      1);
        sp = installNonEnumerableMethod(ctx, sp, "delete",  setDeleteFn, 1);
        sp = installNonEnumerableMethod(ctx, sp, "clear",   setClear,    0);
        sp = installNonEnumerableMethod(ctx, sp, "forEach", setForEach,  1);
        sp = installNonEnumerableMethod(ctx, sp, "values",  setValues,   0);
        // §24.2.3.8 Set.prototype.keys === Set.prototype.values (the
        // SAME function object).  Pre-fix installing each independently
        // produced two distinct wrappers and the identity check
        // assert.sameValue(Set.prototype.keys, Set.prototype.values)
        // failed.  Read the values slot back and alias it to "keys"
        // and Symbol.iterator below.
        {
            const proto::ProtoObject* vKo = ctx->fromUTF8String("values");
            const proto::ProtoString* vK = vKo ? vKo->asString(ctx) : nullptr;
            const proto::ProtoObject* valuesFn = vK
                ? sp->getAttribute(ctx, vK, false) : nullptr;
            const proto::ProtoObject* kKo = ctx->fromUTF8String("keys");
            const proto::ProtoString* kK = kKo ? kKo->asString(ctx) : nullptr;
            if (valuesFn && valuesFn != PROTO_NONE && kK) {
                sp = sp->setAttribute(ctx, kK, valuesFn);
                const proto::ProtoObject* pdko = ctx->fromUTF8String("__pd_keys__");
                const proto::ProtoString* pdks = pdko ? pdko->asString(ctx) : nullptr;
                if (pdks) sp = sp->setAttribute(ctx, pdks, ctx->fromInteger(0x3LL));
            }
            // §24.2.3.12 Set.prototype[@@iterator] === Set.prototype.values
            // (same identity).  Re-alias here after reinstall so the
            // Symbol.iterator slot points at the fresh wrapper rather
            // than the BuildSetPrototype-era one.
            if (valuesFn && valuesFn != PROTO_NONE) {
                const proto::ProtoString* symIterKey = JSSymbols::symbolIterator(ctx);
                if (symIterKey) {
                    sp = sp->setAttribute(ctx, symIterKey, valuesFn);
                    const proto::ProtoObject* pdiko = ctx->fromUTF8String("__pd_Symbol.iterator__");
                    const proto::ProtoString* pdiks = pdiko ? pdiko->asString(ctx) : nullptr;
                    if (pdiks) sp = sp->setAttribute(ctx, pdiks, ctx->fromInteger(0x3LL));
                }
            }
        }
        sp = installNonEnumerableMethod(ctx, sp, "entries", setEntries,  0);
        sp = installNonEnumerableMethod(ctx, sp, "union",               setUnion,               1);
        sp = installNonEnumerableMethod(ctx, sp, "intersection",        setIntersection,        1);
        sp = installNonEnumerableMethod(ctx, sp, "difference",          setDifference,          1);
        sp = installNonEnumerableMethod(ctx, sp, "symmetricDifference", setSymmetricDifference, 1);
        sp = installNonEnumerableMethod(ctx, sp, "isSubsetOf",          setIsSubsetOf,          1);
        sp = installNonEnumerableMethod(ctx, sp, "isSupersetOf",        setIsSupersetOf,        1);
        sp = installNonEnumerableMethod(ctx, sp, "isDisjointFrom",      setIsDisjointFrom,      1);
        // Re-build the size getter wrapper with Function.prototype
        // parent now that methodPrototype is published.  Pre-fix the
        // initial install ran before ensureFunctionPrototype and the
        // getter inherited from a parentless newObject; .call /
        // .apply / .bind were missing, breaking
        // Object.getOwnPropertyDescriptor(Set.prototype,'size').get.call.
        {
            const proto::ProtoObject* gko = ctx->fromUTF8String("__get_size__");
            const proto::ProtoString* gks = gko ? gko->asString(ctx) : nullptr;
            if (gks) {
                const proto::ProtoObject* parent = ctx->space->methodPrototype;
                const proto::ProtoObject* getter = parent ? parent->newChild(ctx, true) : nullptr;
                if (getter) {
                    const proto::ProtoString* nfKey = JSSymbols::nativeFn(ctx);
                    proto::ProtoObject* mGetter = const_cast<proto::ProtoObject*>(getter);
                    const proto::ProtoObject* raw = ctx->fromMethod(mGetter, setSizeGetter);
                    if (nfKey && raw) getter = getter->setAttribute(ctx, nfKey, raw);
                    const proto::ProtoString* lenKey = JSSymbols::length(ctx);
                    if (lenKey) {
                        getter = getter->setAttribute(ctx, lenKey, ctx->fromInteger(0LL));
                        const proto::ProtoString* pdls = JSSymbols::pdLength(ctx);
                        if (pdls) getter = getter->setAttribute(ctx, pdls, ctx->fromInteger(0x2LL));
                    }
                    const proto::ProtoString* nmKey = JSSymbols::name(ctx);
                    if (nmKey) {
                        getter = getter->setAttribute(ctx, nmKey, ctx->fromUTF8String("get size"));
                        const proto::ProtoString* pdns = JSSymbols::pdName(ctx);
                        if (pdns) getter = getter->setAttribute(ctx, pdns, ctx->fromInteger(0x2LL));
                    }
                    const proto::ProtoString* hnwG = JSSymbols::hasNonWritableProps(ctx);
                    if (hnwG) getter = getter->setAttribute(ctx, hnwG, PROTO_TRUE);
                    sp = sp->setAttribute(ctx, gks, getter);
                }
            }
        }
        s_setPrototype = sp;
    }

    const proto::ProtoObject* ctorParent =
        (ctx->space && ctx->space->methodPrototype) ? ctx->space->methodPrototype : nullptr;
    const proto::ProtoObject* ctor = ctorParent
        ? ctorParent->newChild(ctx, true)
        : ctx->newObject(true);
    if (!ctor) return;

    const proto::ProtoString* nameKey = JSSymbols::name(ctx);
    if (nameKey) {
        ctor = ctor->setAttribute(ctx, nameKey, ctx->fromUTF8String("Set"));
        // Per §17 every built-in constructor's .name carries
        // {writable:false, enumerable:false, configurable:true} → 0x2.
        const proto::ProtoString* pdnk = JSSymbols::pdName(ctx);
        if (pdnk) ctor = ctor->setAttribute(ctx, pdnk, ctx->fromInteger(0x2LL));
    }

    // Set.length = 0 per §24.2.1.1 — same descriptor as .name above.
    // Pre-fix the length attribute was absent so test262's
    // 'built-ins/Set/length.js' (which probes the descriptor via
    // verifyProperty) failed with 'desc === undefined'.
    {
        const proto::ProtoString* lenKey = JSSymbols::length(ctx);
        if (lenKey) {
            ctor = ctor->setAttribute(ctx, lenKey, ctx->fromInteger(0LL));
            const proto::ProtoString* pdlk = JSSymbols::pdLength(ctx);
            if (pdlk) ctor = ctor->setAttribute(ctx, pdlk, ctx->fromInteger(0x2LL));
        }
    }
    // Hot-path hint mirroring Boolean / Number / String / Map / RegExp
    // ctors earlier this round.  Without __has_nonwritable_props__ the
    // writable=false bits are ignored — `Set.name = "X"` silently
    // succeeded.
    {
        const proto::ProtoString* hnw = JSSymbols::hasNonWritableProps(ctx);
        if (hnw) ctor = ctor->setAttribute(ctx, hnw, PROTO_TRUE);
    }

    const proto::ProtoString* protoKey = JSSymbols::prototype(ctx);
    if (protoKey && s_setPrototype) {
        ctor = ctor->setAttribute(ctx, protoKey, s_setPrototype);
        // §24.2.2.1 / §17: Set.prototype descriptor bits 0x0.
        const proto::ProtoObject* pdpo = ctx->fromUTF8String("__pd_prototype__");
        const proto::ProtoString* pdpk = pdpo ? pdpo->asString(ctx) : nullptr;
        if (pdpk) ctor = ctor->setAttribute(ctx, pdpk, ctx->fromInteger(0x0LL));
    }

    const proto::ProtoObject* constructKey = ctx->fromUTF8String("__construct__");
    const proto::ProtoString* constructKs  = constructKey ? constructKey->asString(ctx) : nullptr;
    if (constructKs) {
        const proto::ProtoObject* constructFn = ctx->fromMethod(nullptr, setConstruct);
        if (constructFn) ctor = ctor->setAttribute(ctx, constructKs, constructFn);
    }
    // §24.2.1.1 step 1: Set called without new → TypeError.  Install a
    // __native_fn__ throwing stub so `Set()` raises instead of being
    // silently treated as the no-op constructor call (test262 Set/
    // set-undefined-newtarget.js).
    {
        static const proto::ProtoMethod setCall = [](
            proto::ProtoContext* sctx, const proto::ProtoObject*,
            const proto::ParentLink*, const proto::ProtoList*,
            const proto::ProtoSparseList*) -> const proto::ProtoObject* {
            signalNativeException(makeNativeError(sctx, "TypeError",
                "Constructor Set requires 'new'"));
            return PROTO_NONE;
        };
        const proto::ProtoString* nfK = JSSymbols::nativeFn(ctx);
        if (nfK) ctor = ctor->setAttribute(ctx, nfK,
            ctx->fromMethod(nullptr, setCall));
    }

    // Set.prototype.constructor === Set per §24.2.3.2 (non-enumerable).
    if (s_setPrototype) {
        const proto::ProtoString* ctorWordKey = JSSymbols::constructor(ctx);
        if (ctorWordKey) {
            const proto::ProtoObject* updated =
                s_setPrototype->setAttribute(ctx, ctorWordKey, ctor);
            if (updated && updated != PROTO_NONE) {
                const proto::ProtoString* pdk = JSSymbols::pdConstructor(ctx);
                if (pdk) updated = updated->setAttribute(ctx, pdk, ctx->fromInteger(0x3LL));
                s_setPrototype = updated;
            }
        }
    }

    // get Set[Symbol.species] — install with §17 name/length descriptors
    // and the well-known __get_Symbol.species__ accessor key.
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
                    const proto::ProtoObject* raw = ctx->fromMethod(mGetter, setSpeciesGetter);
                    if (raw) getter = getter->setAttribute(ctx, nfKey, raw);
                }
                const proto::ProtoString* lenKey = JSSymbols::length(ctx);
                if (lenKey) {
                    getter = getter->setAttribute(ctx, lenKey, ctx->fromInteger(0LL));
                    const proto::ProtoString* pdls = JSSymbols::pdLength(ctx);
                    if (pdls) getter = getter->setAttribute(ctx, pdls, ctx->fromInteger(0x2LL));
                }
                const proto::ProtoString* nmKey = JSSymbols::name(ctx);
                if (nmKey) {
                    getter = getter->setAttribute(ctx, nmKey, ctx->fromUTF8String("get [Symbol.species]"));
                    const proto::ProtoString* pdns = JSSymbols::pdName(ctx);
                    if (pdns) getter = getter->setAttribute(ctx, pdns, ctx->fromInteger(0x2LL));
                }
                // Stamp the gating flag so the getter wrapper's name +
                // length are actually read-only (Round 12/13 sweep).
                const proto::ProtoString* hnwSp = JSSymbols::hasNonWritableProps(ctx);
                if (hnwSp) getter = getter->setAttribute(ctx, hnwSp, PROTO_TRUE);
                const proto::ProtoString* gksSym =
                    ctx->fromUTF8String("__get_Symbol.species__")->asString(ctx);
                if (gksSym) ctor = ctor->setAttribute(ctx, gksSym, getter);
                // §24.2.2.2 get Set[@@species] descriptor: accessor
                // with {enumerable:false, configurable:true} → 0x2.
                // Pre-fix the species slot had no descriptor and
                // defaulted to fully enumerable.
                const proto::ProtoObject* pdo = ctx->fromUTF8String("__pd_Symbol.species__");
                const proto::ProtoString* pdk = pdo ? pdo->asString(ctx) : nullptr;
                if (pdk) ctor = ctor->setAttribute(ctx, pdk, ctx->fromInteger(0x2LL));
            }
        }
    }

    *globalRoot = (*globalRoot)->setAttribute(ctx, ks, ctor);
    // §17 constructor-of-the-global table: the slot is
    // {writable:true, enumerable:false, configurable:true} (bits 0x3).
    // Pre-fix the slot defaulted to fully enumerable, leaking "Set"
    // through for-in over globalThis.
    {
        const proto::ProtoObject* pdo = ctx->fromUTF8String("__pd_Set__");
        const proto::ProtoString* pdk = pdo ? pdo->asString(ctx) : nullptr;
        if (pdk) *globalRoot = (*globalRoot)->setAttribute(ctx, pdk,
            ctx->fromInteger(0x3LL));
    }
}

} // namespace protojs
