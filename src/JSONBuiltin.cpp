#include "JSONBuiltin.h"
#include "JSSymbols.h"
#include "ArrayElementsStorage.h"
#include "runtime/ProtoInterpreter.h"
#include "JSContext.h"
#include "ProtoNativeModule.h"
#include "TypeBridge.h"
#include <protoCore.h>
#include <cmath>
#include <string>
#include <vector>
#include <sstream>

namespace {
    class ScopedRoot {
    public:
        ScopedRoot(proto::ProtoRootSet* rs, const proto::ProtoObject* obj)
            : rs_(rs), h_((rs && proto::ProtoObject::isCellPointer(obj)) ? rs->add(obj) : proto::ProtoRootSet::kNullHandle) {}
        ~ScopedRoot() { if (rs_ && h_ != proto::ProtoRootSet::kNullHandle) rs_->remove(h_); }
        ScopedRoot(const ScopedRoot&) = delete;
        ScopedRoot& operator=(const ScopedRoot&) = delete;
    private:
        proto::ProtoRootSet* rs_;
        proto::ProtoRootSet::Handle h_;
    };
}
#include <cstdio>

namespace protojs {

namespace {

// Per-call key filter for the replacer-array form of JSON.stringify.
// Thread-local so concurrent stringify calls (worker threads) don't
// clobber each other; populated at entry to JSONBuiltin::stringify
// and cleared on exit.
static thread_local std::vector<std::string>* tlKeyFilter = nullptr;

// Replacer-function form (typeof arg[1] === 'function'): thread-local
// pointer to the JS callable, applied at every property / element
// emission via stringifyRecursive. Same lifecycle as tlKeyFilter.
static thread_local const proto::ProtoObject* tlReplacerFn = nullptr;

static bool keyAllowedByFilter(const std::string& key) {
    if (!tlKeyFilter) return true;
    for (const auto& k : *tlKeyFilter) if (k == key) return true;
    return false;
}

void jsonEscape(const std::string& s, std::string& out) {
    out.push_back('"');
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\n': out += "\\n"; break;
            case '\r': out += "\\r"; break;
            case '\t': out += "\\t"; break;
            case '\b': out += "\\b"; break;
            case '\f': out += "\\f"; break;
            default:
                if (c < 32) {
                    char buf[7];
                    snprintf(buf, sizeof(buf), "\\u%04x", (unsigned int)c);
                    out += buf;
                } else {
                    out.push_back(c);
                }
                break;
        }
    }
    out.push_back('"');
}

