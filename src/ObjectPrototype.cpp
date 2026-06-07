#include "ObjectPrototype.h"
#include "ArrayPrototype.h"
#include "ArrayElementsStorage.h"
#include "FunctionPrototype.h"
#include "JSSymbols.h"
#include "PrototypeUtils.h"
#include "headers/protoCore.h"
#include "JSContext.h"
#include "runtime/ProtoInterpreter.h"
#include "runtime/BehaviorRegistry.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_set>
#include <unordered_map>
#include <vector>
#include <cmath>

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

    // ECMA-262 §19.1.2.16: ToObject(string) yields a String wrapper whose
    // own enumerable keys are the character indices "0".."n-1". A primitive
    // string here represents the ToObject view directly; expose its chars
    // as keys.
    // Helper to enumerate a UTF-8 string's chars as UTF-16 indexed keys.
    auto emitStringChars = [&](const std::string& s) {
        size_t i = 0;
        size_t idx = 0;
        while (i < s.size()) {
            size_t charLen = 1;
            unsigned char c = static_cast<unsigned char>(s[i]);
            if      ((c & 0x80) == 0x00) charLen = 1;
            else if ((c & 0xE0) == 0xC0) charLen = 2;
            else if ((c & 0xF0) == 0xE0) charLen = 3;
            else if ((c & 0xF8) == 0xF0) charLen = 4;
            if (i + charLen > s.size()) break;
            std::string ch = s.substr(i, charLen);
            keys.push_back(std::to_string(idx));
            if (vals) vals->push_back(ctx->fromUTF8String(ch.c_str()));
            i += charLen;
            // 4-byte UTF-8 → surrogate pair, counts as 2 UTF-16 units.
            idx += (charLen == 4) ? 2 : 1;
        }
    };

    if (obj->isString(ctx)) {
        const proto::ProtoString* ps = obj->asString(ctx);
        if (ps) {
            std::string s;
            ps->toUTF8String(ctx, s);
            emitStringChars(s);
            // Per §22.1.4 ToObject(string) the per-char indexed
            // properties are enumerable, but "length" is
            // non-enumerable. Object.keys / values / entries (which
            // pass includeNonEnumerable=false) MUST omit it;
            // Object.getOwnPropertyNames passes includeNonEnumerable=
            // true and DOES include it after the chars (built-ins/
            // Object/values/primitive-strings expected length 3, not 4).
            if (includeNonEnumerable) {
                size_t u16 = 0;
                for (size_t bi = 0; bi < s.size(); ) {
                    unsigned char c = static_cast<unsigned char>(s[bi]);
                    size_t cl = (c < 0x80) ? 1 : (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : 4;
                    if (bi + cl > s.size()) break;
                    u16 += (cl == 4) ? 2 : 1;
                    bi += cl;
                }
                keys.push_back("length");
                if (vals) vals->push_back(ctx->fromInteger(static_cast<long long>(u16)));
            }
        }
        return;
    }

    // ECMA-262 §22.1.3 String-exotic objects — `new String("abc")` exposes
    // the per-char indexed properties "0", "1", ... plus "length" alongside
    // any user-added attributes. Pre-fix the collectOwnKeys path only
    // walked the wrapper's sparse-list attributes, so the synthesised char
    // indices were invisible — getOwnPropertyNames(new String('abc'))
    // returned ['length'] instead of ['0','1','2','length'].
    bool isStringWrapper = false;
    std::string wrapperStr;
    {
        const proto::ProtoString* pvKey = JSSymbols::primitiveValue(ctx);
        if (pvKey) {
            const proto::ProtoObject* pv = obj->getAttribute(ctx, pvKey, false);
            if (pv && pv != PROTO_NONE && pv->isString(ctx)) {
                if (const proto::ProtoString* ps = pv->asString(ctx)) {
                    ps->toUTF8String(ctx, wrapperStr);
                    isStringWrapper = true;
                }
            }
        }
    }
    if (isStringWrapper) {
        emitStringChars(wrapperStr);
        // Fall through to also enumerate explicit user attributes
        // (str[5] = "de", str.foo = ..., etc.) and the "length" key.
    }

    // Detect arrays to suppress the "length" key (length is non-enumerable on arrays).
    const proto::ProtoString* isArrKey = JSSymbols::isArray(ctx);
    bool isArr = false;
    if (isArrKey) {
        const proto::ProtoObject* arrFlag = obj->getAttribute(ctx, isArrKey, false);
        isArr = (arrFlag == PROTO_TRUE);
    }
    // Synthesise array indices first (spec: index keys precede string keys
    // in own-property enumeration).  Pre-fix Object.keys([10,20,30])
    // returned [] because the elements live in __elements__, not as
    // own attributes — the SparseList iterator below misses them.
    if (isArr) {
        const proto::ProtoList* elsList = getArrayElements(ctx, obj);
        if (elsList) {
            unsigned long n = elsList->getSize(ctx);
            for (unsigned long i = 0; i < n; ++i) {
                const proto::ProtoObject* v = elsList->getAt(ctx, static_cast<int>(i));
                if (v && v != PROTO_NONE) {  // skip sparse holes
                    keys.push_back(std::to_string(i));
                    if (vals) vals->push_back(v);
                }
            }
        }
    }
    const proto::ProtoSparseList* own = obj->getOwnAttributes(ctx);
    if (!own) return;
    const proto::ProtoString* lenSymbol = JSSymbols::length(ctx);

    // Per-target hint flags — same idea as resolvePutFieldOOP's gate.
    // When __has_nonwritable_props__ is absent on obj, every property
    // has the default writable=true, enumerable=true, configurable=true
    // → the per-key __pd_<key>__ probe (which builds a fresh
    // ProtoString rope every iteration) is pure waste.  Same for
    // __has_accessor_props__ gating the __get_<key>__ accessor probe.
    bool mightHaveNonWritable = false;
    bool mightHaveAccessors = false;
    {
        const proto::ProtoString* hnwKey = JSSymbols::hasNonWritableProps(ctx);
        if (hnwKey) {
            mightHaveNonWritable = (obj->hasAttribute(ctx, hnwKey) == PROTO_TRUE)
                && (obj->getAttribute(ctx, hnwKey, true) == PROTO_TRUE);
        }
        const proto::ProtoString* hapKey = JSSymbols::hasAccessorProps(ctx);
        if (hapKey) {
            mightHaveAccessors = (obj->hasAttribute(ctx, hapKey) == PROTO_TRUE)
                && (obj->getAttribute(ctx, hapKey, true) == PROTO_TRUE);
        }
    }

    // processElements walks the SparseList via a C-callback — no
    // per-step iterator object allocations.  For SmallSparseList the
    // inline pairs are visited directly without ANY iterator chain;
    // for AVL form, the internal iterator is reused inside a CS but
    // the advance() public API's per-step implAsObject wrapper
    // allocation is gone.
    struct CollectState {
        proto::ProtoContext* ctx;
        const proto::ProtoObject* obj;
        std::vector<std::string>* keys;
        std::vector<const proto::ProtoObject*>* vals;
        const proto::ProtoString* lenSymbol;
        bool isArr;
        bool includeNonEnumerable;
        bool mightHaveNonWritable;
        bool mightHaveAccessors;
        bool aborted;
    } state{ctx, obj, &keys, vals, lenSymbol, isArr,
            includeNonEnumerable, mightHaveNonWritable,
            mightHaveAccessors, false};
    auto cb = [](proto::ProtoContext* cbCtx, void* selfV,
                 unsigned long rawKey, const proto::ProtoObject* val) {
        CollectState* s = static_cast<CollectState*>(selfV);
        if (s->aborted) return;
        const proto::ProtoString* propKey =
            reinterpret_cast<const proto::ProtoString*>(rawKey);
        if (!propKey) return;
        if (isInternalKey(cbCtx, propKey)) return;
        // ECMA-262 §7.3.23 EnumerableOwnProperties step 4.a — at each
        // step re-check that [[GetOwnProperty]] still returns a
        // descriptor for this key on `obj`. The snapshot the iterator
        // captures could mention a key that a previously-invoked
        // getter deleted; re-check on the live object before emitting.
        if (s->obj->hasOwnAttribute(cbCtx, propKey) != PROTO_TRUE) return;
        // Array's "length" is non-enumerable but IS an own property.
        if (s->isArr && s->lenSymbol && propKey == s->lenSymbol
            && !s->includeNonEnumerable) return;
        std::string kstr;
        propKey->toUTF8String(cbCtx, kstr);
        // Skip the __pd_<key>__ enumerable probe when no non-default
        // writable bit has ever been stamped on this target.
        if (!s->includeNonEnumerable && s->mightHaveNonWritable) {
            std::string pdKeyStr = "__pd_" + kstr + "__";
            const proto::ProtoObject* pko = cbCtx->fromUTF8String(pdKeyStr.c_str());
            const proto::ProtoString* pdk = pko ? pko->asString(cbCtx) : nullptr;
            if (pdk) {
                const proto::ProtoObject* pdv = s->obj->getAttribute(cbCtx, pdk, false);
                if (pdv && pdv != PROTO_NONE && pdv->isInteger(cbCtx)) {
                    uint8_t bits = static_cast<uint8_t>(pdv->asLong(cbCtx));
                    if (!(bits & 0x4)) return; // not enumerable — skip
                }
            }
        }
        s->keys->push_back(kstr);
        if (s->vals) {
            // §7.3.1 Get(O, P): accessor descriptor takes precedence
            // over the data slot. Skip the probe when no accessors
            // exist anywhere reachable.
            const proto::ProtoObject* getter = nullptr;
            if (s->mightHaveAccessors) {
                std::string gkStr = "__get_" + kstr + "__";
                const proto::ProtoObject* gko = cbCtx->fromUTF8String(gkStr.c_str());
                const proto::ProtoString* gk = gko ? gko->asString(cbCtx) : nullptr;
                getter = (gk && s->obj->hasOwnAttribute(cbCtx, gk) == PROTO_TRUE)
                    ? s->obj->getAttribute(cbCtx, gk, false) : nullptr;
            }
            if (getter && getter != PROTO_NONE && getter != getUndefinedSentinel()) {
                const proto::ProtoList* noArgs = cbCtx->newList();
                const proto::ProtoObject* gres = callJSFunction(cbCtx, getter, s->obj, noArgs);
                if (hasCallException()) {
                    s->vals->push_back(PROTO_NONE);
                    s->aborted = true;
                    return;
                }
                s->vals->push_back(gres ? gres : PROTO_NONE);
            } else {
                s->vals->push_back(val ? val : PROTO_NONE);
            }
        }
    };
    own->processElements(ctx, &state,
        static_cast<void(*)(proto::ProtoContext*, void*, unsigned long, const proto::ProtoObject*)>(cb));
}

// ---------------------------------------------------------------------------
// Object.keys(obj) → array of own enumerable string property names.
// ---------------------------------------------------------------------------
// Spec helper: ToObject step in Object.keys / values / entries /
// getOwnPropertyNames / getOwnPropertyDescriptors. null and undefined
// must throw TypeError ("not object coercible").
static bool throwIfNullOrUndefined(proto::ProtoContext* ctx,
                                   const proto::ProtoObject* obj,
                                   const char* methodName)
{
    if (!obj || obj == PROTO_NONE || obj == getUndefinedSentinel()) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            (std::string(methodName) + " called on null or undefined").c_str()));
        return true;
    }
    if (obj == getNullSentinel()) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            (std::string(methodName) + " called on null or undefined").c_str()));
        return true;
    }
    return false;
}

