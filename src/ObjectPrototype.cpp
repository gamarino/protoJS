#include "ObjectPrototype.h"
#include "ArrayPrototype.h"
#include "ProxyBuiltin.h"
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
    // Per-instance Symbol identity keys (\`@@sym#<addr>\`) are internal
    // bookkeeping — they map a Symbol value to a unique ProtoString
    // identity for attribute storage, but JS-visible enumeration
    // (Object.keys / values / getOwnPropertyNames / for-in / Object.assign)
    // must skip them.  Object.getOwnPropertySymbols is the only spec
    // surface that reports symbol-keyed properties; it has its own path
    // and consults the Symbol value, not the string-keyed attribute layer.
    if (s.size() >= 6 && s[0] == '@' && s[1] == '@'
        && s[2] == 's' && s[3] == 'y' && s[4] == 'm' && s[5] == '#')
        return true;
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
        // §7.3.21 EnumerableOwnProperties filters by Type(key)=String — only
        // for `Object.{keys,values,entries,assign}`.  protoJS encodes JS
        // Symbol-keyed properties under the canonical "Symbol.<name>"
        // string (or "Symbol(<desc>)" for user-created ones), so skip
        // those by content rather than by ProtoString::isSymbol() — the
        // pre-fix tag check fired for every interned attribute name and
        // dropped every built-in toString/hasOwnProperty/valueOf from
        // the names list.
        {
            std::string kstrTmp;
            propKey->toUTF8String(cbCtx, kstrTmp);
            if (kstrTmp.compare(0, 7, "Symbol.") == 0
                || kstrTmp.compare(0, 7, "Symbol(") == 0) return;
        }
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
        // §7.3.23 EnumerableOwnProperties: re-check enumerable at
        // each step on the LIVE descriptor — a previously-invoked
        // getter may have flipped this key's enumerable bit.  Pre-
        // fix the probe was gated by s->mightHaveNonWritable which
        // is only set when a writable-false descriptor lands; an
        // enumerable-false flip with writable still true (test262
        // Object.values/getter-making-future-key-nonenumerable.js)
        // bypassed the probe and the now-non-enumerable key still
        // emerged in the result.  Always probe when filtering.
        if (!s->includeNonEnumerable) {
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

    // Proxy receiver — dispatch [[OwnPropertyKeys]] via the canonical
    // proxyDispatchOwnKeys so all §10.5.11 + §7.3.18 invariants
    // (revoked-handler, non-Object trap result, duplicate entries,
    // non-string/non-symbol entry) fire as TypeErrors.  Then filter
    // to enumerable string-only keys per §7.3.21 EnumerableOwnProperties.
    if (protojs::isProxy(ctx, obj)) {
        const proto::ProtoObject* keysArr = protojs::proxyDispatchOwnKeys(ctx, obj);
        if (hasCallException()) return PROTO_NONE;
        if (keysArr) {
            const proto::ProtoObject* result = createNewArray(ctx, nullptr);
            const proto::ProtoString* lenKey = JSSymbols::length(ctx);
            const proto::ProtoString* isArrKey2 = JSSymbols::isArray(ctx);
            const proto::ProtoList* elsList = ctx->newList();
            unsigned long count = 0;
            const proto::ProtoList* keysEls = protojs::getArrayElements(ctx, keysArr);
            size_t n = keysEls ? keysEls->getSize(ctx) : 0;
            for (size_t i = 0; i < n; i++) {
                const proto::ProtoObject* keyVal = keysEls->getAt(ctx, i);
                if (!keyVal || keyVal == PROTO_NONE) continue;
                const proto::ProtoString* kStr = keyVal->asString(ctx);
                if (!kStr) continue;
                // §7.3.21 step 5 — only string keys for Object.keys
                // (skip Symbol entries).
                const proto::ProtoString* isSymK = JSSymbols::isSymbol(ctx);
                if (isSymK && keyVal->getAttribute(ctx, isSymK, false) == PROTO_TRUE) continue;
                // Filter by enumerable via the proxy's gOPD trap.
                const proto::ProtoObject* desc =
                    protojs::proxyDispatchGetOwnPropertyDescriptor(ctx, obj, kStr);
                if (hasCallException()) return PROTO_NONE;
                if (!desc || desc == PROTO_NONE) continue;
                const proto::ProtoObject* eko = ctx->fromUTF8String("enumerable");
                const proto::ProtoString* eks = eko ? eko->asString(ctx) : nullptr;
                if (eks) {
                    const proto::ProtoObject* ev = desc->getAttribute(ctx, eks, false);
                    if (ev != PROTO_TRUE) continue;
                }
                elsList = elsList->appendLast(ctx, keyVal);
                count++;
            }
            setArrayElements(ctx, result, elsList);
            if (lenKey) result = result->setAttribute(ctx, lenKey, ctx->fromInteger(count));
            if (isArrKey2) result = result->setAttribute(ctx, isArrKey2, PROTO_TRUE);
            return result;
        }
        // No ownKeys trap → fall through to default own-attr walk
        // on the unwrapped concrete target. Loop until we leave the
        // proxy chain; a single unwrap is not enough when the
        // immediate target is itself a Proxy (test262
        // Proxy/ownKeys/trap-is-null-target-is-proxy.js).
        int guard = 16;
        while (guard-- > 0 && protojs::isProxy(ctx, obj)) {
            const proto::ProtoObject* next = protojs::proxyTarget(ctx, obj);
            if (!next || next == obj) break;
            obj = next;
        }
    }

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

    // Proxy receiver: walk handler.ownKeys → enumerable filter via
    // handler.getOwnPropertyDescriptor → handler.get per §7.3.23
    // EnumerableOwnProperties("value").  Pre-fix Object.values walked
    // the proxy cell's own attribute layer (the __proxy_target__ /
    // __proxy_handler__ sidecars only), so the result was always [].
    if (isProxy(ctx, obj)) {
        const proto::ProtoObject* keysArr = proxyDispatchOwnKeys(ctx, obj);
        if (hasCallException()) return PROTO_NONE;
        if (keysArr) {
            const proto::ProtoList* outEls = ctx->newList();
            long long count = 0;
            const proto::ProtoList* els = getArrayElements(ctx, keysArr);
            size_t n = els ? els->getSize(ctx) : 0;
            for (size_t i = 0; i < n; i++) {
                const proto::ProtoObject* kObj = els->getAt(ctx, i);
                if (!kObj || kObj == PROTO_NONE || !kObj->asString(ctx)) continue;
                const proto::ProtoString* kStr = kObj->asString(ctx);
                const proto::ProtoObject* desc =
                    proxyDispatchGetOwnPropertyDescriptor(ctx, obj, kStr);
                if (hasCallException()) return PROTO_NONE;
                if (!desc || desc == PROTO_NONE) continue;
                const proto::ProtoString* enumK = ctx->fromUTF8String("enumerable")->asString(ctx);
                const proto::ProtoObject* ev = enumK ? desc->getAttribute(ctx, enumK, true) : nullptr;
                if (ev != PROTO_TRUE) continue;
                const proto::ProtoObject* val =
                    protojs::proxyDispatchGet(ctx, obj, kStr, obj);
                if (hasCallException()) return PROTO_NONE;
                outEls = outEls->appendLast(ctx, val ? val : PROTO_NONE);
                count++;
            }
            const proto::ProtoObject* result = createNewArray(ctx, nullptr);
            const proto::ProtoString* lenKey  = JSSymbols::length(ctx);
            const proto::ProtoString* isArrKey2 = JSSymbols::isArray(ctx);
            setArrayElements(ctx, result, outEls);
            if (lenKey)  result = result->setAttribute(ctx, lenKey, ctx->fromInteger(count));
            if (isArrKey2) result = result->setAttribute(ctx, isArrKey2, PROTO_TRUE);
            return result;
        }
        // No ownKeys trap → unwrap target and fall through to normal
        // enumeration so a trap-less Proxy behaves like its target.
        {
            // Loop-unwrap nested Proxies until we reach a concrete
            // target — a single unwrap leaves the immediate target as
            // still a Proxy (whose own attribute layer is just sidecars).
            int _g = 16;
            while (_g-- > 0 && isProxy(ctx, obj)) {
                const proto::ProtoObject* _n = proxyTarget(ctx, obj);
                if (!_n || _n == obj) break;
                obj = _n;
            }
        }
    }

    std::vector<std::string> keys;
    std::vector<const proto::ProtoObject*> vals;
    collectOwnKeys(ctx, obj, keys, &vals);
    // §7.3.23 EnumerableOwnProperties calls Get(O, P) per key; a
    // throwing accessor must abort the whole call.  collectOwnKeys
    // sets s->aborted on hasCallException, but its caller did not
    // propagate — so a getter-throw fell through and Object.values
    // happily returned the partial result instead of re-raising.
    if (hasCallException()) return PROTO_NONE;

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

    // Proxy receiver — same trap-driven enumeration as Object.values
    // per §7.3.23, except the per-key result is a [key, value] pair.
    if (isProxy(ctx, obj)) {
        const proto::ProtoObject* keysArr = proxyDispatchOwnKeys(ctx, obj);
        if (hasCallException()) return PROTO_NONE;
        if (keysArr) {
            const proto::ProtoList* outerList = ctx->newList();
            long long count = 0;
            const proto::ProtoList* els = getArrayElements(ctx, keysArr);
            size_t n = els ? els->getSize(ctx) : 0;
            const proto::ProtoString* lenKey  = JSSymbols::length(ctx);
            const proto::ProtoString* isArrKey2 = JSSymbols::isArray(ctx);
            for (size_t i = 0; i < n; i++) {
                const proto::ProtoObject* kObj = els->getAt(ctx, i);
                if (!kObj || kObj == PROTO_NONE || !kObj->asString(ctx)) continue;
                const proto::ProtoString* kStr = kObj->asString(ctx);
                const proto::ProtoObject* desc =
                    proxyDispatchGetOwnPropertyDescriptor(ctx, obj, kStr);
                if (hasCallException()) return PROTO_NONE;
                if (!desc || desc == PROTO_NONE) continue;
                const proto::ProtoString* enumK = ctx->fromUTF8String("enumerable")->asString(ctx);
                const proto::ProtoObject* ev = enumK ? desc->getAttribute(ctx, enumK, true) : nullptr;
                if (ev != PROTO_TRUE) continue;
                const proto::ProtoObject* val =
                    protojs::proxyDispatchGet(ctx, obj, kStr, obj);
                if (hasCallException()) return PROTO_NONE;
                const proto::ProtoObject* pair = createNewArray(ctx, nullptr);
                const proto::ProtoList* pairEls = ctx->newList();
                pairEls = pairEls->appendLast(ctx, kObj);
                pairEls = pairEls->appendLast(ctx, val ? val : PROTO_NONE);
                setArrayElements(ctx, pair, pairEls);
                if (lenKey)    pair = pair->setAttribute(ctx, lenKey, ctx->fromInteger(2LL));
                if (isArrKey2) pair = pair->setAttribute(ctx, isArrKey2, PROTO_TRUE);
                outerList = outerList->appendLast(ctx, pair);
                count++;
            }
            const proto::ProtoObject* result = createNewArray(ctx, nullptr);
            setArrayElements(ctx, result, outerList);
            if (lenKey)  result = result->setAttribute(ctx, lenKey, ctx->fromInteger(count));
            if (isArrKey2) result = result->setAttribute(ctx, isArrKey2, PROTO_TRUE);
            return result;
        }
        {
            // Loop-unwrap nested Proxies until we reach a concrete
            // target — a single unwrap leaves the immediate target as
            // still a Proxy (whose own attribute layer is just sidecars).
            int _g = 16;
            while (_g-- > 0 && isProxy(ctx, obj)) {
                const proto::ProtoObject* _n = proxyTarget(ctx, obj);
                if (!_n || _n == obj) break;
                obj = _n;
            }
        }
    }

    std::vector<std::string> keys;
    std::vector<const proto::ProtoObject*> vals;
    collectOwnKeys(ctx, obj, keys, &vals);
    if (hasCallException()) return PROTO_NONE;

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
    } else {
        // §7.1.18 ToObject on a Symbol primitive boxes it in a Symbol
        // wrapper whose [[SymbolData]] is the original symbol.  Pre-fix
        // Object.assign(symbol, src) returned the symbol unchanged, so
        // typeof result === "symbol" (test262 Object/assign/Target-Symbol
        // expected "object" because the wrapper hides the primitive
        // typeof tag behind a wrapper-shaped own-attribute layout).
        const proto::ProtoString* isSymK = JSSymbols::isSymbol(ctx);
        if (isSymK && target->getAttribute(ctx, isSymK, true) == PROTO_TRUE) {
            const proto::ProtoObject* symProto = nullptr;
            JSContextWrapper* aw2 = JSContextWrapper::current();
            if (aw2 && aw2->getNativeGlobal()) {
                const proto::ProtoObject* symGKo = ctx->fromUTF8String("Symbol");
                const proto::ProtoString* symGK = symGKo ? symGKo->asString(ctx) : nullptr;
                if (symGK) {
                    const proto::ProtoObject* symCtor =
                        aw2->getNativeGlobal()->getAttribute(ctx, symGK, false);
                    if (symCtor && symCtor != PROTO_NONE) {
                        const proto::ProtoString* pk = JSSymbols::prototype(ctx);
                        if (pk) symProto = symCtor->getAttribute(ctx, pk, false);
                    }
                }
            }
            target = wrapPrimitive(target, symProto);
        }
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
        // §19.1.2.1 step 4.c — Proxy source: route through the [[OwnProperty-
        // Keys]] trap + per-key [[GetOwnProperty]] trap.  Pre-fix the loop
        // walked the protoCore own-attribute iterator directly, so a Proxy
        // with `ownKeys` / `getOwnPropertyDescriptor` traps silently
        // returned the underlying object's own keys without firing the
        // traps (test262 Object/assign/source-own-prop-{error,desc-missing,
        // keys-error}.js).
        if (isProxy(ctx, src)) {
            // OwnPropertyKeys: dispatch the trap if present, else fall
            // through to the target's own keys (the spec forwarding case).
            const proto::ProtoObject* keysArr = proxyDispatchOwnKeys(ctx, src);
            if (hasCallException()) return PROTO_NONE;
            const proto::ProtoList* els = nullptr;
            if (keysArr) {
                els = getArrayElements(ctx, keysArr);
            } else {
                // No ownKeys trap — walk the unwrapped target's own
                // attributes so the per-key gOPD dispatch can still
                // fire (test262 assign/source-own-prop-error.js: the
                // proxy has only getOwnPropertyDescriptor, no ownKeys).
                const proto::ProtoObject* tgt = proxyTarget(ctx, src);
                if (!tgt) continue;
                const proto::ProtoList* tmp = ctx->newList();
                const proto::ProtoSparseList* tOwn = tgt->getOwnAttributes(ctx);
                const proto::ProtoSparseListIterator* tit =
                    tOwn ? tOwn->getIterator(ctx) : nullptr;
                while (tit && tit->hasNext(ctx)) {
                    unsigned long rk = tit->nextKey(ctx);
                    (void)tit->nextValue(ctx);
                    tit = const_cast<proto::ProtoSparseListIterator*>(tit)->advance(ctx);
                    const proto::ProtoString* ks =
                        reinterpret_cast<const proto::ProtoString*>(rk);
                    if (!ks) continue;
                    if (isInternalKey(ctx, ks)) continue;
                    tmp = tmp->appendLast(ctx, ks->asObject(ctx));
                }
                els = tmp;
            }
            size_t n = els ? els->getSize(ctx) : 0;
            for (size_t i = 0; i < n; ++i) {
                const proto::ProtoObject* keyObj = els->getAt(ctx, i);
                if (!keyObj || keyObj == PROTO_NONE) continue;
                const proto::ProtoString* propKey = keyObj->asString(ctx);
                if (!propKey) continue;
                const proto::ProtoObject* desc =
                    proxyDispatchGetOwnPropertyDescriptor(ctx, src, propKey);
                if (hasCallException()) return PROTO_NONE;
                if (!desc || desc == PROTO_NONE) continue;
                {
                    const proto::ProtoObject* eko = ctx->fromUTF8String("enumerable");
                    const proto::ProtoString* eks = eko ? eko->asString(ctx) : nullptr;
                    if (eks) {
                        const proto::ProtoObject* ev = desc->getAttribute(ctx, eks, false);
                        if (ev != PROTO_TRUE) continue;
                    }
                }
                const proto::ProtoObject* val =
                    proxyDispatchGet(ctx, src, propKey, src);
                if (hasCallException()) return PROTO_NONE;
                target = const_cast<proto::ProtoObject*>(target)
                    ->setAttribute(ctx, propKey, val ? val : PROTO_NONE);
            }
            continue;
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
        // §22.1.4.1 — if the target is a String wrapper, any index
        // < the wrapped string's length collides with a non-writable
        // own char slot. Pre-fix the array-source fast path here
        // silently overwrote those slots (test262
        // Object/assign/assignment-to-readonly-property-of-target-
        // must-throw-a-typeerror-exception.js).
        {
            const proto::ProtoString* pvKey =
                JSSymbols::primitiveValue(ctx);
            const proto::ProtoObject* pv = pvKey
                ? target->getAttribute(ctx, pvKey, false) : nullptr;
            if (pv && pv != PROTO_NONE && pv->isString(ctx)) {
                const proto::ProtoList* srcElsProbe = getArrayElements(ctx, src);
                const proto::ProtoString* ps = pv->asString(ctx);
                if (srcElsProbe && srcElsProbe->getSize(ctx) > 0
                    && ps && ps->getSize(ctx) > 0) {
                    signalNativeException(makeNativeError(ctx, "TypeError",
                        "Cannot assign to read only property on a "
                        "String wrapper"));
                    return PROTO_NONE;
                }
            }
        }
        // If src is an array, copy __elements__ first so that
        // numeric-index iteration sees the data.  Skip holes — §20.1.2.1
        // step 4.c.ii only copies own enumerable properties, and array
        // holes are not own properties (`i in src` is false).
        const proto::ProtoList* srcEls = getArrayElements(ctx, src);
        if (srcEls) {
            const proto::ProtoList* tgtEls = getArrayElements(ctx, target);
            if (!tgtEls) tgtEls = ctx->newList();
            size_t srcSz = srcEls->getSize(ctx);
            size_t tgtSz = tgtEls->getSize(ctx);
            bool anyWritten = false;
            for (size_t i = 0; i < srcSz; ++i) {
                const proto::ProtoObject* v = srcEls->getAt(ctx, static_cast<int>(i));
                if (!v || v == PROTO_NONE) continue;  // hole
                if (i < tgtSz) tgtEls = tgtEls->setAt(ctx, static_cast<int>(i), v);
                else {
                    // grow with hole-fillers up to i, then append v.
                    while (tgtEls->getSize(ctx) < i)
                        tgtEls = tgtEls->appendLast(ctx, PROTO_NONE);
                    tgtEls = tgtEls->appendLast(ctx, v);
                }
                anyWritten = true;
            }
            if (anyWritten) protojs::setArrayElements(ctx, target, tgtEls);
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
            // \xc2\xa720.1.2.1 step 4.c iterates ALL own keys including
            // symbol-keyed entries.  isInternalKey skips internal
            // bookkeeping (\`__*__\`) AND per-instance Symbol identity
            // (\`@@sym#<addr>\`) — but the symbol-keyed entries ARE
            // user-visible and must be copied.  Allow \`@@sym#\` keys
            // through here; the regular \`__*__\` filter still applies.
            std::string keyStr;
            propKey->toUTF8String(ctx, keyStr);
            bool isSymKeyStr = keyStr.size() >= 6
                && keyStr[0]=='@' && keyStr[1]=='@'
                && keyStr[2]=='s' && keyStr[3]=='y'
                && keyStr[4]=='m' && keyStr[5]=='#';
            if (!isSymKeyStr && isInternalKey(ctx, propKey)) continue;
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
            //
            // ValidateAndApplyPropertyDescriptor step 2.a (§10.1.6.3):
            // when `current` is undefined (key does not exist on target)
            // and `extensible` is false, return false → Set(..., true)
            // raises TypeError.  Pre-fix Object.assign onto a
            // preventExtensions'd target silently added the property.
            {
                std::string tk;
                propKey->toUTF8String(ctx, tk);
                bool targetHasOwn = target->hasOwnAttribute(ctx, propKey) == PROTO_TRUE;
                if (!targetHasOwn) {
                    // Probe accessor sidecars too — accessor-only slots
                    // are own properties even without a data key.
                    std::string gkStr = std::string("__get_") + tk + "__";
                    const proto::ProtoObject* gko2 = ctx->fromUTF8String(gkStr.c_str());
                    const proto::ProtoString* gk2 = gko2 ? gko2->asString(ctx) : nullptr;
                    if (gk2 && target->hasOwnAttribute(ctx, gk2) == PROTO_TRUE)
                        targetHasOwn = true;
                }
                if (!targetHasOwn) {
                    JSContextWrapper* w2 = JSContextWrapper::current();
                    if (w2 && target->hasParent(ctx, w2->getNonExtensibleMarker())) {
                        signalNativeException(makeNativeError(ctx, "TypeError",
                            "Cannot add property to non-extensible object"));
                        return PROTO_NONE;
                    }
                }
                // §22.1.4.1 — String wrapper char-index data slot is
                // non-writable. `Object.assign("a", [1])` wraps the
                // primitive then assigns "0" which collides with the
                // existing char slot. Pre-fix the write went through
                // and silently overwrote the char (test262
                // Object/assign/assignment-to-readonly-property-of-
                // target-must-throw-a-typeerror-exception.js).
                {
                    const proto::ProtoString* pvKey =
                        JSSymbols::primitiveValue(ctx);
                    const proto::ProtoObject* pv = pvKey
                        ? target->getAttribute(ctx, pvKey, false) : nullptr;
                    if (pv && pv != PROTO_NONE && pv->isString(ctx)
                        && !tk.empty()) {
                        char* end = nullptr;
                        long long iv = std::strtoll(tk.c_str(), &end, 10);
                        if (end && *end == '\0' && iv >= 0
                            && std::to_string(iv) == tk) {
                            const proto::ProtoString* ps = pv->asString(ctx);
                            if (ps && (size_t)iv < ps->getSize(ctx)) {
                                signalNativeException(makeNativeError(ctx, "TypeError",
                                    "Cannot assign to read only property "
                                    "on a String wrapper"));
                                return PROTO_NONE;
                            }
                        }
                        if (tk == "length") {
                            signalNativeException(makeNativeError(ctx, "TypeError",
                                "Cannot assign to read only property "
                                "'length' on a String wrapper"));
                            return PROTO_NONE;
                        }
                    }
                }
                // §10.1.9.3 OrdinarySetWithOwnDescriptor: when the
                // own descriptor is an accessor, the spec dispatches
                // to the setter — IsDataDescriptor is false here, so
                // the writable check below is the wrong gate.  Probe
                // for the accessor sidecars first; if the target has
                // a __set_<key>__ slot, invoke the setter directly
                // and skip the data-slot write.  Pre-fix
                //   Object.defineProperty(t,'a',{set(v){throw X}});
                //   Object.assign(t,{a:1});
                // raised "Cannot assign to read only property" instead
                // of propagating the user-supplied exception
                // (built-ins/Object/assign/target-set-user-error and
                // target-is-{frozen,sealed,non-extensible}-existing-
                // accessor-property).
                std::string setSkStr = std::string("__set_") + tk + "__";
                const proto::ProtoObject* setSko = ctx->fromUTF8String(setSkStr.c_str());
                const proto::ProtoString* setSk = setSko ? setSko->asString(ctx) : nullptr;
                bool targetIsAccessor = false;
                if (setSk && target->hasAttribute(ctx, setSk) == PROTO_TRUE) {
                    const proto::ProtoObject* setter = target->getAttribute(ctx, setSk, true);
                    if (setter && setter != PROTO_NONE
                        && setter != getUndefinedSentinel()) {
                        targetIsAccessor = true;
                        const proto::ProtoList* setArgs = ctx->newList();
                        setArgs = setArgs->appendLast(ctx, effective ? effective : PROTO_NONE);
                        (void)callJSFunction(ctx, setter, target, setArgs);
                        if (hasCallException()) return PROTO_NONE;
                        continue; // setter handled the write; do not fall to setAttribute
                    } else {
                        // accessor with undefined setter — write into a
                        // non-writable slot per §10.1.9.3 step 3.b.
                        signalNativeException(makeNativeError(ctx, "TypeError",
                            "Cannot assign to accessor property without a setter"));
                        return PROTO_NONE;
                    }
                }
                std::string getSkStr = std::string("__get_") + tk + "__";
                const proto::ProtoObject* getSko = ctx->fromUTF8String(getSkStr.c_str());
                const proto::ProtoString* getSk = getSko ? getSko->asString(ctx) : nullptr;
                if (!targetIsAccessor && getSk && target->hasAttribute(ctx, getSk) == PROTO_TRUE) {
                    const proto::ProtoObject* getter = target->getAttribute(ctx, getSk, true);
                    if (getter && getter != PROTO_NONE
                        && getter != getUndefinedSentinel()) {
                        // Getter-only accessor: no setter installed → write fails.
                        signalNativeException(makeNativeError(ctx, "TypeError",
                            "Cannot assign to accessor property without a setter"));
                        return PROTO_NONE;
                    }
                }
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
            // \xc2\xa710.4.2 Array exotic objects: writes to canonical
            // numeric index keys must update __elements__ (the dense
            // storage Array reads from), not just the string-keyed
            // attribute layer.  Pre-fix Object.assign onto an Array
            // target with a numeric source key wrote to the sparse
            // attribute slot; subsequent arr[i] reads still saw the
            // stale __elements__ entry (built-ins/Object/assign/
            // target-Array.js: target = [7,8,9]; Object.assign(target,
            // {1:2, length:2}) was expected to truncate to [1,2] but
            // remained [7,8,9].
            {
                const proto::ProtoString* isArrK = JSSymbols::isArray(ctx);
                bool tgtIsArr = isArrK
                    && target->getAttribute(ctx, isArrK, true) == PROTO_TRUE;
                std::string ks; propKey->toUTF8String(ctx, ks);
                if (tgtIsArr && !ks.empty() && ks[0] >= '0' && ks[0] <= '9') {
                    bool numeric = true;
                    if (ks[0] == '0' && ks.size() > 1) numeric = false;
                    for (char c : ks) if (c < '0' || c > '9') { numeric = false; break; }
                    if (numeric) {
                        try {
                            unsigned long idx = std::stoul(ks);
                            if (idx < 0xFFFFFFFFu) {
                                protojs::arrayTryFastSet(ctx, target, idx,
                                    effective ? effective : PROTO_NONE);
                                continue;
                            }
                        } catch (...) {}
                    }
                }
                // Array length write: truncate __elements__ accordingly.
                if (tgtIsArr && ks == "length"
                    && effective && effective != PROTO_NONE
                    && (effective->isInteger(ctx) || effective->isDouble(ctx)
                        || effective->isFloat(ctx))) {
                    double dlen = effective->isInteger(ctx)
                        ? (double)effective->asLong(ctx)
                        : effective->asDouble(ctx);
                    long long ilen = (long long)dlen;
                    if (!std::isnan(dlen) && !std::isinf(dlen)
                        && (double)ilen == dlen
                        && ilen >= 0 && ilen <= 0xFFFFFFFFLL) {
                        const proto::ProtoList* els =
                            protojs::getArrayElements(ctx, target);
                        if (els) {
                            const proto::ProtoList* trimmed = els;
                            long long curSz = els->getSize(ctx);
                            while (trimmed->getSize(ctx) > ilen)
                                trimmed = trimmed->removeAt(ctx, trimmed->getSize(ctx) - 1);
                            while (trimmed->getSize(ctx) < ilen)
                                trimmed = trimmed->appendLast(ctx, PROTO_NONE);
                            if (trimmed != els || curSz != ilen)
                                protojs::setArrayElements(ctx, target, trimmed);
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

// Reverse map: per-instance __symbol_str_key__ string ("@@sym#<addr>")
// to the originating Symbol() value.  Populated when Symbol() runs via
// registerSymbolByStrKey; consulted by Object.getOwnPropertySymbols and
// Reflect.ownKeys to translate the internal string-keyed attribute
// back to its Symbol identity.  Definitions live at the bottom of the
// outer protojs namespace; only the forward decl is here.

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
    // §20.1.2.6 step 2: SetIntegrityLevel(O, "frozen") begins with
    // O.[[PreventExtensions]]() and aborts (TypeError) when it returns
    // false.  Forward to the Proxy preventExtensions trap before adding
    // the freeze marker.
    if (isProxy(ctx, obj)) {
        const proto::ProtoObject* handler = proxyHandler(ctx, obj);
        const proto::ProtoObject* target  = proxyTarget(ctx, obj);
        const proto::ProtoString* trapKs = nullptr;
        const proto::ProtoObject* trapKo = ctx->fromUTF8String("preventExtensions");
        if (trapKo) trapKs = trapKo->asString(ctx);
        const proto::ProtoObject* trap = (handler && trapKs)
            ? handler->getAttribute(ctx, trapKs, true) : nullptr;
        if (trap && trap != PROTO_NONE && trap != getUndefinedSentinel()
            && trap != getNullSentinel()) {
            const proto::ProtoList* a = ctx->newList();
            a = a->appendLast(ctx, target ? target : PROTO_NONE);
            const proto::ProtoObject* r = callJSFunction(ctx, trap, handler, a);
            if (hasCallException()) return PROTO_NONE;
            bool truthy = !(r == nullptr || r == PROTO_NONE || r == PROTO_FALSE
                || r == getUndefinedSentinel() || r == getNullSentinel());
            if (!truthy) {
                signalNativeException(makeNativeError(ctx, "TypeError",
                    "Object.freeze: preventExtensions trap returned falsish"));
                return PROTO_NONE;
            }
        }
    }

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
    // §10.1.6.4 TestIntegrityLevel: a non-extensible object whose own
    // properties are all non-configurable (and, for "frozen", non-
    // writable data props) is frozen.  Inherited descriptors do NOT
    // count.  A non-extensible object with NO own properties is
    // trivially frozen — pre-fix isFrozen only honoured the explicit
    // FrozenMarker parent, missing the case where Object.preventExtensions
    // had been called on an empty receiver:
    //
    //   var c = new Con();              // empty own props, inherits from proto
    //   Object.preventExtensions(c);
    //   Object.isFrozen(c)              // pre-fix: false  post-fix: true
    if (wrapper && obj->hasParent(ctx, wrapper->getNonExtensibleMarker())) {
        const proto::ProtoSparseList* own = obj->getOwnAttributes(ctx);
        bool anyOwn = false;
        bool allFrozen = true;
        if (own) {
            const proto::ProtoSparseListIterator* it = own->getIterator(ctx);
            while (it && it->hasNext(ctx)) {
                unsigned long raw = it->nextKey(ctx);
                const proto::ProtoString* k =
                    reinterpret_cast<const proto::ProtoString*>(raw);
                it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
                if (!k || isInternalKey(ctx, k)) continue;
                anyOwn = true;
                std::string ks;
                k->toUTF8String(ctx, ks);
                std::string pdStr = std::string("__pd_") + ks + "__";
                const proto::ProtoObject* pdo = ctx->fromUTF8String(pdStr.c_str());
                const proto::ProtoString* pdk = pdo ? pdo->asString(ctx) : nullptr;
                if (!pdk) { allFrozen = false; break; }
                const proto::ProtoObject* pdv = obj->getAttribute(ctx, pdk, false);
                long long bits = (pdv && pdv != PROTO_NONE && pdv->isInteger(ctx))
                    ? pdv->asLong(ctx) : 0x7LL;  // default fully open
                // bit0 writable, bit1 configurable
                if (bits & 0x1) { allFrozen = false; break; }
                if (bits & 0x2) { allFrozen = false; break; }
            }
        }
        if (!anyOwn || allFrozen) return PROTO_TRUE;
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
    // §20.1.2.20 step 2: SetIntegrityLevel(O, "sealed") calls
    // O.[[PreventExtensions]]() first and aborts with TypeError when
    // the result is false — same pattern as Object.freeze.
    if (isProxy(ctx, obj)) {
        const proto::ProtoObject* handler = proxyHandler(ctx, obj);
        const proto::ProtoObject* target  = proxyTarget(ctx, obj);
        const proto::ProtoString* trapKs = nullptr;
        const proto::ProtoObject* trapKo = ctx->fromUTF8String("preventExtensions");
        if (trapKo) trapKs = trapKo->asString(ctx);
        const proto::ProtoObject* trap = (handler && trapKs)
            ? handler->getAttribute(ctx, trapKs, true) : nullptr;
        if (trap && trap != PROTO_NONE && trap != getUndefinedSentinel()
            && trap != getNullSentinel()) {
            const proto::ProtoList* a = ctx->newList();
            a = a->appendLast(ctx, target ? target : PROTO_NONE);
            const proto::ProtoObject* r = callJSFunction(ctx, trap, handler, a);
            if (hasCallException()) return PROTO_NONE;
            bool truthy = !(r == nullptr || r == PROTO_NONE || r == PROTO_FALSE
                || r == getUndefinedSentinel() || r == getNullSentinel());
            if (!truthy) {
                signalNativeException(makeNativeError(ctx, "TypeError",
                    "Object.seal: preventExtensions trap returned falsish"));
                return PROTO_NONE;
            }
        }
    }

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

    // §20.1.2.16 step 2: call [[PreventExtensions]] and throw TypeError
    // when it returns false.  Proxy invariant: the handler may veto by
    // returning a falsy value; pre-fix Object.preventExtensions on a
    // Proxy ignored the trap result and silently flagged the proxy
    // non-extensible.
    if (isProxy(ctx, obj)) {
        const proto::ProtoObject* handler = proxyHandler(ctx, obj);
        const proto::ProtoObject* target  = proxyTarget(ctx, obj);
        // §10.5.4 step 3: a revoked proxy (no target) raises TypeError
        // before any trap dispatch.  Pre-fix the no-handler branch
        // silently no-op'd through to the addParent path on the proxy
        // cell itself, leaving Object.preventExtensions(revoked) as a
        // no-throw.
        if (!target) {
            signalNativeException(makeNativeError(ctx, "TypeError",
                "Cannot perform 'preventExtensions' on a proxy that has been revoked"));
            return PROTO_NONE;
        }
        const proto::ProtoString* trapKs = nullptr;
        const proto::ProtoObject* trapKo = ctx->fromUTF8String("preventExtensions");
        if (trapKo) trapKs = trapKo->asString(ctx);
        const proto::ProtoObject* trap = (handler && trapKs)
            ? handler->getAttribute(ctx, trapKs, true) : nullptr;
        if (trap && trap != PROTO_NONE && trap != getUndefinedSentinel()
            && trap != getNullSentinel()) {
            const proto::ProtoList* a = ctx->newList();
            a = a->appendLast(ctx, target ? target : PROTO_NONE);
            const proto::ProtoObject* r = callJSFunction(ctx, trap, handler, a);
            if (hasCallException()) return PROTO_NONE;
            bool truthy;
            {
                if (r == nullptr || r == PROTO_NONE
                    || r == PROTO_FALSE || r == getNullSentinel()
                    || r == getUndefinedSentinel()) truthy = false;
                else if (r->isBoolean(ctx)) truthy = r->asBoolean(ctx);
                else if (r->isInteger(ctx)) truthy = r->asLong(ctx) != 0;
                else if (r->isDouble(ctx)) { double d = r->asDouble(ctx); truthy = d != 0.0 && d == d; }
                else if (r->isString(ctx)) {
                    std::string s; r->asString(ctx)->toUTF8String(ctx, s);
                    truthy = !s.empty();
                } else truthy = true;
            }
            if (!truthy) {
                signalNativeException(makeNativeError(ctx, "TypeError",
                    "'preventExtensions' on proxy: trap returned falsish"));
                return PROTO_NONE;
            }
            // §10.5.4 step 8 — after a truthy trap result, the target
            // must actually be non-extensible.  The handler is
            // expected to call Object.preventExtensions(target) before
            // returning true.  Pre-fix the truthy path returned obj
            // unconditionally, missing the spec invariant (test262
            // preventExtensions/return-true-target-is-extensible.js).
            JSContextWrapper* w = JSContextWrapper::current();
            bool stillExtensible = !(target && w && w->getNonExtensibleMarker()
                && target->hasParent(ctx, w->getNonExtensibleMarker()));
            if (stillExtensible) {
                signalNativeException(makeNativeError(ctx, "TypeError",
                    "'preventExtensions' on proxy: trap returned truthy "
                    "but the target is still extensible"));
                return PROTO_NONE;
            }
            return obj;
        }
        // No trap → forward to target.[[PreventExtensions]]().
        // Recurse via objectPreventExtensions so a nested Proxy's
        // trap (or a deeper concrete target's actual extensibility
        // state) propagates correctly. A flat unwrap-loop would skip
        // intermediate traps and silently accept the call (test262
        // Proxy/preventExtensions/trap-is-missing-target-is-proxy.js).
        const proto::ProtoList* recurseArgs = ctx->newList();
        recurseArgs = recurseArgs->appendLast(ctx, target ? target : obj);
        return objectPreventExtensions(ctx, nullptr, nullptr, recurseArgs, nullptr);
    }

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

    // Proxy override per §10.5.3 [[IsExtensible]]: route through
    // handler.isExtensible(target); pre-fix the trap was never consulted
    // and the proxy's own non-extensible-marker (always absent because
    // the wrapper is fresh) decided.
    if (isProxy(ctx, obj)) {
        const proto::ProtoObject* handler = proxyHandler(ctx, obj);
        const proto::ProtoObject* target  = proxyTarget(ctx, obj);
        if (!target) {
            signalNativeException(makeNativeError(ctx, "TypeError",
                "Cannot perform 'isExtensible' on a proxy that has been revoked"));
            return PROTO_FALSE;
        }
        const proto::ProtoObject* trapKo = ctx->fromUTF8String("isExtensible");
        const proto::ProtoString* trapKs = trapKo ? trapKo->asString(ctx) : nullptr;
        const proto::ProtoObject* trap = (handler && trapKs)
            ? handler->getAttribute(ctx, trapKs, true) : nullptr;
        if (trap && trap != PROTO_NONE && trap != getUndefinedSentinel()
            && trap != getNullSentinel()) {
            const proto::ProtoList* a = ctx->newList();
            a = a->appendLast(ctx, target);
            const proto::ProtoObject* r = callJSFunction(ctx, trap, handler, a);
            if (hasCallException()) return PROTO_FALSE;
            // §10.5.3 step 5 ToBoolean(trapResult) — null/undefined/0/
            // NaN/"" must reduce to false.  Pre-fix only PROTO_FALSE +
            // sentinels were treated as false, so isExtensible trap
            // returning 0 / "" silently surfaced as true.
            bool truthy;
            {
                if (r == nullptr || r == PROTO_NONE
                    || r == PROTO_FALSE || r == getNullSentinel()
                    || r == getUndefinedSentinel()) truthy = false;
                else if (r->isBoolean(ctx)) truthy = r->asBoolean(ctx);
                else if (r->isInteger(ctx)) truthy = r->asLong(ctx) != 0;
                else if (r->isDouble(ctx)) { double d = r->asDouble(ctx); truthy = d != 0.0 && d == d; }
                else if (r->isString(ctx)) {
                    std::string s; r->asString(ctx)->toUTF8String(ctx, s);
                    truthy = !s.empty();
                } else truthy = true;
            }
            // §10.5.3 step 9: invariant — trap result must equal
            // target's actual extensibility.
            JSContextWrapper* w2 = JSContextWrapper::current();
            bool targetExt = !(w2 && target->hasParent(ctx, w2->getNonExtensibleMarker()));
            if (truthy != targetExt) {
                signalNativeException(makeNativeError(ctx, "TypeError",
                    "'isExtensible' on proxy: trap result doesn't match target's actual extensibility"));
                return PROTO_FALSE;
            }
            return truthy ? PROTO_TRUE : PROTO_FALSE;
        }
        // No trap → unwrap through every nested Proxy until we reach
        // the concrete target. Pre-fix `obj = target` was a single
        // unwrap, so a Proxy wrapping a Proxy of a non-extensible
        // RegExp reported true (test262
        // Proxy/isExtensible/trap-is-missing-target-is-proxy.js).
        int g = 16;
        obj = target;
        while (g-- > 0 && isProxy(ctx, obj)) {
            const proto::ProtoObject* n = proxyTarget(ctx, obj);
            if (!n || n == obj) break;
            obj = n;
        }
    }

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

    // Proxy override per §10.5.11 [[OwnPropertyKeys]]: route through
    // handler.ownKeys then filter to string keys only.
    if (isProxy(ctx, obj)) {
        const proto::ProtoObject* full = proxyDispatchOwnKeys(ctx, obj);
        if (hasCallException()) return PROTO_NONE;
        if (full) {
            const proto::ProtoList* els = getArrayElements(ctx, full);
            const proto::ProtoString* isSymK = JSSymbols::isSymbol(ctx);
            const proto::ProtoList* filt = ctx->newList();
            size_t n = els ? els->getSize(ctx) : 0;
            for (size_t i = 0; i < n; i++) {
                const proto::ProtoObject* k = els->getAt(ctx, i);
                if (!k || k == PROTO_NONE) continue;
                if (isSymK && k->getAttribute(ctx, isSymK, false) == PROTO_TRUE)
                    continue;
                if (!k->isString(ctx)) continue;
                filt = filt->appendLast(ctx, k);
            }
            const proto::ProtoObject* arr = createNewArray(ctx, nullptr);
            setArrayElements(ctx, arr, filt);
            const proto::ProtoString* lenK = JSSymbols::length(ctx);
            const proto::ProtoString* isArrK = JSSymbols::isArray(ctx);
            if (lenK)  arr = arr->setAttribute(ctx, lenK,  ctx->fromInteger(static_cast<long long>(filt->getSize(ctx))));
            if (isArrK) arr = arr->setAttribute(ctx, isArrK, PROTO_TRUE);
            return arr;
        }
        // No trap — fall through to default keys.
        {
            // Loop-unwrap nested Proxies until we reach a concrete
            // target — a single unwrap leaves the immediate target as
            // still a Proxy (whose own attribute layer is just sidecars).
            int _g = 16;
            while (_g-- > 0 && isProxy(ctx, obj)) {
                const proto::ProtoObject* _n = proxyTarget(ctx, obj);
                if (!_n || _n == obj) break;
                obj = _n;
            }
        }
    }

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
    // Proxy override per §10.5.1 [[GetPrototypeOf]].
    if (isProxy(ctx, obj)) {
        return proxyDispatchGetPrototypeOf(ctx, obj);
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
    // §20.1.2.21 step 1 RequireObjectCoercible(O) + step 2 type check
    // on the proto argument.  Pre-fix the impl silently no-op'd for
    // null / undefined target and for invalid proto values, instead of
    // throwing TypeError.
    if (!args || args->getSize(ctx) == 0) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Object.setPrototypeOf requires the first argument"));
        return PROTO_NONE;
    }
    const proto::ProtoObject* obj = args->getAt(ctx, 0);
    if (!obj || obj == PROTO_NONE
        || obj == getNullSentinel() || obj == getUndefinedSentinel()) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Object.setPrototypeOf called on null or undefined"));
        return PROTO_NONE;
    }
    if (args->getSize(ctx) < 2) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Object.setPrototypeOf requires a prototype argument"));
        return PROTO_NONE;
    }
    const proto::ProtoObject* proto = args->getAt(ctx, 1);
    // §20.1.2.21 step 2: proto must be Object or Null — Symbol
    // primitives (objects with __is_symbol__) are also rejected per
    // §6.1.5. Pre-fix the type check missed Symbols and silently
    // installed a Symbol as the prototype slot, breaking the
    // §10.1.2.1 invariant (test262
    // Object/setPrototypeOf/proto-not-obj.js).
    if (proto != getNullSentinel()) {
        bool protoIsSymbol = false;
        {
            const proto::ProtoString* isSymK = JSSymbols::isSymbol(ctx);
            if (isSymK && proto && proto != PROTO_NONE
                && proto->hasAttribute(ctx, isSymK) == PROTO_TRUE
                && proto->getAttribute(ctx, isSymK, true) == PROTO_TRUE)
                protoIsSymbol = true;
        }
        if (!proto || proto == PROTO_NONE || proto == getUndefinedSentinel()
            || proto->isInteger(ctx) || proto->isDouble(ctx)
            || proto->isFloat(ctx) || proto == PROTO_TRUE || proto == PROTO_FALSE
            || proto->isString(ctx) || protoIsSymbol) {
            signalNativeException(makeNativeError(ctx, "TypeError",
                "Object.setPrototypeOf: prototype must be an Object or null"));
            return PROTO_NONE;
        }
    }
    // Proxy override per §10.5.2 [[SetPrototypeOf]].
    if (isProxy(ctx, obj)) {
        const proto::ProtoObject* r = proxyDispatchSetPrototypeOf(ctx, obj, proto);
        if (hasCallException()) return PROTO_NONE;
        if (r == PROTO_TRUE) return obj;
        if (r == PROTO_FALSE) {
            signalNativeException(makeNativeError(ctx, "TypeError",
                "Object.setPrototypeOf: trap returned falsy"));
            return PROTO_NONE;
        }
        // r == nullptr → no trap, fall through onto the proxy target.
        {
            // Loop-unwrap nested Proxies until we reach a concrete
            // target — a single unwrap leaves the immediate target as
            // still a Proxy (whose own attribute layer is just sidecars).
            int _g = 16;
            while (_g-- > 0 && isProxy(ctx, obj)) {
                const proto::ProtoObject* _n = proxyTarget(ctx, obj);
                if (!_n || _n == obj) break;
                obj = _n;
            }
        }
    }
    // §19.1.3 Object.prototype's [[Prototype]] is immutable (null) per
    // SetImmutablePrototype. Any attempt to set it to anything other
    // than null must throw TypeError. Pre-fix Object.setPrototypeOf
    // accepted a non-null prototype on Object.prototype itself
    // (test262 Object/prototype/setPrototypeOf-with-non-circular-
    // values.js, -__proto__.js).
    {
        JSContextWrapper* w = JSContextWrapper::current();
        if (w && obj == w->getJSObjectPrototype()
            && proto != getNullSentinel()) {
            signalNativeException(makeNativeError(ctx, "TypeError",
                "Object.prototype has an immutable [[Prototype]]"));
            return PROTO_NONE;
        }
    }
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
        // §10.1.2.1 OrdinarySetPrototypeOf step 7: walk proto's chain;
        // if `obj` appears in it, the assignment would create a cycle
        // → throw TypeError per step 8.  Pre-fix the impl silently
        // accepted any prototype, so Object.setPrototypeOf(
        // Object.prototype, Array.prototype) — which cycles because
        // Array.prototype's [[Prototype]] is Object.prototype —
        // silently succeeded.
        for (const proto::ProtoObject* p = proto, *hop = nullptr;
             p && p != PROTO_NONE && p != getNullSentinel(); ) {
            if (p == obj) {
                signalNativeException(makeNativeError(ctx, "TypeError",
                    "Object.setPrototypeOf: cyclic prototype chain"));
                return PROTO_NONE;
            }
            // Walk: jsProtoMap override first, then protoCore parent.
            auto it = t_jsProtoMap.find(p);
            hop = (it != t_jsProtoMap.end()) ? it->second : p->getPrototype(ctx);
            if (!hop || hop == p) break;  // stop at fixed point
            p = hop;
        }
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

    // Symbol primitive short-circuit: each Symbol() value stashes a
    // per-instance \`__symbol_str_key__\` ProtoString at construction
    // (ProtoInterpreter.cpp's Symbol() ctor).  Returning that key
    // identity here means \`obj[sym] = X\` (which routes through
    // OP_put_array_el's ensureInternedOOP), \`Object.hasOwn(obj, sym)\`,
    // and every other ToPropertyKey consumer agree on the SAME
    // ProtoString*, so attribute storage by identity round-trips.
    // Pre-fix the Symbol value was ToString-coerced to "Symbol(<desc>)"
    // via toString, which produced a different ProtoString* than the
    // put path's stashed key — hasOwn returned false for an apparently
    // present symbol-keyed property (built-ins/Object/hasOwn/
    // symbol_own_property.js).
    {
        const proto::ProtoString* isSymK = JSSymbols::isSymbol(ctx);
        if (current && isSymK
            && current->getAttribute(ctx, isSymK, true) == PROTO_TRUE) {
            const proto::ProtoObject* ssko = ctx->fromUTF8String("__symbol_str_key__");
            const proto::ProtoString* sskK = ssko ? ssko->asString(ctx) : nullptr;
            if (sskK) {
                const proto::ProtoObject* keyStr =
                    current->getAttribute(ctx, sskK, true);
                if (keyStr && keyStr != PROTO_NONE && keyStr->isString(ctx))
                    return keyStr->asString(ctx);
            }
        }
    }

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

        // 0. Try Symbol.toPrimitive(hint="string") first per §7.1.1 step
        // 2.c — exoticToPrim, when present, takes precedence over
        // toString / valueOf.  Pre-fix coercePropNameToKey skipped the
        // exotic primitive method, so a key with `[Symbol.toPrimitive]`
        // routed through toString = "[object Object]" instead of the
        // user-supplied primitive (built-ins/Object/fromEntries/
        // to-property-key and the wider {defineProperty/computed-key}
        // family).
        {
            const proto::ProtoObject* tpKo = ctx->fromUTF8String("Symbol.toPrimitive");
            const proto::ProtoString* tpKey = tpKo ? tpKo->asString(ctx) : nullptr;
            if (tpKey) {
                const proto::ProtoObject* tpFn = current->getAttribute(ctx, tpKey, true);
                if (isCallable(tpFn)) {
                    const proto::ProtoList* hintArgs = ctx->newList();
                    hintArgs = hintArgs->appendLast(ctx, ctx->fromUTF8String("string"));
                    const proto::ProtoObject* res = callJSFunction(ctx, tpFn, current, hintArgs);
                    if (hasCallException()) return nullptr;
                    bool isSym = false;
                    if (res && res != PROTO_NONE) {
                        const proto::ProtoString* isSK = JSSymbols::isSymbol(ctx);
                        if (isSK && res->hasAttribute(ctx, isSK) == PROTO_TRUE
                            && res->getAttribute(ctx, isSK, false) == PROTO_TRUE) {
                            isSym = true;
                        }
                    }
                    bool isPrim = res
                        && (res->isString(ctx) || res->isInteger(ctx)
                            || res->isDouble(ctx) || res->isFloat(ctx)
                            || res->isBoolean(ctx)
                            || res == getNullSentinel() || res == getUndefinedSentinel()
                            || isSym);
                    if (res && !isPrim) {
                        signalNativeException(makeNativeError(ctx, "TypeError",
                            "Symbol.toPrimitive returned a non-primitive"));
                        return nullptr;
                    }
                    if (isPrim) {
                        // For Symbol primitives, return the per-instance
                        // __symbol_str_key__ ProtoString — same identity
                        // the put / hasOwn paths use.  Bypass toString /
                        // valueOf / number stringification fallthrough.
                        if (isSym) {
                            const proto::ProtoObject* ssko = ctx->fromUTF8String("__symbol_str_key__");
                            const proto::ProtoString* sskK = ssko ? ssko->asString(ctx) : nullptr;
                            if (sskK) {
                                const proto::ProtoObject* keyStr = res->getAttribute(ctx, sskK, true);
                                if (keyStr && keyStr != PROTO_NONE && keyStr->isString(ctx))
                                    return keyStr->asString(ctx);
                            }
                            return res->asString(ctx);
                        }
                        prim = res;
                    }
                }
            }
        }

        // 1. Try toString().  Probe the __get_toString__ accessor
        // sidecar first so a `get toString() { throw }` propagates per
        // §7.1.1 (test262 topropertykey_before_toobject).
        const proto::ProtoString* tsKey = JSSymbols::toString(ctx);
        auto resolveCoercer = [&](const proto::ProtoString* nameK, const char* getKey) -> const proto::ProtoObject* {
            if (!nameK) return nullptr;
            const proto::ProtoObject* gkO = ctx->fromUTF8String(getKey);
            const proto::ProtoString* gk = gkO ? gkO->asString(ctx) : nullptr;
            if (gk) {
                const proto::ProtoObject* getter = current->getAttribute(ctx, gk, true);
                if (getter && getter != PROTO_NONE) {
                    return callJSFunction(ctx, getter, current, ctx->newList());
                }
            }
            return current->getAttribute(ctx, nameK, true);
        };
        const proto::ProtoObject* tsFn = !prim ? resolveCoercer(tsKey, "__get_toString__") : nullptr;
        if (hasCallException()) return nullptr;
        if (!prim && isCallable(tsFn)) {
            const proto::ProtoObject* res = callJSFunction(ctx, tsFn, current, ctx->newList());
            if (!hasCallException() && res) {
                // ECMA-262 §7.1.1.1 OrdinaryToPrimitive returns the first
                // result that is not an Object — Symbols qualify per
                // §6.1.5 (primitive type).  Pre-fix the Symbol arm was
                // missing here, so a wrapper whose toString() returned
                // a Symbol fell through to valueOf().  built-ins/Object
                // /hasOwn/symbol_property_toString pins the case: its
                // valueOf throws a Test262Error to assert it is never
                // called.  Recognise the Symbol marker BEFORE the
                // primitive check and short-circuit to return the
                // symbol-tagged ProtoString directly — mirroring the
                // Symbol.toPrimitive path above.
                const proto::ProtoString* isSK = JSSymbols::isSymbol(ctx);
                if (isSK && res->hasAttribute(ctx, isSK) == PROTO_TRUE
                    && res->getAttribute(ctx, isSK, false) == PROTO_TRUE) {
                    // Route through the per-instance __symbol_str_key__
                    // (same identity OP_put_array_el / hasOwn use), so
                    // a wrapper whose toString / valueOf returns a
                    // Symbol resolves to the same attribute storage
                    // key as the put path.
                    const proto::ProtoObject* ssko = ctx->fromUTF8String("__symbol_str_key__");
                    const proto::ProtoString* sskK = ssko ? ssko->asString(ctx) : nullptr;
                    if (sskK) {
                        const proto::ProtoObject* keyStr = res->getAttribute(ctx, sskK, true);
                        if (keyStr && keyStr != PROTO_NONE && keyStr->isString(ctx))
                            return keyStr->asString(ctx);
                    }
                    return res->asString(ctx);
                }
                if (res->isString(ctx) || res->isInteger(ctx) || res->isDouble(ctx) || res->isFloat(ctx) || res->isBoolean(ctx) || res == getNullSentinel() || res == getUndefinedSentinel()) {
                    prim = res;
                }
            }
        }

        // 2. Try valueOf()
        if (!prim && !hasCallException()) {
            const proto::ProtoString* voKey = ctx->fromUTF8String("valueOf")->asString(ctx);
            const proto::ProtoObject* voFn = resolveCoercer(voKey, "__get_valueOf__");
            if (hasCallException()) return nullptr;
            if (isCallable(voFn)) {
                const proto::ProtoObject* res = callJSFunction(ctx, voFn, current, ctx->newList());
                if (!hasCallException() && res) {
                    // Same Symbol short-circuit as the toString arm —
                    // a Symbol returned from valueOf is a primitive
                    // and IS the property key.  Route through
                    // __symbol_str_key__ for identity-correct lookup.
                    const proto::ProtoString* isSK = JSSymbols::isSymbol(ctx);
                    if (isSK && res->hasAttribute(ctx, isSK) == PROTO_TRUE
                        && res->getAttribute(ctx, isSK, false) == PROTO_TRUE) {
                        const proto::ProtoObject* ssko = ctx->fromUTF8String("__symbol_str_key__");
                        const proto::ProtoString* sskK = ssko ? ssko->asString(ctx) : nullptr;
                        if (sskK) {
                            const proto::ProtoObject* keyStr = res->getAttribute(ctx, sskK, true);
                            if (keyStr && keyStr != PROTO_NONE && keyStr->isString(ctx))
                                return keyStr->asString(ctx);
                        }
                        return res->asString(ctx);
                    }
                    if (res->isString(ctx) || res->isInteger(ctx) || res->isDouble(ctx) || res->isFloat(ctx) || res->isBoolean(ctx) || res == getNullSentinel() || res == getUndefinedSentinel()) {
                        prim = res;
                    }
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
        // §7.1.13 ToPropertyDescriptor rejects BigInt primitives the
        // same way Symbols are rejected — \xc2\xa76.1.5 lists both among the
        // seven primitive types.  Pre-fix the BigInt marker wasn't
        // probed and \`Object.defineProperty({}, 'a', 0n)\` /
        // \`Object.defineProperties({}, {a: 0n})\` reached the descriptor
        // walk on a primitive, where the absent fields fell through to
        // their defaults rather than the spec-mandated TypeError abrupt.
        // built-ins/Object/defineProperties/property-description-must-
        // be-an-object-not-bigint.js (and the matching defineProperty
        // sibling) pin the exact shape.
        const proto::ProtoString* bigK = ctx->fromUTF8String("__is_bigint__")
            ? ctx->fromUTF8String("__is_bigint__")->asString(ctx) : nullptr;
        if (bigK && desc->getAttribute(ctx, bigK, true) == PROTO_TRUE) {
            signalNativeException(makeNativeError(ctx, "TypeError",
                "Property description must be an object"));
            return PROTO_NONE;
        }
    }

    // Proxy override per §10.5.6 [[DefineOwnProperty]]: route through
    // handler.defineProperty(target, P, Desc).  We hand the user
    // descriptor object through verbatim — the trap is expected to
    // interpret it via the spec ToPropertyDescriptor at the receiver's
    // discretion.  Return target on success per Object.defineProperty
    // §20.1.2.4 step 4 (return O).
    if (isProxy(ctx, target)) {
        const proto::ProtoObject* r =
            proxyDispatchDefineProperty(ctx, target, k, desc);
        if (hasCallException()) return PROTO_NONE;
        if (r == nullptr) {
            // No trap → unwrap through nested Proxies until we reach
            // the concrete target so default-path mutations (Array
            // index synthesis, __elements__ updates, etc.) land on
            // the actual cell. A single unwrap left the immediate
            // target as still a Proxy (test262
            // Proxy/defineProperty/trap-is-undefined-target-is-
            // proxy.js — Object.defineProperty(Proxy(Proxy([], {}),
            // {dP: undef}), "0", {value:1}) must materialise array[0]).
            int g = 16;
            while (g-- > 0 && isProxy(ctx, target)) {
                const proto::ProtoObject* n = proxyTarget(ctx, target);
                if (!n || n == target) break;
                target = n;
            }
        } else if (r == PROTO_FALSE) {
            signalNativeException(makeNativeError(ctx, "TypeError",
                "Object.defineProperty: trap returned falsy"));
            return PROTO_NONE;
        } else {
            return args->getAt(ctx, 0);  // return the original proxy
        }
    }

    bool propExists = (target->hasOwnAttribute(ctx, k) == PROTO_TRUE);
    const proto::ProtoObject* existingVal = propExists ? target->getAttribute(ctx, k, false) : nullptr;
    std::string kstr;
    k->toUTF8String(ctx, kstr);
    // §10.4.2.4 step 4.b — eagerly reject any indexed-property define
    // that would extend an Array whose length is non-writable. Pre-fix
    // the per-property mutations (sparse own attr, __elements__ mirror)
    // ran first and the auto-bump check at the function's end saw the
    // already-extended length, so the throw never fired (test262
    // Object/defineProperties/15.2.3.7-6-a-184.js, 185.js).
    if (!kstr.empty()) {
        char* ie = nullptr;
        long long iv0 = std::strtoll(kstr.c_str(), &ie, 10);
        if (ie && *ie == '\0' && iv0 >= 0 && iv0 < 4294967295LL
            && std::to_string(iv0) == kstr) {
            const proto::ProtoString* isAK = JSSymbols::isArray(ctx);
            const proto::ProtoObject* isAV = isAK
                ? target->getAttribute(ctx, isAK, true) : nullptr;
            if (isAV == PROTO_TRUE) {
                const proto::ProtoString* lK = JSSymbols::length(ctx);
                const proto::ProtoObject* lV = lK
                    ? target->getAttribute(ctx, lK, false) : nullptr;
                long long cur = (lV && lV->isInteger(ctx))
                    ? lV->asLong(ctx) : 0;
                if (iv0 + 1 > cur) {
                    const proto::ProtoObject* pdLko =
                        ctx->fromUTF8String("__pd_length__");
                    const proto::ProtoString* pdLk =
                        pdLko ? pdLko->asString(ctx) : nullptr;
                    const proto::ProtoObject* pdlV = pdLk
                        ? target->getAttribute(ctx, pdLk, false) : nullptr;
                    if (pdlV && pdlV->isInteger(ctx)
                        && !(pdlV->asLong(ctx) & 0x1)) {
                        signalNativeException(makeNativeError(ctx, "TypeError",
                            "Cannot extend Array.length when length is "
                            "non-writable"));
                        return PROTO_NONE;
                    }
                }
            }
        }
    }
    // §10.1.6.3 step 2 — define-on-non-extensible with a new property
    // raises TypeError. Pre-fix the path silently extended frozen /
    // sealed / preventExtensions'd objects via defineProperty
    // (test262 Object/preventExtensions/symbol-object-contains-
    // symbol-properties-strict.js: Object.defineProperty(obj, symC, {…})
    // after preventExtensions must throw).
    if (!propExists) {
        JSContextWrapper* wNE = JSContextWrapper::current();
        if (wNE && wNE->getNonExtensibleMarker()
            && target->hasParent(ctx, wNE->getNonExtensibleMarker())) {
            // Array index-extension also counts; we don't carve that out.
            signalNativeException(makeNativeError(ctx, "TypeError",
                "Cannot define property on non-extensible object"));
            return PROTO_NONE;
        }
    }
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
                // Substitute the coerced uint32 back into the
                // descriptor so the downstream redefine paths see the
                // ToUint32 form rather than the raw user value. Pre-fix
                // `Object.defineProperty(arr, "length", {value: null})`
                // stored `null` literally in the length slot and
                // `arr.length` then read back as null instead of 0
                // (test262 Object/defineProperty/15.2.3.6-4-{126,127,
                // 128,131,…}.js).
                desc = desc->setAttribute(ctx, valK,
                    ctx->fromInteger((long long)d));
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
            // §10.1.6.3 step 4.h: the descriptor-bit fields are subject
            // to the same SameValue check as the accessor functions on
            // a non-configurable slot. Pre-fix the accessor branch only
            // compared get / set, so
            //   { get, enumerable:false, configurable:false }
            //   → { get, enumerable:true }   // flip is permitted
            // silently succeeded; the spec demands a TypeError because
            // enumerable changed without configurable allowing it.
            if (ok && hasEnum && descBoolField("enumerable", false) != curEnumerable) ok = false;
            if (ok && hasConf && descBoolField("configurable", false) != curConfigurable) ok = false;
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
            // Array length truncation via Object.defineProperty path.
            // §10.4.2.4 ArraySetLength step 16: when shrinking, walk
            // i = oldLen-1 down to newLen and refuse if any element is
            // non-configurable; otherwise also trim __elements__ so
            // iteration / hasOwn / length round-trips agree.
            // Pre-fix this early-return path skipped both the
            // invariant and the truncation (the later "kstr == length"
            // block is unreachable on a value-only redefine of
            // a writable-but-not-configurable property).
            if (hasValue && kstr == "length") {
                const proto::ProtoString* isArrKArr = JSSymbols::isArray(ctx);
                const proto::ProtoObject* isArrVArr = isArrKArr
                    ? target->getAttribute(ctx, isArrKArr, true) : nullptr;
                if (isArrVArr == PROTO_TRUE) {
                    const proto::ProtoObject* newValRaw = descKeyValue("value");
                    long long newLenArr = -1;
                    if (newValRaw && newValRaw->isInteger(ctx))
                        newLenArr = newValRaw->asLong(ctx);
                    else if (newValRaw && (newValRaw->isDouble(ctx)
                        || newValRaw->isFloat(ctx))) {
                        double d = newValRaw->asDouble(ctx);
                        if (!std::isnan(d) && !std::isinf(d)
                            && d == std::trunc(d) && d >= 0)
                            newLenArr = (long long)d;
                    }
                    long long oldLenArr = -1;
                    if (existingVal && existingVal->isInteger(ctx))
                        oldLenArr = existingVal->asLong(ctx);
                    if (oldLenArr < 0) {
                        const proto::ProtoList* els =
                            protojs::getArrayElements(ctx, target);
                        oldLenArr = els ? (long long)els->getSize(ctx) : 0;
                    }
                    if (newLenArr >= 0 && oldLenArr > newLenArr) {
                        // §10.4.2.4 step 16.b: walk i = oldLen-1 down
                        // to newLen, deleting each index. On the FIRST
                        // failure (non-configurable own descriptor),
                        // stop, set length to i+1, then throw TypeError.
                        // Pre-fix the truncation pre-checked everything
                        // first; tests expect partial progress (test262
                        // Object/defineProperty/15.2.3.6-4-168.js,
                        // 169.js).
                        long long stopAt = newLenArr;
                        bool failed = false;
                        for (long long i = oldLenArr - 1; i >= newLenArr; --i) {
                            std::string pdks = "__pd_" + std::to_string(i) + "__";
                            const proto::ProtoObject* pdko =
                                ctx->fromUTF8String(pdks.c_str());
                            const proto::ProtoString* pdkks =
                                pdko ? pdko->asString(ctx) : nullptr;
                            if (pdkks
                                && target->hasOwnAttribute(ctx, pdkks) == PROTO_TRUE) {
                                const proto::ProtoObject* pdv =
                                    target->getAttribute(ctx, pdkks, false);
                                if (pdv && pdv != PROTO_NONE && pdv->isInteger(ctx)) {
                                    uint8_t pdbits = (uint8_t)pdv->asLong(ctx);
                                    if (!(pdbits & 0x2)) {
                                        stopAt = i + 1;
                                        failed = true;
                                        break;
                                    }
                                }
                            }
                            // Delete this index: drop from __elements__
                            // tail (if present) and drop sparse own attr.
                            const proto::ProtoList* els0 =
                                protojs::getArrayElements(ctx, target);
                            if (els0 && i < (long long)els0->getSize(ctx)
                                && i == (long long)els0->getSize(ctx) - 1) {
                                const proto::ProtoList* trimmed = ctx->newList();
                                for (long long j = 0; j < i; ++j)
                                    trimmed = trimmed->appendLast(ctx,
                                        els0->getAt(ctx, (int)j));
                                protojs::setArrayElements(ctx, target, trimmed);
                            }
                            // Drop sparse own-attribute index entry for i.
                            std::string istr = std::to_string(i);
                            const proto::ProtoObject* ko =
                                ctx->fromUTF8String(istr.c_str());
                            const proto::ProtoString* kk =
                                ko ? ko->asString(ctx) : nullptr;
                            if (kk
                                && target->hasOwnAttribute(ctx, kk) == PROTO_TRUE) {
                                target = target->removeAttribute(ctx, kk);
                            }
                        }
                        if (failed) {
                            // Store the partial-progress length first.
                            target = target->setAttribute(ctx, k,
                                ctx->fromInteger(stopAt));
                            // §10.4.2.4 step 16.j — when the redefine
                            // also requests writable:false, the bit flip
                            // takes effect EVEN when the truncation
                            // failed mid-way (test262
                            // Object/defineProperties/15.2.3.7-6-a-164.js).
                            if (hasWritable && !descBoolField("writable", true)) {
                                long long flipped = existingBits & ~0x1LL;
                                if (pdk) target = target->setAttribute(ctx, pdk,
                                    ctx->fromInteger(flipped));
                            }
                            signalNativeException(makeNativeError(ctx, "TypeError",
                                "Cannot redefine Array.length below a "
                                "non-configurable indexed element"));
                            return PROTO_NONE;
                        }
                    }
                }
            }
            // Apply the in-place updates (value change + bit toggles).
            if (hasValue) {
                const proto::ProtoObject* newVal = descKeyValue("value");
                if (newVal && newVal != PROTO_NONE) {
                    // For Array exotic indices, update __elements__ in
                    // addition to the (sparse) own attribute. Pre-fix
                    // setAttribute alone stored under the "0" key but
                    // arr[0] still read from the stale __elements__
                    // entry (test262 Object/defineProperties/
                    // 15.2.3.7-6-a-178.js, 200.js, 204.js).
                    const proto::ProtoString* isArrK2 = JSSymbols::isArray(ctx);
                    const proto::ProtoObject* isArrV2 = isArrK2
                        ? target->getAttribute(ctx, isArrK2, true) : nullptr;
                    if (isArrV2 == PROTO_TRUE && !kstr.empty()) {
                        char* iend = nullptr;
                        long long iv2 = std::strtoll(kstr.c_str(), &iend, 10);
                        if (iend && *iend == '\0' && iv2 >= 0
                            && std::to_string(iv2) == kstr) {
                            const proto::ProtoList* els =
                                protojs::getArrayElements(ctx, target);
                            if (els && iv2 < (long long)els->getSize(ctx)) {
                                const proto::ProtoList* updated =
                                    els->setAt(ctx, (size_t)iv2, newVal);
                                if (updated)
                                    protojs::setArrayElements(ctx, target, updated);
                            }
                        }
                    }
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
        // §6.2.5.5 ToPropertyDescriptor uses HasProperty / Get with
        // chain lookup (spec step 3.a probes Desc.[[GetOwnProperty]]
        // but the surrounding caller uses [[GetV]] which walks the
        // prototype chain — fixtures like 15.2.3.6-3-144-1 verify
        // that Math doesn't have own 'value' yet Object.defineProperty
        // (obj, 'p', Math) still picks up the inherited Object.prototype
        // .value).  Pre-fix `getAttribute(..., false)` was own-only.
        if (desc->getAttribute(ctx, k2, true) != PROTO_NONE) return true;
        
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
        // §10.1.6.3 ValidateAndApplyPropertyDescriptor step 4: when
        // redefining an accessor over an existing accessor, any field
        // NOT present in the new descriptor must be left untouched.
        // Pre-fix this branch always wrote both __get_<key>__ and
        // __set_<key>__ from the descriptor, even when the field was
        // absent — converting `{get, set}` into `{get, set: undefined}`
        // on a getter-only re-define. Built-ins/Object/defineProperty/
        // 15.2.3.6-4-{107,109,112} caught this.
        std::string gkStr = "__get_" + kstr + "__";
        const proto::ProtoString* gk = ctx->fromUTF8String(gkStr.c_str())->asString(ctx);
        if (gk && hasGet) {
            // §6.2.5.1 ToPropertyDescriptor — when "get" is present in
            // the descriptor (even as undefined), the result is an
            // accessor descriptor with [[Get]] = the value. Pre-fix we
            // stored nullptr (= removeAttribute) when getter was
            // undefined, so gOPD's hasOwnAttribute(__get_<key>__) probe
            // reported false and reconstructed the descriptor as a data
            // slot {value: undefined} — test262
            // Object/defineProperty/15.2.3.6-4-430.js et al.
            const proto::ProtoObject* gVal = getter ? getter : getUndefinedSentinel();
            target = target->setAttribute(ctx, gk, gVal);
        }
        std::string skStr = "__set_" + kstr + "__";
        const proto::ProtoString* sk = ctx->fromUTF8String(skStr.c_str())->asString(ctx);
        if (sk && hasSet) {
            const proto::ProtoObject* sVal = setter ? setter : getUndefinedSentinel();
            target = target->setAttribute(ctx, sk, sVal);
            // Hot-path hint: when the setter is installed at a numeric
            // array-index key, tag the target with __has_indexed_setters__
            // so arrSet (per-element loop in arrayPush etc.) can skip the
            // expensive __set_<idx>__ probe on every element when no
            // accessor descriptor is present on the prototype chain.
            // Validate via round-trip: only counts as an "indexed" key if
            // ToUint32(kstr) == kstr exactly (canonical integer string).
            if (sVal && sVal != getUndefinedSentinel() && !kstr.empty()) {
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
            // For Array exotic indices, update __elements__ alongside
            // the sparse attribute so arr[i] reflects the new value.
            // Pre-fix Object.defineProperty(arr, "0", {value: X}) only
            // wrote the own attribute and arr[0] still read the stale
            // __elements__ entry (test262
            // Object/defineProperties/15.2.3.7-6-a-178.js, 200.js, 204.js).
            {
                const proto::ProtoString* isArrK3 = JSSymbols::isArray(ctx);
                const proto::ProtoObject* isArrV3 = isArrK3
                    ? target->getAttribute(ctx, isArrK3, true) : nullptr;
                if (isArrV3 == PROTO_TRUE && !kstr.empty()) {
                    char* iend = nullptr;
                    long long iv3 = std::strtoll(kstr.c_str(), &iend, 10);
                    if (iend && *iend == '\0' && iv3 >= 0
                        && std::to_string(iv3) == kstr) {
                        const proto::ProtoList* els =
                            protojs::getArrayElements(ctx, target);
                        if (els && iv3 < (long long)els->getSize(ctx)) {
                            const proto::ProtoList* updated =
                                els->setAt(ctx, (size_t)iv3, storedVal);
                            if (updated)
                                protojs::setArrayElements(ctx, target, updated);
                        } else if (els
                            && iv3 < 0xFFFFFFFELL
                            && iv3 - (long long)els->getSize(ctx)
                                <= (long long)protojs::kSparseFallbackThreshold) {
                            // Extension within the auto-bump path below
                            // — append PROTO_NONE up to iv3 then the value.
                            // §22.1.5.1 valid array indices are [0,2^32-2];
                            // anything beyond is a string-keyed own
                            // property only (no __elements__ mirror, no
                            // length bump). Also cap the dense pad-and-
                            // append to kSparseFallbackThreshold so
                            // defineProperty(arr, 10_000_000, …) doesn't
                            // OOM the process trying to materialise
                            // 10 M hole slots (regression introduced in
                            // R47's value-mirror commit; surfaced by
                            // test262 Object/defineProperty/15.2.3.6-4-186.js
                            // calling defineProperty(arr, 4294967297, …)).
                            const proto::ProtoList* grown = els;
                            for (long long i = (long long)els->getSize(ctx); i < iv3; ++i)
                                grown = grown->appendLast(ctx, PROTO_NONE);
                            grown = grown->appendLast(ctx, storedVal);
                            protojs::setArrayElements(ctx, target, grown);
                        }
                    }
                }
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
                // §10.4.2.4 ArraySetLength step 16 — when shrinking,
                // walk from oldLen-1 down to newLen and refuse if any
                // element in the deletion range is non-configurable
                // (its __pd_<i>__ sidecar reports bit 0x2 clear).
                // Pre-fix the truncation went through unconditionally
                // and silently dropped non-configurable own indices
                // (test262 Object/defineProperty/15.2.3.6-4-116.js,
                // 117.js, …).
                const proto::ProtoList* curElsPre =
                    protojs::getArrayElements(ctx, target);
                long long oldLen = curElsPre
                    ? (long long)curElsPre->getSize(ctx) : 0;
                {
                    const proto::ProtoString* lk = JSSymbols::length(ctx);
                    const proto::ProtoObject* lv = lk
                        ? target->getAttribute(ctx, lk, false) : nullptr;
                    if (lv && lv->isInteger(ctx)) {
                        long long stored = lv->asLong(ctx);
                        if (stored > oldLen) oldLen = stored;
                    }
                }
                for (long long i = oldLen - 1; i >= newLen; --i) {
                    std::string pdks = "__pd_" + std::to_string(i) + "__";
                    const proto::ProtoObject* pdko =
                        ctx->fromUTF8String(pdks.c_str());
                    const proto::ProtoString* pdkks =
                        pdko ? pdko->asString(ctx) : nullptr;
                    if (!pdkks
                        || target->hasOwnAttribute(ctx, pdkks) != PROTO_TRUE)
                        continue;
                    const proto::ProtoObject* pdv =
                        target->getAttribute(ctx, pdkks, false);
                    if (!pdv || pdv == PROTO_NONE || !pdv->isInteger(ctx))
                        continue;
                    uint8_t bits = (uint8_t)pdv->asLong(ctx);
                    if (!(bits & 0x2)) {
                        // Non-configurable element blocks length shrink.
                        signalNativeException(makeNativeError(ctx, "TypeError",
                            "Cannot redefine non-configurable Array.length "
                            "below an index with a non-configurable element"));
                        return PROTO_NONE;
                    }
                }
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
                // Walk own attributes once and delete any whose key
                // parses as a uint32 ≥ newLen.  Pre-fix the loop only
                // probed newLen..newLen+1000 with a 8-miss bail-out, so
                // sparse high-index entries (e.g. arr[4294967294]=v
                // before arr.length=2) were left intact and arr[high]
                // still resolved after the truncation.
                const proto::ProtoSparseList* own = target->getOwnAttributes(ctx);
                if (own) {
                    std::vector<std::string> keysToDrop;
                    const proto::ProtoSparseListIterator* it = own->getIterator(ctx);
                    while (it && it->hasNext(ctx)) {
                        unsigned long rawKey = it->nextKey(ctx);
                        (void)it->nextValue(ctx);
                        it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
                        const proto::ProtoString* propKey =
                            reinterpret_cast<const proto::ProtoString*>(rawKey);
                        if (!propKey) continue;
                        std::string ks;
                        propKey->toUTF8String(ctx, ks);
                        if (ks.empty()) continue;
                        bool allDigits = true;
                        for (char c : ks) if (c < '0' || c > '9') { allDigits = false; break; }
                        if (!allDigits) continue;
                        if (ks.size() > 1 && ks[0] == '0') continue;
                        try {
                            unsigned long long idx = std::stoull(ks);
                            if (idx >= static_cast<unsigned long long>(newLen)) {
                                keysToDrop.push_back(ks);
                            }
                        } catch (...) {}
                    }
                    for (const auto& ks : keysToDrop) {
                        const proto::ProtoObject* ko = ctx->fromUTF8String(ks.c_str());
                        const proto::ProtoString* k = ko ? ko->asString(ctx) : nullptr;
                        if (k) target = target->removeAttribute(ctx, k);
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
                if (iv + 1 > curLen) {
                    const proto::ProtoObject* pdLko =
                        ctx->fromUTF8String("__pd_length__");
                    const proto::ProtoString* pdLk =
                        pdLko ? pdLko->asString(ctx) : nullptr;
                    const proto::ProtoObject* pdlV = pdLk
                        ? target->getAttribute(ctx, pdLk, false) : nullptr;
                    if (pdlV && pdlV->isInteger(ctx)
                        && !(pdlV->asLong(ctx) & 0x1)) {
                        signalNativeException(makeNativeError(ctx, "TypeError",
                            "Cannot extend Array.length when length is "
                            "non-writable"));
                        return PROTO_NONE;
                    }
                    target = target->setAttribute(ctx, lenK,
                        ctx->fromInteger(iv + 1));
                }
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

    // Proxy override per §10.5.5 [[GetOwnProperty]]: route through the
    // handler.getOwnPropertyDescriptor trap, with the spec's
    // FromPropertyDescriptor normalisation done inside the dispatch.
    if (isProxy(ctx, target)) {
        return proxyDispatchGetOwnPropertyDescriptor(ctx, target, k);
    }

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
    // Proxy override per §10.5.11: route through handler.ownKeys and
    // keep only Symbol-tagged entries.
    if (isProxy(ctx, target)) {
        const proto::ProtoObject* full = proxyDispatchOwnKeys(ctx, target);
        if (hasCallException()) return PROTO_NONE;
        if (full) {
            const proto::ProtoList* els = getArrayElements(ctx, full);
            const proto::ProtoString* isSymK = JSSymbols::isSymbol(ctx);
            const proto::ProtoList* filt = ctx->newList();
            size_t n = els ? els->getSize(ctx) : 0;
            for (size_t i = 0; i < n; i++) {
                const proto::ProtoObject* k = els->getAt(ctx, i);
                if (!k || k == PROTO_NONE) continue;
                bool isSym = isSymK && k->getAttribute(ctx, isSymK, false) == PROTO_TRUE;
                if (isSym) filt = filt->appendLast(ctx, k);
            }
            const proto::ProtoObject* arr = createNewArray(ctx, nullptr);
            setArrayElements(ctx, arr, filt);
            const proto::ProtoString* lenK = JSSymbols::length(ctx);
            const proto::ProtoString* isArrK2 = JSSymbols::isArray(ctx);
            if (lenK) arr = arr->setAttribute(ctx, lenK, ctx->fromInteger(static_cast<long long>(filt->getSize(ctx))));
            if (isArrK2) arr = arr->setAttribute(ctx, isArrK2, PROTO_TRUE);
            return arr;
        }
        // No trap → forward onto resolved target's own-symbol collection.
        const proto::ProtoObject* unwrapped = proxyTarget(ctx, target);
        if (unwrapped) target = unwrapped;
    }
    // Walk target's own attributes and emit Symbol-keyed entries.  Two
    // shapes apply:
    //   (1) per-instance \`@@sym#<addr>\` string keys installed by the
    //       R50 Symbol-keys feature — look up the originating Symbol
    //       via the protojs::lookupSymbolByStrKey registry.
    //   (2) legacy keys whose own __is_symbol__ marker indicates they
    //       ARE the Symbol object (pre-R50 callers and ad-hoc storage
    //       sites that may still go through that path).
    const proto::ProtoString* isSymK = JSSymbols::isSymbol(ctx);
    const proto::ProtoList* outEls = ctx->newList();
    const proto::ProtoSparseList* own = target->getOwnAttributes(ctx);
    const proto::ProtoSparseListIterator* it = own ? own->getIterator(ctx) : nullptr;
    while (it && it->hasNext(ctx)) {
        unsigned long rawKey = it->nextKey(ctx);
        it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
        const proto::ProtoString* keyStr = reinterpret_cast<const proto::ProtoString*>(rawKey);
        if (!keyStr) continue;
        std::string ks; keyStr->toUTF8String(ctx, ks);
        if (ks.size() >= 6 && ks[0]=='@' && ks[1]=='@'
            && ks[2]=='s' && ks[3]=='y' && ks[4]=='m' && ks[5]=='#') {
            const proto::ProtoObject* sym = lookupSymbolByStrKey(ks);
            if (sym) outEls = outEls->appendLast(ctx, sym);
            continue;
        }
        const proto::ProtoObject* keyObj = keyStr->asObject(ctx);
        if (!keyObj) continue;
        if (isSymK && keyObj->getAttribute(ctx, isSymK, false) == PROTO_TRUE) {
            outEls = outEls->appendLast(ctx, keyObj);
        }
    }
    const proto::ProtoObject* result = createNewArray(ctx, nullptr);
    setArrayElements(ctx, result, outEls);
    const proto::ProtoString* isArrKey = JSSymbols::isArray(ctx);
    if (isArrKey) result = result->setAttribute(ctx, isArrKey, PROTO_TRUE);
    const proto::ProtoString* lenKey = JSSymbols::length(ctx);
    if (lenKey) result = result->setAttribute(ctx, lenKey, ctx->fromInteger(static_cast<long long>(outEls->getSize(ctx))));
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
    // §20.1.2.5 step 1 invokes RequireObjectCoercible on O which rejects
    // both null and undefined.  Pre-fix the explicit `undefined` token
    // (t_undefinedSentinel) bypassed the absence check and reached the
    // own-attribute walk on a non-object, so Object.defineProperties(
    // undefined, {}) silently returned undefined instead of raising
    // TypeError (built-ins/Object/defineProperties/15.2.3.7-1-1).
    {
        const proto::ProtoObject* nullSentinel = getNullSentinel();
        const proto::ProtoObject* undefSentinel = getUndefinedSentinel();
        bool isNull = (target == nullSentinel);
        bool isUndefined = (!target || target == PROTO_NONE || target == undefSentinel);
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

    // \xc2\xa719.1.2.3 step 5: keys be ? from.[[OwnPropertyKeys]]().  A Proxy
    // receiver dispatches the ownKeys / getOwnPropertyDescriptor traps;
    // each per-key descriptor read goes through the gOPD trap with
    // ToString(key) per spec.  Pre-fix defineProperties walked the
    // raw protoCore SparseList and never touched the Proxy traps
    // (built-ins/Object/defineProperties/proxy-no-ownkeys-returned-
    // keys-order.js pins the case — the test asserts the gOPD trap
    // is called with each key, in chronological order).
    if (isProxy(ctx, propsObj)) {
        const proto::ProtoObject* keysArr = proxyDispatchOwnKeys(ctx, propsObj);
        if (hasCallException()) return PROTO_NONE;
        // No ownKeys trap: forward to the target's own keys via
        // collectOwnKeys (the default [[OwnPropertyKeys]] behaviour).
        std::vector<std::string> defaultKeys;
        if (!keysArr || keysArr == PROTO_NONE) {
            const proto::ProtoObject* tgt = propsObj;
            int guard = 16;
            while (guard-- > 0 && isProxy(ctx, tgt)) {
                const proto::ProtoObject* nx = proxyTarget(ctx, tgt);
                if (!nx || nx == tgt) break;
                tgt = nx;
            }
            if (tgt && tgt != propsObj)
                collectOwnKeys(ctx, tgt, defaultKeys, nullptr, /*includeNonEnumerable=*/true);
        }
        const proto::ProtoList* els = keysArr ? getArrayElements(ctx, keysArr) : nullptr;
        size_t kn = els ? els->getSize(ctx) : defaultKeys.size();
        for (size_t i = 0; i < kn; ++i) {
            const proto::ProtoObject* kObj = els
                ? els->getAt(ctx, i)
                : ctx->fromUTF8String(defaultKeys[i].c_str());
            if (!kObj || kObj == PROTO_NONE) continue;
            const proto::ProtoString* kStr = kObj->asString(ctx);
            if (!kStr) continue;
            const proto::ProtoObject* desc =
                proxyDispatchGetOwnPropertyDescriptor(ctx, propsObj, kStr);
            if (hasCallException()) return PROTO_NONE;
            if (!desc || desc == PROTO_NONE
                || desc == getUndefinedSentinel() || desc == getNullSentinel())
                continue;
            // Skip non-enumerable per \xc2\xa719.1.2.3 step 5.b.iii.
            const proto::ProtoString* enumK = ctx->fromUTF8String("enumerable")
                ? ctx->fromUTF8String("enumerable")->asString(ctx) : nullptr;
            if (enumK) {
                const proto::ProtoObject* ev = desc->getAttribute(ctx, enumK, true);
                if (ev != PROTO_TRUE && (!ev || !ev->isBoolean(ctx) || !ev->asBoolean(ctx)))
                    continue;
            }
            // Reflect.get(proxy, key) for the descriptor value.
            const proto::ProtoObject* descRead =
                proxyDispatchGet(ctx, propsObj, kStr, propsObj);
            if (hasCallException()) return PROTO_NONE;
            const proto::ProtoList* dpArgs = ctx->newList();
            dpArgs = dpArgs->appendLast(ctx, target);
            dpArgs = dpArgs->appendLast(ctx, kObj);
            dpArgs = dpArgs->appendLast(ctx, descRead ? descRead : PROTO_NONE);
            const proto::ProtoObject* newTarget = objectDefineProperty(ctx, nullptr, nullptr, dpArgs, nullptr);
            if (hasCallException()) return PROTO_NONE;
            if (newTarget && newTarget != PROTO_NONE) target = newTarget;
        }
        return target;
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
            // If the descriptor entry is an OWN accessor (sidecar getter
            // present on the receiver itself), resolve the value via the
            // getter before dispatching to defineProperty.  Pre-fix the
            // probe used getAttribute(ctx, gk, false) — the third arg
            // is the callbacks gate, not a chain-walk disable, so the
            // walk still crossed into parent prototypes and fired any
            // inherited getter even when the current level already
            // carried an OWN data slot that should have shadowed it
            // (built-ins/Object/defineProperties/15.2.3.7-5-a-2.js
            // pins the case: an OWN \`prop\` data slot was masked by
            // an inherited accessor returning a different descriptor
            // and obj.prop ended up at the inherited value).
            {
                std::string gkStr = std::string("__get_") + keyStr + "__";
                const proto::ProtoObject* gko = ctx->fromUTF8String(gkStr.c_str());
                const proto::ProtoString* gk = gko ? gko->asString(ctx) : nullptr;
                if (gk && propsObj->hasOwnAttribute(ctx, gk) == PROTO_TRUE) {
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

    // Iterable element read.  §7.3.2 Get fires accessor getters; their
    // abrupt completion must propagate (test262 fromEntries/iterator-
    // closed-for-throwing-entry-{key,value}-accessor).  Probe the
    // `__get_<i>__` accessor sidecar FIRST so a throwing entry getter
    // surfaces via hasCallException; else use the array-element fast
    // path; else fall back to the indexed-attribute chain walk.
    auto readEl = [&](const proto::ProtoObject* arr, long long i) -> const proto::ProtoObject* {
        if (!arr || arr == PROTO_NONE) return PROTO_NONE;
        {
            std::string gkStr = "__get_" + std::to_string(i) + "__";
            const proto::ProtoObject* gko = ctx->fromUTF8String(gkStr.c_str());
            const proto::ProtoString* gks = gko ? gko->asString(ctx) : nullptr;
            if (gks) {
                const proto::ProtoObject* getter = arr->getAttribute(ctx, gks, true);
                if (getter && getter != PROTO_NONE) {
                    const proto::ProtoObject* r =
                        callJSFunction(ctx, getter, arr, ctx->newList());
                    return r ? r : PROTO_NONE;
                }
            }
        }
        const proto::ProtoObject* v =
            arrayTryFastGet(ctx, arr, static_cast<unsigned long>(i));
        if (v) return v;
        // String-wrapper entry: `Object('ab')` has __primitive_value__='ab';
        // index 0 → 'a', index 1 → 'b' per §22.1.4 ToObject semantics.
        const proto::ProtoString* pvK = JSSymbols::primitiveValue(ctx);
        if (pvK) {
            const proto::ProtoObject* pv = arr->getAttribute(ctx, pvK, false);
            if (pv && pv != PROTO_NONE && pv->isString(ctx)) {
                const proto::ProtoString* ps = pv->asString(ctx);
                if (ps) {
                    std::string s;
                    ps->toUTF8String(ctx, s);
                    size_t bi = 0;
                    long long cpIdx = 0;
                    while (bi < s.size() && cpIdx < i) {
                        unsigned char c = static_cast<unsigned char>(s[bi]);
                        size_t cl = (c < 0x80) ? 1 : (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : 4;
                        if (bi + cl > s.size()) break;
                        bi += cl;
                        cpIdx++;
                    }
                    if (cpIdx == i && bi < s.size()) {
                        unsigned char c = static_cast<unsigned char>(s[bi]);
                        size_t cl = (c < 0x80) ? 1 : (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : 4;
                        if (bi + cl <= s.size())
                            return ctx->fromUTF8String(s.substr(bi, cl).c_str());
                    }
                }
            }
        }
        const proto::ProtoString* ik = JSSymbols::indexKey(ctx, static_cast<uint32_t>(i));
        v = ik ? arr->getAttribute(ctx, ik, false) : nullptr;
        return v ? v : PROTO_NONE;
    };

    auto processPair = [&](const proto::ProtoObject* pair) -> void {
        // §20.1.2.6 step 8.b — CreateDataPropertyOnObjectFromEntries:
        //   1. If Type(entry) is not Object, throw a TypeError exception.
        // Pre-fix the null / undefined / primitive cases short-circuited
        // to a silent skip, so `Object.fromEntries([null, undefined,
        // 'foo'])` returned {} instead of throwing per the spec.
        // test262 built-ins/Object/fromEntries/iterator-closed-for-*
        // assert.throws(TypeError, ...) on null / string entries.
        bool entryIsObject = pair && pair != PROTO_NONE
            && pair != getUndefinedSentinel() && pair != getNullSentinel()
            && !pair->isInteger(ctx) && !pair->isDouble(ctx)
            && !pair->isFloat(ctx) && !pair->isString(ctx)
            && pair != PROTO_TRUE && pair != PROTO_FALSE;
        if (!entryIsObject) {
            signalNativeException(makeNativeError(ctx, "TypeError",
                "iterator entry must be an Object"));
            return;
        }
        const proto::ProtoObject* keyObj = readEl(pair, 0);
        const proto::ProtoObject* valObj = readEl(pair, 1);
        if (!valObj) valObj = PROTO_NONE;
        if (!keyObj || keyObj == PROTO_NONE) return;
        // §20.1.2.6 step 8.b.iv invokes ToPropertyKey on the key, which
        // routes through ToPrimitive (hint=string) — Symbol.toPrimitive,
        // toString, valueOf in spec order — before stringifying.  Pre-fix
        // only String / Integer keys were honoured and any object key
        // fell through the `keyStr.empty()` short-circuit, silently
        // dropping the entry (built-ins/Object/fromEntries/to-property-key).
        const proto::ProtoString* entryKey = coercePropNameToKey(ctx, keyObj);
        if (hasCallException()) return;
        if (!entryKey) return;
        result = result->setAttribute(ctx, entryKey, valObj);
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
        const proto::ProtoString* returnKey = ctx->fromUTF8String("return")
            ? ctx->fromUTF8String("return")->asString(ctx) : nullptr;
        const proto::ProtoObject* nextFn = iter->getAttribute(ctx, nextKey, true);
        // §7.4.2 GetIteratorFromMethod step 7 / §7.4.6 IteratorStep:
        // when the iterator's `next` slot is not callable, abrupt-
        // complete with TypeError BEFORE the loop body runs, AND DO NOT
        // close the iterator (return() should not fire — the spec only
        // closes after IteratorStep succeeded).  Pre-fix the loop body
        // checked `nextFn && nextFn != PROTO_NONE` and silently exited,
        // returning {} instead of TypeError (built-ins/Object/fromEntries/
        // iterator-not-closed-for-uncallable-next).
        auto isCallable = [&](const proto::ProtoObject* fn) -> bool {
            if (!fn || fn == PROTO_NONE
                || fn == getUndefinedSentinel() || fn == getNullSentinel())
                return false;
            if (fn->isMethod(ctx)) return true;
            const proto::ProtoString* bcK = JSSymbols::bytecodeId(ctx);
            if (bcK && fn->hasAttribute(ctx, bcK) == PROTO_TRUE) return true;
            const proto::ProtoString* nfK = JSSymbols::nativeFn(ctx);
            if (nfK && fn->hasAttribute(ctx, nfK) == PROTO_TRUE) return true;
            return false;
        };
        if (!isCallable(nextFn)) {
            signalNativeException(makeNativeError(ctx, "TypeError",
                "iterator.next is not callable"));
            return PROTO_NONE;
        }
        // §7.4.6 IteratorClose: when an abrupt completion happens during
        // iteration, the spec calls iter.return() before re-raising.
        // Pre-fix processPair's signalNativeException for non-Object
        // entries (null / string / number) was simply followed by
        // `return result` — the inner iterator never received its
        // close call, so test262 fixtures asserting
        // `assert(returned, '...')` failed.
        // Note: any new exception that return() itself throws is
        // intentionally allowed to replace the original abrupt — the
        // test262 close-on-* fixtures only assert that .return was
        // called, not which exception finally surfaces.
        auto closeAndPropagate = [&]() -> const proto::ProtoObject* {
            if (returnKey && iter) {
                const proto::ProtoObject* retFn = iter->getAttribute(ctx, returnKey, true);
                if (retFn && retFn != PROTO_NONE) {
                    const proto::ProtoList* noA = ctx->newList();
                    (void)callJSFunction(ctx, retFn, iter, noA);
                }
            }
            return PROTO_NONE;
        };
        long long safety = 0;
        // Helper: fire `__get_<name>__` accessor sidecar then fall
        // back to chain-walk getAttribute.  Spec §7.4.3 IteratorComplete
        // / §7.4.4 IteratorValue require Get() to invoke the accessor.
        auto readDoneOrValue = [&](const proto::ProtoObject* obj,
                                   const proto::ProtoString* key,
                                   const char* gkStr) -> const proto::ProtoObject* {
            const proto::ProtoObject* gko = ctx->fromUTF8String(gkStr);
            const proto::ProtoString* gks = gko ? gko->asString(ctx) : nullptr;
            if (gks) {
                const proto::ProtoObject* getter = obj->getAttribute(ctx, gks, true);
                if (getter && getter != PROTO_NONE) {
                    const proto::ProtoObject* r =
                        callJSFunction(ctx, getter, obj, ctx->newList());
                    return r ? r : PROTO_NONE;
                }
            }
            return obj->getAttribute(ctx, key, true);
        };
        while (nextFn && nextFn != PROTO_NONE) {
            const proto::ProtoList* nArgs = ctx->newList();
            const proto::ProtoObject* res = callJSFunction(ctx, nextFn, iter, nArgs);
            if (hasCallException()) return PROTO_NONE;  // next() threw
            if (!res || res == PROTO_NONE) break;
            // §7.4.3 IteratorComplete: Get(step, "done") fires the
            // accessor; abrupt completion propagates WITHOUT calling
            // iterator.return (test262 fromEntries/iterator-not-
            // closed-for-throwing-done-accessor).
            const proto::ProtoObject* dv = readDoneOrValue(res, doneKey, "__get_done__");
            if (hasCallException()) return PROTO_NONE;
            bool isDone = dv == PROTO_TRUE
                || (dv && dv->isBoolean(ctx) && dv->asBoolean(ctx));
            if (isDone) break;
            const proto::ProtoObject* pair = readDoneOrValue(res, valueKey, "__get_value__");
            // §7.4.4 IteratorValue: a throwing `get value()` DOES
            // propagate without iterator.return either (the spec only
            // closes for abrupt completions after a successful step).
            if (hasCallException()) return PROTO_NONE;
            processPair(pair);
            if (hasCallException()) return closeAndPropagate();
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

    // §20.1.2.13 step 2 invokes ToPropertyKey(P), which routes through
    // ToPrimitive (Symbol.toPrimitive → toString → valueOf).  Pre-fix
    // hasOwn only honoured raw String / Integer keys — an object key
    // whose @@toPrimitive coerced to a Symbol fell to the empty-key
    // short-circuit (built-ins/Object/hasOwn/symbol_property_*).
    const proto::ProtoString* strKey = coercePropNameToKey(ctx, key);
    if (hasCallException()) return PROTO_NONE;
    if (!strKey) return PROTO_FALSE;
    std::string keyStr;
    strKey->toUTF8String(ctx, keyStr);
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
// Object.groupBy(items, callbackfn) — ECMAScript 2024 §20.1.2.13a
// Group items of an iterable by the property-key returned by callbackfn.
// Returns a null-prototype object whose own properties are arrays of grouped
// items.  This implementation iterates array-shaped inputs; generic iterators
// would need a full GetIterator/IteratorStep shim.
// ---------------------------------------------------------------------------

static const proto::ProtoObject* objectGroupBy(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    const proto::ProtoObject* items = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0) : PROTO_NONE;
    const proto::ProtoObject* cb = (args && args->getSize(ctx) > 1)
        ? args->getAt(ctx, 1) : PROTO_NONE;
    if (throwIfNullOrUndefined(ctx, items, "Object.groupBy")) return PROTO_NONE;
    // Spec: callbackfn must be callable.  Reject explicit non-callable
    // values (null, undefined, primitives, objects without a callable
    // shape) up-front so the abrupt completion is observable to the
    // caller — callJSFunction's no-op fallback would silently return
    // undefined for non-callable receivers.
    auto isCb = [&](const proto::ProtoObject* fn) -> bool {
        if (!fn || fn == PROTO_NONE
            || fn == getUndefinedSentinel() || fn == getNullSentinel())
            return false;
        if (fn->isMethod(ctx)) return true;
        const proto::ProtoString* bcKey = JSSymbols::bytecodeId(ctx);
        if (bcKey && fn->hasAttribute(ctx, bcKey) == PROTO_TRUE) return true;
        const proto::ProtoString* nfKey = JSSymbols::nativeFn(ctx);
        if (nfKey && fn->hasAttribute(ctx, nfKey) == PROTO_TRUE) return true;
        return false;
    };
    if (!isCb(cb)) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Object.groupBy: callback is not callable"));
        return PROTO_NONE;
    }
    // Build a temporary ProtoList of values to iterate.  For Array
    // sources we reuse the __elements__ list; for strings we materialise
    // a list of single-codepoint string cells (per §22.1.4 each
    // codepoint is the iterated value).  Generic iterators are still
    // pending — see R14 commit 089ec599.
    const proto::ProtoList* els = protojs::getArrayElements(ctx, items);
    bool ownedEls = false;
    if (!els && items->isString(ctx)) {
        std::string s;
        items->asString(ctx)->toUTF8String(ctx, s);
        const proto::ProtoList* built = ctx->newList();
        for (size_t i = 0; i < s.size(); ) {
            unsigned char c = static_cast<unsigned char>(s[i]);
            size_t cl = (c < 0x80) ? 1 : (c < 0xE0) ? 2 : (c < 0xF0) ? 3 : 4;
            if (i + cl > s.size()) break;
            std::string ch = s.substr(i, cl);
            built = built->appendLast(ctx, ctx->fromUTF8String(ch.c_str()));
            i += cl;
        }
        els = built;
        ownedEls = true;
    }
    if (!els) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Object.groupBy: items is not iterable"));
        return PROTO_NONE;
    }
    (void)ownedEls;

    const proto::ProtoObject* result = ctx->newObject(true);
    if (!result) return PROTO_NONE;
    // Spec: result has null prototype.
    protojs::setJSProtoOverride(ctx, result, getNullSentinel());

    unsigned long n = els->getSize(ctx);
    for (unsigned long i = 0; i < n; ++i) {
        const proto::ProtoObject* v = els->getAt(ctx, static_cast<int>(i));
        if (!v || v == PROTO_NONE) continue;  // skip holes
        const proto::ProtoList* cbArgs = ctx->newList();
        cbArgs = cbArgs->appendLast(ctx, v);
        cbArgs = cbArgs->appendLast(ctx, ctx->fromInteger(static_cast<long long>(i)));
        const proto::ProtoObject* keyVal = callJSFunction(ctx, cb,
            getUndefinedSentinel(), cbArgs);
        if (hasCallException()) return PROTO_NONE;
        const proto::ProtoString* keyStr = coercePropNameToKey(ctx, keyVal);
        if (!keyStr) continue;
        // Append v to result[key]'s element list; create a new array if absent.
        const proto::ProtoObject* bucket = result->getAttribute(ctx, keyStr, false);
        const proto::ProtoList* bucketEls = nullptr;
        if (bucket && bucket != PROTO_NONE) {
            bucketEls = protojs::getArrayElements(ctx, bucket);
        }
        if (!bucket || bucket == PROTO_NONE) {
            bucket = createNewArray(ctx, nullptr);
            bucketEls = ctx->newList();
        }
        bucketEls = bucketEls->appendLast(ctx, v);
        protojs::setArrayElements(ctx, bucket, bucketEls);
        const proto::ProtoString* lenK = JSSymbols::length(ctx);
        if (lenK) bucket = bucket->setAttribute(ctx, lenK,
            ctx->fromInteger(static_cast<long long>(bucketEls->getSize(ctx))));
        result = result->setAttribute(ctx, keyStr, bucket);
    }
    return result;
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
    // §20.1.3.2 step 1: ToPropertyKey(V) precedes ToObject(this).
    // A throwing toString / @@toPrimitive on V surfaces BEFORE the
    // null/undefined receiver check (Sputnik
    // hasOwnProperty/topropertykey_before_toobject pins this order).
    if (!args || args->getSize(ctx) == 0) {
        // No key supplied — fall through to ToObject(this); spec
        // returns false after the receiver-check throws or succeeds.
        if (!self || self == PROTO_NONE
            || self == getNullSentinel() || self == getUndefinedSentinel()) {
            signalNativeException(makeNativeError(ctx, "TypeError",
                "Cannot convert undefined or null to object"));
            return PROTO_NONE;
        }
        return PROTO_FALSE;
    }
    const proto::ProtoObject* key = args->getAt(ctx, 0);
    if (!key) key = getUndefinedSentinel();
    const proto::ProtoString* k = coercePropNameToKey(ctx, key);
    if (hasCallException()) return PROTO_NONE;
    // Step 2: ToObject(this) — null / undefined now throw post key coercion.
    if (!self || self == PROTO_NONE
        || self == getNullSentinel() || self == getUndefinedSentinel()) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Cannot convert undefined or null to object"));
        return PROTO_NONE;
    }
    if (!k) return PROTO_FALSE;

    // §20.1.3.2 / §7.3.13 — HasOwnProperty(O, P) is defined as
    // ? O.[[GetOwnProperty]](P) !== undefined. For Proxies, [[GetOwnProperty]]
    // is the gOPD trap (or its target fallback). Pre-fix the method went
    // straight to hasOwnAttribute on the Proxy receiver, which always
    // reported false because the trap's result lives on the target.
    // test262 Proxy/getOwnPropertyDescriptor/trap-is-undefined.js pinned
    // this — Object.prototype.hasOwnProperty.call(p, 'attr') must return
    // true when target has the own data property and the Proxy has no
    // handler.
    if (protojs::isProxy(ctx, self)) {
        const proto::ProtoObject* desc =
            protojs::proxyDispatchGetOwnPropertyDescriptor(ctx, self, k);
        if (hasCallException()) return PROTO_NONE;
        if (desc && desc != PROTO_NONE
            && desc != getUndefinedSentinel() && desc != getNullSentinel()) {
            return PROTO_TRUE;
        }
        return PROTO_FALSE;
    }

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
                        // PROTO_NONE → hole; fall through.
                    } else if (self->hasOwnAttribute(ctx, k) == PROTO_TRUE) {
                        // idx in [els.size(), length) AND the index is
                        // backed by a named attribute (sparse explicit
                        // store like `arr[5] = undefined`): treat as own.
                        return PROTO_TRUE;
                    }
                    // Otherwise it is a hole — `new Array(3)` slots, or
                    // sparse pre-allocated tail without a named attribute
                    // — return false per ECMA-262 (CreateArrayFromList).
                }
            }
        }
    }

    // §22.1.4 String-exotic wrapper char-index fallback —
    // `new String("abc")` exposes "0".."length-1" as own data slots
    // via __primitive_value__, not as own attributes. Pre-fix
    // `Object.prototype.hasOwnProperty.call(new String("abc"), "0")`
    // returned false (test262
    // Object/freeze/15.2.3.9-2-a-12.js, Object/keys/15.2.3.14-5-15.js).
    {
        const proto::ProtoString* pvKey = JSSymbols::primitiveValue(ctx);
        const proto::ProtoObject* pv = pvKey
            ? self->getAttribute(ctx, pvKey, false) : nullptr;
        if (pv && pv != PROTO_NONE && pv->isString(ctx)) {
            if (keyStr == "length") return PROTO_TRUE;
            if (!keyStr.empty()) {
                char* end = nullptr;
                long long iv = std::strtoll(keyStr.c_str(), &end, 10);
                if (end && *end == '\0' && iv >= 0
                    && std::to_string(iv) == keyStr) {
                    const proto::ProtoString* ps = pv->asString(ctx);
                    if (ps && (size_t)iv < ps->getSize(ctx)) return PROTO_TRUE;
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
    // §20.1.3.4 step 1: ToPropertyKey(V) precedes ToObject(this).
    if (!args || args->getSize(ctx) == 0) {
        if (!self || self == PROTO_NONE
            || self == getNullSentinel() || self == getUndefinedSentinel()) {
            signalNativeException(makeNativeError(ctx, "TypeError",
                "Cannot convert undefined or null to object"));
            return PROTO_NONE;
        }
        return PROTO_FALSE;
    }
    const proto::ProtoObject* key = args->getAt(ctx, 0);
    if (!key) key = getUndefinedSentinel();
    const proto::ProtoString* k = coercePropNameToKey(ctx, key);
    if (hasCallException()) return PROTO_NONE;
    if (!self || self == PROTO_NONE
        || self == getNullSentinel() || self == getUndefinedSentinel()) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Cannot convert undefined or null to object"));
        return PROTO_NONE;
    }
    if (!k) return PROTO_FALSE;

    // §20.1.3.4 / §7.3.14 — OrdinaryHasProperty derived directly from
    // [[GetOwnProperty]] result's [[Enumerable]] attribute. Pre-fix
    // a Proxy receiver was probed via hasOwnAttribute and always
    // reported false, breaking verifyProperty's isEnumerable() check
    // on Proxy/getOwnPropertyDescriptor/trap-is-undefined.js.
    if (protojs::isProxy(ctx, self)) {
        const proto::ProtoObject* desc =
            protojs::proxyDispatchGetOwnPropertyDescriptor(ctx, self, k);
        if (hasCallException()) return PROTO_NONE;
        if (!desc || desc == PROTO_NONE
            || desc == getUndefinedSentinel() || desc == getNullSentinel()) {
            return PROTO_FALSE;
        }
        // Read the descriptor's `enumerable` data slot.
        const proto::ProtoObject* eko = ctx->fromUTF8String("enumerable");
        const proto::ProtoString* eks = eko ? eko->asString(ctx) : nullptr;
        if (!eks) return PROTO_FALSE;
        const proto::ProtoObject* ev = desc->getAttribute(ctx, eks, true);
        if (!ev || ev == PROTO_NONE) return PROTO_FALSE;
        if (ev == PROTO_TRUE) return PROTO_TRUE;
        if (ev == PROTO_FALSE) return PROTO_FALSE;
        if (ev->isBoolean(ctx)) return ev->asBoolean(ctx) ? PROTO_TRUE : PROTO_FALSE;
        return PROTO_FALSE;
    }

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
            long long idx = -1;
            if (!keyStr.empty()) {
                char* end = nullptr;
                long long v = std::strtoll(keyStr.c_str(), &end, 10);
                if (end && *end == '\0' && v >= 0 && std::to_string(v) == keyStr)
                    idx = v;
            }
            if (isArr == PROTO_TRUE && idx >= 0) {
                const proto::ProtoString* lenKey = JSSymbols::length(ctx);
                const proto::ProtoObject* lenVal = lenKey
                    ? self->getAttribute(ctx, lenKey, false) : nullptr;
                if (lenVal && lenVal != PROTO_NONE && lenVal->isInteger(ctx)) {
                    if (idx < lenVal->asLong(ctx)) return PROTO_TRUE;
                }
            }
            // \xc2\xa722.1.4 String wrapper char-index probe: indices in
            // [0, length) are own enumerable per spec.  Pre-fix the
            // descriptor-walk path returned PROTO_FALSE because the char
            // indices live in __primitive_value__, not as own attributes
            // — propertyIsEnumerable diverged from
            // getOwnPropertyDescriptor (which DID surface them via the
            // String-wrapper char-index synthesis at lines 3412+).
            // built-ins/String/numeric-properties.js: verifyProperty
            // asserts every char index is enumerable.
            if (idx >= 0) {
                const proto::ProtoString* pvKey = JSSymbols::primitiveValue(ctx);
                const proto::ProtoObject* pv = pvKey
                    ? self->getAttribute(ctx, pvKey, false) : nullptr;
                if (pv && pv != PROTO_NONE && pv->isString(ctx)) {
                    const proto::ProtoString* ps = pv->asString(ctx);
                    if (ps && idx < (long long)ps->getSize(ctx))
                        return PROTO_TRUE;
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

    // §22.1.3.7 step 14-17: compute builtinTag, then ALWAYS probe
    // @@toStringTag; if Type(tag) is String, override builtinTag.
    // For primitives we route the @@toStringTag probe through the
    // matching wrapper prototype so user overrides on Boolean.prototype
    // / Number.prototype / String.prototype propagate to the primitive
    // value (built-ins/Object/prototype/toString/symbol-tag-override-
    // primitives.js).
    const char* primitiveBuiltinTag = nullptr;
    const proto::ProtoObject* tagSource = self;
    if (self->isBoolean(ctx)) {
        primitiveBuiltinTag = "Boolean";
        if (ctx->space && ctx->space->booleanPrototype)
            tagSource = ctx->space->booleanPrototype;
    } else if (self->isInteger(ctx) || self->isDouble(ctx) || self->isFloat(ctx)) {
        primitiveBuiltinTag = "Number";
        if (ctx->space && ctx->space->smallIntegerPrototype)
            tagSource = ctx->space->smallIntegerPrototype;
    } else if (self->isString(ctx)) {
        primitiveBuiltinTag = "String";
        if (ctx->space && ctx->space->stringPrototype)
            tagSource = reinterpret_cast<const proto::ProtoObject*>(ctx->space->stringPrototype);
    }
    if (primitiveBuiltinTag) {
        const proto::ProtoObject* tagKo = ctx->fromUTF8String("Symbol.toStringTag");
        const proto::ProtoString* tagK = tagKo ? tagKo->asString(ctx) : nullptr;
        if (tagK) {
            const proto::ProtoObject* val = tagSource->getAttribute(ctx, tagK, true);
            if (val && val != PROTO_NONE && val->isString(ctx)) {
                const proto::ProtoString* symMk = JSSymbols::isSymbol(ctx);
                bool isSym = symMk && val->getAttribute(ctx, symMk, true) == PROTO_TRUE;
                if (!isSym) {
                    std::string tag;
                    val->asString(ctx)->toUTF8String(ctx, tag);
                    if (tag.compare(0, 7, "Symbol.") != 0
                        && tag.compare(0, 7, "Symbol(") != 0) {
                        std::string out = "[object " + tag + "]";
                        return ctx->fromUTF8String(out.c_str());
                    }
                }
            }
        }
        std::string out = "[object ";
        out += primitiveBuiltinTag;
        out += "]";
        return ctx->fromUTF8String(out.c_str());
    }

    // Function: JS closure (__bytecode_id__), native ProtoMethod, or wrapped
    // native function (__native_fn__ holds a ProtoMethod pointer).
    // §22.1.3.7 step 14: builtinTag for callables is "Function" but
    // @@toStringTag on the instance still overrides per step 16-17.
    // Track the candidate and fall through to the unified WKS probe.
    // For a Proxy, recurse into the target — IsCallable(proxy) is
    // routed through IsCallable(target).
    const proto::ProtoObject* funcProbe = self;
    {
        const proto::ProtoObject* walk = self;
        int hops = 0;
        while (walk && walk != PROTO_NONE && isProxy(ctx, walk) && hops < 16) {
            walk = proxyTarget(ctx, walk);
            hops++;
        }
        if (walk && walk != PROTO_NONE) funcProbe = walk;
    }
    const char* funcBuiltinTag = nullptr;
    {
        const proto::ProtoString* bcKey = JSSymbols::bytecodeId(ctx);
        if (bcKey) {
            const proto::ProtoObject* bcVal = funcProbe->getAttribute(ctx, bcKey, false);
            if (bcVal && bcVal != PROTO_NONE && bcVal->isInteger(ctx))
                funcBuiltinTag = "Function";
        }
        if (!funcBuiltinTag && funcProbe->isMethod(ctx))
            funcBuiltinTag = "Function";
        if (!funcBuiltinTag) {
            const proto::ProtoString* nfKey = JSSymbols::nativeFn(ctx);
            if (nfKey) {
                const proto::ProtoObject* nfVal = funcProbe->getAttribute(ctx, nfKey, false);
                if (nfVal && nfVal != PROTO_NONE && nfVal->isMethod(ctx))
                    funcBuiltinTag = "Function";
            }
        }
        if (!funcBuiltinTag) {
            const proto::ProtoString* bfKey = JSSymbols::boundFn(ctx);
            if (bfKey) {
                const proto::ProtoObject* bfVal = funcProbe->getAttribute(ctx, bfKey, false);
                if (bfVal && bfVal != PROTO_NONE)
                    funcBuiltinTag = "Function";
            }
        }
        if (!funcBuiltinTag) {
            const proto::ProtoObject* fpmo = ctx->fromUTF8String("__is_function_prototype__");
            const proto::ProtoString* fpms = fpmo ? fpmo->asString(ctx) : nullptr;
            if (fpms) {
                const proto::ProtoObject* fpv = self->getAttribute(ctx, fpms, false);
                if (fpv == PROTO_TRUE)
                    funcBuiltinTag = "Function";
            }
        }
        if (!funcBuiltinTag) {
            const proto::ProtoString* icKey = ctx->fromUTF8String("__is_constructor__")->asString(ctx);
            if (icKey) {
                const proto::ProtoObject* icVal = self->getAttribute(ctx, icKey, false);
                if (icVal == PROTO_TRUE)
                    funcBuiltinTag = "Function";
            }
        }
    }

    // Array: has __is_array__ as an own attribute (moved from prototype in Phase 7).
    // §22.1.3.7: builtinTag for Arrays is "Array" but @@toStringTag
    // on the instance must still override per step 16-17.  Remember
    // the candidate tag and fall through to the WKS probe.
    // IsArray follows Proxy target chains (§7.2.2 step 3.a), so probe
    // through __proxy_target__ until we hit a concrete object.
    const char* arrayBuiltinTag = nullptr;
    {
        const proto::ProtoString* iaKey = JSSymbols::isArray(ctx);
        const proto::ProtoObject* probe = self;
        const proto::ProtoObject* targetKeyObj = ctx->fromUTF8String("__proxy_target__");
        const proto::ProtoString* targetKey = targetKeyObj ? targetKeyObj->asString(ctx) : nullptr;
        int hops = 0;
        while (probe && probe != PROTO_NONE && hops < 16) {
            if (iaKey) {
                const proto::ProtoObject* iaVal = probe->getAttribute(ctx, iaKey, false);
                if (iaVal == PROTO_TRUE) {
                    arrayBuiltinTag = "Array";
                    break;
                }
            }
            if (!targetKey) break;
            if (probe->hasOwnAttribute(ctx, targetKey) != PROTO_TRUE) break;
            const proto::ProtoObject* nextTarget = probe->getAttribute(ctx, targetKey, false);
            // §7.2.2 IsArray step 3.a: a Proxy whose [[ProxyHandler]] is
            // null (revoked) throws TypeError before the chain walk
            // continues.  Pre-fix the loop silently broke and toString
            // returned "[object Object]" (test262 toString/proxy-revoked).
            if (!nextTarget || nextTarget == PROTO_NONE) {
                signalNativeException(makeNativeError(ctx, "TypeError",
                    "Cannot perform 'IsArray' on a proxy that has been revoked"));
                return PROTO_NONE;
            }
            probe = nextTarget;
            hops++;
        }
    }
    // Symbol primitive: protoJS carries Symbols with the __is_symbol__
    // marker.  Default tag is "Symbol" but @@toStringTag may override
    // (§22.1.3.7 step 16-17) — Symbol.prototype carries the WKS
    // mapping by default but user code may delete or replace it.
    // Pre-fix the symbol branch hard-coded "[object Symbol]" and
    // ignored @@toStringTag overrides, so
    // `delete Symbol.prototype[Symbol.toStringTag]` still surfaced
    // "[object Symbol]" instead of falling through to the default
    // "Object" tag (test262 toString/symbol-tag-non-str-builtin).
    {
        const proto::ProtoString* symK = JSSymbols::isSymbol(ctx);
        if (symK && self->getAttribute(ctx, symK, true) == PROTO_TRUE) {
            const proto::ProtoObject* tagKo = ctx->fromUTF8String("Symbol.toStringTag");
            const proto::ProtoString* tagK = tagKo ? tagKo->asString(ctx) : nullptr;
            std::string tag = "Symbol";
            if (tagK) {
                const proto::ProtoObject* val = self->getAttribute(ctx, tagK, true);
                if (val && val != PROTO_NONE && val != getUndefinedSentinel()
                    && val->isString(ctx)) {
                    const proto::ProtoString* isSymMk = JSSymbols::isSymbol(ctx);
                    bool isSymVal = isSymMk && val->getAttribute(ctx, isSymMk, true) == PROTO_TRUE;
                    if (!isSymVal) {
                        std::string s;
                        val->asString(ctx)->toUTF8String(ctx, s);
                        if (s.compare(0, 7, "Symbol.") != 0
                            && s.compare(0, 7, "Symbol(") != 0) {
                            tag = s;
                        }
                    } else {
                        // §22.1.3.7 step 16: non-string tag → use the
                        // builtinTag.  For a Symbol primitive whose
                        // Symbol.prototype[@@toStringTag] is itself a
                        // Symbol (the spec default), that means
                        // builtinTag for Symbol-shaped objects is
                        // "Object" since the spec doesn't list Symbol
                        // among the slot-detected builtins.
                        tag = "Object";
                    }
                } else if (val && val != PROTO_NONE && val != getUndefinedSentinel()) {
                    // Non-string tag → fall back to builtinTag = "Object".
                    tag = "Object";
                } else {
                    // tag absent / undefined → builtinTag = "Object"
                    // per §22.1.3.7 step 16.
                    tag = "Object";
                }
            }
            std::string out = "[object " + tag + "]";
            return ctx->fromUTF8String(out.c_str());
        }
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
    const char* wrapperBuiltinTag = nullptr;
    {
        const proto::ProtoString* pvKey = JSSymbols::primitiveValue(ctx);
        if (pvKey) {
            const proto::ProtoObject* pv = self->getAttribute(ctx, pvKey, false);
            if (pv && pv != PROTO_NONE) {
                if (pv->isBoolean(ctx)) wrapperBuiltinTag = "Boolean";
                else if (pv->isInteger(ctx) || pv->isDouble(ctx) || pv->isFloat(ctx))
                    wrapperBuiltinTag = "Number";
                else if (pv->isString(ctx)) wrapperBuiltinTag = "String";
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
            // §22.1.3.7 step 16: Get(O, @@toStringTag) — must fire
            // accessor getters and propagate their abrupt completion.
            // Pre-fix only the data slot was probed, so
            // Object.defineProperty(obj, Symbol.toStringTag, {get:fn})
            // never triggered fn (test262 get-symbol-tag-err).
            const proto::ProtoObject* val = nullptr;
            std::string keyName;
            key->toUTF8String(ctx, keyName);
            std::string gkStr = "__get_" + keyName + "__";
            const proto::ProtoString* gk =
                ctx->fromUTF8String(gkStr.c_str())->asString(ctx);
            if (gk) {
                const proto::ProtoObject* getter = self->getAttribute(ctx, gk, true);
                if (getter && getter != PROTO_NONE) {
                    val = callJSFunction(ctx, getter, self, ctx->newList());
                    if (hasCallException()) return {};
                }
            }
            if (!val || val == PROTO_NONE) {
                val = self->getAttribute(ctx, key, true);
            }
            if (!val || val == PROTO_NONE || !val->isString(ctx)) return {};
            // §22.1.3.7 step 18: if Type(tag) is not String, fall back to
            // the builtin tag.  In protoJS well-known Symbols are encoded
            // as ProtoStrings ("Symbol.iterator", etc.) so the
            // isString check alone passes them through.  Distinguish a
            // bona-fide JS Symbol via the __is_symbol__ marker on the
            // value object before returning the textual tag.
            const proto::ProtoString* symMk = JSSymbols::isSymbol(ctx);
            if (symMk && val->getAttribute(ctx, symMk, true) == PROTO_TRUE) return {};
            // Symbol-as-string textual encoding: any value that looks
            // like "Symbol." or "Symbol(" came from a well-known or
            // user-created Symbol in protoJS's storage representation
            // and must be treated as a Symbol per the spec, not a
            // String.
            std::string tag;
            val->asString(ctx)->toUTF8String(ctx, tag);
            if (tag.compare(0, 7, "Symbol.") == 0 || tag.compare(0, 7, "Symbol(") == 0) return {};
            return tag;
        };

        // §22.1.3.7 step 16 requires Get(O, @@toStringTag), i.e. ONLY
        // the well-known-symbol key. The internal `__toStringTag__`
        // sidecar was a second store kept for legacy reasons, but
        // letting it survive caused tests that `delete obj[
        // Symbol.toStringTag]` and expect the builtinTag fallback to
        // keep seeing the sidecar (Object/prototype/toString/
        // symbol-tag-*-builtin verifyProperty). Probe ONLY the WKS
        // key.
        const proto::ProtoObject* wksKeyObj = ctx->fromUTF8String("Symbol.toStringTag");
        const proto::ProtoString* wksKey = wksKeyObj ? wksKeyObj->asString(ctx) : nullptr;
        std::string tag = tryTagKey(wksKey);
        if (hasCallException()) return PROTO_NONE;

        if (!tag.empty()) {
            std::string tagResult = "[object " + tag + "]";
            return ctx->fromUTF8String(tagResult.c_str());
        }
    }

    if (arrayBuiltinTag) {
        std::string out = "[object ";
        out += arrayBuiltinTag;
        out += "]";
        return ctx->fromUTF8String(out.c_str());
    }
    if (funcBuiltinTag) {
        std::string out = "[object ";
        out += funcBuiltinTag;
        out += "]";
        return ctx->fromUTF8String(out.c_str());
    }
    if (wrapperBuiltinTag) {
        std::string out = "[object ";
        out += wrapperBuiltinTag;
        out += "]";
        return ctx->fromUTF8String(out.c_str());
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
    // §20.1.3.7 step 1 ToObject(this): primitives are boxed into the
    // matching wrapper.  Pre-fix valueOf returned the boolean / number /
    // string sentinel as-is, so typeof Object.prototype.valueOf.call(true)
    // was "boolean" instead of the spec-required "object" (test262
    // valueOf/15.2.4.4-1, -2).
    if (self == PROTO_TRUE || self == PROTO_FALSE || self->isBoolean(ctx)) {
        JSContextWrapper* w = JSContextWrapper::current();
        const proto::ProtoObject* boolProto =
            (ctx->space && ctx->space->booleanPrototype)
            ? ctx->space->booleanPrototype : nullptr;
        const proto::ProtoObject* wrapper = boolProto
            ? boolProto->newChild(ctx, true) : ctx->newObject(true);
        if (wrapper) {
            const proto::ProtoString* pvK = JSSymbols::primitiveValue(ctx);
            if (pvK) wrapper = wrapper->setAttribute(ctx, pvK, self);
            (void)w;
            return wrapper;
        }
    }
    if (self->isInteger(ctx) || self->isDouble(ctx) || self->isFloat(ctx)) {
        const proto::ProtoObject* numProto =
            (ctx->space && ctx->space->smallIntegerPrototype)
            ? ctx->space->smallIntegerPrototype : nullptr;
        const proto::ProtoObject* wrapper = numProto
            ? numProto->newChild(ctx, true) : ctx->newObject(true);
        if (wrapper) {
            const proto::ProtoString* pvK = JSSymbols::primitiveValue(ctx);
            if (pvK) wrapper = wrapper->setAttribute(ctx, pvK, self);
            return wrapper;
        }
    }
    if (self->isString(ctx)) {
        const proto::ProtoObject* strProto = (ctx->space && ctx->space->stringPrototype)
            ? reinterpret_cast<const proto::ProtoObject*>(ctx->space->stringPrototype) : nullptr;
        const proto::ProtoObject* wrapper = strProto
            ? strProto->newChild(ctx, true) : ctx->newObject(true);
        if (wrapper) {
            const proto::ProtoString* pvK = JSSymbols::primitiveValue(ctx);
            if (pvK) wrapper = wrapper->setAttribute(ctx, pvK, self);
            return wrapper;
        }
    }
    return self;
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
    // §B.2.2.4 step 2: ToPropertyKey(P) — route through the canonical
    // coercion so a throwing toString / @@toPrimitive propagates the
    // abrupt completion instead of being silently converted to
    // "undefined".  Pre-fix non-primitive keys fell through to the
    // return-undefined branch (test262 __lookupGetter__/key-invalid).
    const proto::ProtoString* dataKey = coercePropNameToKey(ctx, keyArg);
    if (hasCallException()) return PROTO_NONE;
    if (!dataKey) return getUndefinedSentinel();
    std::string keyStr;
    dataKey->toUTF8String(ctx, keyStr);
    std::string gkStr = "__get_" + keyStr + "__";
    const proto::ProtoObject* gko = ctx->fromUTF8String(gkStr.c_str());
    const proto::ProtoString* gk = gko ? gko->asString(ctx) : nullptr;
    if (!gk) return getUndefinedSentinel();
    // §B.2.2.4 step 4 walks the chain via [[GetOwnProperty]] /
    // [[GetPrototypeOf]] internal methods — both of which dispatch
    // through the Proxy traps when the current step is a Proxy.
    // Pre-fix the walk read __get_<key>__ / dataKey directly and
    // bypassed the proxy handler, so a getOwnPropertyDescriptor /
    // getPrototypeOf trap that threw (test262 lookup-{own,proto}-
    // {get,proto}-err.js) was silently ignored.
    const proto::ProtoObject* getKo = ctx->fromUTF8String("get");
    const proto::ProtoString* getK = getKo ? getKo->asString(ctx) : nullptr;
    const proto::ProtoObject* curr = self;
    while (curr && curr != PROTO_NONE && curr != getNullSentinel()) {
        if (isProxy(ctx, curr)) {
            const proto::ProtoObject* desc =
                proxyDispatchGetOwnPropertyDescriptor(ctx, curr, dataKey);
            if (hasCallException()) return PROTO_NONE;
            if (desc && desc != PROTO_NONE) {
                if (getK) {
                    const proto::ProtoObject* g = desc->getAttribute(ctx, getK, false);
                    if (g && g != PROTO_NONE) return g;
                }
                return getUndefinedSentinel();
            }
            const proto::ProtoObject* nx =
                proxyDispatchGetPrototypeOf(ctx, curr);
            if (hasCallException()) return PROTO_NONE;
            curr = nx;
            continue;
        }
        if (curr->hasOwnAttribute(ctx, gk) == PROTO_TRUE) {
            const proto::ProtoObject* getter = curr->getAttribute(ctx, gk, false);
            if (getter && getter != PROTO_NONE) return getter;
        }
        // §B.2.2.4 step 4.b.ii: a shadowing data descriptor on the
        // current level stops the walk and returns undefined.  Pre-fix
        // we kept walking and surfaced the parent's getter.
        if (curr->hasOwnAttribute(ctx, dataKey) == PROTO_TRUE) {
            return getUndefinedSentinel();
        }
        // §B.2.2.4 step 4.c: advance via [[GetPrototypeOf]], not the
        // raw C++ parent.  The JS prototype-override map (set by
        // Object.create / Object.setPrototypeOf) supersedes the
        // C++ parent — pre-fix the walk took the wrong branch when
        // an intermediate Proxy was reached through Object.create.
        const proto::ProtoObject* nx = nullptr;
        auto it = t_jsProtoMap.find(curr);
        if (it != t_jsProtoMap.end()) nx = it->second;
        else                          nx = curr->getPrototype(ctx);
        if (!nx || nx == PROTO_NONE || nx == getNullSentinel()) break;
        curr = nx;
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
    // §B.2.2.5 step 2: ToPropertyKey(P) — propagate abrupts from a
    // throwing toString / @@toPrimitive.
    const proto::ProtoString* dataKey = coercePropNameToKey(ctx, keyArg);
    if (hasCallException()) return PROTO_NONE;
    if (!dataKey) return getUndefinedSentinel();
    std::string keyStr;
    dataKey->toUTF8String(ctx, keyStr);
    std::string skStr = "__set_" + keyStr + "__";
    const proto::ProtoObject* sko = ctx->fromUTF8String(skStr.c_str());
    const proto::ProtoString* sk = sko ? sko->asString(ctx) : nullptr;
    if (!sk) return getUndefinedSentinel();
    // §B.2.2.5 step 4 — same Proxy-aware walk shape as __lookupGetter__,
    // but extract desc.set instead of desc.get.
    const proto::ProtoString* setK = JSSymbols::set(ctx);
    const proto::ProtoObject* curr = self;
    while (curr && curr != PROTO_NONE && curr != getNullSentinel()) {
        if (isProxy(ctx, curr)) {
            const proto::ProtoObject* desc =
                proxyDispatchGetOwnPropertyDescriptor(ctx, curr, dataKey);
            if (hasCallException()) return PROTO_NONE;
            if (desc && desc != PROTO_NONE) {
                if (setK) {
                    const proto::ProtoObject* s = desc->getAttribute(ctx, setK, false);
                    if (s && s != PROTO_NONE) return s;
                }
                return getUndefinedSentinel();
            }
            const proto::ProtoObject* nx =
                proxyDispatchGetPrototypeOf(ctx, curr);
            if (hasCallException()) return PROTO_NONE;
            curr = nx;
            continue;
        }
        if (curr->hasOwnAttribute(ctx, sk) == PROTO_TRUE) {
            const proto::ProtoObject* setter = curr->getAttribute(ctx, sk, false);
            if (setter && setter != PROTO_NONE) return setter;
        }
        // §B.2.2.5 step 4.b.ii: shadowing data slot stops the walk.
        if (curr->hasOwnAttribute(ctx, dataKey) == PROTO_TRUE) {
            return getUndefinedSentinel();
        }
        // §B.2.2.5 step 4.c — same as __lookupGetter__: prefer the
        // JS prototype override when present, otherwise the protoCore
        // parent.  Pre-fix used getFirstParent which missed the
        // Object.create / Object.setPrototypeOf rebind.
        const proto::ProtoObject* nx = nullptr;
        auto it = t_jsProtoMap.find(curr);
        if (it != t_jsProtoMap.end()) nx = it->second;
        else                          nx = curr->getPrototype(ctx);
        if (!nx || nx == PROTO_NONE || nx == getNullSentinel()) break;
        curr = nx;
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

    // §20.1.3.3 step 3 walks the chain via V.[[GetPrototypeOf]]; for
    // a Proxy step that's the handler.getPrototypeOf trap.  Pre-fix
    // we walked the raw C++ parent chain and missed:
    //   - Proxy receivers (the trap never fired; arg-is-proxy test).
    //   - JS-side [[Prototype]] overrides set by Object.setPrototypeOf
    //     (rebinds live in t_jsProtoMap, not the C++ chain).
    auto advance = [&](const proto::ProtoObject* o) -> const proto::ProtoObject* {
        if (isProxy(ctx, o)) {
            const proto::ProtoObject* nx = proxyDispatchGetPrototypeOf(ctx, o);
            if (hasCallException()) return nullptr;
            return nx;
        }
        auto it = t_jsProtoMap.find(o);
        if (it != t_jsProtoMap.end()) return it->second;
        return o->getPrototype(ctx);
    };
    const proto::ProtoObject* curr = advance(arg);
    while (curr && curr != PROTO_NONE && curr != getNullSentinel()) {
        if (curr == self) return PROTO_TRUE;
        curr = advance(curr);
        if (hasCallException()) return PROTO_NONE;
    }
    return PROTO_FALSE;
}

// File-scope static handlers for Object.prototype.__proto__ so
// ensureObjectConstructor can RE-wrap them later (after
// Function.prototype is published) into proper Function-prototype-
// parented wrappers carrying name + length descriptors.  Pre-fix the
// initial install ran at BootstrapJSPrototypes time when
// methodPrototype was still Object.prototype, so the resulting
// accessor wraps had no .call / .apply / .bind in their chain — every
// `Object.getOwnPropertyDescriptor(Object.prototype, '__proto__').set
// .call({})` test262 case failed with "is not a function".
static const proto::ProtoObject* protoAccessorGetter(
    proto::ProtoContext* gctx, const proto::ProtoObject* gself,
    const proto::ParentLink*, const proto::ProtoList*,
    const proto::ProtoSparseList*)
{
    if (!gself || gself == PROTO_NONE
        || gself == getNullSentinel() || gself == getUndefinedSentinel()) {
        signalNativeException(makeNativeError(gctx, "TypeError",
            "Cannot convert undefined or null to object"));
        return PROTO_NONE;
    }
    // §B.2.2.1 step 2: return O.[[GetPrototypeOf]]().  When O is a
    // Proxy, this routes through the handler.getPrototypeOf trap;
    // pre-fix protoAccessorGetter read t_jsProtoMap / getPrototype
    // directly, bypassing the trap and silently dropping the throw
    // (built-ins/Object/prototype/__proto__/get-abrupt.js).
    if (isProxy(gctx, gself)) {
        return proxyDispatchGetPrototypeOf(gctx, gself);
    }
    auto it = t_jsProtoMap.find(gself);
    if (it != t_jsProtoMap.end()) return it->second;
    const proto::ProtoObject* p = gself->getPrototype(gctx);
    return (p && p != PROTO_NONE) ? p : getNullSentinel();
}

static const proto::ProtoObject* protoAccessorSetter(
    proto::ProtoContext* sctx, const proto::ProtoObject* sself,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!sself || sself == PROTO_NONE
        || sself == getNullSentinel() || sself == getUndefinedSentinel()) {
        signalNativeException(makeNativeError(sctx, "TypeError",
            "Cannot convert undefined or null to object"));
        return PROTO_NONE;
    }
    const proto::ProtoObject* proto = (args && args->getSize(sctx) > 0)
        ? args->getAt(sctx, 0) : getUndefinedSentinel();
    // §B.2.2.1 step 2: when proto is neither Object nor Null, return
    // undefined without rebinding.  Symbol primitives are tagged
    // Objects (the `__is_symbol__` marker) — the spec excludes them
    // from the "Object" set, so reject explicitly.  Pre-fix only the
    // raw primitives (boolean/number/string/undefined) were filtered
    // and a Symbol argument fell through to setJSProtoOverride.
    bool protoIsSymbol = false;
    {
        const proto::ProtoString* isSymK = JSSymbols::isSymbol(sctx);
        if (isSymK && proto && proto != PROTO_NONE
            && proto != getNullSentinel()
            && proto->hasAttribute(sctx, isSymK) == PROTO_TRUE
            && proto->getAttribute(sctx, isSymK, true) == PROTO_TRUE)
            protoIsSymbol = true;
    }
    if (proto != getNullSentinel()
        && (!proto || proto == PROTO_NONE
            || proto == getUndefinedSentinel()
            || proto->isBoolean(sctx) || proto->isInteger(sctx)
            || proto->isDouble(sctx) || proto->isFloat(sctx)
            || proto->isString(sctx) || protoIsSymbol)) {
        return getUndefinedSentinel();
    }
    // §B.2.2.1 step 4: dispatch [[SetPrototypeOf]] through Proxy
    // trap if applicable.  Per §10.5.2, the trap returns boolean;
    // false → TypeError; nullptr → fall through to default.
    if (isProxy(sctx, sself)) {
        const proto::ProtoObject* r =
            proxyDispatchSetPrototypeOf(sctx, sself, proto);
        if (hasCallException()) return PROTO_NONE;
        if (r == PROTO_FALSE) {
            signalNativeException(makeNativeError(sctx, "TypeError",
                "Proxy setPrototypeOf returned false"));
            return PROTO_NONE;
        }
        if (r == PROTO_TRUE) return getUndefinedSentinel();
        // r == nullptr means no trap — fall through to default.
    }
    // §10.1.2.1 OrdinarySetPrototypeOf step 2: a non-extensible
    // receiver must reject any rebind to a DIFFERENT prototype.
    // Same-value rebinds remain a no-op (step 4: SameValue(V,
    // current) is true → return true).
    {
        JSContextWrapper* w = JSContextWrapper::current();
        if (w && w->getNonExtensibleMarker()
            && sself->hasParent(sctx, w->getNonExtensibleMarker())) {
            const proto::ProtoObject* curr = nullptr;
            auto it2 = t_jsProtoMap.find(sself);
            if (it2 != t_jsProtoMap.end()) curr = it2->second;
            else                          curr = sself->getPrototype(sctx);
            if (!curr || curr == PROTO_NONE) curr = getNullSentinel();
            if (curr != proto) {
                signalNativeException(makeNativeError(sctx, "TypeError",
                    "Cannot rebind [[Prototype]] of non-extensible object"));
                return PROTO_NONE;
            }
            return getUndefinedSentinel();
        }
    }
    if (proto && proto != getNullSentinel()) {
        const proto::ProtoObject* p = proto;
        while (p && p != getNullSentinel() && p != PROTO_NONE) {
            if (p == sself) {
                signalNativeException(makeNativeError(sctx, "TypeError",
                    "Cyclic __proto__ value"));
                return PROTO_NONE;
            }
            auto it = t_jsProtoMap.find(p);
            if (it != t_jsProtoMap.end()) { p = it->second; continue; }
            p = p->getPrototype(sctx);
        }
    }
    setJSProtoOverride(sctx, sself, proto);
    return getUndefinedSentinel();
}

} // anonymous namespace

// Re-wrap the Object.prototype.__proto__ getter / setter with a
// Function.prototype-parented wrapper so .call / .apply / .bind
// resolve through the chain.  Called from ensureObjectConstructor
// after ensureFunctionPrototype has published the canonical
// Function.prototype as ctx->space->methodPrototype.
const proto::ProtoString* toPropertyKey(proto::ProtoContext* ctx,
                                        const proto::ProtoObject* value) {
    return coercePropNameToKey(ctx, value);
}

void reinstallObjectProtoAccessor(proto::ProtoContext* ctx) {
    if (!ctx || !ctx->space || !ctx->space->objectPrototype) return;
    const proto::ProtoObject* base = ctx->space->objectPrototype;
    auto wrapAccessor = [&](proto::ProtoMethod fn, const char* nm) -> const proto::ProtoObject* {
        const proto::ProtoObject* parent =
            (ctx->space && ctx->space->methodPrototype)
            ? ctx->space->methodPrototype : nullptr;
        const proto::ProtoObject* wrap = parent
            ? parent->newChild(ctx, true) : ctx->newObject(true);
        if (!wrap) return nullptr;
        proto::ProtoObject* mWrap = const_cast<proto::ProtoObject*>(wrap);
        const proto::ProtoObject* raw = ctx->fromMethod(mWrap, fn);
        const proto::ProtoString* nfK = JSSymbols::nativeFn(ctx);
        if (nfK && raw) wrap = wrap->setAttribute(ctx, nfK, raw);
        const proto::ProtoString* lenK = JSSymbols::length(ctx);
        if (lenK) {
            wrap = wrap->setAttribute(ctx, lenK, ctx->fromInteger(0LL));
            const proto::ProtoString* pdlK = JSSymbols::pdLength(ctx);
            if (pdlK) wrap = wrap->setAttribute(ctx, pdlK, ctx->fromInteger(0x2LL));
        }
        const proto::ProtoString* nmK = JSSymbols::name(ctx);
        if (nmK) {
            wrap = wrap->setAttribute(ctx, nmK, ctx->fromUTF8String(nm));
            const proto::ProtoString* pdnK = JSSymbols::pdName(ctx);
            if (pdnK) wrap = wrap->setAttribute(ctx, pdnK, ctx->fromInteger(0x2LL));
        }
        const proto::ProtoString* hnwK = JSSymbols::hasNonWritableProps(ctx);
        if (hnwK) wrap = wrap->setAttribute(ctx, hnwK, PROTO_TRUE);
        return wrap;
    };
    const proto::ProtoString* getK = ctx->fromUTF8String("__get___proto____")->asString(ctx);
    const proto::ProtoString* setK = ctx->fromUTF8String("__set___proto____")->asString(ctx);
    if (getK) base = base->setAttribute(ctx, getK,
        wrapAccessor(protojs::protoAccessorGetter, "get __proto__"));
    if (setK) base = base->setAttribute(ctx, setK,
        wrapAccessor(protojs::protoAccessorSetter, "set __proto__"));
    ctx->space->objectPrototype = const_cast<proto::ProtoObject*>(base);
}

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
    // §B.2.2.1 Object.prototype.__proto__: paired accessor whose getter
    // returns ? OrdinaryGetPrototypeOf(? ToObject(this)) and whose
    // setter forwards to OrdinarySetPrototypeOf. Install via the
    // accessor sidecar (__get_<key>__ / __set_<key>__ + the
    // __has_accessor_props__ hint) so OP_get_field and OP_set_field
    // route through them.
    {
        // The actual getter/setter live as file-scope static functions
        // (`protoAccessorGetter` / `protoAccessorSetter`) so they can be
        // referenced later from `reinstallObjectProtoAccessor()` — called
        // from ensureObjectConstructor after Function.prototype is
        // published as space->methodPrototype.
        proto::ProtoMethod protoGetter = protoAccessorGetter;
        proto::ProtoMethod protoSetter = protoAccessorSetter;
        // Accessor sidecar naming: __get_<propname>__ / __set_<propname>__.
        // The property name is "__proto__", so the sidecar keys are
        // "__get___proto____" / "__set___proto____" (4 trailing
        // underscores: 2 from the propname, 2 from the sidecar pattern).
        // Build the getter/setter as full Function objects (parented at
        // methodPrototype, with name + length descriptors) so
        // getOwnPropertyDescriptor(Object.prototype, "__proto__").get.name
        // resolves to "get __proto__" per §17 spec.  Pre-fix the raw
        // ProtoMethod wrapper carried no name slot, so the test262
        // __proto__/get-fn-name / set-fn-name checks failed.
        auto wrapAccessor = [&](proto::ProtoMethod fn, const char* nm) -> const proto::ProtoObject* {
            const proto::ProtoObject* parent =
                (ctx->space && ctx->space->methodPrototype)
                ? ctx->space->methodPrototype : nullptr;
            const proto::ProtoObject* wrap = parent
                ? parent->newChild(ctx, true) : ctx->newObject(true);
            if (!wrap) return nullptr;
            proto::ProtoObject* mWrap = const_cast<proto::ProtoObject*>(wrap);
            const proto::ProtoObject* raw = ctx->fromMethod(mWrap, fn);
            const proto::ProtoString* nfK = JSSymbols::nativeFn(ctx);
            if (nfK && raw) wrap = wrap->setAttribute(ctx, nfK, raw);
            const proto::ProtoString* lenK = JSSymbols::length(ctx);
            if (lenK) {
                wrap = wrap->setAttribute(ctx, lenK, ctx->fromInteger(0LL));
                const proto::ProtoString* pdlK = JSSymbols::pdLength(ctx);
                if (pdlK) wrap = wrap->setAttribute(ctx, pdlK, ctx->fromInteger(0x2LL));
            }
            const proto::ProtoString* nmK = JSSymbols::name(ctx);
            if (nmK) {
                wrap = wrap->setAttribute(ctx, nmK, ctx->fromUTF8String(nm));
                const proto::ProtoString* pdnK = JSSymbols::pdName(ctx);
                if (pdnK) wrap = wrap->setAttribute(ctx, pdnK, ctx->fromInteger(0x2LL));
            }
            const proto::ProtoString* hnwK = JSSymbols::hasNonWritableProps(ctx);
            if (hnwK) wrap = wrap->setAttribute(ctx, hnwK, PROTO_TRUE);
            return wrap;
        };
        const proto::ProtoObject* getKo = ctx->fromUTF8String("__get___proto____");
        const proto::ProtoString* getK = getKo ? getKo->asString(ctx) : nullptr;
        if (getK) base = base->setAttribute(ctx, getK,
            wrapAccessor(protoGetter, "get __proto__"));
        const proto::ProtoObject* setKo = ctx->fromUTF8String("__set___proto____");
        const proto::ProtoString* setK = setKo ? setKo->asString(ctx) : nullptr;
        if (setK) base = base->setAttribute(ctx, setK,
            wrapAccessor(protoSetter, "set __proto__"));
        const proto::ProtoString* hapK = JSSymbols::hasAccessorProps(ctx);
        if (hapK) base = base->setAttribute(ctx, hapK, PROTO_TRUE);
        // §B.2.2.1: __proto__ is {enumerable:false, configurable:true}
        // — descriptor bits 0x2 (no writable bit on accessor props).
        const proto::ProtoObject* pdo = ctx->fromUTF8String("__pd___proto____");
        const proto::ProtoString* pdk = pdo ? pdo->asString(ctx) : nullptr;
        if (pdk) base = base->setAttribute(ctx, pdk, ctx->fromInteger(0x2LL));
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
    // §20.1.3.{6,7,8}: toLocaleString / toString / valueOf are
    // installed via installNonEnumerableMethod so the wrapping method
    // object carries spec-correct length / name + their non-writable
    // descriptors. Pre-fix the raw `reg` lambda left the method
    // object with default 0 length, empty name and writable
    // descriptors, so propertyHelper checks via
    // built-ins/Object/prototype/toString/length / name / prop-desc
    // failed.
    base = installNonEnumerableMethod(ctx, base, "toLocaleString", objectToLocaleStringFn, 0);
    base = installNonEnumerableMethod(ctx, base, "toString",       objectToString,         0);
    base = installNonEnumerableMethod(ctx, base, "valueOf",        objectValueOf,          0);
    return base;
}

void ensureObjectConstructor(proto::ProtoContext* ctx,
                             const proto::ProtoObject** globalRoot) {
    if (!ctx || !globalRoot || !*globalRoot) return;

    // Re-wrap the Object.prototype.__proto__ getter / setter with a
    // Function.prototype-parented Function object now that
    // ensureFunctionPrototype has published methodPrototype.
    // BootstrapJSPrototypes installed them too early to inherit
    // .call / .apply / .bind.
    reinstallObjectProtoAccessor(ctx);

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
    reg("groupBy",               objectGroupBy,               2);
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
        // SameValue numeric path: both Number (Integer or Double).  NaN
        // == NaN per spec, +0 vs -0 handled above.  Pre-fix only the
        // both-Double branch ran, so Object.is(0, NaN) fell through to
        // a generic compare() that bucketed them equal.
        auto isNumber = [&](const proto::ProtoObject* v) {
            return v && (v->isInteger(ictx) || v->isDouble(ictx) || v->isFloat(ictx));
        };
        if (a && b && isNumber(a) && isNumber(b)) {
            double da = a->isInteger(ictx) ? static_cast<double>(a->asLong(ictx))
                                           : a->asDouble(ictx);
            double db = b->isInteger(ictx) ? static_cast<double>(b->asLong(ictx))
                                           : b->asDouble(ictx);
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
        // §7.2.10 SameValue step 1: if Type(x) ≠ Type(y), return false.
        // protoCore's ProtoObject::compare returns 0 for cross-type pairs
        // that happen to share an internal ordering (e.g. integer 0 and
        // double NaN both surface as the same compare bucket) — pre-fix
        // the final branch trusted that == and reported true.
        auto sameJsType = [&](const proto::ProtoObject* x, const proto::ProtoObject* y) -> bool {
            if (!x || !y) return false;
            // Number type covers Integer / Double / Float.
            bool xNum = x->isInteger(ictx) || x->isDouble(ictx) || x->isFloat(ictx);
            bool yNum = y->isInteger(ictx) || y->isDouble(ictx) || y->isFloat(ictx);
            if (xNum != yNum) return false;
            // Boolean
            bool xBool = (x == PROTO_TRUE || x == PROTO_FALSE || x->isBoolean(ictx));
            bool yBool = (y == PROTO_TRUE || y == PROTO_FALSE || y->isBoolean(ictx));
            if (xBool != yBool) return false;
            // String
            if (x->isString(ictx) != y->isString(ictx)) return false;
            // Null / Object distinction
            bool xNull = (x == getNullSentinel());
            bool yNull = (y == getNullSentinel());
            if (xNull != yNull) return false;
            return true;
        };
        if (a && b && !sameJsType(a, b)) return PROTO_FALSE;
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
        // §6.1.5: Symbols are primitives.  Object(sym) MUST produce a
        // Symbol wrapper distinct from sym itself — the wrapped object's
        // [[SymbolData]] is sym, but the wrapper's Type is Object so
        // \`typeof Object(sym) === \"object\"\` and the wrapper does NOT
        // round-trip through Symbol.keyFor (\xc2\xa720.4.2.5 step 1 throws).
        // Pre-fix Symbol fell through to the bottom \`return val\` arm so
        // Object(Symbol(\"s\")) returned the Symbol primitive itself,
        // \`typeof\` reported \"symbol\", and Symbol.keyFor accepted it
        // (built-ins/Symbol/keyFor/arg-non-symbol.js's wrapped-Symbol
        // arm pinned the divergence).  Wrap explicitly via the
        // primitive-value protocol used by the other primitives.
        const proto::ProtoString* isSymK = JSSymbols::isSymbol(ctx);
        bool valIsSymbol = isSymK && val
            && val->getAttribute(ctx, isSymK, true) == PROTO_TRUE;
        const proto::ProtoObject* wrapProto = nullptr;
        if (ctx->space) {
            if (val == PROTO_TRUE || val == PROTO_FALSE || val->isBoolean(ctx))
                wrapProto = ctx->space->booleanPrototype;
            else if (val->isInteger(ctx) || val->isDouble(ctx) || val->isFloat(ctx))
                wrapProto = ctx->space->doublePrototype;
            else if (val->isString(ctx))
                wrapProto = ctx->space->stringPrototype;
        }
        // Symbol primitives: Object(sym) must produce a Symbol wrapper
        // whose Type is Object so \`typeof Object(sym) === "object"\`
        // and Symbol.keyFor rejects it.  ProtoSpace doesn't expose a
        // symbolPrototype field, so build the wrapper off the live
        // Symbol.prototype resolved through the global object — the
        // same path the Symbol constructor uses internally.
        const proto::ProtoObject* symProtoLocal = nullptr;
        if (valIsSymbol) {
            JSContextWrapper* w = JSContextWrapper::current();
            const proto::ProtoObject* g = w ? w->getNativeGlobal() : nullptr;
            const proto::ProtoString* symKey = ctx->fromUTF8String("Symbol")
                ? ctx->fromUTF8String("Symbol")->asString(ctx) : nullptr;
            const proto::ProtoObject* symCtor = (g && symKey)
                ? g->getAttribute(ctx, symKey, false) : nullptr;
            if (symCtor && symCtor != PROTO_NONE) {
                const proto::ProtoString* pk = JSSymbols::prototype(ctx);
                if (pk) symProtoLocal = symCtor->getAttribute(ctx, pk, false);
            }
        }
        if (wrapProto && wrapProto != PROTO_NONE) {
            const proto::ProtoObject* boxed = wrapProto->newChild(ctx, true);
            if (boxed) {
                const proto::ProtoString* pvKey = JSSymbols::primitiveValue(ctx);
                if (pvKey) boxed = boxed->setAttribute(ctx, pvKey, val);
                return boxed;
            }
        }
        if (valIsSymbol && symProtoLocal && symProtoLocal != PROTO_NONE) {
            const proto::ProtoObject* boxed = symProtoLocal->newChild(ctx, true);
            if (boxed) {
                const proto::ProtoString* pvKey = JSSymbols::primitiveValue(ctx);
                if (pvKey) boxed = boxed->setAttribute(ctx, pvKey, val);
                // Symbol wrappers must NOT report Type Symbol — clear
                // the marker on the wrapper so typeof returns "object"
                // and Symbol.keyFor's Type(sym) check rejects the
                // wrapper at \xc2\xa720.4.2.5 step 1.
                if (isSymK) boxed = boxed->setAttribute(ctx, isSymK, PROTO_FALSE);
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

static thread_local std::unordered_map<std::string,
                                       const proto::ProtoObject*> t_symbolByStrKey;
void registerSymbolByStrKey(const std::string& key, const proto::ProtoObject* sym) {
    t_symbolByStrKey[key] = sym;
}
const proto::ProtoObject* lookupSymbolByStrKey(const std::string& key) {
    auto it = t_symbolByStrKey.find(key);
    return it != t_symbolByStrKey.end() ? it->second : nullptr;
}

} // namespace protojs