void stringifyRecursive(proto::ProtoContext* ctx,
                        const proto::ProtoObject* obj,
                        std::string& out,
                        const proto::ProtoObject* arrayPrototype,
                        std::vector<const proto::ProtoObject*>& stack,
                        proto::ProtoRootSet* rs,
                        const std::string& indentUnit,
                        const std::string& currentIndent) {
    // ECMA-262 §25.5.2 step 10 (SerializeJSONProperty): undefined and
    // function values inside arrays become "null"; inside objects they
    // are dropped (handled at the parent-object emission site below).
    // The undefined sentinel must be caught here too — pre-fix only
    // PROTO_NONE was treated as undefined, so `undefined` literals
    // serialised through the object-fallback path and rendered as "{}".
    if (!obj || obj == PROTO_NONE || obj->isNone(ctx)
        || obj == getUndefinedSentinel()) {
        out += "null";
        return;
    }
    if (obj == getNullSentinel()) {
        out += "null";
        return;
    }
    // Callable JS functions (wrappers carrying __bytecode_id__ or
    // __native_fn__) and raw native methods also serialise as "null"
    // when they appear inside an array. Without this guard a function
    // wrapper would drop into the generic-object branch below and emit
    // its `{name, length, prototype}` shape instead of "null".
    if (obj->isMethod(ctx)) {
        out += "null";
        return;
    }
    {
        const proto::ProtoString* bcKey = JSSymbols::bytecodeId(ctx);
        if (bcKey && obj->getAttribute(ctx, bcKey, false) != PROTO_NONE) {
            out += "null";
            return;
        }
        const proto::ProtoString* nfKey = JSSymbols::nativeFn(ctx);
        if (nfKey && obj->getAttribute(ctx, nfKey, false) != PROTO_NONE) {
            out += "null";
            return;
        }
    }
    if (obj->isBoolean(ctx)) {
        out += obj->asBoolean(ctx) ? "true" : "false";
        return;
    }
    if (obj->isInteger(ctx)) {
        out += std::to_string(obj->asLong(ctx));
        return;
    }
    if (obj->isDouble(ctx)) {
        double d = obj->asDouble(ctx);
        // ECMA-262 §24.5.2 step 10: non-finite numbers serialize as null.
        if (std::isnan(d) || std::isinf(d)) {
            out += "null";
            return;
        }
        // ToString(-0) === "0".
        if (d == 0.0) {
            out += "0";
            return;
        }
        char buf[64];
        snprintf(buf, sizeof(buf), "%.15g", d);
        // %g pads exponents to at least two digits; ECMA-262 forbids
        // the leading zero. Strip it so JSON.stringify(1e-7) emits
        // '1e-7' instead of '1e-07' (matches V8 / SpiderMonkey).
        std::string s = buf;
        size_t ePos = s.find('e');
        if (ePos != std::string::npos && ePos + 1 < s.size()) {
            size_t signPos = (s[ePos + 1] == '+' || s[ePos + 1] == '-')
                ? ePos + 2 : ePos + 1;
            while (signPos < s.size() - 1 && s[signPos] == '0') {
                s.erase(signPos, 1);
            }
        }
        out += s;
        return;
    }
    if (obj->isString(ctx)) {
        std::string s;
        obj->asString(ctx)->toUTF8String(ctx, s);
        jsonEscape(s, out);
        return;
    }

    // Spec §25.5.2 SerializeJSONObject step 2 / SerializeJSONArray
    // step 2: throw TypeError on a cyclic reference. Pre-fix the
    // recursion emitted 'null' for the back-edge, which both silently
    // truncated the user's data and contradicted the spec test that
    // exercises 'TypeError: Converting circular structure to JSON'.
    for (const auto* seen : stack) {
        if (seen == obj) {
            signalNativeException(makeNativeError(ctx, "TypeError",
                "Converting circular structure to JSON"));
            return;
        }
    }

    // ECMA-262 §25.5.2 step 3 (SerializeJSONProperty): if the value
    // has a callable toJSON method, replace the value with its result
    // before further serialisation. Skips arrays whose toJSON would
    // recurse on themselves; primitive-only check above already
    // handled.
    {
        const proto::ProtoObject* tjsObj = ctx->fromUTF8String("toJSON");
        const proto::ProtoString* tjsKey = tjsObj ? tjsObj->asString(ctx) : nullptr;
        if (tjsKey) {
            const proto::ProtoObject* tjsFn = obj->getAttribute(ctx, tjsKey, true);
            if (tjsFn && tjsFn != PROTO_NONE) {
                bool callable = tjsFn->isMethod(ctx);
                if (!callable) {
                    const proto::ProtoString* bcKey = JSSymbols::bytecodeId(ctx);
                    if (bcKey && tjsFn->getAttribute(ctx, bcKey, false) != PROTO_NONE) callable = true;
                    const proto::ProtoString* nfKey = JSSymbols::nativeFn(ctx);
                    if (!callable && nfKey && tjsFn->getAttribute(ctx, nfKey, false) != PROTO_NONE) callable = true;
                }
                if (callable) {
                    const proto::ProtoObject* replaced =
                        callJSFunction(ctx, tjsFn, obj, ctx->newList());
                    if (replaced != obj) {
                        stringifyRecursive(ctx, replaced, out, arrayPrototype, stack, rs, indentUnit, currentIndent);
                        return;
                    }
                }
            }
        }
    }
    // Spec §25.5.2.2 SerializeJSONProperty step 4: unbox Number /
    // String / Boolean wrapper objects to their primitive value
    // before falling through to the primitive serialisation arms.
    // Pre-fix `JSON.stringify(new Number(5))` produced '{}' because
    // the wrapper object was treated as a regular object with no own
    // enumerable props.
    {
        const proto::ProtoObject* pvObj = ctx->fromUTF8String("__primitive_value__");
        const proto::ProtoString* pvKey = pvObj ? pvObj->asString(ctx) : nullptr;
        if (pvKey) {
            const proto::ProtoObject* prim = obj->getAttribute(ctx, pvKey, false);
            if (prim && prim != PROTO_NONE
                && (prim->isString(ctx) || prim->isInteger(ctx)
                    || prim->isDouble(ctx) || prim->isFloat(ctx)
                    || prim->isBoolean(ctx)
                    || prim == PROTO_TRUE || prim == PROTO_FALSE)) {
                // §25.5.2.2 step 4.a: Number wrapper → ToNumber(value),
                // which invokes valueOf; §25.5.2.2 step 4.b: String
                // wrapper → ToString, invoking toString. Pre-fix the
                // wrapper's static __primitive_value__ snapshot was used
                // directly, so `new Number(42)` with an overridden
                // valueOf() (returning 2) still serialised as 42.
                if (prim->isString(ctx)) {
                    const proto::ProtoString* tsK = JSSymbols::toString(ctx);
                    const proto::ProtoObject* fn = tsK
                        ? obj->getAttribute(ctx, tsK, true) : nullptr;
                    if (fn && fn != PROTO_NONE) {
                        const proto::ProtoObject* r =
                            callJSFunction(ctx, fn, obj, ctx->newList());
                        if (hasCallException()) return;
                        if (r && r != PROTO_NONE && r->isString(ctx)) prim = r;
                    }
                } else {
                    const proto::ProtoString* voK = JSSymbols::valueOf(ctx);
                    const proto::ProtoObject* fn = voK
                        ? obj->getAttribute(ctx, voK, true) : nullptr;
                    if (fn && fn != PROTO_NONE) {
                        const proto::ProtoObject* r =
                            callJSFunction(ctx, fn, obj, ctx->newList());
                        if (hasCallException()) return;
                        if (r && r != PROTO_NONE
                            && (r->isInteger(ctx) || r->isDouble(ctx)
                                || r->isFloat(ctx) || r->isBoolean(ctx)
                                || r == PROTO_TRUE || r == PROTO_FALSE)) prim = r;
                    }
                }
                stringifyRecursive(ctx, prim, out, arrayPrototype, stack, rs, indentUnit, currentIndent);
                return;
            }
        }
    }

    stack.push_back(obj);

    // Array check: Fast path via prototype check
    bool isArr = arrayPrototype && obj->hasParent(ctx, arrayPrototype);
    if (!isArr) {
        // Fallback for objects that might have manually set __is_array__
        const proto::ProtoObject* isArrAttr = obj->getAttribute(ctx, JSSymbols::isArray(ctx), false);
        if (isArrAttr && isArrAttr != PROTO_NONE && isArrAttr->asBoolean(ctx)) {
            isArr = true;
        }
    }
    
    // When indent is non-empty: spec §25.5.2 produces
    //   {newline}{nestedIndent}elem,{newline}...{newline}{stepIndent}]
    // Sep after the comma; newline+nestedIndent before each element;
    // colon followed by a single space in object members.
    const bool indenting = !indentUnit.empty();
    const std::string nestedIndent = currentIndent + indentUnit;
    auto emitSep = [&]() {
        out.push_back(',');
        if (indenting) {
            out.push_back('\n');
            out += nestedIndent;
        }
    };
    auto emitOpenLine = [&]() {
        if (indenting) {
            out.push_back('\n');
            out += nestedIndent;
        }
    };
    auto emitCloseLine = [&]() {
        if (indenting) {
            out.push_back('\n');
            out += currentIndent;
        }
    };

    if (isArr) {
        out.push_back('[');
        bool any = false;
        // §25.5.2.4 SerializeJSONArray step 8: invoke the replacer per
        // element with (key:str, value) and holder=array as `this`.
        // Per-element undefined-from-replacer renders as 'null' (vs
        // dropped, like objects). Pre-fix the replacer was bypassed
        // for arrays entirely.
        auto applyReplacer = [&](long long idx, const proto::ProtoObject* v)
            -> const proto::ProtoObject* {
            if (!tlReplacerFn) return v;
            const proto::ProtoList* replArgs = ctx->newList();
            std::string ks = std::to_string(idx);
            replArgs = replArgs->appendLast(ctx, ctx->fromUTF8String(ks.c_str()));
            replArgs = replArgs->appendLast(ctx, v ? v : PROTO_NONE);
            const proto::ProtoObject* r = callJSFunction(ctx, tlReplacerFn, obj, replArgs);
            if (hasCallException()) return PROTO_NONE;
            return r ? r : PROTO_NONE;
        };
        const proto::ProtoList* els = getArrayElements(ctx, obj);
        if (els) {
            ScopedRoot r_els(rs, els->asObject(ctx));
            size_t size = els->getSize(ctx);
            for (size_t i = 0; i < size; ++i) {
                if (i == 0) emitOpenLine(); else emitSep();
                const proto::ProtoObject* v = applyReplacer(static_cast<long long>(i), els->getAt(ctx, static_cast<int>(i)));
                stringifyRecursive(ctx, v, out, arrayPrototype, stack, rs, indentUnit, nestedIndent);
                any = true;
            }
        } else {
            const proto::ProtoString* lenK = JSSymbols::length(ctx);
            const proto::ProtoObject* lenV = lenK ? obj->getAttribute(ctx, lenK, false) : nullptr;
            long long n = 0;
            if (lenV && lenV != PROTO_NONE && lenV->isInteger(ctx)) n = lenV->asLong(ctx);
            for (long long i = 0; i < n; ++i) {
                if (i == 0) emitOpenLine(); else emitSep();
                const proto::ProtoString* ik = JSSymbols::indexKey(ctx, static_cast<uint32_t>(i));
                const proto::ProtoObject* ev = ik ? obj->getAttribute(ctx, ik, false) : nullptr;
                const proto::ProtoObject* v = applyReplacer(i, ev);
                stringifyRecursive(ctx, v, out, arrayPrototype, stack, rs, indentUnit, nestedIndent);
                any = true;
            }
        }
        if (any) emitCloseLine();
        out.push_back(']');
    } else if (obj->isTuple(ctx)) {
        out.push_back('[');
        bool any = false;
        const proto::ProtoTuple* tuple = obj->asTuple(ctx);
        if (tuple) {
            ScopedRoot r_tuple(rs, tuple->asObject(ctx));
            size_t size = tuple->getSize(ctx);
            for (size_t i = 0; i < size; ++i) {
                if (i == 0) emitOpenLine(); else emitSep();
                stringifyRecursive(ctx, tuple->getAt(ctx, static_cast<int>(i)), out, arrayPrototype, stack, rs, indentUnit, nestedIndent);
                any = true;
            }
        }
        if (any) emitCloseLine();
        out.push_back(']');
    } else {
        out.push_back('{');
        bool any = false;
        // Collect own-property names in insertion order, deduplicating
        // and stripping internal sidecars. For each accessor sidecar
        // (__get_X__ / __set_X__) the property name 'X' is added once
        // (its value will be fetched via the getter chain below).
        std::vector<std::string> keysSeen;
        auto pushKey = [&](const std::string& s) {
            for (const auto& e : keysSeen) if (e == s) return;
            keysSeen.push_back(s);
        };
        const proto::ProtoSparseList* attrs = obj->getOwnAttributes(ctx);
        if (attrs) {
            ScopedRoot r_attrs(rs, reinterpret_cast<const proto::ProtoObject*>(attrs));
            const proto::ProtoSparseListIterator* it = attrs->getIterator(ctx);
            while (it && it->hasNext(ctx)) {
                ScopedRoot r_it(rs, reinterpret_cast<const proto::ProtoObject*>(it));
                unsigned long hash = it->nextKey(ctx);
                (void)it->nextValue(ctx);
                std::string key = JSSymbols::getNameFromHash(ctx, hash);
                if (key.empty()) {
                    const proto::ProtoString* sym = reinterpret_cast<const proto::ProtoString*>(hash);
                    if (hash > 0x1000) sym->toUTF8String(ctx, key);
                }
                if (!key.empty()) {
                    // Spec §25.5.2 SerializeJSONObject iterates OWN
                    // enumerable string-keyed properties — including
                    // accessor-backed ones. Surface the underlying
                    // property name by stripping the __get_/__set_
                    // sidecar prefix. Pre-fix, JSON.stringify({get k(){...}})
                    // produced '{}' because the getter's storage key
                    // ('__get_k__') failed the leading-underscore filter
                    // and the actual 'k' was never recorded.
                    if (key.size() > 7 && key.compare(0, 6, "__get_") == 0
                        && key.compare(key.size() - 2, 2, "__") == 0) {
                        pushKey(key.substr(6, key.size() - 8));
                    } else if (key.size() > 7 && key.compare(0, 6, "__set_") == 0
                        && key.compare(key.size() - 2, 2, "__") == 0) {
                        pushKey(key.substr(6, key.size() - 8));
                    } else if (key[0] != '_') {
                        pushKey(key);
                    }
                }
                it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
            }
        }
        // §25.5.2.5 SerializeJSONObject step 5.a: when a PropertyList
        // is supplied, iteration follows ITS order, not the object's
        // own-key insertion order. Pre-fix the filter was only a
        // membership test.
        std::vector<std::string> iterKeys;
        if (tlKeyFilter) {
            for (const auto& k : *tlKeyFilter) {
                bool seen = false;
                for (const auto& e : iterKeys) if (e == k) { seen = true; break; }
                if (!seen) iterKeys.push_back(k);
            }
        } else {
            iterKeys = keysSeen;
        }
        bool first = true;
        for (const auto& key : iterKeys) {
            const proto::ProtoString* propKs =
                ctx->fromUTF8String(key.c_str())->asString(ctx);
            const proto::ProtoObject* val = propKs
                ? obj->getAttribute(ctx, propKs, true) : PROTO_NONE;
            // Accessor lookup: data attribute miss → check __get_<key>__
            // and invoke. Pre-fix accessor-backed properties came back
            // as missing because their storage lives at __get_<key>__,
            // not at <key>. JSON.stringify({get k(){...}}) returned '{}'.
            // Object.defineProperty stores undefinedSentinel under <key>
            // as the accessor-presence marker, so a sentinel value here
            // also means 'go check the getter sidecar'.
            if (!val || val == PROTO_NONE || val == getUndefinedSentinel()) {
                std::string gkStr = "__get_" + key + "__";
                const proto::ProtoString* gks =
                    ctx->fromUTF8String(gkStr.c_str())->asString(ctx);
                if (gks) {
                    const proto::ProtoObject* getter = obj->getAttribute(ctx, gks, true);
                    if (getter && getter != PROTO_NONE) {
                        const proto::ProtoList* getterArgs = ctx->newList();
                        val = callJSFunction(ctx, getter, obj, getterArgs);
                    }
                }
            }
            ScopedRoot r_val(rs, val);
            bool isCallable = false;
            if (val && val != PROTO_NONE) {
                if (val->isMethod(ctx)) isCallable = true;
                else {
                    const proto::ProtoString* bcKey = JSSymbols::bytecodeId(ctx);
                    if (bcKey && val->getAttribute(ctx, bcKey, false) != PROTO_NONE) isCallable = true;
                    else {
                        const proto::ProtoString* nfKey = JSSymbols::nativeFn(ctx);
                        if (nfKey && val->getAttribute(ctx, nfKey, false) != PROTO_NONE) isCallable = true;
                    }
                }
            }
            if (!keyAllowedByFilter(key)) continue;
            // §25.5.2.4 SerializeJSONProperty: the replacer fires for
            // EVERY key in the iteration list, even when [[Get]]
            // returned undefined (e.g. property was deleted during
            // serialisation). The replacer's return value can rescue
            // such a missing entry. Pre-fix we silently skipped any
            // key whose data lookup yielded undefined.
            const proto::ProtoObject* outVal = (val && val != PROTO_NONE)
                ? val : getUndefinedSentinel();
            if (tlReplacerFn) {
                const proto::ProtoList* replArgs = ctx->newList();
                replArgs = replArgs->appendLast(ctx, ctx->fromUTF8String(key.c_str()));
                replArgs = replArgs->appendLast(ctx, outVal);
                outVal = callJSFunction(ctx, tlReplacerFn, obj, replArgs);
                if (hasCallException()) return;
                if (!outVal || outVal == PROTO_NONE
                    || outVal == getUndefinedSentinel()) {
                    continue;
                }
            } else {
                // No replacer: drop the key entirely when value is
                // undefined / a function / a Symbol.
                if (val == PROTO_NONE || !val
                    || val == getUndefinedSentinel() || isCallable) {
                    continue;
                }
            }
            {
                if (first) emitOpenLine(); else emitSep();
                jsonEscape(key, out);
                out.push_back(':');
                if (indenting) out.push_back(' ');
                stringifyRecursive(ctx, outVal, out, arrayPrototype, stack, rs, indentUnit, nestedIndent);
                first = false;
                any = true;
            }
        }
        if (any) emitCloseLine();
        out.push_back('}');
    }
    stack.pop_back();
}

} // anon namespace