static const proto::ProtoObject* objectKeys(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    const proto::ProtoObject* obj = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0) : nullptr;
    if (throwIfNullOrUndefined(ctx, obj, "Object.keys")) return PROTO_NONE;

    std::vector<std::string> keys;
    collectOwnKeys(ctx, obj, keys, nullptr);

    const proto::ProtoObject* result = createNewArray(ctx, nullptr);
    const proto::ProtoString* lenKey = JSSymbols::length(ctx);
    const proto::ProtoString* isArrKey2 = JSSymbols::isArray(ctx);
    const proto::ProtoList* elsList = ctx->newList();
    for (size_t i = 0; i < keys.size(); i++) {
        const proto::ProtoObject* kv = ctx->fromUTF8String(keys[i].c_str());
        elsList = elsList->appendLast(ctx, kv ? kv : PROTO_NONE);
    }
    setArrayElements(ctx, result, elsList);
    if (lenKey) result = result->setAttribute(ctx, lenKey, ctx->fromInteger(static_cast<long long>(keys.size())));
    if (isArrKey2) result = result->setAttribute(ctx, isArrKey2, PROTO_TRUE);
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
    if (throwIfNullOrUndefined(ctx, obj, "Object.values")) return PROTO_NONE;

    std::vector<std::string> keys;
    std::vector<const proto::ProtoObject*> vals;
    collectOwnKeys(ctx, obj, keys, &vals);

    const proto::ProtoObject* result = createNewArray(ctx, nullptr);
    const proto::ProtoString* lenKey = JSSymbols::length(ctx);
    const proto::ProtoString* isArrKey2 = JSSymbols::isArray(ctx);
    const proto::ProtoList* elsList = ctx->newList();
    for (size_t i = 0; i < vals.size(); i++) {
        elsList = elsList->appendLast(ctx, vals[i] ? vals[i] : PROTO_NONE);
    }
    setArrayElements(ctx, result, elsList);
    if (lenKey) result = result->setAttribute(ctx, lenKey, ctx->fromInteger(static_cast<long long>(vals.size())));
    if (isArrKey2) result = result->setAttribute(ctx, isArrKey2, PROTO_TRUE);
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
    if (throwIfNullOrUndefined(ctx, obj, "Object.entries")) return PROTO_NONE;

    std::vector<std::string> keys;
    std::vector<const proto::ProtoObject*> vals;
    collectOwnKeys(ctx, obj, keys, &vals);

    const proto::ProtoObject* result = createNewArray(ctx, nullptr);
    const proto::ProtoString* lenKey = JSSymbols::length(ctx);
    const proto::ProtoString* isArrKey2 = JSSymbols::isArray(ctx);
    const proto::ProtoString* idx0 = JSSymbols::indexKey(ctx, 0);
    const proto::ProtoString* idx1 = JSSymbols::indexKey(ctx, 1);

    (void)idx0; (void)idx1;  // pairs built via __elements__ below
    const proto::ProtoList* outerList = ctx->newList();
    for (size_t i = 0; i < keys.size(); i++) {
        // Build pair [key, value] as a 2-element array — use __elements__
        // storage just like Object.keys/values, so the result round-trips
        // through arrayTryFastGet and JSON.stringify cleanly.
        const proto::ProtoObject* pair = createNewArray(ctx, nullptr);
        const proto::ProtoObject* kv = ctx->fromUTF8String(keys[i].c_str());
        const proto::ProtoList* pairEls = ctx->newList();
        pairEls = pairEls->appendLast(ctx, kv ? kv : PROTO_NONE);
        pairEls = pairEls->appendLast(ctx, vals[i] ? vals[i] : PROTO_NONE);
        setArrayElements(ctx, pair, pairEls);
        if (lenKey)    pair = pair->setAttribute(ctx, lenKey, ctx->fromInteger(2LL));
        if (isArrKey2) pair = pair->setAttribute(ctx, isArrKey2, PROTO_TRUE);
        outerList = outerList->appendLast(ctx, pair);
    }
    setArrayElements(ctx, result, outerList);
    if (lenKey) result = result->setAttribute(ctx, lenKey, ctx->fromInteger(static_cast<long long>(keys.size())));
    if (isArrKey2) result = result->setAttribute(ctx, isArrKey2, PROTO_TRUE);
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
    // ECMA-262 §19.1.2.1 step 1: target = ToObject(target). Throws
    // TypeError for null/undefined.
    if (argc == 0) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Cannot convert undefined to object"));
        return PROTO_NONE;
    }
    const proto::ProtoObject* target = args->getAt(ctx, 0);
    if (!target || target == PROTO_NONE || target == getUndefinedSentinel()) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Cannot convert undefined to object"));
        return PROTO_NONE;
    }
    if (target == getNullSentinel()) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Cannot convert null to object"));
        return PROTO_NONE;
    }
    // §19.1.2.1 step 1: To = ToObject(target). For primitives the spec
    // produces the corresponding wrapper (String / Number / Boolean /
    // Symbol). Pre-fix Object.assign("a") returned the raw "a" primitive,
    // so typeof === "string" (built-ins/Object/assign/OnlyOneArgument
    // expected "object" and `.valueOf()` access).
    JSContextWrapper* aw = JSContextWrapper::current();
    auto wrapPrimitive = [&](const proto::ProtoObject* prim,
                              const proto::ProtoObject* protoForWrapper) -> const proto::ProtoObject* {
        const proto::ProtoObject* wrap = (protoForWrapper && protoForWrapper != PROTO_NONE)
            ? protoForWrapper->newChild(ctx, true)
            : ctx->newObject(true);
        if (wrap) {
            const proto::ProtoString* pvK = JSSymbols::primitiveValue(ctx);
            if (pvK) wrap = wrap->setAttribute(ctx, pvK, prim);
        }
        return wrap ? wrap : target;
    };
    if (target->isString(ctx) && ctx->space && aw) {
        target = wrapPrimitive(target, ctx->space->stringPrototype);
    } else if (target->isInteger(ctx) || target->isDouble(ctx) || target->isFloat(ctx)) {
        if (ctx->space) target = wrapPrimitive(target, ctx->space->doublePrototype);
    } else if (target == PROTO_TRUE || target == PROTO_FALSE) {
        if (ctx->space) target = wrapPrimitive(target, ctx->space->booleanPrototype);
    }

    for (int si = 1; si < argc; si++) {
        const proto::ProtoObject* src = args->getAt(ctx, si);
        if (!src || src == PROTO_NONE) continue;
        // §19.1.2.1 step 4.a: undefined / null sources are skipped; any
        // other value is ToObject-coerced. The primitive coercions of
        // interest here are Strings (own indexed character properties)
        // and Numbers / Booleans (no own enumerable properties).
        if (src == getNullSentinel() || src == getUndefinedSentinel()) continue;
        if (src->isInteger(ctx) || src->isDouble(ctx) || src->isFloat(ctx)
            || src == PROTO_TRUE || src == PROTO_FALSE) {
            continue; // wrapper has no own enumerable properties
        }
        if (src->isString(ctx)) {
            // §22.1.4 String exotic: own enumerable data properties
            // "0".."len-1" expose each UTF-16 code unit as a 1-char
            // string. Walk the UTF-8 buffer codepoint-by-codepoint.
            std::string sv;
            src->asString(ctx)->toUTF8String(ctx, sv);
            uint32_t cpIdx = 0;
            for (size_t bi = 0; bi < sv.size(); ) {
                unsigned char c = static_cast<unsigned char>(sv[bi]);
                size_t cl = (c < 0x80) ? 1 : (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : 4;
                if (bi + cl > sv.size()) break;
                std::string ch = sv.substr(bi, cl);
                const proto::ProtoString* k = JSSymbols::indexKey(ctx, cpIdx);
                if (k) target = target->setAttribute(ctx, k,
                    ctx->fromUTF8String(ch.c_str()));
                bi += cl;
                ++cpIdx;
            }
            continue;
        }
        // If src is an array, copy __elements__ first so that
        // numeric-index iteration sees the data.
        const proto::ProtoList* srcEls = getArrayElements(ctx, src);
        if (srcEls) {
            const proto::ProtoList* tgtEls = getArrayElements(ctx, target);
            if (!tgtEls) tgtEls = ctx->newList();
            size_t srcSz = srcEls->getSize(ctx);
            size_t tgtSz = tgtEls->getSize(ctx);
            for (size_t i = 0; i < srcSz; ++i) {
                const proto::ProtoObject* v = srcEls->getAt(ctx, static_cast<int>(i));
                if (i < tgtSz) tgtEls = tgtEls->setAt(ctx, static_cast<int>(i), v);
                else           tgtEls = tgtEls->appendLast(ctx, v);
            }
            protojs::setArrayElements(ctx, target, tgtEls);
            // length: target.length = max(target.length, srcSz)
            const proto::ProtoString* lk = JSSymbols::length(ctx);
            if (lk) {
                long long curLen = 0;
                const proto::ProtoObject* lv = target->getAttribute(ctx, lk, false);
                if (lv && lv != PROTO_NONE && lv->isInteger(ctx)) curLen = lv->asLong(ctx);
                if (static_cast<long long>(srcSz) > curLen)
                    target = target->setAttribute(ctx, lk, ctx->fromInteger(static_cast<long long>(srcSz)));
            }
        }
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
            // §20.1.2.1 step 4.c.ii.1: only enumerable own properties
            // are copied. Probe the __pd_<key>__ descriptor sidecar;
            // when absent the property defaults to fully enumerable
            // (matches our other descriptor probes).
            std::string keyStr;
            propKey->toUTF8String(ctx, keyStr);
            std::string pdStr = std::string("__pd_") + keyStr + "__";
            const proto::ProtoObject* pdo = ctx->fromUTF8String(pdStr.c_str());
            const proto::ProtoString* pdk = pdo ? pdo->asString(ctx) : nullptr;
            if (pdk) {
                const proto::ProtoObject* pdv = src->getAttribute(ctx, pdk, false);
                if (pdv && pdv != PROTO_NONE && pdv->isInteger(ctx)) {
                    long long bits = pdv->asLong(ctx);
                    if ((bits & 0x4) == 0) continue; // not enumerable — skip
                }
            }
            // Spec §20.1.2.1 step 4.c.ii.2: Get(from, key) — must invoke
            // the getter when the property is an accessor. The iterator
            // yields the data slot only (typically PROTO_NONE for
            // accessor entries), so probe the __get_<key>__ sidecar and
            // call it on src. Pre-fix accessor properties were copied as
            // undefined.
            std::string gkStr = std::string("__get_") + keyStr + "__";
            const proto::ProtoObject* gko = ctx->fromUTF8String(gkStr.c_str());
            const proto::ProtoString* gk = gko ? gko->asString(ctx) : nullptr;
            const proto::ProtoObject* effective = val;
            if (gk) {
                const proto::ProtoObject* getter = src->getAttribute(ctx, gk, true);
                if (getter && getter != PROTO_NONE
                    && getter != getUndefinedSentinel()) {
                    const proto::ProtoList* emptyArgs = ctx->newList();
                    effective = callJSFunction(ctx, getter, src, emptyArgs);
                    if (hasCallException()) return PROTO_NONE;
                }
            }
            // §19.1.2.1 step 5.c.iv: Set(to, nextKey, propValue, true).
            // The trailing "true" is Throw — a non-writable target slot
            // raises TypeError. Probe the target's __pd_<key>__ sidecar
            // (default 0x7 = writable+configurable+enumerable when
            // absent); refuse the write when bit 0 (writable) is clear.
            {
                std::string tk;
                propKey->toUTF8String(ctx, tk);
                std::string pdStr = std::string("__pd_") + tk + "__";
                const proto::ProtoObject* pdo = ctx->fromUTF8String(pdStr.c_str());
                const proto::ProtoString* pdk = pdo ? pdo->asString(ctx) : nullptr;
                if (pdk && target->hasOwnAttribute(ctx, pdk) == PROTO_TRUE) {
                    const proto::ProtoObject* pdv = target->getAttribute(ctx, pdk, false);
                    if (pdv && pdv != PROTO_NONE && pdv->isInteger(ctx)) {
                        long long bits = pdv->asLong(ctx);
                        if (!(bits & 0x1)) {
                            signalNativeException(makeNativeError(ctx, "TypeError",
                                "Cannot assign to read only property"));
                            return PROTO_NONE;
                        }
                    }
                }
            }
            target = target->setAttribute(ctx, propKey,
                effective ? effective : PROTO_NONE);
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

// Forward decl — defined further below; objectCreate / objectSetPrototypeOf
// both consult/update this thread-local map.
extern thread_local std::unordered_map<const proto::ProtoObject*,
                                       const proto::ProtoObject*> t_jsProtoMap;

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
    // ECMA-262 §20.1.2.2 step 1: if O is neither Object nor null,
    // throw TypeError. Pre-fix the no-args call returned a plain
    // empty object (spec: TypeError because undefined is neither),
    // and primitives (1 / "x" / true) silently routed through
    // newChild, producing degenerate objects.
    const proto::ProtoObject* protoArg = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0) : PROTO_NONE;
    const proto::ProtoObject* nullSent = getNullSentinel();
    if (!protoArg || protoArg == PROTO_NONE
        || protoArg == getUndefinedSentinel()
        || protoArg->isInteger(ctx) || protoArg->isDouble(ctx)
        || protoArg->isFloat(ctx)   || protoArg->isBoolean(ctx)
        || protoArg->isString(ctx)) {
        // The null sentinel is the only "primitive" accepted.
        if (protoArg != nullSent) {
            signalNativeException(makeNativeError(ctx, "TypeError",
                "Object prototype may only be an Object or null"));
            return PROTO_NONE;
        }
    }

    const proto::ProtoObject* result;
    if (protoArg == nullSent) {
        // Object.create(null) → plain object with no prototype.
        // Record the override in t_jsProtoMap so Object.getPrototypeOf
        // returns null (not the protoCore-default Object.prototype).
        result = ctx->newObject(true);
        if (result) t_jsProtoMap[result] = getNullSentinel();
    } else {
        // Object.create(proto) → child inheriting from proto
        result = protoArg->newChild(ctx, true);
    }

    // Second argument: property descriptors object
    if (args->getSize(ctx) >= 2) {
        const proto::ProtoObject* propsObj = args->getAt(ctx, 1);
        if (propsObj && propsObj != PROTO_NONE && propsObj != getUndefinedSentinel()) {
            // ECMA-262 §19.1.2.2 step 3: properties = ToObject(properties).
            // ToObject(null/undefined) -> TypeError. Then iterate its own
            // enumerable keys and feed each value to ToPropertyDescriptor
            // (which itself throws TypeError on non-object descriptors).
            if (propsObj == getNullSentinel()) {
                signalNativeException(makeNativeError(ctx, "TypeError",
                    "Object.create properties must be an object"));
                return PROTO_NONE;
            }
            // A primitive string is ToObject-coerced to a String wrapper
            // whose own enumerable keys are the character indices. Each
            // produced descObj is a one-character string — not an Object —
            // so the spec's ToPropertyDescriptor immediately throws
            // TypeError. Short-circuit here rather than fishing for the
            // implicit iteration semantics.
            if (propsObj->isString(ctx)) {
                const proto::ProtoString* ps = propsObj->asString(ctx);
                if (ps && ps->getSize(ctx) > 0) {
                    signalNativeException(makeNativeError(ctx, "TypeError",
                        "Property description must be an object"));
                    return PROTO_NONE;
                }
            }
            // §19.1.2.4 ObjectDefineProperties step 2: iterate the OWN
            // enumerable keys of Properties; for each, descObj = Get(props, key)
            // — the spec-mandated descriptor lookup that invokes any
            // accessor getter on Properties (test262
            // built-ins/Object/create/15.2.3.5-4-4.js and 4-10..4-40 cover
            // this: `Object.defineProperty(props, "prop", {get: function(){
            // ... return {}; }, enumerable: true})` then
            // `Object.create({}, props)` MUST invoke the getter).  Pre-fix
            // the loop walked the raw OwnAttributes sparse-list (no getter
            // invocation, no enumerable filter, and on accessor properties
            // returned the unrelated `__get_<key>__` sentinel object as
            // descObj — which ToPropertyDescriptor then rejected as
            // "Property description must be an object").
            std::vector<std::string> keys;
            collectOwnKeys(ctx, propsObj, keys, nullptr, /*includeNonEnumerable=*/false);
            for (const std::string& keyStr : keys) {
                if (keyStr == "length") continue;
                const proto::ProtoObject* keyObj = ctx->fromUTF8String(keyStr.c_str());
                const proto::ProtoString* propKey = keyObj ? keyObj->asString(ctx) : nullptr;
                if (!propKey) continue;
                // Get(props, key) — spec requires invoking any accessor getter
                // on Properties.  protoCore's getAttribute(callbacks=true) only
                // fires native-callback accessors, not the JS-level
                // __get_<key>__ sidecar that Object.defineProperty installs,
                // so probe the sidecar explicitly and invoke via
                // callJSFunction.  Without this, accessor-only descriptor
                // entries returned the unrelated undefined sentinel (raw value
                // when __pd_<key>__ marks the slot as accessor) — making the
                // wrapping objectDefineProperty throw "Property description
                // must be an object".
                const proto::ProtoObject* descObj = nullptr;
                std::string gkStr = "__get_" + keyStr + "__";
                const proto::ProtoObject* gko = ctx->fromUTF8String(gkStr.c_str());
                const proto::ProtoString* gk = gko ? gko->asString(ctx) : nullptr;
                if (gk && propsObj->hasOwnAttribute(ctx, gk) == PROTO_TRUE) {
                    const proto::ProtoObject* getter = propsObj->getAttribute(ctx, gk, false);
                    if (getter && getter != PROTO_NONE && getter != getUndefinedSentinel()) {
                        const proto::ProtoList* emptyArgs = ctx->newList();
                        descObj = callJSFunction(ctx, getter, propsObj, emptyArgs);
                        if (hasCallException()) return PROTO_NONE;
                    }
                }
                if (!descObj) {
                    descObj = propsObj->getAttribute(ctx, propKey, true);
                }
                if (!descObj || descObj == PROTO_NONE) continue;
                const proto::ProtoList* dpArgs = ctx->newList();
                dpArgs = dpArgs->appendLast(ctx, result);
                dpArgs = dpArgs->appendLast(ctx, keyObj);
                dpArgs = dpArgs->appendLast(ctx, descObj);
                const proto::ProtoObject* nr = objectDefineProperty(ctx, nullptr, nullptr, dpArgs, nullptr);
                if (nr && nr != PROTO_NONE) result = nr;
                if (hasCallException()) return PROTO_NONE;
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
thread_local std::unordered_map<const proto::ProtoObject*,
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
    if (obj == getUndefinedSentinel()) return true;
    if (obj == PROTO_TRUE || obj == PROTO_FALSE) return true;
    // Fast tagged-pointer checks first; isString last because it can
    // require following an indirection on plain cells. Primitive
    // strings produced by JS literals reach this path as bare
    // ProtoString cells, so isString returns the right answer here.
    if (obj->isBoolean(ctx) || obj->isInteger(ctx) || obj->isDouble(ctx)
        || obj->isFloat(ctx) || obj->isNone(ctx)) return true;
    if (obj->isString(ctx)) return true;
    return false;
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
        // Order matters: addParent prepends, so the LAST added shows
        // up at parents[0]. The composite behavior iterates parents
        // front-to-back and stops at the first non-null putField; with
        // NonExtensibleBehavior at parents[0] it would happily forward
        // existing-key writes to setAttribute, defeating freeze.
        // Add the non-extensible marker first so the frozen marker
        // ends up at parents[0] and gets the first say on every write.
        obj->addParent(ctx, wrapper->getNonExtensibleMarker());
        obj->addParent(ctx, wrapper->getFrozenMarker());
        BehaviorRegistry::instance().invalidateObjectCache(obj);
    }
    // §7.3.16 SetIntegrityLevel("frozen") clears both the writable AND
    // configurable bits on every own property. Pre-fix the markers
    // blocked writes / deletes but the per-property descriptor sidecar
    // still reported writable:true, configurable:true via
    // Object.getOwnPropertyDescriptor (built-ins/Object/freeze/*
    // verifyProperty fixtures).
    {
        // Hot-path hint: freeze always clears writable on every own
        // property, so OrdinarySet must consult __pd_<key>__ from now on
        // for this target.  Stamp the flag once up front.
        const proto::ProtoString* hnw = JSSymbols::hasNonWritableProps(ctx);
        if (hnw) obj->setAttribute(ctx, hnw, PROTO_TRUE);
        const proto::ProtoSparseList* own = obj->getOwnAttributes(ctx);
        if (own) {
            const proto::ProtoSparseListIterator* it = own->getIterator(ctx);
            std::vector<std::string> keysToUpdate;
            while (it && it->hasNext(ctx)) {
                unsigned long raw = it->nextKey(ctx);
                it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
                const proto::ProtoString* k =
                    reinterpret_cast<const proto::ProtoString*>(raw);
                if (!k || isInternalKey(ctx, k)) continue;
                std::string ks;
                k->toUTF8String(ctx, ks);
                keysToUpdate.push_back(std::move(ks));
            }
            for (const auto& ks : keysToUpdate) {
                std::string pdStr = std::string("__pd_") + ks + "__";
                const proto::ProtoObject* pdo = ctx->fromUTF8String(pdStr.c_str());
                const proto::ProtoString* pdk = pdo ? pdo->asString(ctx) : nullptr;
                if (!pdk) continue;
                const proto::ProtoObject* cur = obj->getAttribute(ctx, pdk, false);
                long long bits = (cur && cur != PROTO_NONE && cur->isInteger(ctx))
                    ? cur->asLong(ctx) : 0x7LL;
                bits &= ~0x3LL; // clear writable and configurable
                obj->setAttribute(ctx, pdk, ctx->fromInteger(bits));
            }
        }
    }
    // Array indices live in __elements__, not in the own-attribute
    // SparseList, so the loop above misses them. Stamp a per-index
    // descriptor sidecar for each materialised element so
    // verifyProperty(arrObj, "0", {writable:false, configurable:false})
    // sees the frozen bits (built-ins/Object/freeze/15.2.3.9-2-a-14).
    {
        const proto::ProtoString* isArrK = JSSymbols::isArray(ctx);
        if (isArrK && obj->getAttribute(ctx, isArrK, true) == PROTO_TRUE) {
            const proto::ProtoList* els = getArrayElements(ctx, obj);
            if (els) {
                size_t n = els->getSize(ctx);
                for (size_t i = 0; i < n; ++i) {
                    std::string pdStr = std::string("__pd_") + std::to_string(i) + "__";
                    const proto::ProtoObject* pdo = ctx->fromUTF8String(pdStr.c_str());
                    const proto::ProtoString* pdk = pdo ? pdo->asString(ctx) : nullptr;
                    if (!pdk) continue;
                    const proto::ProtoObject* cur = obj->getAttribute(ctx, pdk, false);
                    long long bits = (cur && cur != PROTO_NONE && cur->isInteger(ctx))
                        ? cur->asLong(ctx) : 0x7LL;
                    bits &= ~0x3LL;
                    obj->setAttribute(ctx, pdk, ctx->fromInteger(bits));
                }
            }
        }
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
        BehaviorRegistry::instance().invalidateObjectCache(obj);
    }
    // §7.3.16 SetIntegrityLevel("sealed"): walk each own property and
    // clear the configurable bit. Pre-fix the markers above prevented
    // future delete / redefine, but getOwnPropertyDescriptor still
    // read the original sidecar bits and reported configurable:true,
    // so verifyProperty fixtures (built-ins/Object/seal/*) failed.
    {
        const proto::ProtoSparseList* own = obj->getOwnAttributes(ctx);
        if (own) {
            const proto::ProtoSparseListIterator* it = own->getIterator(ctx);
            std::vector<std::string> keysToUpdate;
            while (it && it->hasNext(ctx)) {
                unsigned long raw = it->nextKey(ctx);
                it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
                const proto::ProtoString* k =
                    reinterpret_cast<const proto::ProtoString*>(raw);
                if (!k || isInternalKey(ctx, k)) continue;
                std::string ks;
                k->toUTF8String(ctx, ks);
                keysToUpdate.push_back(std::move(ks));
            }
            for (const auto& ks : keysToUpdate) {
                std::string pdStr = std::string("__pd_") + ks + "__";
                const proto::ProtoObject* pdo = ctx->fromUTF8String(pdStr.c_str());
                const proto::ProtoString* pdk = pdo ? pdo->asString(ctx) : nullptr;
                if (!pdk) continue;
                const proto::ProtoObject* cur = obj->getAttribute(ctx, pdk, false);
                long long bits = (cur && cur != PROTO_NONE && cur->isInteger(ctx))
                    ? cur->asLong(ctx) : 0x7LL; // default {w, c, e}
                bits &= ~0x2LL; // clear configurable
                obj->setAttribute(ctx, pdk, ctx->fromInteger(bits));
            }
        }
    }
    // Same fix for Array indices stored in __elements__.
    {
        const proto::ProtoString* isArrK = JSSymbols::isArray(ctx);
        if (isArrK && obj->getAttribute(ctx, isArrK, true) == PROTO_TRUE) {
            const proto::ProtoList* els = getArrayElements(ctx, obj);
            if (els) {
                size_t n = els->getSize(ctx);
                for (size_t i = 0; i < n; ++i) {
                    std::string pdStr = std::string("__pd_") + std::to_string(i) + "__";
                    const proto::ProtoObject* pdo = ctx->fromUTF8String(pdStr.c_str());
                    const proto::ProtoString* pdk = pdo ? pdo->asString(ctx) : nullptr;
                    if (!pdk) continue;
                    const proto::ProtoObject* cur = obj->getAttribute(ctx, pdk, false);
                    long long bits = (cur && cur != PROTO_NONE && cur->isInteger(ctx))
                        ? cur->asLong(ctx) : 0x7LL;
                    bits &= ~0x2LL;
                    obj->setAttribute(ctx, pdk, ctx->fromInteger(bits));
                }
            }
        }
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
        BehaviorRegistry::instance().invalidateObjectCache(obj);
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
        ? args->getAt(ctx, 0) : PROTO_NONE;

    // ECMA-262 §20.1.2.10 step 1: Let obj be ? ToObject(O).
    // null / undefined throw TypeError.
    if (throwIfNullOrUndefined(ctx, obj, "Object.getOwnPropertyNames"))
        return PROTO_NONE;

    // Pass includeNonEnumerable=true to collect all own string properties.
    std::vector<std::string> keys;
    collectOwnKeys(ctx, obj, keys, nullptr, /*includeNonEnumerable=*/true);

    const proto::ProtoObject* result = createNewArray(ctx, nullptr);
    const proto::ProtoString* lenKey  = JSSymbols::length(ctx);
    const proto::ProtoString* isArrKey2 = JSSymbols::isArray(ctx);
    // Use the canonical __elements__ ProtoList storage so JSON.stringify,
    // arrayTryFastGet, and Array.prototype methods see the entries.
    // The prior string-indexed-attribute layout was invisible to those
    // paths — getArrayElements() returned nullptr, so a freshly produced
    // `Object.keys(...)` rendered as '[]' under JSON.stringify.
    const proto::ProtoList* elsList = ctx->newList();
    for (size_t i = 0; i < keys.size(); i++) {
        const proto::ProtoObject* kv = ctx->fromUTF8String(keys[i].c_str());
        elsList = elsList->appendLast(ctx, kv ? kv : PROTO_NONE);
    }
    setArrayElements(ctx, result, elsList);
    if (lenKey)   result = result->setAttribute(ctx, lenKey,   ctx->fromInteger(static_cast<long long>(keys.size())));
    if (isArrKey2) result = result->setAttribute(ctx, isArrKey2, PROTO_TRUE);
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
    // ECMA-262 §19.1.2.12 (ES5) / §20.1.2.12 (ES2015+): Object.getPrototypeOf
    // throws TypeError when called with null/undefined. (Modern spec allows
    // primitives via ToObject, but null and undefined remain TypeError.)
    if (!args || args->getSize(ctx) == 0) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Cannot convert undefined to object"));
        return PROTO_NONE;
    }
    const proto::ProtoObject* obj = args->getAt(ctx, 0);
    if (!obj || obj == PROTO_NONE || obj == getUndefinedSentinel()) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Cannot convert undefined to object"));
        return PROTO_NONE;
    }
    if (obj == getNullSentinel()) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Cannot convert null to object"));
        return PROTO_NONE;
    }
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
    // §10.1.2.1 SetImmutablePrototype-style check: non-extensible
    // objects can only accept a SetPrototypeOf when proto matches the
    // current prototype (no-op). Anything else throws TypeError.
    // Pre-fix Object.setPrototypeOf silently accepted any prototype
    // even on non-extensible targets.
    {
        JSContextWrapper* w = JSContextWrapper::current();
        if (w && obj->hasParent(ctx, w->getNonExtensibleMarker())) {
            const proto::ProtoObject* current = nullptr;
            auto ovrIt = t_jsProtoMap.find(obj);
            if (ovrIt != t_jsProtoMap.end()) current = ovrIt->second;
            else current = obj->getPrototype(ctx);
            const proto::ProtoObject* requested = proto;
            if (proto == getNullSentinel()) requested = getNullSentinel();
            if (current != requested) {
                signalNativeException(makeNativeError(ctx, "TypeError",
                    "Object.setPrototypeOf: target is non-extensible"));
                return PROTO_NONE;
            }
            return obj;
        }
    }
    if (proto == getNullSentinel()) {
        // Setting proto to null per spec §20.1.2.21 step 5: STORE the
        // null sentinel (not erase) so subsequent getPrototypeOf
        // returns null instead of falling back to the natural parent.
        // Pre-fix this branch erased the override, so
        // Object.setPrototypeOf(o, null) had no observable effect.
        setJSProtoOverride(ctx, obj, getNullSentinel());
    } else if (proto && proto != PROTO_NONE) {
        // 3-arg form: in addition to writing the map, rebinds the
        // protoCore parent chain via ProtoObject::setParents.  Read
        // paths through resolveFieldOOP see the new prototype natively
        // and no longer fall through to the t_jsProtoMap extension.
        setJSProtoOverride(ctx, obj, proto);
    }
    // Spec: returns the modified object.
    return obj;
}

