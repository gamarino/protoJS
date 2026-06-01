#include "JSONBuiltin.h"
#include "JSSymbols.h"
#include "ArrayElementsStorage.h"
#include "runtime/ProtoInterpreter.h"
#include "JSContext.h"
#include "ProtoNativeModule.h"
#include "TypeBridge.h"
#include <protoCore.h>
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
                        proto::ProtoRootSet* rs) {
    if (!obj || obj == PROTO_NONE || obj->isNone(ctx)) {
        out += "null";
        return;
    }
    if (obj == getNullSentinel()) {
        out += "null";
        return;
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
        char buf[64];
        snprintf(buf, sizeof(buf), "%.15g", obj->asDouble(ctx));
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
    
    if (isArr) {
        out.push_back('[');
        const proto::ProtoList* els = getArrayElements(ctx, obj);
        if (els) {
            ScopedRoot r_els(rs, els->asObject(ctx));
            size_t size = els->getSize(ctx);
            for (size_t i = 0; i < size; ++i) {
                if (i > 0) out.push_back(',');
                stringifyRecursive(ctx, els->getAt(ctx, static_cast<int>(i)), out, arrayPrototype, stack, rs);
            }
        }
        out.push_back(']');
    } else if (obj->isTuple(ctx)) {
        out.push_back('[');
        const proto::ProtoTuple* tuple = obj->asTuple(ctx);
        if (tuple) {
            ScopedRoot r_tuple(rs, tuple->asObject(ctx));
            size_t size = tuple->getSize(ctx);
            for (size_t i = 0; i < size; ++i) {
                if (i > 0) out.push_back(',');
                stringifyRecursive(ctx, tuple->getAt(ctx, static_cast<int>(i)), out, arrayPrototype, stack, rs);
            }
        }
        out.push_back(']');
    } else {
        out.push_back('{');
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
                
                // Debug: log progress occasionally
                static int keyCount = 0;
                if (++keyCount % 1000 == 0) {
                    fprintf(stderr, "JSON: stringifying key %s (total %d)\n", key.c_str(), keyCount);
                }

                // Skip internal keys (__ prefix) and functions/none
                if (!key.empty() && key[0] != '_' && val && val != PROTO_NONE && !val->isMethod(ctx)) {
                    if (!first) out.push_back(',');
                    jsonEscape(key, out);
                    out.push_back(':');
                    stringifyRecursive(ctx, val, out, arrayPrototype, stack, rs);
                    first = false;
                }
                it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
            }
        }
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
    
    JSContextWrapper* wrapper = JSContextWrapper::current();
    const proto::ProtoObject* arrayProto = wrapper ? wrapper->getJSArrayPrototype() : nullptr;
    
    std::string out;
    std::vector<const proto::ProtoObject*> stack;
    
    if (!wrapper) wrapper = protojs::JSContextWrapper::current();
    proto::ProtoRootSet* rs = wrapper ? wrapper->getRootSet() : nullptr;
    
    stringifyRecursive(ctx, val, out, arrayProto, stack, rs);
    return ctx->fromUTF8String(out.c_str());
}

const proto::ProtoObject* JSONBuiltin::parse(proto::ProtoContext* ctx,
                                        const proto::ProtoObject* /*self*/,
                                        const proto::ParentLink* /*parentLink*/,
                                        const proto::ProtoList* args,
                                        const proto::ProtoSparseList* /*kwargs*/) {
    if (!ctx || !args || args->getSize(ctx) == 0) return PROTO_NONE;
    const proto::ProtoObject* textObj = args->getAt(ctx, 0);
    if (!textObj || !textObj->isString(ctx)) return PROTO_NONE;
    
    std::string text;
    textObj->asString(ctx)->toUTF8String(ctx, text);
    
    JSContextWrapper* wrapper = JSContextWrapper::current();
    if (!wrapper) return PROTO_NONE;
    
    JSContext* qjsCtx = wrapper->getJSContext();
    JSValue jv = JS_ParseJSON(qjsCtx, text.c_str(), text.size(), "JSON.parse");
    if (JS_IsException(jv)) {
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
    globalObj = ProtoNativeModule::registerOnGlobal(ctx, globalObj, "JSON", jsonObj);
}

} // namespace protojs