const proto::ProtoObject* JSONBuiltin::stringify(proto::ProtoContext* ctx,
                                            const proto::ProtoObject* /*self*/,
                                            const proto::ParentLink* /*parentLink*/,
                                            const proto::ProtoList* args,
                                            const proto::ProtoSparseList* /*kwargs*/) {
    if (!ctx || !args || args->getSize(ctx) == 0) return PROTO_NONE;
    const proto::ProtoObject* val = args->getAt(ctx, 0);

    // ECMA-262 §25.5.2 step 4: if arg[1] is an Array, build the
    // property-keys filter. Strings and numbers in the array are
    // converted to PropertyKey form. Anything else is ignored.
    // The replacer-function form (typeof arg[1] === "function") is
    // not yet supported — fall through (no replacement).
    std::vector<std::string> keyFilter;
    bool hasFilter = false;
    const proto::ProtoObject* replacerFnLocal = nullptr;
    if (args->getSize(ctx) > 1) {
        const proto::ProtoObject* replacer = args->getAt(ctx, 1);
        if (replacer && replacer != PROTO_NONE
            && replacer != getUndefinedSentinel() && replacer != getNullSentinel()) {
            // Replacer-array detection: arrays carry __is_array__ marker.
            // Some real arrays don't (yet) have a __elements__ ProtoList
            // — e.g. \`new Array(3)\` stores its slots as indexed
            // attribute keys plus an explicit length, NOT in
            // __elements__. Pre-fix the replacer scan only handled the
            // ProtoList form, so a length-only sparse replacer fell
            // through and disabled the filter, leaking every key of the
            // value object into the output.
            const proto::ProtoString* isArrKs = JSSymbols::isArray(ctx);
            bool isArr = isArrKs &&
                replacer->getAttribute(ctx, isArrKs, false) == PROTO_TRUE;
            if (isArr) {
                hasFilter = true;
                long len = 0;
                const proto::ProtoString* lenK = JSSymbols::length(ctx);
                if (lenK) {
                    const proto::ProtoObject* lv = replacer->getAttribute(ctx, lenK, false);
                    if (lv && lv != PROTO_NONE && lv->isInteger(ctx))
                        len = lv->asLong(ctx);
                }
                const proto::ProtoList* els = getArrayElements(ctx, replacer);
                long elsSize = els ? static_cast<long>(els->getSize(ctx)) : 0;
                auto fetch = [&](long i) -> const proto::ProtoObject* {
                    const proto::ProtoObject* v = nullptr;
                    if (els && i < elsSize) {
                        v = els->getAt(ctx, static_cast<int>(i));
                    }
                    if (!v || v == PROTO_NONE || v == getUndefinedSentinel()) {
                        const proto::ProtoString* ik = JSSymbols::indexKey(ctx, static_cast<uint32_t>(i));
                        if (ik) {
                            v = replacer->getAttribute(ctx, ik, false);
                        }
                    }
                    // Index accessor: when the data slot holds the
                    // undefined sentinel and __get_<i>__ is present,
                    // invoke the getter. Object.defineProperty on an
                    // array index installs the accessor this way, and
                    // §25.5.2 step 4.a uses [[Get]] which honours it.
                    if (!v || v == PROTO_NONE || v == getUndefinedSentinel()) {
                        std::string gkStr = "__get_" + std::to_string(i) + "__";
                        const proto::ProtoObject* gko = ctx->fromUTF8String(gkStr.c_str());
                        const proto::ProtoString* gks = gko ? gko->asString(ctx) : nullptr;
                        if (gks) {
                            const proto::ProtoObject* getter = replacer->getAttribute(ctx, gks, true);
                            if (getter && getter != PROTO_NONE) {
                                v = callJSFunction(ctx, getter, replacer, ctx->newList());
                                if (hasCallException()) return PROTO_NONE;
                            }
                        }
                    }
                    return v ? v : PROTO_NONE;
                };
                for (long i = 0; i < len; ++i) {
                    const proto::ProtoObject* e = fetch(i);
                    if (!e || e == PROTO_NONE || e == getUndefinedSentinel()
                        || e == getNullSentinel() || e == PROTO_TRUE || e == PROTO_FALSE
                        || (e->isBoolean(ctx))) continue;
                    if (e->isString(ctx)) {
                        std::string s;
                        e->asString(ctx)->toUTF8String(ctx, s);
                        keyFilter.push_back(std::move(s));
                    } else if (e->isInteger(ctx)) {
                        keyFilter.push_back(std::to_string(e->asLong(ctx)));
                    } else if (e->isDouble(ctx) || e->isFloat(ctx)) {
                        double d = e->asDouble(ctx);
                        if (std::isnan(d)) keyFilter.push_back("NaN");
                        else if (std::isinf(d)) keyFilter.push_back(d > 0 ? "Infinity" : "-Infinity");
                        else if (d == 0.0) keyFilter.push_back("0");
                        else {
                            char buf[64];
                            snprintf(buf, sizeof(buf), "%.15g", d);
                            std::string s = buf;
                            size_t ePos = s.find('e');
                            if (ePos != std::string::npos && ePos + 1 < s.size()) {
                                size_t sp = (s[ePos+1] == '+' || s[ePos+1] == '-')
                                    ? ePos + 2 : ePos + 1;
                                while (sp < s.size() - 1 && s[sp] == '0') s.erase(sp, 1);
                            }
                            keyFilter.push_back(std::move(s));
                        }
                    } else {
                        const proto::ProtoString* pvK = JSSymbols::primitiveValue(ctx);
                        const proto::ProtoObject* pv = pvK
                            ? e->getAttribute(ctx, pvK, false) : nullptr;
                        if (!pv || pv == PROTO_NONE) continue;
                        bool wrap = pv->isString(ctx) || pv->isInteger(ctx)
                                    || pv->isDouble(ctx) || pv->isFloat(ctx);
                        if (!wrap) continue;
                        const proto::ProtoString* tsK = JSSymbols::toString(ctx);
                        const proto::ProtoObject* tsFn = tsK
                            ? e->getAttribute(ctx, tsK, true) : nullptr;
                        if (tsFn && tsFn != PROTO_NONE) {
                            const proto::ProtoObject* r =
                                callJSFunction(ctx, tsFn, e, ctx->newList());
                            if (r && r != PROTO_NONE && r->isString(ctx)) {
                                std::string s;
                                r->asString(ctx)->toUTF8String(ctx, s);
                                keyFilter.push_back(std::move(s));
                            }
                        }
                    }
                }
            } else
            // Try array form: read its __elements__ list.
            if (const proto::ProtoList* els = getArrayElements(ctx, replacer)) {
                hasFilter = true;
                size_t sz = els->getSize(ctx);
                for (size_t i = 0; i < sz; ++i) {
                    const proto::ProtoObject* e = els->getAt(ctx, static_cast<int>(i));
                    // §25.5.2 step 4.b.f: undefined entries (including
                    // sparse-array holes that materialise as undefined),
                    // null, booleans, and Symbols are silently ignored.
                    // Only String / Number / String-wrapper / Number-wrapper
                    // produce property-key entries.
                    if (!e || e == PROTO_NONE || e == getUndefinedSentinel()
                        || e == getNullSentinel() || e == PROTO_TRUE || e == PROTO_FALSE
                        || (e->isBoolean(ctx) && !e->isString(ctx))) continue;
                    if (e->isString(ctx)) {
                        std::string s;
                        e->asString(ctx)->toUTF8String(ctx, s);
                        keyFilter.push_back(std::move(s));
                    } else if (e->isInteger(ctx)) {
                        keyFilter.push_back(std::to_string(e->asLong(ctx)));
                    } else if (!e->isInteger(ctx) && !e->isDouble(ctx)
                               && !e->isFloat(ctx) && !e->isString(ctx)
                               && !e->isBoolean(ctx)) {
                        // §25.5.2 step 4.b.e.i: Number-wrapper / String-
                        // wrapper objects in the replacer array are
                        // ToString-coerced via their .toString method.
                        // Other Object types (plain {}, undefined / null
                        // sentinels with surprising parent chains, etc.)
                        // are silently ignored. Restrict the toString
                        // invocation to entries that actually carry a
                        // __primitive_value__ — that's our wrapper marker.
                        const proto::ProtoString* pvK = JSSymbols::primitiveValue(ctx);
                        const proto::ProtoObject* pv = pvK
                            ? e->getAttribute(ctx, pvK, false) : nullptr;
                        if (!pv || pv == PROTO_NONE) continue;
                        bool isNumOrStrWrapper =
                            pv->isString(ctx) || pv->isInteger(ctx)
                            || pv->isDouble(ctx) || pv->isFloat(ctx);
                        if (!isNumOrStrWrapper) continue;
                        const proto::ProtoString* tsK = JSSymbols::toString(ctx);
                        const proto::ProtoObject* tsFn = tsK
                            ? e->getAttribute(ctx, tsK, true) : nullptr;
                        if (tsFn && tsFn != PROTO_NONE) {
                            const proto::ProtoObject* r =
                                callJSFunction(ctx, tsFn, e, ctx->newList());
                            if (r && r != PROTO_NONE && r->isString(ctx)) {
                                std::string s;
                                r->asString(ctx)->toUTF8String(ctx, s);
                                keyFilter.push_back(std::move(s));
                            }
                        }
                    } else if (e->isDouble(ctx) || e->isFloat(ctx)) {
                        // Spec §25.5.2 step 4.d: Number primitives in
                        // the replacer array are ToString-coerced via
                        // Number::toString, so NaN / ±Infinity /
                        // fractional values become valid PropertyKey
                        // forms ('NaN' / '-Infinity' / '0.3'). Pre-fix
                        // only Integers were handled, so doubles and
                        // NaN / Infinity were silently dropped from
                        // the filter and their matching object keys
                        // failed to serialise.
                        double d = e->asDouble(ctx);
                        if (std::isnan(d)) {
                            keyFilter.push_back("NaN");
                        } else if (std::isinf(d)) {
                            keyFilter.push_back(d > 0 ? "Infinity" : "-Infinity");
                        } else if (d == 0.0) {
                            keyFilter.push_back("0");
                        } else {
                            char buf[64];
                            snprintf(buf, sizeof(buf), "%.15g", d);
                            // Strip the leading-zero exponent padding
                            // for consistency with Number.toString.
                            std::string s = buf;
                            size_t ePos = s.find('e');
                            if (ePos != std::string::npos && ePos + 1 < s.size()) {
                                size_t sp = (s[ePos+1] == '+' || s[ePos+1] == '-')
                                    ? ePos + 2 : ePos + 1;
                                while (sp < s.size() - 1 && s[sp] == '0') s.erase(sp, 1);
                            }
                            keyFilter.push_back(std::move(s));
                        }
                    }
                }
            } else {
                // Function form: anything callable goes here. The
                // replacer is invoked at each property emission with
                // (key, value) and its return value REPLACES value;
                // undefined drops the key.
                bool callable = replacer->isMethod(ctx);
                if (!callable) {
                    const proto::ProtoString* bcKey = JSSymbols::bytecodeId(ctx);
                    if (bcKey && replacer->getAttribute(ctx, bcKey, false) != PROTO_NONE) callable = true;
                    const proto::ProtoString* nfKey = JSSymbols::nativeFn(ctx);
                    if (!callable && nfKey && replacer->getAttribute(ctx, nfKey, false) != PROTO_NONE) callable = true;
                }
                if (callable) replacerFnLocal = replacer;
            }
        }
    }
    // Pin the filter so stringifyRecursive's object branch can
    // consult it. RAII guard at the end of the function restores
    // the previous value.
    struct FilterGuard {
        std::vector<std::string>* prev;
        FilterGuard(std::vector<std::string>* v) : prev(tlKeyFilter) { tlKeyFilter = v; }
        ~FilterGuard() { tlKeyFilter = prev; }
    };
    FilterGuard guard(hasFilter ? &keyFilter : nullptr);
    struct ReplacerGuard {
        const proto::ProtoObject* prev;
        ReplacerGuard(const proto::ProtoObject* v) : prev(tlReplacerFn) { tlReplacerFn = v; }
        ~ReplacerGuard() { tlReplacerFn = prev; }
    };
    ReplacerGuard replGuard(replacerFnLocal);

    // ECMA-262 §25.5.2 step 6: derive the indent unit from arg[2].
    // - Number: ToInteger, min(10, n) spaces.
    // - String: first 10 code units, used verbatim.
    // - anything else: no indentation.
    std::string indentUnit;
    if (args->getSize(ctx) > 2) {
        const proto::ProtoObject* space = args->getAt(ctx, 2);
        // §25.5.2 step 5: when space is an Object, unbox a Number /
        // String wrapper (via [[NumberData]] / [[StringData]] internal
        // slot) before applying the step-6 spacing rules. Pre-fix a
        // `new Number(1)` space silently fell through to 'no indent'.
        if (space && space != PROTO_NONE && space != getUndefinedSentinel()) {
            if (!space->isInteger(ctx) && !space->isDouble(ctx)
                && !space->isFloat(ctx) && !space->isString(ctx)
                && !space->isBoolean(ctx)) {
                const proto::ProtoString* pvK = JSSymbols::primitiveValue(ctx);
                const proto::ProtoObject* pv = pvK
                    ? space->getAttribute(ctx, pvK, false) : nullptr;
                if (pv && pv != PROTO_NONE
                    && (pv->isString(ctx) || pv->isInteger(ctx)
                        || pv->isDouble(ctx) || pv->isFloat(ctx))) {
                    // Number wrapper -> ToNumber (valueOf path);
                    // String wrapper -> ToString.
                    if (pv->isString(ctx)) {
                        const proto::ProtoString* tsK = JSSymbols::toString(ctx);
                        const proto::ProtoObject* fn = tsK
                            ? space->getAttribute(ctx, tsK, true) : nullptr;
                        if (fn && fn != PROTO_NONE) {
                            const proto::ProtoObject* r =
                                callJSFunction(ctx, fn, space, ctx->newList());
                            if (hasCallException()) return PROTO_NONE;
                            if (r && r != PROTO_NONE) space = r;
                        }
                    } else {
                        const proto::ProtoString* voK = JSSymbols::valueOf(ctx);
                        const proto::ProtoObject* fn = voK
                            ? space->getAttribute(ctx, voK, true) : nullptr;
                        if (fn && fn != PROTO_NONE) {
                            const proto::ProtoObject* r =
                                callJSFunction(ctx, fn, space, ctx->newList());
                            if (hasCallException()) return PROTO_NONE;
                            if (r && r != PROTO_NONE) space = r;
                        }
                    }
                }
            }
        }
        if (space && space != PROTO_NONE) {
            long long n = -1;
            if (space->isInteger(ctx)) n = space->asLong(ctx);
            else if (space->isDouble(ctx)) {
                double d = space->asDouble(ctx);
                if (!std::isnan(d)) n = static_cast<long long>(d);
            }
            if (n > 0) {
                if (n > 10) n = 10;
                indentUnit.assign(static_cast<size_t>(n), ' ');
            } else if (space->isString(ctx)) {
                const proto::ProtoString* sp = space->asString(ctx);
                if (sp) {
                    std::string s; sp->toUTF8String(ctx, s);
                    // Take at most 10 code units; for ASCII this is just
                    // 10 bytes. Multi-byte UTF-8 still fits since spec
                    // limit is on code units and string indentation
                    // tests in test262 use single-byte tabs/spaces.
                    if (s.size() > 10) s.resize(10);
                    indentUnit = std::move(s);
                }
            }
        }
    }

    JSContextWrapper* wrapper = JSContextWrapper::current();
    const proto::ProtoObject* arrayProto = wrapper ? wrapper->getJSArrayPrototype() : nullptr;

    std::string out;
    std::vector<const proto::ProtoObject*> stack;

    if (!wrapper) wrapper = protojs::JSContextWrapper::current();
    proto::ProtoRootSet* rs = wrapper ? wrapper->getRootSet() : nullptr;

    // ECMA-262 §25.5.2 step 11: if the top-level value would serialise
    // as "undefined" (i.e. it is undefined / a function / a symbol),
    // JSON.stringify returns undefined — NOT the literal string 'null'.
    // Detect this case before invoking stringifyRecursive so the
    // outer return distinguishes between 'value would be null' (top
    // level null becomes the string 'null') and 'value is omitted'.
    if (!val || val == PROTO_NONE || val == getUndefinedSentinel()
        || (val && val->isMethod(ctx))) {
        return getUndefinedSentinel();
    }
    // User functions and constructor wrappers are detected via the
    // __bytecode_id__ / __native_fn__ markers used elsewhere.
    {
        const proto::ProtoString* bcKey = JSSymbols::bytecodeId(ctx);
        if (bcKey && val->getAttribute(ctx, bcKey, false) != PROTO_NONE) {
            return getUndefinedSentinel();
        }
        const proto::ProtoString* nfKey = JSSymbols::nativeFn(ctx);
        if (nfKey && val->getAttribute(ctx, nfKey, false) != PROTO_NONE) {
            return getUndefinedSentinel();
        }
    }

    // §25.5.2 step 12 → SerializeJSONProperty('', wrapper) invokes
    // toJSON first (step 2), then the replacer (step 3). Pre-fix
    // neither was applied at the top level so the wrapper object went
    // straight into stringifyRecursive.
    {
        if (val && val != PROTO_NONE && val != getUndefinedSentinel()
            && val != getNullSentinel() && val != PROTO_TRUE && val != PROTO_FALSE
            && !val->isInteger(ctx) && !val->isDouble(ctx) && !val->isFloat(ctx)
            && !val->isString(ctx) && !val->isBoolean(ctx)) {
            const proto::ProtoObject* tjKo = ctx->fromUTF8String("toJSON");
            const proto::ProtoString* tjKs = tjKo ? tjKo->asString(ctx) : nullptr;
            if (tjKs) {
                const proto::ProtoObject* fn = val->getAttribute(ctx, tjKs, true);
                bool callable = false;
                if (fn && fn != PROTO_NONE) {
                    if (fn->isMethod(ctx)) callable = true;
                    const proto::ProtoString* bcK = JSSymbols::bytecodeId(ctx);
                    if (!callable && bcK && fn->getAttribute(ctx, bcK, false) != PROTO_NONE) callable = true;
                    const proto::ProtoString* nfK = JSSymbols::nativeFn(ctx);
                    if (!callable && nfK && fn->getAttribute(ctx, nfK, false) != PROTO_NONE) callable = true;
                }
                if (callable) {
                    const proto::ProtoList* tjArgs = ctx->newList();
                    tjArgs = tjArgs->appendLast(ctx, ctx->fromUTF8String(""));
                    const proto::ProtoObject* r = callJSFunction(ctx, fn, val, tjArgs);
                    if (hasCallException()) return PROTO_NONE;
                    if (r) val = r;
                }
            }
        }
        // Post-toJSON: if the result is undefined / a function / a
        // symbol, the top-level return is undefined per §25.5.2 step 11.
        // Pre-fix the post-toJSON value fell into stringifyRecursive and
        // arrays.toJSON returning undefined rendered as 'null'.
        if (!val || val == PROTO_NONE || val == getUndefinedSentinel()) {
            return getUndefinedSentinel();
        }
        if (val->isMethod(ctx)) return getUndefinedSentinel();
        {
            const proto::ProtoString* bcKey = JSSymbols::bytecodeId(ctx);
            if (bcKey && val->getAttribute(ctx, bcKey, false) != PROTO_NONE)
                return getUndefinedSentinel();
            const proto::ProtoString* nfKey = JSSymbols::nativeFn(ctx);
            if (nfKey && val->getAttribute(ctx, nfKey, false) != PROTO_NONE)
                return getUndefinedSentinel();
        }
    }
    if (replacerFnLocal) {
        const proto::ProtoList* topArgs = ctx->newList();
        topArgs = topArgs->appendLast(ctx, ctx->fromUTF8String(""));
        topArgs = topArgs->appendLast(ctx, val);
        // Holder is a synthetic { "": val } object per §25.5.2 step 8.
        const proto::ProtoObject* holder = ctx->newObject(true);
        if (holder) {
            const proto::ProtoString* emptyKs =
                ctx->fromUTF8String("")->asString(ctx);
            if (emptyKs) holder = holder->setAttribute(ctx, emptyKs, val);
        }
        const proto::ProtoObject* newVal =
            callJSFunction(ctx, replacerFnLocal, holder ? holder : val, topArgs);
        if (hasCallException()) return PROTO_NONE;
        if (!newVal || newVal == PROTO_NONE
            || newVal == getUndefinedSentinel()) {
            return getUndefinedSentinel();
        }
        // Functions / native fns → undefined at top level.
        if (newVal->isMethod(ctx)) return getUndefinedSentinel();
        {
            const proto::ProtoString* bcKey = JSSymbols::bytecodeId(ctx);
            if (bcKey && newVal->getAttribute(ctx, bcKey, false) != PROTO_NONE)
                return getUndefinedSentinel();
            const proto::ProtoString* nfKey = JSSymbols::nativeFn(ctx);
            if (nfKey && newVal->getAttribute(ctx, nfKey, false) != PROTO_NONE)
                return getUndefinedSentinel();
        }
        val = newVal;
    }
    stringifyRecursive(ctx, val, out, arrayProto, stack, rs, indentUnit, "");
    return ctx->fromUTF8String(out.c_str());
}