// ---------------------------------------------------------------------------
// coercePropNameToKey — convert any JS value to a property name ProtoString*
// per ECMAScript ToPropertyKey (supports Symbols natively).
// ---------------------------------------------------------------------------
static const proto::ProtoString* coercePropNameToKey(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* nameObj)
{
    std::string out;
    const proto::ProtoObject* current = nameObj;

    // ToPropertyKey(argument):
    // 1. Let key be ? ToPrimitive(argument, hint String).
    if (current && !current->isString(ctx) && !current->isInteger(ctx) && 
        !current->isDouble(ctx) && !current->isFloat(ctx) && 
        !current->isBoolean(ctx) && current != getNullSentinel() && 
        current != getUndefinedSentinel() && current != PROTO_NONE) 
    {
        auto isCallable = [&](const proto::ProtoObject* fn) -> bool {
            if (!fn || fn == PROTO_NONE || fn == getUndefinedSentinel() || fn == getNullSentinel()) return false;
            if (fn->isMethod(ctx)) return true;
            const proto::ProtoString* bcKey = JSSymbols::bytecodeId(ctx);
            if (bcKey && fn->hasAttribute(ctx, bcKey) == PROTO_TRUE) return true;
            const proto::ProtoString* nfKey = JSSymbols::nativeFn(ctx);
            if (nfKey && fn->hasAttribute(ctx, nfKey) == PROTO_TRUE) return true;
            return false;
        };

        const proto::ProtoObject* prim = nullptr;
        // 1. Try toString()
        const proto::ProtoString* tsKey = JSSymbols::toString(ctx);
        const proto::ProtoObject* tsFn = tsKey ? current->getAttribute(ctx, tsKey, true) : nullptr;
        if (isCallable(tsFn)) {
            const proto::ProtoObject* res = callJSFunction(ctx, tsFn, current, ctx->newList());
            if (!hasCallException() && res && (res->isString(ctx) || res->isInteger(ctx) || res->isDouble(ctx) || res->isFloat(ctx) || res->isBoolean(ctx) || res == getNullSentinel() || res == getUndefinedSentinel())) {
                prim = res;
            }
        }
        
        // 2. Try valueOf()
        if (!prim && !hasCallException()) {
            const proto::ProtoString* voKey = ctx->fromUTF8String("valueOf")->asString(ctx);
            const proto::ProtoObject* voFn = voKey ? current->getAttribute(ctx, voKey, true) : nullptr;
            if (isCallable(voFn)) {
                const proto::ProtoObject* res = callJSFunction(ctx, voFn, current, ctx->newList());
                if (!hasCallException() && res && (res->isString(ctx) || res->isInteger(ctx) || res->isDouble(ctx) || res->isFloat(ctx) || res->isBoolean(ctx) || res == getNullSentinel() || res == getUndefinedSentinel())) {
                    prim = res;
                }
            }
        }
        
        if (prim) {
            current = prim;
        } else {
            if (!hasCallException()) {
                signalNativeException(makeNativeError(ctx, "TypeError", "Cannot convert object to primitive value"));
            }
            return nullptr;
        }
    }

    if (!current || current == PROTO_NONE || current == getUndefinedSentinel()) {
        out = "undefined";
    } else if (current->isString(ctx)) {
        return current->asString(ctx);
    } else if (current->isInteger(ctx)) {
        out = std::to_string(current->asLong(ctx));
    } else if (current->isDouble(ctx) || current->isFloat(ctx)) {
        double d = current->asDouble(ctx);
        if (std::isnan(d)) out = "NaN";
        else if (std::isinf(d)) out = d < 0 ? "-Infinity" : "Infinity";
        else if (d == 0.0) out = "0";
        else {
            double absD = std::abs(d);
            char buf[128];
            if (absD >= 1e21 || (absD > 0 && absD < 1e-6)) {
                snprintf(buf, sizeof(buf), "%.15g", d);
                out = buf;
                for (auto &c : out) if (c == 'E') c = 'e';
                size_t ePos = out.find('e');
                if (ePos != std::string::npos) {
                    std::string base = out.substr(0, ePos);
                    std::string exp  = out.substr(ePos + 1);
                    if (!exp.empty() && exp[0] == '+') exp.erase(0, 1);
                    bool neg = false;
                    if (!exp.empty() && exp[0] == '-') { neg = true; exp.erase(0, 1); }
                    while (exp.size() > 1 && exp[0] == '0') exp.erase(0, 1);
                    out = base + "e" + (neg ? "-" : "+") + exp;
                }
            } else {
                snprintf(buf, sizeof(buf), "%.15g", d);
                out = buf;
                if (out.find('e') != std::string::npos || out.find('E') != std::string::npos) {
                    snprintf(buf, sizeof(buf), "%.20f", d);
                    out = buf;
                    if (out.find('.') != std::string::npos) {
                        while (out.back() == '0') out.pop_back();
                        if (out.back() == '.') out.pop_back();
                    }
                }
            }
        }
    } else if (current->isBoolean(ctx)) {
        out = current == PROTO_TRUE ? "true" : "false";
    } else if (current == getNullSentinel()) {
        out = "null";
    } else {
        out = "[object Object]";
    }

    return ctx->fromUTF8String(out.c_str())->asString(ctx);
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
    if (!ctx) return PROTO_NONE;
    // ECMA-262 §20.1.2.4 step 1: target must be an Object. Treat
    // missing args as undefined to trigger the same TypeError path.
    const proto::ProtoObject* target = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0) : PROTO_NONE;
    const proto::ProtoObject* desc = (args && args->getSize(ctx) > 2)
        ? args->getAt(ctx, 2) : PROTO_NONE;

    if (!target || target == PROTO_NONE || target == getNullSentinel() || target == getUndefinedSentinel() ||
        target->isBoolean(ctx) || target->isInteger(ctx) ||
        target->isDouble(ctx)  || target->isFloat(ctx)   ||
        target->isString(ctx)) {
        signalNativeException(makeNativeError(ctx, "TypeError", "Object.defineProperty called on non-object"));
        return PROTO_NONE;
    }

    const proto::ProtoObject* propNameObj = args->getAt(ctx, 1);
    const proto::ProtoString* k = coercePropNameToKey(ctx, propNameObj);
    if (!k) return target;

    if (!desc || desc == PROTO_NONE || desc == getNullSentinel() || desc == getUndefinedSentinel() ||
        desc->isBoolean(ctx) || desc->isInteger(ctx) ||
        desc->isDouble(ctx)  || desc->isFloat(ctx)   ||
        desc->isString(ctx)) {
        signalNativeException(makeNativeError(ctx, "TypeError", "Property description must be an object"));
        return PROTO_NONE;
    }
    // §20.1.2.4 step 2 invokes ToPropertyDescriptor which gates on
    // Type(O) === Object; a Symbol primitive (carried in protoJS as an
    // object with the __is_symbol__ marker) is treated as Object by
    // the type system but the spec rejects it as a descriptor —
    // built-ins/Object/defineProperty/property-description-must-be-an-
    // object-not-symbol.js expects the TypeError abrupt.
    {
        const proto::ProtoString* symK = JSSymbols::isSymbol(ctx);
        if (symK && desc->getAttribute(ctx, symK, false) == PROTO_TRUE) {
            signalNativeException(makeNativeError(ctx, "TypeError",
                "Property description must be an object"));
            return PROTO_NONE;
        }
    }

    bool propExists = (target->hasOwnAttribute(ctx, k) == PROTO_TRUE);
    const proto::ProtoObject* existingVal = propExists ? target->getAttribute(ctx, k, false) : nullptr;
    std::string kstr;
    k->toUTF8String(ctx, kstr);
    // ECMA-262 §10.4.2.4 ArraySetLength considers each indexed element
    // (k in [0, length)) an own data property. protoJS keeps these in
    // the native __elements__ ProtoList, NOT as string-keyed own
    // attributes, so hasOwnAttribute("0") returns false on
    // `[undefined]`. The descriptor-merge path below would then treat
    // the property as "newly created", default writable / enumerable /
    // configurable to false, and Object.defineProperty(arr, "0",
    // {value:100}) returned a non-enumerable, non-writable,
    // non-configurable slot (built-ins/Object/defineProperty/15.2.3.6-
    // 4-260 and a long tail of the "data descriptor partial redefine"
    // tests caught this).  Recognise indexed entries on arrays as own
    // data properties whose default descriptor matches the spec
    // ({writable, enumerable, configurable}).
    if (!propExists) {
        const proto::ProtoString* isArrK = JSSymbols::isArray(ctx);
        const proto::ProtoObject* isArrV = isArrK
            ? target->getAttribute(ctx, isArrK, true) : nullptr;
        if (isArrV == PROTO_TRUE && !kstr.empty()) {
            char* end = nullptr;
            long long v = std::strtoll(kstr.c_str(), &end, 10);
            if (end && *end == '\0' && v >= 0 && std::to_string(v) == kstr) {
                const proto::ProtoString* lenK = JSSymbols::length(ctx);
                const proto::ProtoObject* lenV = lenK
                    ? target->getAttribute(ctx, lenK, false) : nullptr;
                long long len = (lenV && lenV != PROTO_NONE && lenV->isInteger(ctx))
                    ? lenV->asLong(ctx) : -1;
                if (len >= 0 && v < len) {
                    propExists = true;
                    // Pull the value from __elements__ first (where
                    // Array indices actually live); fall back to a
                    // sparse / own-attribute slot only when the
                    // native list lacks that index. Pre-fix this read
                    // came from `getAttribute("0", false)` which
                    // returned PROTO_NONE for materialised entries
                    // and then stored undefined back over the
                    // __elements__ value (built-ins/Object/
                    // defineProperties/15.2.3.7-6-a-251 expected the
                    // 12 to survive an enumerable-only redefine).
                    const proto::ProtoList* els =
                        protojs::getArrayElements(ctx, target);
                    if (els && v < static_cast<long long>(els->getSize(ctx))) {
                        existingVal = els->getAt(ctx, static_cast<int>(v));
                    }
                    if (!existingVal || existingVal == PROTO_NONE) {
                        existingVal = target->getAttribute(ctx, k, false);
                    }
                    if (!existingVal) existingVal = getUndefinedSentinel();
                }
            }
        }
    }
    // ECMA-262 §10.4.2.1 ArraySetLength: when defining the `length`
    // property on an Array exotic object, the spec runs the new
    // value through ToUint32 and SameValue(numberLen, ToNumber(len))
    // — anything else throws RangeError, NOT TypeError.  Pre-fix the
    // generic defineProperty path forwarded the invalid value to
    // setAttribute which surfaced a TypeError ("invalid length"
    // somewhere down the stack), so test262 tests asserting the
    // spec's RangeError mistyped the failure.
    if (kstr == "length") {
        const proto::ProtoString* isArrKey = JSSymbols::isArray(ctx);
        const proto::ProtoObject* isArrV = isArrKey
            ? target->getAttribute(ctx, isArrKey, true) : PROTO_NONE;
        if (isArrV == PROTO_TRUE) {
            const proto::ProtoObject* valKo = ctx->fromUTF8String("value");
            const proto::ProtoString* valK = valKo ? valKo->asString(ctx) : nullptr;
            const proto::ProtoObject* newVal = valK
                ? desc->getAttribute(ctx, valK, false) : nullptr;
            if (newVal && newVal != PROTO_NONE) {
                const proto::ProtoObject* numVal = newVal;
                if (!newVal->isInteger(ctx) && !newVal->isDouble(ctx)
                    && !newVal->isFloat(ctx)) {
                    numVal = jsToNumber(ctx, newVal);
                    if (hasCallException()) return PROTO_NONE;
                }
                double d = 0.0;
                bool gotNum = false;
                if (numVal) {
                    if (numVal->isInteger(ctx)) { d = static_cast<double>(numVal->asLong(ctx)); gotNum = true; }
                    else if (numVal->isDouble(ctx) || numVal->isFloat(ctx)) { d = numVal->asDouble(ctx); gotNum = true; }
                }
                if (!gotNum || std::isnan(d) || std::isinf(d)
                    || d < 0 || d > 4294967295.0
                    || d != std::trunc(d)) {
                    signalNativeException(makeNativeError(ctx, "RangeError",
                        "Invalid array length"));
                    return PROTO_NONE;
                }
            }
        }
    }
    std::string pdKeyStr = "__pd_" + kstr + "__";
    const proto::ProtoObject* pko = ctx->fromUTF8String(pdKeyStr.c_str());
    const proto::ProtoString* pdk = pko ? pko->asString(ctx) : nullptr;
    
    const proto::ProtoObject* existingBitsObj = (pdk && propExists && target->hasOwnAttribute(ctx, pdk) == PROTO_TRUE) 
        ? target->getAttribute(ctx, pdk, false) : nullptr;
    long existingBits = (existingBitsObj && existingBitsObj->isInteger(ctx)) ? existingBitsObj->asLong(ctx) : 0x7;

    if (propExists && !(existingBits & 0x2)) { // configurable=false (bit 1)
        // ECMA-262 §10.1.6.3 ValidateAndApplyPropertyDescriptor step 3:
        // an empty descriptor (no fields specified) is always allowed;
        // a value-only redefine is allowed when SameValue(new, current)
        // holds — even for non-configurable, non-writable data props.
        // Pre-fix we rejected every redefine of a non-configurable
        // property, so Object.defineProperty(o,'a',{value:1}) threw
        // even when the value matched the existing slot.
        auto descKeyValue = [&](const char* name) -> const proto::ProtoObject* {
            const proto::ProtoObject* ko = ctx->fromUTF8String(name);
            const proto::ProtoString* dk = ko ? ko->asString(ctx) : nullptr;
            return dk ? desc->getAttribute(ctx, dk, false) : nullptr;
        };
        auto descHasField = [&](const char* name) -> bool {
            const proto::ProtoObject* v = descKeyValue(name);
            if (v && v != PROTO_NONE) return true;
            // Accessor sidecar form.
            std::string nstr = name;
            std::string gkStr = "__get_" + nstr + "__";
            const proto::ProtoObject* gko = ctx->fromUTF8String(gkStr.c_str());
            const proto::ProtoString* gk = gko ? gko->asString(ctx) : nullptr;
            if (gk && desc->hasAttribute(ctx, gk) == PROTO_TRUE) return true;
            return false;
        };
        bool hasValue = descHasField("value");
        bool hasWritable = descHasField("writable");
        bool hasEnum = descHasField("enumerable");
        bool hasConf = descHasField("configurable");
        bool hasGet = descHasField("get");
        bool hasSet = descHasField("set");
        bool anyField = hasValue || hasWritable || hasEnum || hasConf || hasGet || hasSet;
        if (!anyField) {
            return target; // empty descriptor: no-op.
        }
        auto sameValue = [&](const proto::ProtoObject* a, const proto::ProtoObject* b) -> bool {
            if (a == b) return true;
            if (!a || !b) return false;
            if (a->isInteger(ctx) && b->isInteger(ctx))
                return a->asLong(ctx) == b->asLong(ctx);
            if ((a->isDouble(ctx) || a->isFloat(ctx)) && (b->isDouble(ctx) || b->isFloat(ctx))) {
                double da = a->asDouble(ctx), db = b->asDouble(ctx);
                if (std::isnan(da) && std::isnan(db)) return true;
                if (da == 0.0 && db == 0.0) return std::signbit(da) == std::signbit(db);
                return da == db;
            }
            if (a->isString(ctx) && b->isString(ctx)) {
                std::string sa, sb;
                a->asString(ctx)->toUTF8String(ctx, sa);
                b->asString(ctx)->toUTF8String(ctx, sb);
                return sa == sb;
            }
            return false;
        };
        // §10.1.6.3 ValidateAndApplyPropertyDescriptor step 4:
        // every specified field must match the existing slot for the
        // redefine to be permitted. Pre-fix only the value-only case
        // was honoured, so
        //   Object.defineProperty(o,'f',{value:undefined,
        //                                writable:false,
        //                                configurable:false});
        //   Object.defineProperty(o,'f',{value:undefined,
        //                                writable:false,
        //                                configurable:false});
        // threw on the redefine even though both descriptors agreed
        // with the slot.  Extend the equality check across writable /
        // enumerable / configurable so any explicit redefine that
        // agrees with the current bits is allowed.
        auto descBoolField = [&](const char* name, bool defaultV) -> bool {
            const proto::ProtoObject* v = descKeyValue(name);
            if (!v || v == PROTO_NONE) return defaultV;
            if (v == PROTO_TRUE) return true;
            if (v == PROTO_FALSE) return false;
            if (v == getUndefinedSentinel() || v == getNullSentinel()) return false;
            if (v->isBoolean(ctx)) return v->asBoolean(ctx);
            if (v->isInteger(ctx)) return v->asLong(ctx) != 0;
            if (v->isString(ctx)) {
                const proto::ProtoString* s = v->asString(ctx);
                return s && s->getSize(ctx) > 0;
            }
            return true;
        };
        const bool curWritable     = (existingBits & 0x1) != 0;
        const bool curConfigurable = (existingBits & 0x2) != 0;
        const bool curEnumerable   = (existingBits & 0x4) != 0;
        if (hasGet || hasSet) {
            // §10.1.6.3 step 4.c rejects converting a data property
            // into an accessor (or replacing the accessor functions
            // themselves) on a non-configurable slot — UNLESS the
            // specified get / set are SameValue with the existing ones
            // (built-ins/Object/defineProperty/15.2.3.6-4-257 redefines
            // {get:undefined} on an already-undefined-getter accessor
            // and expects success). Probe the existing __get_<key>__ /
            // __set_<key>__ sidecars and compare.
            auto fetchExisting = [&](const std::string& prefix) -> const proto::ProtoObject* {
                std::string sk = prefix + kstr + "__";
                const proto::ProtoObject* sko = ctx->fromUTF8String(sk.c_str());
                const proto::ProtoString* sks = sko ? sko->asString(ctx) : nullptr;
                if (!sks) return nullptr;
                return target->getAttribute(ctx, sks, false);
            };
            auto fetchDesc = [&](const char* name) -> const proto::ProtoObject* {
                const proto::ProtoObject* ko = ctx->fromUTF8String(name);
                const proto::ProtoString* dk = ko ? ko->asString(ctx) : nullptr;
                return dk ? desc->getAttribute(ctx, dk, false) : nullptr;
            };
            auto normaliseAccessorFn = [&](const proto::ProtoObject* v) -> const proto::ProtoObject* {
                if (!v || v == PROTO_NONE || v == getUndefinedSentinel())
                    return getUndefinedSentinel();
                return v;
            };
            bool ok = true;
            if (hasGet) {
                const proto::ProtoObject* existingGet = normaliseAccessorFn(fetchExisting("__get_"));
                const proto::ProtoObject* descGet = normaliseAccessorFn(fetchDesc("get"));
                if (!sameValue(descGet, existingGet)) ok = false;
            }
            if (ok && hasSet) {
                const proto::ProtoObject* existingSet = normaliseAccessorFn(fetchExisting("__set_"));
                const proto::ProtoObject* descSet = normaliseAccessorFn(fetchDesc("set"));
                if (!sameValue(descSet, existingSet)) ok = false;
            }
            if (!ok) {
                signalNativeException(makeNativeError(ctx, "TypeError",
                    "Cannot redefine non-configurable property"));
                return PROTO_NONE;
            }
            return target; // accept the no-op redefine
        }
        bool ok = true;
        // §10.1.6.3 step 4.h: a value change on a non-configurable but
        // WRITABLE data property is allowed even when SameValue would
        // fail (this is what makes `arr.length = 5` and
        // `Object.defineProperty(arr, 'length', {value:5})` work — the
        // length slot is non-configurable but writable). Without this
        // branch the all-field SameValue gate rejected every Array-
        // length growth (built-ins/Object/defineProperty/15.2.3.6-4-
        // 159 and the wider "writable but non-configurable" set).
        if (hasValue && !curWritable) {
            const proto::ProtoObject* newVal = descKeyValue("value");
            if (!sameValue(newVal, existingVal)) ok = false;
        }
        // Writable can only transition false → true when configurable;
        // false ↔ false / true ↔ true are SameValue ok'd by descBoolField.
        if (ok && hasWritable) {
            bool newW = descBoolField("writable", false);
            if (curWritable && !newW) {
                // true → false is allowed even when non-configurable.
            } else if (newW != curWritable) {
                ok = false; // false → true not allowed on non-configurable
            }
        }
        if (ok && hasEnum     && descBoolField("enumerable", false) != curEnumerable) ok = false;
        if (ok && hasConf) {
            bool newC = descBoolField("configurable", false);
            // Going to non-configurable is fine; going to configurable
            // from non-configurable is forbidden.
            if (newC && !curConfigurable) ok = false;
        }
        if (ok) {
            // Apply the in-place updates (value change + bit toggles).
            if (hasValue) {
                const proto::ProtoObject* newVal = descKeyValue("value");
                if (newVal && newVal != PROTO_NONE) {
                    target = target->setAttribute(ctx, k, newVal);
                }
            }
            long long newBits = existingBits;
            if (hasWritable) {
                if (descBoolField("writable", false)) newBits |= 0x1; else newBits &= ~0x1;
            }
            if (hasConf) {
                if (descBoolField("configurable", false)) newBits |= 0x2; else newBits &= ~0x2;
            }
            if (hasEnum) {
                if (descBoolField("enumerable", false)) newBits |= 0x4; else newBits &= ~0x4;
            }
            if (pdk && newBits != existingBits) {
                target = target->setAttribute(ctx, pdk, ctx->fromInteger(newBits));
            }
            return target;
        }
        signalNativeException(makeNativeError(ctx, "TypeError", "Cannot redefine non-configurable property"));
        return PROTO_NONE;
    }

    auto descHasKey = [&](const char* name) -> bool {
        const proto::ProtoObject* ko2 = ctx->fromUTF8String(name);
        const proto::ProtoString* k2  = ko2 ? ko2->asString(ctx) : nullptr;
        if (!k2) return false;
        if (desc->getAttribute(ctx, k2, false) != PROTO_NONE) return true;
        
        std::string nstr = name;
        std::string gkStr = "__get_" + nstr + "__";
        const proto::ProtoString* gk = ctx->fromUTF8String(gkStr.c_str())->asString(ctx);
        if (gk && desc->hasAttribute(ctx, gk) == PROTO_TRUE) return true;
        
        std::string skStr = "__set_" + nstr + "__";
        const proto::ProtoString* sk = ctx->fromUTF8String(skStr.c_str())->asString(ctx);
        if (sk && desc->hasAttribute(ctx, sk) == PROTO_TRUE) return true;
        
        return false;
    };

    auto jsGetAttribute = [&](const proto::ProtoObject* obj, const proto::ProtoString* key) -> const proto::ProtoObject* {
        if (!obj || obj == PROTO_NONE || !key) return getUndefinedSentinel();
        
        const proto::ProtoObject* curr = obj;
        while (curr && curr != PROTO_NONE) {
            if (curr->hasOwnAttribute(ctx, key) == PROTO_TRUE) {
                std::string ks;
                key->toUTF8String(ctx, ks);
                std::string gkStr = "__get_" + ks + "__";
                const proto::ProtoObject* gko = ctx->fromUTF8String(gkStr.c_str());
                const proto::ProtoString* gk = gko ? gko->asString(ctx) : nullptr;
                
                if (gk && curr->hasOwnAttribute(ctx, gk) == PROTO_TRUE) {
                    const proto::ProtoObject* getter = curr->getAttribute(ctx, gk, false);
                    if (getter && getter != PROTO_NONE && getter != getUndefinedSentinel()) {
                        const proto::ProtoList* emptyArgs = ctx->newList();
                        const proto::ProtoObject* res = callJSFunction(ctx, getter, obj, emptyArgs);
                        if (hasCallException()) return PROTO_NONE;
                        return res ? res : getUndefinedSentinel();
                    }
                }
                return curr->getAttribute(ctx, key, false);
            }
            curr = curr->getPrototype(ctx);
        }
        return getUndefinedSentinel();
    };

    auto getBoolProp = [&](const char* name, bool defaultVal) -> bool {
        if (!descHasKey(name)) return defaultVal;
        const proto::ProtoObject* ko2 = ctx->fromUTF8String(name);
        const proto::ProtoString* k2  = ko2 ? ko2->asString(ctx) : nullptr;
        const proto::ProtoObject* d = jsGetAttribute(desc, k2);
        if (hasCallException()) return defaultVal;
        if (!d || d == PROTO_NONE || d == getUndefinedSentinel() || d == getNullSentinel()) return false;
        if (d->isBoolean(ctx)) return (d == PROTO_TRUE);
        if (d->isInteger(ctx)) return (d->asLong(ctx) != 0);
        if (d->isDouble(ctx) || d->isFloat(ctx)) {
            double v = d->asDouble(ctx);
            return (!std::isnan(v) && v != 0.0 && v != -0.0);
        }
        if (d->isString(ctx)) {
            const proto::ProtoString* s = d->asString(ctx);
            return s && s->getSize(ctx) > 0;
        }
        return true; 
    };

    auto isCallable = [&](const proto::ProtoObject* fn) -> bool {
        if (!fn || fn == PROTO_NONE || fn == getUndefinedSentinel() || fn == getNullSentinel()) return false;
        if (fn->isMethod(ctx)) return true;
        const proto::ProtoString* bcKey = JSSymbols::bytecodeId(ctx);
        if (bcKey && fn->hasAttribute(ctx, bcKey) == PROTO_TRUE) return true;
        const proto::ProtoString* nfKey = JSSymbols::nativeFn(ctx);
        if (nfKey && fn->hasAttribute(ctx, nfKey) == PROTO_TRUE) return true;
        return false;
    };

    auto getFnProp = [&](const char* name) -> const proto::ProtoObject* {
        if (!descHasKey(name)) return nullptr;
        const proto::ProtoObject* ko2 = ctx->fromUTF8String(name);
        const proto::ProtoString* k2  = ko2 ? ko2->asString(ctx) : nullptr;
        const proto::ProtoObject* v = jsGetAttribute(desc, k2);
        if (hasCallException()) return PROTO_NONE;
        if (!v || v == PROTO_NONE || v == getUndefinedSentinel()) return getUndefinedSentinel();
        
        if (!isCallable(v)) {
            signalNativeException(makeNativeError(ctx, "TypeError", (std::string(name) + " must be a function").c_str()));
            return PROTO_NONE;
        }
        return v;
    };

    const proto::ProtoObject* getter = getFnProp("get");
    if (hasCallException()) return PROTO_NONE;
    const proto::ProtoObject* setter = getFnProp("set");
    if (hasCallException()) return PROTO_NONE;

    bool hasGet = descHasKey("get");
    bool hasSet = descHasKey("set");
    bool isAccessor = hasGet || hasSet;

    if (isAccessor && (descHasKey("value") || descHasKey("writable"))) {
        signalNativeException(makeNativeError(ctx, "TypeError", "Cannot specify both accessors and value/writable"));
        return PROTO_NONE;
    }

    if (!propExists) {
        JSContextWrapper* wrapper = JSContextWrapper::current();
        if (wrapper && target->hasParent(ctx, wrapper->getNonExtensibleMarker())) {
            signalNativeException(makeNativeError(ctx, "TypeError", "Cannot add property to non-extensible object"));
            return PROTO_NONE;
        }
    }

    bool writable     = getBoolProp("writable",     propExists ? ((existingBits & 0x1) != 0) : false);
    bool configurable = getBoolProp("configurable",  propExists ? ((existingBits & 0x2) != 0) : false);
    bool enumerable   = getBoolProp("enumerable",    propExists ? ((existingBits & 0x4) != 0) : false);

    if (isAccessor) {
        target = target->setAttribute(ctx, k, getUndefinedSentinel());
        // Hot-path hint: tag the target with __has_accessor_props__ so
        // resolvePutFieldOOP (every obj[key]=val write) can skip the
        // entire __set_<key>__/__get_<key>__ chain-walk probe when no
        // accessor descriptor exists anywhere reachable from the target.
        {
            const proto::ProtoString* hap = JSSymbols::hasAccessorProps(ctx);
            if (hap) target = target->setAttribute(ctx, hap, PROTO_TRUE);
        }
        std::string gkStr = "__get_" + kstr + "__";
        const proto::ProtoString* gk = ctx->fromUTF8String(gkStr.c_str())->asString(ctx);
        if (gk) {
            const proto::ProtoObject* gVal = (getter && getter != getUndefinedSentinel()) ? getter : nullptr;
            target = target->setAttribute(ctx, gk, gVal);
        }
        std::string skStr = "__set_" + kstr + "__";
        const proto::ProtoString* sk = ctx->fromUTF8String(skStr.c_str())->asString(ctx);
        if (sk) {
            const proto::ProtoObject* sVal = (setter && setter != getUndefinedSentinel()) ? setter : nullptr;
            target = target->setAttribute(ctx, sk, sVal);
            // Hot-path hint: when the setter is installed at a numeric
            // array-index key, tag the target with __has_indexed_setters__
            // so arrSet (per-element loop in arrayPush etc.) can skip the
            // expensive __set_<idx>__ probe on every element when no
            // accessor descriptor is present on the prototype chain.
            // Validate via round-trip: only counts as an "indexed" key if
            // ToUint32(kstr) == kstr exactly (canonical integer string).
            if (sVal && !kstr.empty()) {
                const char* c = kstr.c_str();
                bool allDigits = true;
                for (size_t i = 0; i < kstr.size(); ++i) {
                    if (c[i] < '0' || c[i] > '9') { allDigits = false; break; }
                }
                // Reject leading zero except for the bare "0".
                if (allDigits && (kstr.size() == 1 || c[0] != '0')) {
                    const proto::ProtoString* his = JSSymbols::hasIndexedSetters(ctx);
                    if (his) target = target->setAttribute(ctx, his, PROTO_TRUE);
                }
            }
        }
    } else {
        // §10.1.6.3 step 4: a redefine that touches NEITHER value /
        // writable nor get / set is a "generic" descriptor — only the
        // enumerable / configurable bits change. Pre-fix this branch
        // unconditionally stripped __get_<key>__ / __set_<key>__ even
        // when the descriptor lacked those fields, so
        //   Object.defineProperty(o, 'foo', { get, set, enumerable:true });
        //   Object.defineProperty(o, 'foo', { enumerable:false });
        // turned an accessor into a plain `undefined` data slot
        // (built-ins/Object/defineProperty/15.2.3.6-4-82-7 caught this).
        const bool descTouchesData = descHasKey("value") || descHasKey("writable");
        bool propExistsAsAccessor = false;
        if (propExists && !descTouchesData) {
            std::string gkStr = "__get_" + kstr + "__";
            const proto::ProtoObject* gko = ctx->fromUTF8String(gkStr.c_str());
            const proto::ProtoString* gk = gko ? gko->asString(ctx) : nullptr;
            std::string skStr = "__set_" + kstr + "__";
            const proto::ProtoObject* sko = ctx->fromUTF8String(skStr.c_str());
            const proto::ProtoString* sk = sko ? sko->asString(ctx) : nullptr;
            if ((gk && target->hasOwnAttribute(ctx, gk) == PROTO_TRUE)
                || (sk && target->hasOwnAttribute(ctx, sk) == PROTO_TRUE)) {
                propExistsAsAccessor = true;
            }
        }
        if (propExistsAsAccessor) {
            // Generic redefine over an existing accessor: keep the
            // accessor in place; only the descriptor bits change below.
        } else {
            const proto::ProtoObject* val = nullptr;
            if (descHasKey("value")) {
                const proto::ProtoObject* koV = ctx->fromUTF8String("value");
                val = jsGetAttribute(desc, koV ? koV->asString(ctx) : nullptr);
                if (hasCallException()) return PROTO_NONE;
            }

            const proto::ProtoObject* storedVal = nullptr;
            if (val) {
                storedVal = (val == PROTO_NONE) ? getUndefinedSentinel() : val;
            } else if (propExists) {
                storedVal = existingVal;
            } else {
                storedVal = getUndefinedSentinel();
            }
            target = target->setAttribute(ctx, k, storedVal);

            // Remove accessor sidecars if transitioning to data property.
            std::string gkStr = "__get_" + kstr + "__";
            const proto::ProtoString* gk = ctx->fromUTF8String(gkStr.c_str())->asString(ctx);
            if (gk) target = target->setAttribute(ctx, gk, nullptr);
            std::string skStr = "__set_" + kstr + "__";
            const proto::ProtoString* sk = ctx->fromUTF8String(skStr.c_str())->asString(ctx);
            if (sk) target = target->setAttribute(ctx, sk, nullptr);
        }
    }

    uint8_t bits = (writable ? 0x1 : 0) | (configurable ? 0x2 : 0) | (enumerable ? 0x4 : 0);
    if (pdk) target = target->setAttribute(ctx, pdk, ctx->fromInteger((long long)bits));
    // Hot-path hint: if the descriptor leaves writable=false, tag the
    // target with __has_nonwritable_props__ so resolvePutFieldOOP can
    // skip the __pd_<key>__ probe entirely on writes when every property
    // along the chain is writable (the universal case).
    if (!writable) {
        const proto::ProtoString* hnw = JSSymbols::hasNonWritableProps(ctx);
        if (hnw) target = target->setAttribute(ctx, hnw, PROTO_TRUE);
    }

    // §10.4.2.4 ArraySetLength: shrinking Array.length via
    // defineProperty truncates __elements__ and removes own
    // numeric-key entries at the trailing indices. Pre-fix
    //   Object.defineProperty(arr, "length", { value: 1 })
    // updated the stored length but left __elements__ at its old
    // size, so iteration / hasOwnProperty / length comparisons still
    // saw the trailing entries (built-ins/Object/defineProperties/
    // 15.2.3.7-6-a-157 expected hasOwn("1") false after length=1).
    if (kstr == "length") {
        const proto::ProtoString* isArrK = JSSymbols::isArray(ctx);
        const proto::ProtoObject* isArrV = isArrK
            ? target->getAttribute(ctx, isArrK, true) : nullptr;
        if (isArrV == PROTO_TRUE) {
            const proto::ProtoString* lenK = JSSymbols::length(ctx);
            const proto::ProtoObject* newLenV = lenK
                ? target->getAttribute(ctx, lenK, false) : nullptr;
            long long newLen = (newLenV && newLenV != PROTO_NONE && newLenV->isInteger(ctx))
                ? newLenV->asLong(ctx) : -1;
            if (newLen >= 0) {
                const proto::ProtoList* curEls = protojs::getArrayElements(ctx, target);
                if (curEls) {
                    size_t curSz = curEls->getSize(ctx);
                    if (static_cast<long long>(curSz) > newLen) {
                        const proto::ProtoList* trimmed = ctx->newList();
                        for (long long i = 0; i < newLen; ++i)
                            trimmed = trimmed->appendLast(ctx, curEls->getAt(ctx, static_cast<int>(i)));
                        protojs::setArrayElements(ctx, target, trimmed);
                    }
                }
                // Also delete any indexed string-keyed slots beyond newLen.
                int misses = 0;
                for (long long i = newLen; i < newLen + 1000LL && misses < 8; ++i) {
                    const proto::ProtoString* ik = JSSymbols::indexKey(ctx, static_cast<uint32_t>(i));
                    if (!ik) break;
                    if (target->hasOwnAttribute(ctx, ik) == PROTO_TRUE) {
                        target = target->removeAttribute(ctx, ik);
                        misses = 0;
                    } else {
                        misses++;
                    }
                }
            }
        }
    }
    // ECMA-262 §10.4.2.4 ArraySetLength step 6.f: if the redefine adds
    // (or replaces) an indexed property at i ≥ length, length must be
    // updated to i + 1. The OP_put_array_el path already bumps length;
    // Object.defineProperty(arr, "5", { value: 3 }) reaches us through
    // the generic setAttribute path and used to leave length stuck at
    // its prior value (built-ins/Object/defineProperty/15.2.3.6-4-276,
    // and built-ins/Object/defineProperties/15.2.3.7-{5-b-103,6-a-144,
    // 6-a-291}).
    if (!kstr.empty()) {
        char* end = nullptr;
        long long iv = std::strtoll(kstr.c_str(), &end, 10);
        if (end && *end == '\0' && iv >= 0 && iv < 4294967295LL
            && std::to_string(iv) == kstr) {
            const proto::ProtoString* isArrK = JSSymbols::isArray(ctx);
            const proto::ProtoObject* isArrV = isArrK
                ? target->getAttribute(ctx, isArrK, true) : nullptr;
            if (isArrV == PROTO_TRUE) {
                const proto::ProtoString* lenK = JSSymbols::length(ctx);
                const proto::ProtoObject* lenV = lenK
                    ? target->getAttribute(ctx, lenK, false) : nullptr;
                long long curLen = (lenV && lenV != PROTO_NONE && lenV->isInteger(ctx))
                    ? lenV->asLong(ctx) : 0;
                if (iv + 1 > curLen)
                    target = target->setAttribute(ctx, lenK,
                        ctx->fromInteger(iv + 1));
            }
        }
    }
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
    if (!ctx || !args) return PROTO_NONE;
    const proto::ProtoObject* target = args->getSize(ctx) > 0 ? args->getAt(ctx, 0) : PROTO_NONE;
    const proto::ProtoObject* propNameObj = args->getSize(ctx) > 1 ? args->getAt(ctx, 1) : PROTO_NONE;
    // ECMA-262 §20.1.2.10 step 1: Let obj be ? ToObject(O).
    // null / undefined are not Object-coercible — throw TypeError.
    if (throwIfNullOrUndefined(ctx, target, "Object.getOwnPropertyDescriptor"))
        return PROTO_NONE;

    const proto::ProtoString* k = coercePropNameToKey(ctx, propNameObj);
    if (!k) return PROTO_NONE;

    // String primitive: per §22.1.4 ToObject promotes to a String
    // exotic so the per-char and 'length' descriptors materialise.
    // Pre-fix the primitive path returned undefined for any key —
    // getOwnPropertyDescriptors('abc') yielded an empty object.
    if (target && target->isString(ctx)) {
        std::string kstr2;
        k->toUTF8String(ctx, kstr2);
        const proto::ProtoString* ps = target->asString(ctx);
        std::string sv;
        if (ps) ps->toUTF8String(ctx, sv);
        unsigned long len = 0;
        for (size_t bi = 0; bi < sv.size(); ) {
            unsigned char c = static_cast<unsigned char>(sv[bi]);
            size_t cl = (c < 0x80) ? 1 : (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : 4;
            if (bi + cl > sv.size()) break;
            len += (cl == 4) ? 2 : 1;
            bi += cl;
        }
        JSContextWrapper* w = JSContextWrapper::current();
        const proto::ProtoObject* op = w ? w->getJSObjectPrototype() : nullptr;
        auto mk = [&](){ return op ? op->newChild(ctx, true) : ctx->newObject(true); };
        auto setKV = [&](const proto::ProtoObject*& r, const char* name, const proto::ProtoObject* v){
            const proto::ProtoString* ks = ctx->fromUTF8String(name)->asString(ctx);
            if (ks) r = r->setAttribute(ctx, ks, v ? v : getUndefinedSentinel());
        };
        if (kstr2 == "length") {
            const proto::ProtoObject* res = mk();
            setKV(res, "value",        ctx->fromInteger(static_cast<long long>(len)));
            setKV(res, "writable",     PROTO_FALSE);
            setKV(res, "enumerable",   PROTO_FALSE);
            setKV(res, "configurable", PROTO_FALSE);
            return res;
        }
        bool numeric = !kstr2.empty();
        for (char c : kstr2) { if (c < '0' || c > '9') { numeric = false; break; } }
        if (numeric && (kstr2.size() == 1 || kstr2[0] != '0')) {
            try {
                unsigned long idx = std::stoul(kstr2);
                if (idx < len) {
                    size_t i = 0; unsigned long pos = 0;
                    while (i < sv.size() && pos < idx) {
                        unsigned char c = static_cast<unsigned char>(sv[i]);
                        size_t cl = (c < 0x80) ? 1 : (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : 4;
                        if (i + cl > sv.size()) break;
                        i += cl;
                        pos += (cl == 4) ? 2 : 1;
                    }
                    if (pos == idx && i < sv.size()) {
                        unsigned char c = static_cast<unsigned char>(sv[i]);
                        size_t cl = (c < 0x80) ? 1 : (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : 4;
                        std::string ch = sv.substr(i, cl);
                        const proto::ProtoObject* res = mk();
                        setKV(res, "value",        ctx->fromUTF8String(ch.c_str()));
                        setKV(res, "writable",     PROTO_FALSE);
                        setKV(res, "enumerable",   PROTO_TRUE);
                        setKV(res, "configurable", PROTO_FALSE);
                        return res;
                    }
                }
            } catch (...) {}
        }
        return PROTO_NONE;
    }

    // Helper: build result descriptor object.
    auto setAttr = [&](const proto::ProtoObject*& r, const char* name, const proto::ProtoObject* v) {
        const proto::ProtoString* ks = ctx->fromUTF8String(name)->asString(ctx);
        if (ks) r = r->setAttribute(ctx, ks, (v && v != PROTO_NONE) ? v : getUndefinedSentinel());
    };

    std::string kstr;
    k->toUTF8String(ctx, kstr);

    // 1. Check accessor sidecars first.
    std::string gkStr = "__get_" + kstr + "__";
    const proto::ProtoString* gk = ctx->fromUTF8String(gkStr.c_str())->asString(ctx);
    bool hasG = gk && (target->hasOwnAttribute(ctx, gk) == PROTO_TRUE);
    const proto::ProtoObject* gv = hasG ? target->getAttribute(ctx, gk, false) : nullptr;

    std::string skStr = "__set_" + kstr + "__";
    const proto::ProtoString* sk = ctx->fromUTF8String(skStr.c_str())->asString(ctx);
    bool hasS = sk && (target->hasOwnAttribute(ctx, sk) == PROTO_TRUE);
    const proto::ProtoObject* sv = hasS ? target->getAttribute(ctx, sk, false) : nullptr;

    std::string pdKeyStr = "__pd_" + kstr + "__";
    const proto::ProtoString* pdk = ctx->fromUTF8String(pdKeyStr.c_str())->asString(ctx);
    bool hasPd = pdk && (target->hasOwnAttribute(ctx, pdk) == PROTO_TRUE);
    const proto::ProtoObject* bitsObj = hasPd ? target->getAttribute(ctx, pdk, false) : nullptr;

    // Per ECMA-262 §6.2.5.4 FromPropertyDescriptor, the result must be
    // an ordinary object — i.e. its [[Prototype]] is the live
    // Object.prototype. Pre-fix ctx->newObject(true) produced a
    // parentless object; getPrototypeOf reported Object.prototype via
    // the override path but live attribute lookups (d.hasOwnProperty,
    // 'foo' in d, d[key]) never walked the chain — every Object.proto
    // method came back as undefined.
    JSContextWrapper* descWrapper = JSContextWrapper::current();
    const proto::ProtoObject* objProto = descWrapper
        ? descWrapper->getJSObjectPrototype() : nullptr;
    auto newDescriptor = [&]() -> const proto::ProtoObject* {
        return objProto ? objProto->newChild(ctx, true) : ctx->newObject(true);
    };

    if (hasG || hasS) {
        const proto::ProtoObject* res = newDescriptor();
        setAttr(res, "get", gv);
        setAttr(res, "set", sv);
        uint8_t bits = (bitsObj && bitsObj->isInteger(ctx)) ? (uint8_t)bitsObj->asLong(ctx) : 0x7;
        setAttr(res, "enumerable",   (bits & 0x4) ? PROTO_TRUE : PROTO_FALSE);
        setAttr(res, "configurable", (bits & 0x2) ? PROTO_TRUE : PROTO_FALSE);
        return res;
    }

    // 2. Data property.
    const proto::ProtoObject* val = nullptr;
    bool found = (target->hasOwnAttribute(ctx, k) == PROTO_TRUE);
    if (found) {
        val = target->getAttribute(ctx, k, false);
    } else {
        // Array index stored in __elements__ — not an own attribute,
        // but spec-wise it IS an own property of the Array exotic
        // object. Pre-fix Object.getOwnPropertyDescriptor(arr, "0")
        // returned undefined for index 0 of a non-empty array because
        // the value lives in the native ProtoList storage, not in the
        // sparse-list attribute map. Similar for String-wrapper char
        // indices ("0"..."n-1").
        const proto::ProtoString* isArrKey = JSSymbols::isArray(ctx);
        bool isArr = isArrKey
            && (target->getAttribute(ctx, isArrKey, false) == PROTO_TRUE);
        if (isArr) {
            // Parse k as a uint32 index. Per spec only "canonical
            // numeric indices" qualify — leading zero / signs / dots
            // do NOT match.
            bool numeric = !kstr.empty();
            for (char c : kstr) { if (c < '0' || c > '9') { numeric = false; break; } }
            if (numeric && (kstr.size() == 1 || kstr[0] != '0')) {
                try {
                    unsigned long idx = std::stoul(kstr);
                    const proto::ProtoObject* v =
                        arrayTryFastGet(ctx, target, idx);
                    if (v && v != PROTO_NONE) { val = v; found = true; }
                } catch (...) {}
            }
        }
        if (!found) {
            // String wrapper char-index lookup.
            const proto::ProtoString* pvKey = JSSymbols::primitiveValue(ctx);
            if (pvKey) {
                const proto::ProtoObject* pv = target->getAttribute(ctx, pvKey, false);
                if (pv && pv != PROTO_NONE && pv->isString(ctx)) {
                    bool numeric = !kstr.empty();
                    for (char c : kstr) { if (c < '0' || c > '9') { numeric = false; break; } }
                    if (numeric && (kstr.size() == 1 || kstr[0] != '0')) {
                        try {
                            unsigned long idx = std::stoul(kstr);
                            const proto::ProtoString* ps = pv->asString(ctx);
                            if (ps) {
                                std::string s;
                                ps->toUTF8String(ctx, s);
                                // Walk UTF-16 code units.
                                size_t i = 0;
                                unsigned long pos = 0;
                                while (i < s.size() && pos < idx) {
                                    unsigned char c = static_cast<unsigned char>(s[i]);
                                    size_t cl = (c < 0x80) ? 1 : (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : 4;
                                    if (i + cl > s.size()) break;
                                    i += cl;
                                    pos += (cl == 4) ? 2 : 1;
                                }
                                if (pos == idx && i < s.size()) {
                                    unsigned char c = static_cast<unsigned char>(s[i]);
                                    size_t cl = (c < 0x80) ? 1 : (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : 4;
                                    if (i + cl <= s.size()) {
                                        val = ctx->fromUTF8String(s.substr(i, cl).c_str());
                                        found = true;
                                        // String index slots are non-writable, non-configurable, enumerable per §22.1.3.
                                        // Override bits explicitly.
                                        if (!hasPd) bitsObj = ctx->fromInteger(0x4LL);
                                    }
                                }
                            }
                        } catch (...) {}
                    }
                }
            }
        }
    }
    if (!found) return PROTO_NONE;

    const proto::ProtoObject* res = newDescriptor();
    setAttr(res, "value", val);
    uint8_t bits = (bitsObj && bitsObj->isInteger(ctx)) ? (uint8_t)bitsObj->asLong(ctx) : 0x7;
    setAttr(res, "writable",     (bits & 0x1) ? PROTO_TRUE : PROTO_FALSE);
    setAttr(res, "enumerable",   (bits & 0x4) ? PROTO_TRUE : PROTO_FALSE);
    setAttr(res, "configurable", (bits & 0x2) ? PROTO_TRUE : PROTO_FALSE);
    return res;
}

// ---------------------------------------------------------------------------
// Object.getOwnPropertySymbols(obj) — ECMA-262 §20.1.2.12
// Returns an array of Symbol-keyed own properties. protoJS doesn't yet
// implement Symbol primitives as attribute keys, so the result is
// always an empty array — but the function itself must exist and must
// throw TypeError on null / undefined per ToObject.
// ---------------------------------------------------------------------------

static const proto::ProtoObject* objectGetOwnPropertySymbols(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!ctx || !args) return PROTO_NONE;
    const proto::ProtoObject* target = args->getSize(ctx) > 0
        ? args->getAt(ctx, 0) : PROTO_NONE;
    if (throwIfNullOrUndefined(ctx, target, "Object.getOwnPropertySymbols"))
        return PROTO_NONE;
    const proto::ProtoObject* result = createNewArray(ctx, nullptr);
    const proto::ProtoString* isArrKey = JSSymbols::isArray(ctx);
    if (isArrKey) result = result->setAttribute(ctx, isArrKey, PROTO_TRUE);
    const proto::ProtoString* lenKey = JSSymbols::length(ctx);
    if (lenKey) result = result->setAttribute(ctx, lenKey, ctx->fromInteger(0LL));
    return result;
}

// ---------------------------------------------------------------------------
// Object.getOwnPropertyDescriptors(obj) — ECMA-262 §20.1.2.11
// Returns a new object with one descriptor per own property of obj.
// ---------------------------------------------------------------------------

static const proto::ProtoObject* objectGetOwnPropertyDescriptors(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!ctx || !args) return PROTO_NONE;
    const proto::ProtoObject* target = args->getSize(ctx) > 0
        ? args->getAt(ctx, 0) : PROTO_NONE;
    if (throwIfNullOrUndefined(ctx, target, "Object.getOwnPropertyDescriptors"))
        return PROTO_NONE;

    std::vector<std::string> keys;
    collectOwnKeys(ctx, target, keys, nullptr, /*includeNonEnumerable=*/true);

    const proto::ProtoObject* result = ctx->newObject(true);
    for (const std::string& k : keys) {
        // Build a per-key argument list and delegate to the existing
        // getOwnPropertyDescriptor implementation so the data/accessor
        // branch logic and descriptor sidecar handling stay in one
        // place.
        const proto::ProtoList* keyArgs = ctx->newList();
        keyArgs = keyArgs->appendLast(ctx, target);
        keyArgs = keyArgs->appendLast(ctx, ctx->fromUTF8String(k.c_str()));
        const proto::ProtoObject* desc =
            objectGetOwnPropertyDescriptor(ctx, nullptr, nullptr, keyArgs, nullptr);
        if (!desc || desc == PROTO_NONE) continue;
        const proto::ProtoString* kk = ctx->fromUTF8String(k.c_str())->asString(ctx);
        if (kk) result = result->setAttribute(ctx, kk, desc);
    }
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
    // Pre-fix the explicit undefined value (t_undefinedSentinel) bypassed
    // the absence check — Object.defineProperties({}, undefined) returned
    // the receiver silently (built-ins/Object/defineProperties/
    // 15.2.3.7-2-2).
    {
        const proto::ProtoObject* nullSentinel = getNullSentinel();
        const proto::ProtoObject* undefSentinel = getUndefinedSentinel();
        if (!propsObj || propsObj == PROTO_NONE
            || propsObj == nullSentinel || propsObj == undefSentinel) {
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
            // Skip "length" only when the Properties argument is
            // itself an Array (whose own "length" slot is just
            // metadata, not a descriptor entry). Plain-object props
            // legitimately use { length: {...} } to alter the
            // target's length descriptor — built-ins/Object/
            // defineProperties/15.2.3.7-6-a-157 sets arr.length via
            // a plain props object and expected the array to shrink.
            if (keyStr == "length") {
                const proto::ProtoString* propsIsArrK = JSSymbols::isArray(ctx);
                const proto::ProtoObject* propsIsArrV = propsIsArrK
                    ? propsObj->getAttribute(ctx, propsIsArrK, true) : nullptr;
                if (propsIsArrV == PROTO_TRUE) continue;
            }
            // §19.1.2.3 step 5.b.iii: only enumerable own properties
            // contribute. Pre-fix the iterator surfaced non-enumerable
            // entries too, so `Object.defineProperties(o, propsWithGetter)`
            // tried to apply non-Object descriptors and surfaced a
            // spurious TypeError (built-ins/Object/defineProperties/
            // 15.2.3.7-3-7 covers the omission).
            std::string pdStr = std::string("__pd_") + keyStr + "__";
            const proto::ProtoObject* pdo = ctx->fromUTF8String(pdStr.c_str());
            const proto::ProtoString* pdk = pdo ? pdo->asString(ctx) : nullptr;
            if (pdk) {
                const proto::ProtoObject* pdv = propsObj->getAttribute(ctx, pdk, false);
                if (pdv && pdv != PROTO_NONE && pdv->isInteger(ctx)) {
                    long long bits = pdv->asLong(ctx);
                    if ((bits & 0x4) == 0) continue; // skip non-enumerable
                }
            }
            // If the descriptor entry is an accessor (sidecar getter
            // present), resolve the value via the getter before
            // dispatching to defineProperty.
            {
                std::string gkStr = std::string("__get_") + keyStr + "__";
                const proto::ProtoObject* gko = ctx->fromUTF8String(gkStr.c_str());
                const proto::ProtoString* gk = gko ? gko->asString(ctx) : nullptr;
                if (gk) {
                    const proto::ProtoObject* getter =
                        propsObj->getAttribute(ctx, gk, false);
                    if (getter && getter != PROTO_NONE
                        && getter != getUndefinedSentinel()) {
                        const proto::ProtoList* noArgs = ctx->newList();
                        descObj = callJSFunction(ctx, getter, propsObj, noArgs);
                        if (hasCallException()) return PROTO_NONE;
                    }
                }
            }
            const proto::ProtoList* dpArgs = ctx->newList();
            dpArgs = dpArgs->appendLast(ctx, target);
            dpArgs = dpArgs->appendLast(ctx, ctx->fromUTF8String(keyStr.c_str()));
            dpArgs = dpArgs->appendLast(ctx, descObj);
            const proto::ProtoObject* newTarget = objectDefineProperty(ctx, nullptr, nullptr, dpArgs, nullptr);
            if (hasCallException()) return PROTO_NONE;
            if (newTarget && newTarget != PROTO_NONE) target = newTarget;
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
    const proto::ProtoObject* iterable = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0) : PROTO_NONE;
    // ECMA-262 §20.1.2.6 step 1: RequireObjectCoercible(iterable).
    // null / undefined throw TypeError before any iteration begins.
    if (throwIfNullOrUndefined(ctx, iterable, "Object.fromEntries"))
        return PROTO_NONE;
    // GetIterator(iterable) per step 3: primitives that don't carry
    // @@iterator throw 'X is not iterable'. Pre-fix the iterator-first
    // path silently fell through to the empty-result branch, so
    // Object.fromEntries(1) returned {} instead of throwing.
    if (iterable && (iterable->isInteger(ctx) || iterable->isDouble(ctx)
                     || iterable->isFloat(ctx) || iterable->isBoolean(ctx))) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "is not iterable"));
        return PROTO_NONE;
    }
    const proto::ProtoObject* result = ctx->newObject(true);

    // Iterable element read: prefer __elements__ first (real arrays
    // and Map.entries() / Set.entries() pair tuples store data there;
    // pre-fix only checked indexed-attribute keys and silently produced
    // {} for both [[k,v],...] arrays AND Map iterables).
    auto readEl = [&](const proto::ProtoObject* arr, long long i) -> const proto::ProtoObject* {
        if (!arr || arr == PROTO_NONE) return PROTO_NONE;
        const proto::ProtoObject* v =
            arrayTryFastGet(ctx, arr, static_cast<unsigned long>(i));
        if (v) return v;
        const proto::ProtoString* ik = JSSymbols::indexKey(ctx, static_cast<uint32_t>(i));
        v = ik ? arr->getAttribute(ctx, ik, false) : nullptr;
        return v ? v : PROTO_NONE;
    };

    auto processPair = [&](const proto::ProtoObject* pair) -> void {
        if (!pair || pair == PROTO_NONE) return;
        const proto::ProtoObject* keyObj = readEl(pair, 0);
        const proto::ProtoObject* valObj = readEl(pair, 1);
        if (!valObj) valObj = PROTO_NONE;
        if (!keyObj || keyObj == PROTO_NONE) return;
        std::string keyStr;
        if (keyObj->isString(ctx)) {
            const proto::ProtoString* ps = keyObj->asString(ctx);
            if (ps) ps->toUTF8String(ctx, keyStr);
        } else if (keyObj->isInteger(ctx)) {
            keyStr = std::to_string(keyObj->asLong(ctx));
        }
        if (keyStr.empty()) return;
        const proto::ProtoString* entryKey =
            ctx->fromUTF8String(keyStr.c_str())->asString(ctx);
        if (entryKey) result = result->setAttribute(ctx, entryKey, valObj);
    };

    // Iterator-first path: try Symbol.iterator, or treat iterable as
    // iterator if it already has .next.  Mirrors the OP_append /
    // Array.from logic added in 21b00b45 / 9a1d70f8.
    const proto::ProtoString* symIterKey = JSSymbols::symbolIterator(ctx);
    const proto::ProtoObject* iterFn = symIterKey
        ? iterable->getAttribute(ctx, symIterKey, true) : nullptr;
    const proto::ProtoObject* iter = nullptr;
    if (iterFn && iterFn != PROTO_NONE) {
        const proto::ProtoList* noArgs = ctx->newList();
        iter = callJSFunction(ctx, iterFn, iterable, noArgs);
    } else {
        const proto::ProtoString* probeNextK = JSSymbols::next(ctx);
        const proto::ProtoObject* probeNext = probeNextK
            ? iterable->getAttribute(ctx, probeNextK, true) : nullptr;
        if (probeNext && probeNext != PROTO_NONE) iter = iterable;
    }
    if (iter && iter != PROTO_NONE) {
        const proto::ProtoString* nextKey  = JSSymbols::next(ctx);
        const proto::ProtoString* doneKey  = JSSymbols::done(ctx);
        const proto::ProtoString* valueKey = JSSymbols::value(ctx);
        const proto::ProtoObject* nextFn = iter->getAttribute(ctx, nextKey, true);
        long long safety = 0;
        while (nextFn && nextFn != PROTO_NONE) {
            const proto::ProtoList* nArgs = ctx->newList();
            const proto::ProtoObject* res = callJSFunction(ctx, nextFn, iter, nArgs);
            if (!res || res == PROTO_NONE) break;
            const proto::ProtoObject* dv = res->getAttribute(ctx, doneKey, false);
            if (dv == PROTO_TRUE) break;
            const proto::ProtoObject* pair = res->getAttribute(ctx, valueKey, false);
            processPair(pair);
            if (++safety > 100000) break;
        }
        return result;
    }

    const proto::ProtoString* lenKey = JSSymbols::length(ctx);
    if (!lenKey) return result;
    const proto::ProtoObject* lenObj = iterable->getAttribute(ctx, lenKey, false);
    long long len = -1;
    if (lenObj && lenObj != PROTO_NONE) {
        if (lenObj->isInteger(ctx)) len = lenObj->asLong(ctx);
        else if (lenObj->isDouble(ctx)) len = static_cast<long long>(lenObj->asDouble(ctx));
    }
    if (len < 0) {
        const proto::ProtoList* els = getArrayElements(ctx, iterable);
        if (els) len = static_cast<long long>(els->getSize(ctx));
        else len = 0;
    }

    for (long long i = 0; i < len; i++) {
        const proto::ProtoObject* pair = readEl(iterable, i);
        if (!pair || pair == PROTO_NONE) continue;

        const proto::ProtoObject* keyObj = readEl(pair, 0);
        const proto::ProtoObject* valObj = readEl(pair, 1);
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
    // ECMA-262 §20.1.2.13 step 1: ToObject(target). null / undefined
    // throw TypeError. Pre-fix the implementation returned false
    // silently for any non-object first arg.
    const proto::ProtoObject* obj = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0) : PROTO_NONE;
    if (throwIfNullOrUndefined(ctx, obj, "Object.hasOwn"))
        return PROTO_NONE;
    const proto::ProtoObject* key = (args && args->getSize(ctx) > 1)
        ? args->getAt(ctx, 1) : PROTO_NONE;
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
    // §20.1.3.2 step 1 calls ToObject(this) before any property test;
    // null / undefined make ToObject throw TypeError. Pre-fix the
    // method returned false silently, breaking
    // Object.prototype.hasOwnProperty.call(null, 'foo') and a string of
    // built-ins/Object/prototype/* tests that depend on the abrupt.
    // §20.1.3.2 step 1 calls ToObject(this); null / undefined throw
    // TypeError before any property lookup (built-ins/Object/prototype/
    // hasOwnProperty/this-not-object-coercible).
    if (!self || self == PROTO_NONE
        || self == getNullSentinel() || self == getUndefinedSentinel()) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Cannot convert undefined or null to object"));
        return PROTO_NONE;
    }
    if (!args || args->getSize(ctx) == 0) return PROTO_FALSE;
    const proto::ProtoObject* key = args->getAt(ctx, 0);
    if (!key || key == PROTO_NONE) return PROTO_FALSE;

    const proto::ProtoString* k = coercePropNameToKey(ctx, key);
    if (!k) return PROTO_FALSE;

    if (self->hasOwnAttribute(ctx, k) == PROTO_TRUE) {
        // protoCore has no public deleteAttribute; the array prototype
        // simulates 'delete arr[i]' by writing PROTO_NONE to the slot.
        // hasOwnAttribute still reports true, but for spec parity the
        // user-visible hasOwnProperty must return false once the slot
        // has been deleted. Probe the value: if it is PROTO_NONE
        // (and no accessor sidecar exists), treat the property as
        // absent. This affects Array.prototype.pop / shift / splice
        // residue checks.
        const proto::ProtoObject* probe = self->getAttribute(ctx, k, false);
        if (probe && probe != PROTO_NONE) return PROTO_TRUE;
        // Allow the accessor-sidecar / array-index fallbacks below to
        // confirm even when the data slot is PROTO_NONE (accessor-only
        // properties have no value attribute).
    }

    // Also check accessor sidecars — accessor properties have no data key when
    // defined via Object.defineProperty (the data key is removed on creation).
    std::string keyStr;
    k->toUTF8String(ctx, keyStr);
    for (const char* prefix : {"__get_", "__set_"}) {
        std::string sk = std::string(prefix) + keyStr + "__";
        const proto::ProtoObject* sko = ctx->fromUTF8String(sk.c_str());
        const proto::ProtoString* sks = sko ? sko->asString(ctx) : nullptr;
        if (sks && self->hasOwnAttribute(ctx, sks) == PROTO_TRUE) return PROTO_TRUE;
    }

    // Array element fallback: array elements live in the internal
    // `__elements__` ProtoList, NOT as own attributes — so hasOwnAttribute
    // returns false for "0" on `[101]` even though spec-wise the element
    // is an own data property.  Synthesise the check: if self is an
    // array and the key parses as a valid non-negative integer index
    // strictly less than the array's length AND the slot is not the
    // PROTO_NONE deletion marker, the property exists.  Pre-fix the
    // length check alone treated `delete a[0]` followed by
    // hasOwnProperty(0) as "still present" because OP_delete writes
    // PROTO_NONE into __elements__ in place rather than shrinking the
    // list; the verifyProperty harness's isConfigurable probe (delete
    // then hasOwnProperty) then incorrectly reported the property as
    // non-configurable.
    const proto::ProtoString* isArrKey = JSSymbols::isArray(ctx);
    const proto::ProtoObject* isArr = isArrKey ? self->getAttribute(ctx, isArrKey, true) : nullptr;
    if (isArr == PROTO_TRUE) {
        long long idx = -1;
        // The key string must round-trip through ToString to be a valid
        // integer index per ES2015 §7.1.16 (canonical numeric strings).
        if (!keyStr.empty()) {
            char* end = nullptr;
            long long v = std::strtoll(keyStr.c_str(), &end, 10);
            if (end && *end == '\0' && v >= 0 && std::to_string(v) == keyStr)
                idx = v;
        }
        if (idx >= 0) {
            const proto::ProtoString* lenKey = JSSymbols::length(ctx);
            const proto::ProtoObject* lenVal = lenKey
                ? self->getAttribute(ctx, lenKey, false) : nullptr;
            if (lenVal && lenVal != PROTO_NONE && lenVal->isInteger(ctx)) {
                long long len = lenVal->asLong(ctx);
                if (idx < len) {
                    const proto::ProtoList* els =
                        protojs::getArrayElements(ctx, self);
                    if (els && idx < static_cast<long long>(els->getSize(ctx))) {
                        const proto::ProtoObject* slot = els->getAt(ctx, static_cast<int>(idx));
                        if (slot && slot != PROTO_NONE) return PROTO_TRUE;
                        // PROTO_NONE → deleted hole; fall through.
                    } else {
                        // Sparse pre-allocated tail (Array(n) without
                        // __elements__ materialised) keeps the indices
                        // logically present.
                        return PROTO_TRUE;
                    }
                }
            }
        }
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
    // §20.1.3.4 step 1: ToObject(this); null / undefined throw TypeError
    // before any inspection (built-ins/Object/prototype/
    // propertyIsEnumerable/S15.2.4.7_A13).
    if (!self || self == PROTO_NONE
        || self == getNullSentinel() || self == getUndefinedSentinel()) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Cannot convert undefined or null to object"));
        return PROTO_NONE;
    }
    if (!args || args->getSize(ctx) == 0) return PROTO_FALSE;
    const proto::ProtoObject* key = args->getAt(ctx, 0);
    if (!key || key == PROTO_NONE) return PROTO_FALSE;

    const proto::ProtoString* k = coercePropNameToKey(ctx, key);
    if (!k) return PROTO_FALSE;

    if (self->hasOwnAttribute(ctx, k) != PROTO_TRUE) {
        // Also check accessors
        std::string keyStr;
        k->toUTF8String(ctx, keyStr);
        bool found = false;
        for (const char* prefix : {"__get_", "__set_"}) {
            std::string sk = std::string(prefix) + keyStr + "__";
            const proto::ProtoObject* sko = ctx->fromUTF8String(sk.c_str());
            const proto::ProtoString* sks = sko ? sko->asString(ctx) : nullptr;
            if (sks && self->hasOwnAttribute(ctx, sks) == PROTO_TRUE) {
                found = true;
                break;
            }
        }
        if (!found) {
            // Array element fallback: see objectHasOwnProperty for the
            // same shape of check.  Array elements are enumerable by spec
            // (writable: true, enumerable: true, configurable: true), so
            // an in-range index returns true directly.
            const proto::ProtoString* isArrKey = JSSymbols::isArray(ctx);
            const proto::ProtoObject* isArr = isArrKey
                ? self->getAttribute(ctx, isArrKey, true) : nullptr;
            if (isArr == PROTO_TRUE) {
                long long idx = -1;
                if (!keyStr.empty()) {
                    char* end = nullptr;
                    long long v = std::strtoll(keyStr.c_str(), &end, 10);
                    if (end && *end == '\0' && v >= 0 && std::to_string(v) == keyStr)
                        idx = v;
                }
                if (idx >= 0) {
                    const proto::ProtoString* lenKey = JSSymbols::length(ctx);
                    const proto::ProtoObject* lenVal = lenKey
                        ? self->getAttribute(ctx, lenKey, false) : nullptr;
                    if (lenVal && lenVal != PROTO_NONE && lenVal->isInteger(ctx)) {
                        if (idx < lenVal->asLong(ctx)) return PROTO_TRUE;
                    }
                }
            }
            return PROTO_FALSE;
        }
    }

    std::string keyStr;
    k->toUTF8String(ctx, keyStr);
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
    if (!self || self == PROTO_NONE || self->isNone(ctx) || self == getUndefinedSentinel())
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
        // Function.prototype itself is a function per §20.2.3 (calling
        // it returns undefined) but is allocated as a plain object
        // newChild of Object.prototype and carries none of the standard
        // callable markers. Function.prototype is stamped with
        // __is_function_prototype__ specifically so toString can
        // dispatch it (Sputnik S15.3.4_A1).
        const proto::ProtoObject* fpmo = ctx->fromUTF8String("__is_function_prototype__");
        const proto::ProtoString* fpms = fpmo ? fpmo->asString(ctx) : nullptr;
        if (fpms) {
            const proto::ProtoObject* fpv = self->getAttribute(ctx, fpms, false);
            if (fpv == PROTO_TRUE)
                return ctx->fromUTF8String("[object Function]");
        }
        // Built-in constructor objects (Array, Object, Number, Boolean,
        // String, Error, ...) are callable via the spec's [[Call]] /
        // [[Construct]] internal methods. They expose neither
        // __native_fn__ nor __bytecode_id__ — dispatch goes through the
        // dedicated __<name>_ctor__ marker — so a pre-fix lookup
        // produced "[object Object]" instead of the spec-required
        // "[object Function]" (Object.prototype.toString.call(Array)
        // and the Sputnik S15.4.3_A1.1_T2 conformance check both broke).
        const proto::ProtoString* icKey = ctx->fromUTF8String("__is_constructor__")->asString(ctx);
        if (icKey) {
            const proto::ProtoObject* icVal = self->getAttribute(ctx, icKey, false);
            if (icVal == PROTO_TRUE)
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
    // Symbol primitive: protoJS carries Symbols with the __is_symbol__
    // marker. §20.1.3.6 dispatches them to the built-in tag "Symbol"
    // before the generic Object fallback.
    {
        const proto::ProtoString* symK = JSSymbols::isSymbol(ctx);
        if (symK && self->getAttribute(ctx, symK, true) == PROTO_TRUE)
            return ctx->fromUTF8String("[object Symbol]");
    }

    // ECMA-262 §22.1.3.7 Object.prototype.toString: when O has an
    // internal-slot match the spec dispatches by slot before the
    // generic [[Class]] / Symbol.toStringTag walk.  Boolean / Number
    // / String prototypes (and any wrapper produced by `new Boolean(x)`
    // / `new Number(x)` / `new String(x)`) carry their primitive
    // value in the protoJS `__primitive_value__` sidecar.  Pre-fix
    // we only inspected the primitive types of `self` itself and
    // fell through to "[object Object]" for the prototypes — so
    // after `delete Boolean.prototype.toString` the call to
    // `Boolean.prototype.toString()` (which resolves to
    // Object.prototype.toString) returned "[object Object]"
    // instead of the spec-required "[object Boolean]".
    {
        const proto::ProtoString* pvKey = JSSymbols::primitiveValue(ctx);
        if (pvKey) {
            const proto::ProtoObject* pv = self->getAttribute(ctx, pvKey, false);
            if (pv && pv != PROTO_NONE) {
                if (pv->isBoolean(ctx))
                    return ctx->fromUTF8String("[object Boolean]");
                if (pv->isInteger(ctx) || pv->isDouble(ctx) || pv->isFloat(ctx))
                    return ctx->fromUTF8String("[object Number]");
                if (pv->isString(ctx))
                    return ctx->fromUTF8String("[object String]");
            }
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
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*,
    const proto::ProtoSparseList*)
{
    // §20.1.3.7 step 1: O = ToObject(this); null / undefined raise
    // TypeError before the slot is read. Pre-fix the trivial passthrough
    // returned the sentinel unchanged, so
    //   Object.prototype.valueOf.call(null);
    // produced null instead of the spec-required TypeError abrupt
    // (Sputnik S15.2.4.4_A13).
    if (!self || self == PROTO_NONE
        || self == getNullSentinel() || self == getUndefinedSentinel()) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Cannot convert undefined or null to object"));
        return PROTO_NONE;
    }
    return self;
}

// Forward decls — defined further below in the static-method section.
static const proto::ProtoObject* objectGetPrototypeOf(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*);
static const proto::ProtoObject* objectSetPrototypeOf(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*);

// §B.2.2.1 Object.prototype.__proto__ accessor getter:
//   get __proto__() { return Object.getPrototypeOf(ToObject(this)); }
static const proto::ProtoObject* objectProtoGetter(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*,
    const proto::ProtoSparseList*)
{
    if (!self || self == PROTO_NONE
        || self == getNullSentinel() || self == getUndefinedSentinel()) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Cannot convert undefined or null to object"));
        return PROTO_NONE;
    }
    // For protoJS the user-facing prototype is whatever Object.getPrototypeOf
    // returns — which honours the t_jsProtoMap override applied via
    // setPrototypeOf — so route through that path rather than getFirstParent
    // directly.  Build a single-element args list and dispatch.
    const proto::ProtoList* gpoArgs = ctx->newList();
    gpoArgs = gpoArgs->appendLast(ctx, self);
    return objectGetPrototypeOf(ctx, nullptr, nullptr, gpoArgs, nullptr);
}

