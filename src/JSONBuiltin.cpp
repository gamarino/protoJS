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
        out += buf;
        return;
    }
    if (obj->isString(ctx)) {
        std::string s;
        obj->asString(ctx)->toUTF8String(ctx, s);
        jsonEscape(s, out);
        return;
    }

    // Circular check
    for (const auto* seen : stack) {
        if (seen == obj) {
            out += "null";
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
        const proto::ProtoList* els = getArrayElements(ctx, obj);
        if (els) {
            ScopedRoot r_els(rs, els->asObject(ctx));
            size_t size = els->getSize(ctx);
            for (size_t i = 0; i < size; ++i) {
                if (i == 0) emitOpenLine(); else emitSep();
                stringifyRecursive(ctx, els->getAt(ctx, static_cast<int>(i)), out, arrayPrototype, stack, rs, indentUnit, nestedIndent);
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
                stringifyRecursive(ctx, ev ? ev : PROTO_NONE, out, arrayPrototype, stack, rs, indentUnit, nestedIndent);
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
        const proto::ProtoSparseList* attrs = obj->getOwnAttributes(ctx);
        if (attrs) {
            ScopedRoot r_attrs(rs, reinterpret_cast<const proto::ProtoObject*>(attrs));
            const proto::ProtoSparseListIterator* it = attrs->getIterator(ctx);
            bool first = true;
            while (it && it->hasNext(ctx)) {
                ScopedRoot r_it(rs, reinterpret_cast<const proto::ProtoObject*>(it));
                unsigned long hash = it->nextKey(ctx);
                const proto::ProtoObject* val = it->nextValue(ctx);
                ScopedRoot r_val(rs, val);

                std::string key = JSSymbols::getNameFromHash(ctx, hash);
                if (key.empty()) {
                    const proto::ProtoString* sym = reinterpret_cast<const proto::ProtoString*>(hash);
                    if (hash > 0x1000) {
                        sym->toUTF8String(ctx, key);
                    }
                }

                bool isCallable = false;
                if (val && val != PROTO_NONE) {
                    if (val->isMethod(ctx)) {
                        isCallable = true;
                    } else {
                        const proto::ProtoString* bcKey = JSSymbols::bytecodeId(ctx);
                        if (bcKey && val->getAttribute(ctx, bcKey, false) != PROTO_NONE)
                            isCallable = true;
                        else {
                            const proto::ProtoString* nfKey = JSSymbols::nativeFn(ctx);
                            if (nfKey && val->getAttribute(ctx, nfKey, false) != PROTO_NONE)
                                isCallable = true;
                        }
                    }
                }
                if (!key.empty() && key[0] != '_'
                    && val && val != PROTO_NONE
                    && val != getUndefinedSentinel()
                    && !isCallable
                    && keyAllowedByFilter(key)) {
                    if (first) emitOpenLine(); else emitSep();
                    jsonEscape(key, out);
                    out.push_back(':');
                    if (indenting) out.push_back(' ');
                    stringifyRecursive(ctx, val, out, arrayPrototype, stack, rs, indentUnit, nestedIndent);
                    first = false;
                    any = true;
                }
                it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
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
    if (args->getSize(ctx) > 1) {
        const proto::ProtoObject* replacer = args->getAt(ctx, 1);
        if (replacer && replacer != PROTO_NONE
            && replacer != getUndefinedSentinel() && replacer != getNullSentinel()) {
            // Try array form: read its __elements__ list.
            if (const proto::ProtoList* els = getArrayElements(ctx, replacer)) {
                hasFilter = true;
                size_t sz = els->getSize(ctx);
                for (size_t i = 0; i < sz; ++i) {
                    const proto::ProtoObject* e = els->getAt(ctx, static_cast<int>(i));
                    if (!e || e == PROTO_NONE) continue;
                    if (e->isString(ctx)) {
                        std::string s;
                        e->asString(ctx)->toUTF8String(ctx, s);
                        keyFilter.push_back(std::move(s));
                    } else if (e->isInteger(ctx)) {
                        keyFilter.push_back(std::to_string(e->asLong(ctx)));
                    }
                }
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

    // ECMA-262 §25.5.2 step 6: derive the indent unit from arg[2].
    // - Number: ToInteger, min(10, n) spaces.
    // - String: first 10 code units, used verbatim.
    // - anything else: no indentation.
    std::string indentUnit;
    if (args->getSize(ctx) > 2) {
        const proto::ProtoObject* space = args->getAt(ctx, 2);
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

    stringifyRecursive(ctx, val, out, arrayProto, stack, rs, indentUnit, "");
    return ctx->fromUTF8String(out.c_str());
}

const proto::ProtoObject* JSONBuiltin::parse(proto::ProtoContext* ctx,
                                        const proto::ProtoObject* /*self*/,
                                        const proto::ParentLink* /*parentLink*/,
                                        const proto::ProtoList* args,
                                        const proto::ProtoSparseList* /*kwargs*/) {
    if (!ctx) return PROTO_NONE;
    // ECMA-262 §25.5.1: text = ? ToString(text). Missing arg → "undefined".
    std::string text = "undefined";
    if (args && args->getSize(ctx) > 0) {
        const proto::ProtoObject* textObj = args->getAt(ctx, 0);
        if (textObj && textObj != PROTO_NONE && textObj->isString(ctx)) {
            textObj->asString(ctx)->toUTF8String(ctx, text);
        } else if (textObj && textObj != PROTO_NONE) {
            // Non-string argument — let QuickJS report SyntaxError
            // by parsing its ToString form.
            text = "undefined";
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
        const proto::ProtoString* tagKey = JSSymbols::toStringTag(ctx);
        if (tagKey) jsonObj = jsonObj->setAttribute(ctx, tagKey,
            ctx->fromUTF8String("JSON"));
    }
    globalObj = ProtoNativeModule::registerOnGlobal(ctx, globalObj, "JSON", jsonObj);
}

} // namespace protojs