// Spec §25.5.1.1 InternalizeJSONProperty — recursively walks the
// parsed structure, calls reviver(key, val) bottom-up, and uses the
// return value to replace each property. Returning undefined deletes
// the property.
//
// protoJS arrays/objects produced by TypeBridge::fromJS are mutable
// children of their prototypes, so setAttribute / setArrayElements
// mutate them in place — which is what the spec needs (the holder
// passed to the reviver must show prior replacements).
static const proto::ProtoObject* internalizeJSONProperty(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* holder,
    const proto::ProtoObject* keyObj,
    const proto::ProtoObject* val,
    const proto::ProtoObject* reviver)
{
    if (val && val != PROTO_NONE
        && val != getNullSentinel() && val != getUndefinedSentinel()
        && !val->isString(ctx) && !val->isInteger(ctx)
        && !val->isDouble(ctx) && !val->isFloat(ctx)
        && !val->isBoolean(ctx)) {
        const proto::ProtoString* isArrKey = JSSymbols::isArray(ctx);
        const proto::ProtoObject* isArr = isArrKey
            ? val->getAttribute(ctx, isArrKey, true) : PROTO_NONE;
        if (isArr == PROTO_TRUE) {
            const proto::ProtoList* els = getArrayElements(ctx, val);
            if (els) {
                long long n = static_cast<long long>(els->getSize(ctx));
                for (long long i = 0; i < n; ++i) {
                    const proto::ProtoObject* item =
                        els->getAt(ctx, static_cast<int>(i));
                    char idxBuf[32];
                    snprintf(idxBuf, sizeof(idxBuf), "%lld", i);
                    const proto::ProtoObject* idxObj =
                        ctx->fromUTF8String(idxBuf);
                    const proto::ProtoObject* newItem =
                        internalizeJSONProperty(
                            ctx, val, idxObj, item, reviver);
                    els = els->setAt(ctx, static_cast<int>(i),
                        newItem ? newItem : getUndefinedSentinel());
                }
                protojs::setArrayElements(ctx, val, els);
            }
        } else {
            // Snapshot keys before mutation — iterator state would
            // otherwise drift when setAttribute rewrites the chain.
            const proto::ProtoSparseList* own = val->getOwnAttributes(ctx);
            std::vector<const proto::ProtoString*> keys;
            const proto::ProtoSparseListIterator* it =
                own ? own->getIterator(ctx) : nullptr;
            while (it && it->hasNext(ctx)) {
                unsigned long rawKey = it->nextKey(ctx);
                it = const_cast<proto::ProtoSparseListIterator*>(it)
                        ->advance(ctx);
                auto* pk =
                    reinterpret_cast<const proto::ProtoString*>(rawKey);
                if (!pk) continue;
                std::string ks;
                pk->toUTF8String(ctx, ks);
                if (ks.compare(0, 2, "__") == 0) continue;
                keys.push_back(pk);
            }
            for (auto* pk : keys) {
                const proto::ProtoObject* item =
                    val->getAttribute(ctx, pk, false);
                const proto::ProtoObject* keyObj2 = pk->asObject(ctx);
                const proto::ProtoObject* newItem =
                    internalizeJSONProperty(
                        ctx, val, keyObj2, item, reviver);
                val = val->setAttribute(ctx, pk,
                    newItem ? newItem : getUndefinedSentinel());
            }
        }
    }
    const proto::ProtoList* callArgs = ctx->newList();
    callArgs = callArgs->appendLast(ctx,
        keyObj ? keyObj : ctx->fromUTF8String(""));
    callArgs = callArgs->appendLast(ctx,
        val ? val : getUndefinedSentinel());
    return callJSFunction(ctx, reviver, holder, callArgs);
}