// §B.2.2.1 setter: same coercion + setPrototypeOf dispatch.  No-op on
// non-object / null receivers; silently ignores non-object / non-null
// values per spec.
static const proto::ProtoObject* objectProtoSetter(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!self || self == PROTO_NONE
        || self == getNullSentinel() || self == getUndefinedSentinel()) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Cannot convert undefined or null to object"));
        return PROTO_NONE;
    }
    if (!args || args->getSize(ctx) < 1) return getUndefinedSentinel();
    const proto::ProtoObject* newProto = args->getAt(ctx, 0);
    // §B.2.2.1.1 step 4: if Type(V) is neither Object nor Null, return
    // undefined.
    bool isNull = (newProto == getNullSentinel());
    bool isObj = newProto && newProto != PROTO_NONE && !newProto->isString(ctx)
        && !newProto->isInteger(ctx) && !newProto->isDouble(ctx)
        && !newProto->isFloat(ctx) && newProto != PROTO_TRUE
        && newProto != PROTO_FALSE && newProto != getUndefinedSentinel();
    if (!isNull && !isObj) return getUndefinedSentinel();
    // Dispatch through objectSetPrototypeOf so the t_jsProtoMap +
    // protoCore::setParents pair stays consistent.
    const proto::ProtoList* spoArgs = ctx->newList();
    spoArgs = spoArgs->appendLast(ctx, self);
    spoArgs = spoArgs->appendLast(ctx, newProto);
    (void)objectSetPrototypeOf(ctx, nullptr, nullptr, spoArgs, nullptr);
    return getUndefinedSentinel();
}

