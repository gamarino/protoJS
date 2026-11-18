#include "JsType.h"

#include "../JSSymbols.h"

#include <string>

namespace protojs {

// The sentinels of the running thread (TypeBridge.cpp), read directly as the
// interpreter reads them.
extern thread_local const proto::ProtoObject* t_undefinedSentinel;
extern thread_local const proto::ProtoObject* t_nullSentinel;

namespace {

// Every JavaScript type but the split of protoCore strings into Strings and
// well-known Symbols, which `strings` answers for every string.
JsType classify(proto::ProtoContext* ctx, const proto::ProtoObject* v, JsType strings) {
    if (!v || v == PROTO_NONE || v == t_undefinedSentinel) return JsType::Undefined;
    if (v == t_nullSentinel) return JsType::Null;
    if (proto::isSmallInt(v)) return JsType::Number;
    if (v == PROTO_TRUE || v == PROTO_FALSE) return JsType::Boolean;
    if (proto::ProtoObject::isStringTagFast(v)) return strings;
    if (v->isDouble(ctx) || v->isInteger(ctx)) return JsType::Number;
    // A Symbol carries the marker, on itself or on its prototype (cached
    // Symbols); Symbol.prototype does not.
    const proto::ProtoString* symK = JSSymbols::isSymbol(ctx);
    if (symK && v->getAttribute(ctx, symK, false) == PROTO_TRUE) return JsType::Symbol;
    const proto::ProtoString* bigK = JSSymbols::bigIntValue(ctx);
    if (bigK && v->hasOwnAttribute(ctx, bigK) == PROTO_TRUE) return JsType::BigInt;
    return JsType::Object;
}

}  // namespace

JsType jsTypeOf(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    const JsType t = classify(ctx, v, JsType::String);
    if (t != JsType::String) return t;
    std::string s;
    v->asString(ctx)->toUTF8String(ctx, s);
    return (s.compare(0, 7, "Symbol.") == 0 || s.compare(0, 7, "Symbol(") == 0) ? JsType::Symbol
                                                                              : JsType::String;
}

bool jsIsObject(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    return classify(ctx, v, JsType::String) == JsType::Object;
}

const char* jsTypeName(JsType t) {
    switch (t) {
        case JsType::Undefined: return "undefined";
        case JsType::Null: return "object";
        case JsType::Boolean: return "boolean";
        case JsType::Number: return "number";
        case JsType::String: return "string";
        case JsType::Symbol: return "symbol";
        case JsType::BigInt: return "bigint";
        case JsType::Object: return "object";
    }
    return "object";
}

}  // namespace protojs