const proto::ProtoObject* JSONBuiltin::parse(proto::ProtoContext* ctx,
                                        const proto::ProtoObject* /*self*/,
                                        const proto::ParentLink* /*parentLink*/,
                                        const proto::ProtoList* args,
                                        const proto::ProtoSparseList* /*kwargs*/) {
    if (!ctx) return PROTO_NONE;
    // ECMA-262 §25.5.1 step 1: text = ? ToString(text). The ToString of
    // primitives produces real JSON-parseable forms for null / boolean /
    // number, so `JSON.parse(null)` is `JSON.parse("null") === null`,
    // `JSON.parse(3.14)` parses the literal 3.14, etc. Pre-fix only
    // strings were respected — every other primitive collapsed to the
    // literal "undefined" string and threw SyntaxError.
    // Symbol is uncoercible → spec mandates TypeError (see step 1 via
    // ToString algorithm). protoJS doesn't have real Symbol primitives,
    // so the typeof('symbol') check is omitted; the upstream parser
    // would still reject the Symbol's String() output as syntax error.
    std::string text = "undefined";
    if (args && args->getSize(ctx) > 0) {
        const proto::ProtoObject* textObj = args->getAt(ctx, 0);
        if (!textObj || textObj == PROTO_NONE || textObj == getUndefinedSentinel()) {
            text = "undefined";
        } else if (textObj == getNullSentinel()) {
            text = "null";
        } else if (textObj == PROTO_TRUE) {
            text = "true";
        } else if (textObj == PROTO_FALSE) {
            text = "false";
        } else if (textObj->isString(ctx)) {
            textObj->asString(ctx)->toUTF8String(ctx, text);
        } else if (textObj->isBoolean(ctx)) {
            text = textObj->asBoolean(ctx) ? "true" : "false";
        } else if (textObj->isInteger(ctx)) {
            text = std::to_string(textObj->asLong(ctx));
        } else if (textObj->isDouble(ctx) || textObj->isFloat(ctx)) {
            double d = textObj->asDouble(ctx);
            if (std::isnan(d)) text = "NaN";
            else if (std::isinf(d)) text = (d > 0) ? "Infinity" : "-Infinity";
            else {
                long long ll = static_cast<long long>(d);
                if (static_cast<double>(ll) == d) text = std::to_string(ll);
                else {
                    char buf[64];
                    snprintf(buf, sizeof(buf), "%.17g", d);
                    text = buf;
                }
            }
        } else {
            // Object: ToPrimitive(hint:'string') — toString first, then
            // valueOf — followed by ToString on the resulting primitive.
            // Pre-fix the Object branch collapsed to 'undefined' and
            // surfaced as SyntaxError; now an object with toString
            // returning a JSON text parses normally.
            auto isCallable = [&](const proto::ProtoObject* fn) -> bool {
                if (!fn || fn == PROTO_NONE) return false;
                if (fn->isMethod(ctx)) return true;
                const proto::ProtoString* bcK = JSSymbols::bytecodeId(ctx);
                if (bcK && fn->getAttribute(ctx, bcK, false) != PROTO_NONE) return true;
                const proto::ProtoString* nfK = JSSymbols::nativeFn(ctx);
                if (nfK && fn->getAttribute(ctx, nfK, false) != PROTO_NONE) return true;
                return false;
            };
            auto isPrim = [&](const proto::ProtoObject* v) -> bool {
                if (!v || v == PROTO_NONE) return true;
                if (v == getUndefinedSentinel() || v == getNullSentinel()) return true;
                if (v == PROTO_TRUE || v == PROTO_FALSE) return true;
                return v->isInteger(ctx) || v->isDouble(ctx) || v->isFloat(ctx)
                    || v->isString(ctx) || v->isBoolean(ctx);
            };
            const proto::ProtoObject* prim = nullptr;
            // Helper: resolve a method or accessor — for accessor-form
            // descriptors invoke the getter so the returned value is
            // either the function or whatever the getter produces. Pre-
            // fix `get valueOf(){throw}` came back as the undefined
            // placeholder so isCallable was false and the abrupt
            // completion never fired.
            auto resolveMethod = [&](const char* name) -> const proto::ProtoObject* {
                const proto::ProtoObject* ko = ctx->fromUTF8String(name);
                const proto::ProtoString* ks = ko ? ko->asString(ctx) : nullptr;
                if (!ks) return nullptr;
                const proto::ProtoObject* fn = textObj->getAttribute(ctx, ks, true);
                if (!fn || fn == PROTO_NONE || fn == getUndefinedSentinel()) {
                    std::string gkStr = std::string("__get_") + name + "__";
                    const proto::ProtoObject* gko = ctx->fromUTF8String(gkStr.c_str());
                    const proto::ProtoString* gks = gko ? gko->asString(ctx) : nullptr;
                    if (gks) {
                        const proto::ProtoObject* getter = textObj->getAttribute(ctx, gks, true);
                        if (getter && getter != PROTO_NONE) {
                            fn = callJSFunction(ctx, getter, textObj, ctx->newList());
                            if (hasCallException()) return nullptr;
                        }
                    }
                }
                return fn;
            };
            const proto::ProtoObject* tsFn = resolveMethod("toString");
            if (hasCallException()) return PROTO_NONE;
            if (isCallable(tsFn)) {
                const proto::ProtoObject* r = callJSFunction(ctx, tsFn, textObj, ctx->newList());
                if (hasCallException()) return PROTO_NONE;
                if (isPrim(r)) prim = r;
            }
            if (!prim) {
                const proto::ProtoObject* voFn = resolveMethod("valueOf");
                if (hasCallException()) return PROTO_NONE;
                if (isCallable(voFn)) {
                    const proto::ProtoObject* r = callJSFunction(ctx, voFn, textObj, ctx->newList());
                    if (hasCallException()) return PROTO_NONE;
                    if (isPrim(r)) prim = r;
                }
            }
            if (!prim) {
                signalNativeException(makeNativeError(ctx, "TypeError",
                    "JSON.parse: cannot convert object to primitive"));
                return PROTO_NONE;
            }
            if (prim == getUndefinedSentinel() || prim == PROTO_NONE) text = "undefined";
            else if (prim == getNullSentinel()) text = "null";
            else if (prim == PROTO_TRUE) text = "true";
            else if (prim == PROTO_FALSE) text = "false";
            else if (prim->isString(ctx)) {
                prim->asString(ctx)->toUTF8String(ctx, text);
            } else if (prim->isInteger(ctx)) text = std::to_string(prim->asLong(ctx));
            else if (prim->isDouble(ctx) || prim->isFloat(ctx)) {
                double d = prim->asDouble(ctx);
                if (std::isnan(d)) text = "NaN";
                else if (std::isinf(d)) text = (d > 0) ? "Infinity" : "-Infinity";
                else {
                    long long ll = static_cast<long long>(d);
                    if (static_cast<double>(ll) == d) text = std::to_string(ll);
                    else {
                        char buf[64]; snprintf(buf, sizeof(buf), "%.17g", d); text = buf;
                    }
                }
            } else if (prim->isBoolean(ctx)) text = prim->asBoolean(ctx) ? "true" : "false";
            else text = "undefined";
        }
    }

    JSContextWrapper* wrapper = JSContextWrapper::current();
    if (!wrapper) return PROTO_NONE;

    JSContext* qjsCtx = wrapper->getJSContext();
    JSValue jv = JS_ParseJSON(qjsCtx, text.c_str(), text.size(), "JSON.parse");
    if (JS_IsException(jv)) {
        // QuickJS recorded the failure; surface it as SyntaxError per
        // §25.5.1 step 3. Pre-fix this branch returned PROTO_NONE which
        // collapsed bad input to a silent `null`.
        JS_FreeValue(qjsCtx, JS_GetException(qjsCtx));
        signalNativeException(makeNativeError(ctx, "SyntaxError",
            "JSON.parse: unexpected character"));
        return PROTO_NONE;
    }
    const proto::ProtoObject* res = TypeBridge::fromJS(qjsCtx, jv, ctx);
    JS_FreeValue(qjsCtx, jv);

    // Spec §25.5.1 step 7: if reviver is callable, wrap `res` in a
    // synthetic { "": res } holder and run InternalizeJSONProperty.
    // Non-callable reviver is silently ignored (callJSFunction would
    // detect that path; we mirror the spec by skipping when the arg
    // is null/undefined or has no callable shape).
    if (args && args->getSize(ctx) > 1) {
        const proto::ProtoObject* reviver = args->getAt(ctx, 1);
        if (reviver && reviver != PROTO_NONE
            && reviver != getUndefinedSentinel()
            && reviver != getNullSentinel()) {
            const proto::ProtoObject* holder = ctx->newObject(true);
            const proto::ProtoObject* emptyKeyObj =
                ctx->fromUTF8String("");
            const proto::ProtoString* emptyKey =
                emptyKeyObj ? emptyKeyObj->asString(ctx) : nullptr;
            if (emptyKey) {
                holder = holder->setAttribute(ctx, emptyKey,
                    res ? res : getUndefinedSentinel());
            }
            res = internalizeJSONProperty(
                ctx, holder, emptyKeyObj, res, reviver);
        }
    }
    return res;
}