// §B.2.2.2 Object.prototype.__defineGetter__(P, getter): install an
// accessor descriptor with the supplied getter on `this`, leaving any
// existing setter intact.  Equivalent to
//   Object.defineProperty(this, P, {get: getter, enumerable: true,
//                                   configurable: true})
// per the spec.  Throws TypeError if getter is not callable.
// objectDefineProperty handles the descriptor sidecar bookkeeping
// (__pd_<key>__ + __has_accessor_props__ + the actual __get_<key>__
// slot), so we just synthesise the descriptor object and dispatch.
static const proto::ProtoObject* objectDefineProperty(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* parents,
    const proto::ProtoList* args,
    const proto::ProtoSparseList* slots);
// Lightweight callable probe: a value is callable iff it has any of the
// markers protoJS uses for callables — __native_fn__ (wrapped C++ fn),
// __bytecode_id__ (Python-side function), __bound_fn__ (bound function),
// __is_constructor__, or the raw isMethod() type tag.  Matches the
// dispatch test used by objectToString.
static bool jsIsCallable(proto::ProtoContext* ctx, const proto::ProtoObject* o) {
    if (!o || o == PROTO_NONE) return false;
    if (o->isMethod(ctx)) return true;
    const proto::ProtoString* nfKey = JSSymbols::nativeFn(ctx);
    if (nfKey && o->getAttribute(ctx, nfKey, false) != PROTO_NONE) return true;
    const proto::ProtoString* bcKey = JSSymbols::bytecodeId(ctx);
    if (bcKey && o->getAttribute(ctx, bcKey, false) != PROTO_NONE) return true;
    const proto::ProtoString* bfKey = JSSymbols::boundFn(ctx);
    if (bfKey && o->getAttribute(ctx, bfKey, false) != PROTO_NONE) return true;
    return false;
}
static const proto::ProtoObject* objectDefineGetter(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!self || self == PROTO_NONE
        || self == getNullSentinel() || self == getUndefinedSentinel()) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Cannot convert undefined or null to object"));
        return PROTO_NONE;
    }
    if (!args || args->getSize(ctx) < 2) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "__defineGetter__ requires a property key and a getter function"));
        return PROTO_NONE;
    }
    const proto::ProtoObject* keyArg = args->getAt(ctx, 0);
    const proto::ProtoObject* getter = args->getAt(ctx, 1);
    if (!jsIsCallable(ctx, getter)) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "__defineGetter__: getter must be callable"));
        return PROTO_NONE;
    }
    // Build descriptor object {get: getter, enumerable: true, configurable: true}.
    const proto::ProtoObject* desc = ctx->newObject(true);
    if (desc) {
        auto setK = [&](const char* k, const proto::ProtoObject* v) {
            const proto::ProtoObject* ko = ctx->fromUTF8String(k);
            const proto::ProtoString* ks = ko ? ko->asString(ctx) : nullptr;
            if (ks) desc = desc->setAttribute(ctx, ks, v);
        };
        setK("get",          getter);
        setK("enumerable",   PROTO_TRUE);
        setK("configurable", PROTO_TRUE);
    }
    const proto::ProtoList* dpArgs = ctx->newList();
    dpArgs = dpArgs->appendLast(ctx, self);
    dpArgs = dpArgs->appendLast(ctx, keyArg);
    dpArgs = dpArgs->appendLast(ctx, desc);
    (void)objectDefineProperty(ctx, nullptr, nullptr, dpArgs, nullptr);
    if (hasCallException()) return PROTO_NONE;
    return getUndefinedSentinel();
}

// §B.2.2.3 Object.prototype.__defineSetter__: symmetric to
// __defineGetter__.
static const proto::ProtoObject* objectDefineSetter(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!self || self == PROTO_NONE
        || self == getNullSentinel() || self == getUndefinedSentinel()) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Cannot convert undefined or null to object"));
        return PROTO_NONE;
    }
    if (!args || args->getSize(ctx) < 2) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "__defineSetter__ requires a property key and a setter function"));
        return PROTO_NONE;
    }
    const proto::ProtoObject* keyArg = args->getAt(ctx, 0);
    const proto::ProtoObject* setter = args->getAt(ctx, 1);
    if (!jsIsCallable(ctx, setter)) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "__defineSetter__: setter must be callable"));
        return PROTO_NONE;
    }
    const proto::ProtoObject* desc = ctx->newObject(true);
    if (desc) {
        auto setK = [&](const char* k, const proto::ProtoObject* v) {
            const proto::ProtoObject* ko = ctx->fromUTF8String(k);
            const proto::ProtoString* ks = ko ? ko->asString(ctx) : nullptr;
            if (ks) desc = desc->setAttribute(ctx, ks, v);
        };
        setK("set",          setter);
        setK("enumerable",   PROTO_TRUE);
        setK("configurable", PROTO_TRUE);
    }
    const proto::ProtoList* dpArgs = ctx->newList();
    dpArgs = dpArgs->appendLast(ctx, self);
    dpArgs = dpArgs->appendLast(ctx, keyArg);
    dpArgs = dpArgs->appendLast(ctx, desc);
    (void)objectDefineProperty(ctx, nullptr, nullptr, dpArgs, nullptr);
    if (hasCallException()) return PROTO_NONE;
    return getUndefinedSentinel();
}