void JSONBuiltin::init(proto::ProtoContext* ctx, const proto::ProtoObject*& globalObj) {
    if (!ctx || !globalObj) return;
    static const NativeEntry entries[] = {
        {"stringify", JSONBuiltin::stringify},
        {"parse",     JSONBuiltin::parse},
        NATIVE_MODULE_END
    };
    const proto::ProtoObject* jsonObj = ProtoNativeModule::buildModule(ctx, entries, 2);
    // Per §25.5.2 / §25.5.1: stringify.length === 3, parse.length === 2.
    // The generic ProtoNativeModule wrapper defaults to 0; patch each
    // method's length on the wrapper object.
    if (jsonObj) {
        const proto::ProtoString* lenKey = JSSymbols::length(ctx);
        auto patchLen = [&](const char* methodName, long long len) {
            const proto::ProtoObject* mo = ctx->fromUTF8String(methodName);
            const proto::ProtoString* mk = mo ? mo->asString(ctx) : nullptr;
            if (!mk) return;
            const proto::ProtoObject* wrapper = jsonObj->getAttribute(ctx, mk, false);
            if (wrapper && wrapper != PROTO_NONE && lenKey) {
                const proto::ProtoObject* updated =
                    wrapper->setAttribute(ctx, lenKey, ctx->fromInteger(len));
                jsonObj = jsonObj->setAttribute(ctx, mk, updated);
            }
        };
        patchLen("stringify", 3);
        patchLen("parse",     2);
        // Symbol.toStringTag = "JSON" per §25.5.4 so
        // Object.prototype.toString.call(JSON) === "[object JSON]".
        // Install under both internal sidecar and user-visible key
        // (see the Map / Set / Promise / Math fixes in this round).
        const proto::ProtoString* tagKey = JSSymbols::toStringTag(ctx);
        if (tagKey) jsonObj = jsonObj->setAttribute(ctx, tagKey,
            ctx->fromUTF8String("JSON"));
        const proto::ProtoString* userKey = JSSymbols::symbolToStringTag(ctx);
        if (userKey) {
            jsonObj = jsonObj->setAttribute(ctx, userKey, ctx->fromUTF8String("JSON"));
            const proto::ProtoObject* pdko = ctx->fromUTF8String("__pd_Symbol.toStringTag__");
            const proto::ProtoString* pdks = pdko ? pdko->asString(ctx) : nullptr;
            if (pdks) jsonObj = jsonObj->setAttribute(ctx, pdks, ctx->fromInteger(0x2LL));
        }
    }
    globalObj = ProtoNativeModule::registerOnGlobal(ctx, globalObj, "JSON", jsonObj);
    // Spec §17: globalThis.JSON's slot is {writable:true, enumerable:false,
    // configurable:true} → 0x3. Pre-fix no sidecar so the default 0x7
    // (full enumerable) leaked JSON into for-in / Object.keys(globalThis).
    {
        const proto::ProtoObject* pdo = ctx->fromUTF8String("__pd_JSON__");
        const proto::ProtoString* pdks = pdo ? pdo->asString(ctx) : nullptr;
        if (pdks && globalObj) globalObj = globalObj->setAttribute(ctx, pdks, ctx->fromInteger(0x3LL));
    }
}

} // namespace protojs