// §B.2.2.4 Object.prototype.__lookupGetter__(P): walk the prototype
// chain; for each ancestor with an own __get_<key>__ sidecar return
// that getter.  Returns undefined when no accessor with a getter is
// found.  Throws TypeError if `this` is null/undefined.  Test262 fix
// for built-ins/Object/prototype/__lookupGetter__/{length,name,
// lookup-not-found,key-invalid,...}.
static const proto::ProtoObject* objectLookupGetter(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!self || self == PROTO_NONE
        || self == getNullSentinel() || self == getUndefinedSentinel()) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Cannot convert undefined or null to object"));
        return PROTO_NONE;
    }
    if (!args || args->getSize(ctx) < 1) return getUndefinedSentinel();
    const proto::ProtoObject* keyArg = args->getAt(ctx, 0);
    if (!keyArg) return getUndefinedSentinel();
    // ToPropertyKey on the argument — primitive ToString for non-Symbol.
    std::string keyStr;
    if (keyArg->isString(ctx)) {
        keyArg->asString(ctx)->toUTF8String(ctx, keyStr);
    } else {
        // Reuse the same coercion the protoJS coercePropNameToKey path
        // already performs — for now ToString through asString.
        const proto::ProtoObject* coerced = keyArg;
        if (coerced->isInteger(ctx)) {
            keyStr = std::to_string(coerced->asLong(ctx));
        } else if (coerced->isDouble(ctx) || coerced->isFloat(ctx)) {
            keyStr = std::to_string(coerced->asDouble(ctx));
        } else if (coerced == PROTO_TRUE) {
            keyStr = "true";
        } else if (coerced == PROTO_FALSE) {
            keyStr = "false";
        } else {
            return getUndefinedSentinel();
        }
    }
    std::string gkStr = "__get_" + keyStr + "__";
    const proto::ProtoObject* gko = ctx->fromUTF8String(gkStr.c_str());
    const proto::ProtoString* gk = gko ? gko->asString(ctx) : nullptr;
    if (!gk) return getUndefinedSentinel();
    const proto::ProtoObject* curr = self;
    while (curr && curr != PROTO_NONE) {
        if (curr->hasOwnAttribute(ctx, gk) == PROTO_TRUE) {
            const proto::ProtoObject* getter = curr->getAttribute(ctx, gk, false);
            if (getter && getter != PROTO_NONE) return getter;
        }
        curr = curr->getFirstParent(ctx);
    }
    return getUndefinedSentinel();
}

// §B.2.2.5 Object.prototype.__lookupSetter__(P): symmetric to
// __lookupGetter__ but searches __set_<key>__.
static const proto::ProtoObject* objectLookupSetter(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!self || self == PROTO_NONE
        || self == getNullSentinel() || self == getUndefinedSentinel()) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Cannot convert undefined or null to object"));
        return PROTO_NONE;
    }
    if (!args || args->getSize(ctx) < 1) return getUndefinedSentinel();
    const proto::ProtoObject* keyArg = args->getAt(ctx, 0);
    if (!keyArg) return getUndefinedSentinel();
    std::string keyStr;
    if (keyArg->isString(ctx)) {
        keyArg->asString(ctx)->toUTF8String(ctx, keyStr);
    } else if (keyArg->isInteger(ctx)) {
        keyStr = std::to_string(keyArg->asLong(ctx));
    } else if (keyArg->isDouble(ctx) || keyArg->isFloat(ctx)) {
        keyStr = std::to_string(keyArg->asDouble(ctx));
    } else if (keyArg == PROTO_TRUE) {
        keyStr = "true";
    } else if (keyArg == PROTO_FALSE) {
        keyStr = "false";
    } else {
        return getUndefinedSentinel();
    }
    std::string skStr = "__set_" + keyStr + "__";
    const proto::ProtoObject* sko = ctx->fromUTF8String(skStr.c_str());
    const proto::ProtoString* sk = sko ? sko->asString(ctx) : nullptr;
    if (!sk) return getUndefinedSentinel();
    const proto::ProtoObject* curr = self;
    while (curr && curr != PROTO_NONE) {
        if (curr->hasOwnAttribute(ctx, sk) == PROTO_TRUE) {
            const proto::ProtoObject* setter = curr->getAttribute(ctx, sk, false);
            if (setter && setter != PROTO_NONE) return setter;
        }
        curr = curr->getFirstParent(ctx);
    }
    return getUndefinedSentinel();
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
    // §20.1.3.3 step 1 returns false WHEN V is not an Object, BEFORE
    // step 2 invokes ToObject on this. Pre-fix the previous patch
    // reordered the null/undefined-this check ahead of the V check, so
    //   Object.prototype.isPrototypeOf.call(null, undefined)
    // raised TypeError instead of returning false
    // (built-ins/Object/prototype/isPrototypeOf/null-this-and-
    // primitive-arg-returns-false caught this).
    if (!arg || arg == PROTO_NONE || arg->isNone(ctx)
        || arg == getUndefinedSentinel() || arg == getNullSentinel()
        || arg->isInteger(ctx) || arg->isDouble(ctx) || arg->isFloat(ctx)
        || arg->asString(ctx) || arg == PROTO_TRUE || arg == PROTO_FALSE)
        return PROTO_FALSE;
    // Symbol primitive is also "not an Object" here.
    {
        const proto::ProtoString* symK = JSSymbols::isSymbol(ctx);
        if (symK && arg->getAttribute(ctx, symK, true) == PROTO_TRUE)
            return PROTO_FALSE;
    }
    // §20.1.3.3 step 2: ToObject(this); null / undefined throw TypeError.
    if (!self || self == PROTO_NONE
        || self == getNullSentinel() || self == getUndefinedSentinel()) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Cannot convert undefined or null to object"));
        return PROTO_NONE;
    }

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
    // installNonEnumerableMethod (Function.prototype-parented wrapper)
    // gives the methods the spec-mandated name + length attributes.
    base = installNonEnumerableMethod(ctx, base, "hasOwnProperty",      objectHasOwnProperty,      1);
    base = installNonEnumerableMethod(ctx, base, "isPrototypeOf",        objectIsPrototypeOf,        1);
    base = installNonEnumerableMethod(ctx, base, "propertyIsEnumerable", objectPropertyIsEnumerable, 1);
    // §B.2.2.4 / §B.2.2.5 annex B legacy accessor reflectors.  Required
    // by the property-helper harness in any test using verifyProperty +
    // accessor descriptors, and directly checked by built-ins/Object/
    // prototype/__lookupGetter__ / __lookupSetter__.
    base = installNonEnumerableMethod(ctx, base, "__lookupGetter__",    objectLookupGetter,         1);
    base = installNonEnumerableMethod(ctx, base, "__lookupSetter__",    objectLookupSetter,         1);
    base = installNonEnumerableMethod(ctx, base, "__defineGetter__",    objectDefineGetter,         2);
    base = installNonEnumerableMethod(ctx, base, "__defineSetter__",    objectDefineSetter,         2);

    // §B.2.2.1 Object.prototype.__proto__ accessor pair.  Pre-fix the
    // slot was absent so Object.getOwnPropertyDescriptor(Object.prototype,
    // "__proto__") returned undefined, breaking the entire built-ins/
    // Object/prototype/__proto__ test family (the descriptor probe is
    // the first thing every test does).  Install via the __get_/__set_
    // sidecars + __has_accessor_props__ flag — exactly the shape
    // Object.defineProperty stamps for user-installed accessors.
    {
        // Wrap the getter / setter with their spec-required names
        // "get __proto__" / "set __proto__".  wrapNativeFunction sets
        // length, name, and the §17 descriptor sidecars in one shot.
        // globalRoot is unavailable here so the wrapper falls back to
        // methodPrototype (Function.prototype) for parenting — same
        // path the rest of the prototype methods use.
        const proto::ProtoObject* getFn =
            wrapNativeFunction(ctx, objectProtoGetter, "get __proto__", 0, nullptr);
        const proto::ProtoObject* setFn =
            wrapNativeFunction(ctx, objectProtoSetter, "set __proto__", 1, nullptr);
        const proto::ProtoString* pKey = ctx->fromUTF8String("__proto__")
            ? ctx->fromUTF8String("__proto__")->asString(ctx) : nullptr;
        if (pKey && getFn && setFn) {
            // Stamp the accessor sidecars directly — Object.defineProperty
            // would do the same but for Object.prototype itself we want
            // to avoid the cycle of defineProperty needing __proto__ to
            // already exist for its own internal walks.
            const proto::ProtoObject* gko = ctx->fromUTF8String("__get___proto____");
            const proto::ProtoObject* sko = ctx->fromUTF8String("__set___proto____");
            const proto::ProtoString* gk = gko ? gko->asString(ctx) : nullptr;
            const proto::ProtoString* sk = sko ? sko->asString(ctx) : nullptr;
            if (gk) base = base->setAttribute(ctx, gk, getFn);
            if (sk) base = base->setAttribute(ctx, sk, setFn);
            // Descriptor sidecar __pd___proto____ = 0x2
            // (writable:false, enumerable:false, configurable:true).
            const proto::ProtoObject* pdo = ctx->fromUTF8String("__pd___proto____");
            const proto::ProtoString* pdk = pdo ? pdo->asString(ctx) : nullptr;
            if (pdk) base = base->setAttribute(ctx, pdk, ctx->fromInteger(0x2LL));
            // Light the accessor + nonwritable hot-path flags so
            // resolveFieldOOP / resolvePutFieldOOP actually consult the
            // sidecars on every Object.prototype.__proto__ access.
            const proto::ProtoString* hapKey = JSSymbols::hasAccessorProps(ctx);
            if (hapKey) base = base->setAttribute(ctx, hapKey, PROTO_TRUE);
            const proto::ProtoString* hnwKey = JSSymbols::hasNonWritableProps(ctx);
            if (hnwKey) base = base->setAttribute(ctx, hnwKey, PROTO_TRUE);
        }
    }
    (void)reg; // legacy raw-method installer kept for the special-case branches below
    // Object.prototype.toLocaleString (§20.1.3.5): "Return ? Invoke(O, 'toString')".
    // Delegate to the receiver's own toString — for a Number this gives the
    // numeric ToString. For primitive String/Boolean/etc. the toString
    // attribute lookup can fall over odd code paths, so we special-case
    // the typed primitives via the same isInteger/isDouble/isBoolean
    // probes objectToString uses, and route through ToString-style
    // conversion via fromInteger(...)->asString-ish logic. For
    // protoCore objects (real objects) we delegate to their toString.
    static const proto::ProtoMethod objectToLocaleStringFn = [](
        proto::ProtoContext* ictx,
        const proto::ProtoObject* self,
        const proto::ParentLink*,
        const proto::ProtoList*,
        const proto::ProtoSparseList*) -> const proto::ProtoObject* {
        // §20.1.3.5 step 1 invokes ToObject(this) before reading the
        // toString slot; null / undefined must raise TypeError. Pre-
        // fix the entry routed null / undefined into objectToString
        // which produces "[object Null]" / "[object Undefined]" — the
        // spec demands an abrupt completion.
        if (!self || self == PROTO_NONE
            || self == getNullSentinel() || self == getUndefinedSentinel()) {
            signalNativeException(makeNativeError(ictx, "TypeError",
                "Cannot convert undefined or null to object"));
            return PROTO_NONE;
        }
        // §20.1.3.5 step 2: Invoke(O, "toString").  ALWAYS dispatch
        // through the prototype chain so a userland override on
        // Boolean.prototype.toString / Number.prototype.toString /
        // String.prototype.toString fires.  Pre-fix we short-circuited
        // primitives to their natural ToString, which silently ignored
        // overrides — built-ins/Array/prototype/toLocaleString/
        // primitive_this_value installs Boolean.prototype.toString =
        // function(){return typeof this;} and probes that
        // [true,false].toLocaleString() === "boolean,boolean".
        // Only fall back to the natural synthesis when chain lookup
        // truly returns nothing callable (e.g. attribute deleted off
        // the prototype mid-call).
        const proto::ProtoString* tsKey = JSSymbols::toString(ictx);
        const proto::ProtoObject* tsFn = nullptr;
        // Accessor descriptor: a getter for toString lives at
        // __get_toString__ on the chain (Object.defineProperty installs
        // accessors there).  The data slot may still carry the inherited
        // pre-defineProperty value, so prefer the accessor when both
        // are present.  Pre-fix Boolean.prototype.toString installed as
        // a getter (built-ins/Array/prototype/toLocaleString/
        // primitive_this_value_getter) fell through to the natural
        // ToString.
        {
            const proto::ProtoObject* gko = ictx->fromUTF8String("__get_toString__");
            const proto::ProtoString* gk = gko ? gko->asString(ictx) : nullptr;
            if (gk) {
                const proto::ProtoObject* getter = self->getAttribute(ictx, gk, true);
                if (getter && getter != PROTO_NONE) {
                    tsFn = callJSFunction(ictx, getter, self, ictx->newList());
                    if (hasCallException()) return PROTO_NONE;
                }
            }
        }
        if (!tsFn || tsFn == PROTO_NONE) {
            tsFn = tsKey ? self->getAttribute(ictx, tsKey, true) : nullptr;
        }
        auto naturalToString = [&]() -> const proto::ProtoObject* {
            if (self->isString(ictx)) return self;
            if (self->isBoolean(ictx))
                return ictx->fromUTF8String(self->asBoolean(ictx) ? "true" : "false");
            if (self->isInteger(ictx)) {
                const std::string tmp = std::to_string(self->asLong(ictx));
                return ictx->fromUTF8String(tmp.c_str());
            }
            if (self->isDouble(ictx) || self->isFloat(ictx)) {
                char buf[64];
                double d = self->asDouble(ictx);
                if (std::isnan(d))      return ictx->fromUTF8String("NaN");
                if (std::isinf(d))      return ictx->fromUTF8String(d > 0 ? "Infinity" : "-Infinity");
                if (d == 0.0)           return ictx->fromUTF8String("0");
                if (d == std::trunc(d) && std::abs(d) < 1e21) {
                    long long iv = static_cast<long long>(d);
                    if (static_cast<double>(iv) == d)
                        return ictx->fromUTF8String(std::to_string(iv).c_str());
                }
                snprintf(buf, sizeof(buf), "%.15g", d);
                return ictx->fromUTF8String(buf);
            }
            return objectToString(ictx, self, nullptr, nullptr, nullptr);
        };
        if (!tsFn || tsFn == PROTO_NONE) return naturalToString();
        // tsFn must be callable; if not, fall back to the natural
        // primitive ToString synthesis rather than throwing TypeError
        // mid-locale formatting.
        if (!tsFn->isMethod(ictx)) {
            const proto::ProtoString* bcKey = JSSymbols::bytecodeId(ictx);
            const proto::ProtoString* nfKey = JSSymbols::nativeFn(ictx);
            bool callable =
                (bcKey && tsFn->hasAttribute(ictx, bcKey) == PROTO_TRUE) ||
                (nfKey && tsFn->hasAttribute(ictx, nfKey) == PROTO_TRUE);
            if (!callable) return naturalToString();
        }
        return callJSFunction(ictx, tsFn, self, ictx->newList());
    };
    // §20.1.3 — Object.prototype.{toString,toLocaleString,valueOf} are
    // built-in functions and must expose the §17 name/length descriptor
    // shape (length = 0, name = "<method>", both {writable:false,
    // enumerable:false, configurable:true}).  Pre-fix the local `reg`
    // helper here installed the raw ProtoMethod cell (no length, no name)
    // so test262 built-ins/Object/prototype/toString/{length,name}.js and
    // the analogous valueOf / toLocaleString fixtures failed with
    // "obj should have an own property length".  Route through
    // installNonEnumerableMethod (which goes via wrapNativeFunction
    // → name/length sidecars + the just-fixed __has_nonwritable_props__
    // hot-path flag) for the spec-mandated shape.
    base = installNonEnumerableMethod(ctx, base, "toLocaleString", objectToLocaleStringFn, 0);
    base = installNonEnumerableMethod(ctx, base, "toString",       objectToString,        0);
    base = installNonEnumerableMethod(ctx, base, "valueOf",        objectValueOf,         0);
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

    // Build Object constructor object.  Parent: Function.prototype
    // so that \`Object.apply\`, \`Object.call\`, \`Object.bind\` resolve
    // via the standard chain walk (same fix as ArrayPrototype).
    const proto::ProtoObject* ctorParent =
        (ctx->space && ctx->space->methodPrototype) ? ctx->space->methodPrototype : nullptr;
    // ctor must be mutable so the constructor backref roundtrip
    // (Object.prototype.constructor === Object) survives. With an
    // immutable ctor the final ctor.prototype re-link split it into a
    // different identity than the one stored at globalRoot.Object.
    const proto::ProtoObject* ctor = ctorParent
        ? ctorParent->newChild(ctx, true)
        : ctx->newObject(true);
    if (!ctor) return;

    auto reg = [&](const char* name, proto::ProtoMethod fn, long long length = 1) {
        const proto::ProtoString* key = ctx->fromUTF8String(name)->asString(ctx);
        if (key) {
            const proto::ProtoObject* wrapped = wrapNativeFunction(ctx, fn, name, length, globalRoot);
            if (wrapped && wrapped != PROTO_NONE) {
                ctor = ctor->setAttribute(ctx, key, wrapped);
                // §17 says built-in methods are
                //   {writable:true, enumerable:false, configurable:true} → 0x3.
                // Pre-fix no sidecar was set so the default
                //   {writable:true, enumerable:true, configurable:true}
                // bled through — making Object.getOwnPropertyDescriptors,
                // Object.assign, Object.keys, Object.create, etc.
                // enumerate as own properties of the Object constructor
                // (and visible in for-in / Object.keys(Object)).
                std::string pdStr = std::string("__pd_") + name + "__";
                const proto::ProtoString* pdk =
                    ctx->fromUTF8String(pdStr.c_str())->asString(ctx);
                if (pdk) ctor = ctor->setAttribute(ctx, pdk, ctx->fromInteger(0x3LL));
            }
        }
    };

    reg("keys",                  objectKeys,                  1);
    reg("values",                objectValues,                1);
    reg("entries",               objectEntries,               1);
    reg("assign",                objectAssign,                2);
    reg("create",                objectCreate,                2);
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
    reg("getOwnPropertyDescriptors", objectGetOwnPropertyDescriptors, 1);
    reg("getOwnPropertySymbols",    objectGetOwnPropertySymbols,    1);

    // Object.is(a, b) — SameValue per ECMA-262 §7.2.11.  Differs from ===
    // in two ways: NaN is Object.is NaN, and +0 is NOT Object.is -0.
    static const proto::ProtoMethod objectIsFn = [](
        proto::ProtoContext* ictx, const proto::ProtoObject* /*self*/,
        const proto::ParentLink*, const proto::ProtoList* ia,
        const proto::ProtoSparseList*) -> const proto::ProtoObject* {
        const proto::ProtoObject* a = (ia && ia->getSize(ictx) > 0) ? ia->getAt(ictx, 0) : PROTO_NONE;
        const proto::ProtoObject* b = (ia && ia->getSize(ictx) > 1) ? ia->getAt(ictx, 1) : PROTO_NONE;
        // SameValue must distinguish +0 from -0 even when both numeric
        // operands collapse to the same protoCore representation. Run the
        // zero check BEFORE the identity short-circuit; otherwise
        // Object.is(0, -0) returns true.
        auto isNumericZero = [&](const proto::ProtoObject* v, bool& negZero) -> bool {
            if (!v) return false;
            if (v->isInteger(ictx)) {
                if (v->asLong(ictx) == 0) { negZero = false; return true; }
                return false;
            }
            if (v->isDouble(ictx) || v->isFloat(ictx)) {
                double d = v->asDouble(ictx);
                if (d == 0.0) { negZero = std::signbit(d); return true; }
                return false;
            }
            return false;
        };
        bool aNeg = false, bNeg = false;
        bool aZ = isNumericZero(a, aNeg);
        bool bZ = isNumericZero(b, bNeg);
        if (aZ && bZ) return (aNeg == bNeg) ? PROTO_TRUE : PROTO_FALSE;
        if (a == b) return PROTO_TRUE;
        // NaN === NaN check
        if (a && b && (a->isDouble(ictx) || a->isFloat(ictx)) &&
            (b->isDouble(ictx) || b->isFloat(ictx))) {
            double da = a->asDouble(ictx);
            double db = b->asDouble(ictx);
            if (std::isnan(da) && std::isnan(db)) return PROTO_TRUE;
            return (da == db) ? PROTO_TRUE : PROTO_FALSE;
        }
        // Both undefined (any form): SameValue is true.  §7.2.10 step 4
        // collapses ALL undefined values; the explicit-undefined argument
        // and the missing-argument sentinel must compare equal.  Pre-fix
        // Object.is(undefined) returned false because the explicit
        // undefined surfaced as t_undefinedSentinel while the missing
        // second argument defaulted to PROTO_NONE — isNone() rejected
        // the sentinel and a == b also did not match.
        auto isUndef = [&](const proto::ProtoObject* x) {
            return !x || x == PROTO_NONE || x == getUndefinedSentinel()
                   || (x && x->isNone(ictx));
        };
        if (isUndef(a) && isUndef(b)) return PROTO_TRUE;
        if (a && b) {
            int cmp = a->compare(ictx, b);
            return (cmp == 0) ? PROTO_TRUE : PROTO_FALSE;
        }
        return PROTO_FALSE;
    };
    {
        const proto::ProtoString* key = ctx->fromUTF8String("is")->asString(ctx);
        if (key) {
            const proto::ProtoObject* wrapped = wrapNativeFunction(ctx, objectIsFn, "is", 2, globalRoot);
            if (wrapped && wrapped != PROTO_NONE) {
                ctor = ctor->setAttribute(ctx, key, wrapped);
                // §17 descriptor 0x3 — same as the `reg` lambda.
                const proto::ProtoString* pdk =
                    ctx->fromUTF8String("__pd_is__")->asString(ctx);
                if (pdk) ctor = ctor->setAttribute(ctx, pdk, ctx->fromInteger(0x3LL));
            }
        }
    }

    const proto::ProtoString* protoKey = JSSymbols::prototype(ctx);
    if (protoKey) {
        ctor = ctor->setAttribute(ctx, protoKey, proto);
        // §20.1.2.{17} Object.prototype is {writable:false,
        // enumerable:false, configurable:false} → bits 0x0. Pre-fix
        // no sidecar so the default 0x7 (full enumerable) leaked it
        // into Object.keys(Object).
        const proto::ProtoString* pdk =
            ctx->fromUTF8String("__pd_prototype__")->asString(ctx);
        if (pdk) ctor = ctor->setAttribute(ctx, pdk, ctx->fromInteger(0x0LL));
    }
    const proto::ProtoString* nameKey = JSSymbols::name(ctx);
    if (nameKey) {
        ctor = ctor->setAttribute(ctx, nameKey, ctx->fromUTF8String("Object"));
        // §17: built-in ctor name descriptor 0x2.
        const proto::ProtoString* pdns = JSSymbols::pdName(ctx);
        if (pdns) ctor = ctor->setAttribute(ctx, pdns, ctx->fromInteger(0x2LL));
    }
    // Object.length === 1 per §20.1.1.
    const proto::ProtoString* lenKey = JSSymbols::length(ctx);
    if (lenKey) {
        ctor = ctor->setAttribute(ctx, lenKey, ctx->fromInteger(1LL));
        const proto::ProtoString* pdlk = JSSymbols::pdLength(ctx);
        if (pdlk) ctor = ctor->setAttribute(ctx, pdlk, ctx->fromInteger(0x2LL));
    }

    // Explicitly mark as a constructor for OP_call_constructor.
    const proto::ProtoString* isCtorKey = ctx->fromUTF8String("__is_constructor__")->asString(ctx);
    if (isCtorKey) ctor = ctor->setAttribute(ctx, isCtorKey, PROTO_TRUE);

    // Mark as callable so OP_typeof / OP_typeof_is_function return "function".
    // The __construct__ method also handles `new Object(value)` → coerce to object.
    static const proto::ProtoMethod objectCtorFn = [](
        proto::ProtoContext* ctx, const proto::ProtoObject* self,
        const proto::ParentLink*, const proto::ProtoList* args,
        const proto::ProtoSparseList*) -> const proto::ProtoObject* {
        // ECMA-262 §19.1.1 Object([value]):
        //   - no/undefined/null value → fresh empty object (the new self).
        //   - object value → return value unchanged (boxing identity).
        //   - primitive value (boolean/number/string) → wrap as a
        //     primitive-wrapper object carrying __primitive_value__,
        //     so subsequent valueOf/toString/coercion sees the primitive.
        //     Pre-fix the primitive case returned `self` with no
        //     primitive-value attribute, so `Object(5) + 0` produced
        //     '[object Object]0' instead of 5.
        if (!args || args->getSize(ctx) == 0) return self;
        const proto::ProtoObject* val = args->getAt(ctx, 0);
        if (!val || val == PROTO_NONE
            || val == getUndefinedSentinel() || val == getNullSentinel())
            return self;
        // §19.1.1 Object(value): primitive boxed via the matching
        // wrapper prototype (Boolean / Number / String) so
        //   Object(0).valueOf() === 0, Object(0).constructor === Number
        // (the spec-required identity check). Pre-fix the boxed
        // wrapper inherited Object.prototype directly, so `.valueOf()`
        // returned the object itself and `.constructor` was Object.
        const proto::ProtoObject* wrapProto = nullptr;
        if (ctx->space) {
            if (val == PROTO_TRUE || val == PROTO_FALSE || val->isBoolean(ctx))
                wrapProto = ctx->space->booleanPrototype;
            else if (val->isInteger(ctx) || val->isDouble(ctx) || val->isFloat(ctx))
                wrapProto = ctx->space->doublePrototype;
            else if (val->isString(ctx))
                wrapProto = ctx->space->stringPrototype;
        }
        if (wrapProto && wrapProto != PROTO_NONE) {
            const proto::ProtoObject* boxed = wrapProto->newChild(ctx, true);
            if (boxed) {
                const proto::ProtoString* pvKey = JSSymbols::primitiveValue(ctx);
                if (pvKey) boxed = boxed->setAttribute(ctx, pvKey, val);
                return boxed;
            }
        }
        if (val->isBoolean(ctx) || val->isInteger(ctx) || val->isDouble(ctx) ||
            val->isString(ctx)) {
            const proto::ProtoString* pvKey = JSSymbols::primitiveValue(ctx);
            if (pvKey && self) self = self->setAttribute(ctx, pvKey, val);
            return self;
        }
        return val;
    };
    const proto::ProtoObject* ctorMethodObj = ctx->fromMethod(nullptr, objectCtorFn);
    const proto::ProtoString* constructKey =
        ctx->fromUTF8String("__construct__") ? ctx->fromUTF8String("__construct__")->asString(ctx) : nullptr;
    if (constructKey && ctorMethodObj)
        ctor = ctor->setAttribute(ctx, constructKey, ctorMethodObj);

    // Object.prototype.constructor === Object per §20.1.3.1.
    // Without this `({}).constructor` is undefined, breaking every
    // test262 case that walks `instance.constructor` to compare against
    // the Object identity.
    if (proto && proto != PROTO_NONE) {
        const proto::ProtoString* ctorWordKey = JSSymbols::constructor(ctx);
        if (ctorWordKey) {
            const proto::ProtoObject* updatedProto =
                proto->setAttribute(ctx, ctorWordKey, ctor);
            // Spec §20.1.3.1: prototype.constructor descriptor is
            // {writable:true, enumerable:false, configurable:true} → 0x3.
            // Without the descriptor sidecar `for (k in obj) ...`
            // emits "constructor" because the default is fully
            // enumerable.
            if (updatedProto && updatedProto != PROTO_NONE) {
                const proto::ProtoString* pdk = JSSymbols::pdConstructor(ctx);
                if (pdk) updatedProto = updatedProto->setAttribute(ctx, pdk,
                    ctx->fromInteger(0x3LL));
            }
            if (ctx->space && updatedProto && updatedProto != PROTO_NONE) {
                const proto::ProtoObject* oldOP = ctx->space->objectPrototype;
                ctx->space->objectPrototype = const_cast<proto::ProtoObject*>(updatedProto);
                // ensureFunctionPrototype already parented Function.prototype
                // at the OLD objectPrototype pointer; that snapshot lacks
                // the `constructor` backref we just added. Without further
                // help, `Function.prototype instanceof Object` and
                // `Array instanceof Object` both return false because the
                // user-visible Object.prototype (= updatedProto) is not
                // reachable through Function.prototype's chain. Tie the
                // two by adding updatedProto as an extra parent of fp
                // (fp is mutable, so addParent mutates in place).
                if (oldOP != updatedProto && ctx->space->methodPrototype
                    && ctx->space->methodPrototype != oldOP) {
                    ctx->space->methodPrototype->addParent(ctx, updatedProto);
                }
                // Sync the wrapper-level JS Object prototype too, so
                // TypeBridge::fromJS stamps JSON-parsed objects with
                // the populated proto (matches the array fix from
                // round 4 commit 4b3fde8f). Pre-fix
                // JSON.parse('{"a":1}') produced an object whose proto
                // looked like Object.prototype but wasn't ===.
                if (JSContextWrapper* w = JSContextWrapper::current()) {
                    w->setJSObjectPrototype(updatedProto);
                }
            }
            // Re-link ctor.prototype to the post-update prototype.
            // objectPrototype is created immutable by protoCore, so
            // setAttribute returned a NEW object — ctor.prototype must
            // be updated or `Object.prototype.constructor` (which
            // reads through globalRoot.Object.prototype) still sees
            // the pre-update proto without the backref.
            if (updatedProto && updatedProto != PROTO_NONE) {
                const proto::ProtoString* protoKey2 = JSSymbols::prototype(ctx);
                if (protoKey2) ctor = ctor->setAttribute(ctx, protoKey2, updatedProto);
            }
        }
    }

    *globalRoot = (*globalRoot)->setAttribute(ctx, keyObject, ctor);
    // §17 globalThis.Object descriptor 0x3.
    {
        const proto::ProtoObject* pdo = ctx->fromUTF8String("__pd_Object__");
        const proto::ProtoString* pdk = pdo ? pdo->asString(ctx) : nullptr;
        if (pdk) *globalRoot = (*globalRoot)->setAttribute(ctx, pdk,
            ctx->fromInteger(0x3LL));
    }
}

const proto::ProtoObject* getJSProtoOverride(const proto::ProtoObject* obj)
{
    if (!obj) return nullptr;
    auto it = t_jsProtoMap.find(obj);
    return (it != t_jsProtoMap.end()) ? it->second : nullptr;
}

void setJSProtoOverride(const proto::ProtoObject* obj,
                        const proto::ProtoObject* proto)
{
    if (!obj) return;
    if (proto == nullptr) {
        t_jsProtoMap.erase(obj);
        return;
    }
    t_jsProtoMap[obj] = proto;
}

void setJSProtoOverride(proto::ProtoContext* ctx,
                        const proto::ProtoObject* obj,
                        const proto::ProtoObject* proto)
{
    if (!obj) return;
    // The legacy map is still updated as a safety net for any read
    // path that has not yet been migrated.  When all sites are
    // converted, the map writes here can be removed and the map type
    // itself retired.
    if (proto == nullptr) {
        t_jsProtoMap.erase(obj);
        return;
    }
    t_jsProtoMap[obj] = proto;

    // Skip the protoCore rebind for the null-sentinel case: setParents
    // with an empty parent list would expose the protoCore default
    // parent (typically the Object cell prototype), which is the
    // opposite of what `Object.setPrototypeOf(o, null)` requests.  The
    // map override above is sufficient for that case.
    if (!ctx || proto == getNullSentinel()) return;

    // For mutable obj, setParents CAS-rebinds the parent chain in
    // place and returns the same handle.  For immutable obj, it would
    // return a different handle — we deliberately discard that case
    // and lean on the map fallback so the identity of obj is preserved.
    const proto::ProtoList* parents = ctx->newList();
    if (!parents) return;
    parents = parents->appendLast(ctx, proto);
    if (!parents) return;
    (void)obj->setParents(ctx, parents);
}

} // namespace protojs
