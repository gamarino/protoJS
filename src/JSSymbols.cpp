#include "JSSymbols.h"
#include <mutex>
#include <string>
#include <array>
#include <unordered_map>

namespace protojs::JSSymbols {

// Helper macro: define a lazy getter that interns "literal" on first call.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define DEFINE_SYMBOL(getter, literal)                                      \
    const proto::ProtoString* getter(proto::ProtoContext* ctx) {            \
        static const proto::ProtoString* s_sym = nullptr;                   \
        static std::once_flag s_flag;                                        \
        /* Any valid ctx suffices: createSymbol interns at ProtoSpace level, \
         * so the result is globally unique regardless of which ctx wins.  */ \
        std::call_once(s_flag, [ctx]() {                                    \
            s_sym = proto::ProtoString::createSymbol(ctx, literal);         \
        });                                                                  \
        return s_sym;                                                        \
    }

// ---- JS global constructor names ----------------------------------------
DEFINE_SYMBOL(Array,       "Array")
DEFINE_SYMBOL(Function,    "Function")
DEFINE_SYMBOL(Math,        "Math")
DEFINE_SYMBOL(Number,      "Number")
DEFINE_SYMBOL(Object,      "Object")
DEFINE_SYMBOL(RegExp,      "RegExp")
DEFINE_SYMBOL(String,      "String")

// ---- Common JS property names -------------------------------------------
DEFINE_SYMBOL(apply,            "apply")
DEFINE_SYMBOL(bind,             "bind")
DEFINE_SYMBOL(call,             "call")
DEFINE_SYMBOL(constructor,      "constructor")
DEFINE_SYMBOL(createdTimestamp, "createdTimestamp")
DEFINE_SYMBOL(description,      "description")
DEFINE_SYMBOL(done,             "done")
DEFINE_SYMBOL(dotAll,           "dotAll")
DEFINE_SYMBOL(elements,         "elements")
DEFINE_SYMBOL(exports,          "exports")
DEFINE_SYMBOL(flags,            "flags")
DEFINE_SYMBOL(global,           "global")
DEFINE_SYMBOL(ignoreCase,       "ignoreCase")
DEFINE_SYMBOL(index,            "index")
DEFINE_SYMBOL(input,            "input")
DEFINE_SYMBOL(isRoot,           "isRoot")
DEFINE_SYMBOL(isWeakRef,        "isWeakRef")
DEFINE_SYMBOL(jsValueTag,       "jsValueTag")
DEFINE_SYMBOL(lastIndex,        "lastIndex")
DEFINE_SYMBOL(length,           "length")
DEFINE_SYMBOL(message,          "message")
DEFINE_SYMBOL(multiline,        "multiline")
DEFINE_SYMBOL(name,             "name")
DEFINE_SYMBOL(next,             "next")
DEFINE_SYMBOL(prototype,        "prototype")
DEFINE_SYMBOL(protoObj,         "protoObj")
DEFINE_SYMBOL(require,          "require")
DEFINE_SYMBOL(stringProtoBuild, "__string_proto_built__")
DEFINE_SYMBOL(source,           "source")
DEFINE_SYMBOL(sticky,           "sticky")
DEFINE_SYMBOL(toExponential,    "toExponential")
DEFINE_SYMBOL(toFixed,          "toFixed")
DEFINE_SYMBOL(toPrecision,      "toPrecision")
DEFINE_SYMBOL(toString,         "toString")
DEFINE_SYMBOL(unicode,          "unicode")
DEFINE_SYMBOL(value,            "value")
DEFINE_SYMBOL(valueOf,          "valueOf")
DEFINE_SYMBOL(values,           "values")
DEFINE_SYMBOL(groups,           "groups")
DEFINE_SYMBOL(hasIndices,       "hasIndices")
DEFINE_SYMBOL(indices,          "indices")

// ---- Internal implementation keys ---------------------------------------
DEFINE_SYMBOL(arrayCtor,        "__array_ctor__")
DEFINE_SYMBOL(arrayProto,       "__array_proto__")
DEFINE_SYMBOL(boundArgs,        "__bound_args__")
DEFINE_SYMBOL(boundFn,          "__bound_fn__")
DEFINE_SYMBOL(boundThis,        "__bound_this__")
DEFINE_SYMBOL(bytecodeId,       "__bytecode_id__")
DEFINE_SYMBOL(errorCtor,        "__error_ctor__")
DEFINE_SYMBOL(externalPtrField, "_externalPtr")
DEFINE_SYMBOL(functionProto,    "__function_proto__")
DEFINE_SYMBOL(iterArr,          "__iter_arr__")
DEFINE_SYMBOL(iterIdx,          "__iter_idx__")
DEFINE_SYMBOL(iterKind,         "__iter_kind__")
DEFINE_SYMBOL(iterSlot,         "__iter_slot__")
DEFINE_SYMBOL(jsValuePtrField,  "_jsValuePtr")
DEFINE_SYMBOL(jsValueTagField,  "_jsValueTag")
DEFINE_SYMBOL(reBytecode,       "__re_bytecode__")
DEFINE_SYMBOL(regexpCtor,       "__regexp_ctor__")
DEFINE_SYMBOL(iterDone,         "__iter_done__")
DEFINE_SYMBOL(iterRe,           "__iter_re__")
DEFINE_SYMBOL(iterStr,          "__iter_str__")

// ---- Well-known JS protocol symbols -------------------------------------
DEFINE_SYMBOL(symbolMatch,   "Symbol.match")
DEFINE_SYMBOL(symbolReplace, "Symbol.replace")
DEFINE_SYMBOL(symbolSearch,  "Symbol.search")
DEFINE_SYMBOL(symbolSplit,   "Symbol.split")
DEFINE_SYMBOL(symbolMatchAll,   "Symbol.matchAll")

#undef DEFINE_SYMBOL

// ---- Numeric index symbols ----------------------------------------------
namespace {
    constexpr uint32_t kIndexCacheSize = 256;
    std::array<const proto::ProtoString*, kIndexCacheSize> s_indexCache{};
    std::array<std::once_flag, kIndexCacheSize> s_indexFlags{};
}

const proto::ProtoString* indexKey(proto::ProtoContext* ctx, uint32_t i) {
    if (i < kIndexCacheSize) {
        std::call_once(s_indexFlags[i], [ctx, i]() {
            s_indexCache[i] = proto::ProtoString::createSymbol(ctx, std::to_string(i).c_str());
        });
        return s_indexCache[i];
    }
    return proto::ProtoString::createSymbol(ctx, std::to_string(i).c_str());
}

std::string getNameFromHash(proto::ProtoContext* ctx, unsigned long hash) {
    // Build a static hash→name map once, covering all known JSSymbols keys.
    // Any ctx suffices — createSymbol interns at ProtoSpace level.
    static std::unordered_map<unsigned long, std::string> s_map;
    static std::once_flag s_mapFlag;
    std::call_once(s_mapFlag, [ctx]() {
#define REGISTER(getter, literal) \
        s_map[getter(ctx)->getHash(ctx)] = literal;
        REGISTER(Array,           "Array")
        REGISTER(Function,        "Function")
        REGISTER(Math,            "Math")
        REGISTER(Number,          "Number")
        REGISTER(Object,          "Object")
        REGISTER(RegExp,          "RegExp")
        REGISTER(String,          "String")
        REGISTER(constructor,     "constructor")
        REGISTER(createdTimestamp,"createdTimestamp")
        REGISTER(description,     "description")
        REGISTER(done,            "done")
        REGISTER(dotAll,          "dotAll")
        REGISTER(elements,        "elements")
        REGISTER(exports,         "exports")
        REGISTER(flags,           "flags")
        REGISTER(global,          "global")
        REGISTER(ignoreCase,      "ignoreCase")
        REGISTER(index,           "index")
        REGISTER(input,           "input")
        REGISTER(isRoot,          "isRoot")
        REGISTER(isWeakRef,       "isWeakRef")
        REGISTER(jsValueTag,      "jsValueTag")
        REGISTER(lastIndex,       "lastIndex")
        REGISTER(length,          "length")
        REGISTER(message,         "message")
        REGISTER(multiline,       "multiline")
        REGISTER(name,            "name")
        REGISTER(next,            "next")
        REGISTER(prototype,       "prototype")
        REGISTER(protoObj,        "protoObj")
        REGISTER(require,         "require")
        REGISTER(source,          "source")
        REGISTER(sticky,          "sticky")
        REGISTER(toExponential,   "toExponential")
        REGISTER(toFixed,         "toFixed")
        REGISTER(toPrecision,     "toPrecision")
        REGISTER(toString,        "toString")
        REGISTER(unicode,         "unicode")
        REGISTER(value,           "value")
        REGISTER(valueOf,         "valueOf")
        REGISTER(values,          "values")
        REGISTER(apply,           "apply")
        REGISTER(bind,            "bind")
        REGISTER(call,            "call")
        REGISTER(arrayCtor,       "__array_ctor__")
        REGISTER(arrayProto,      "__array_proto__")
        REGISTER(boundArgs,       "__bound_args__")
        REGISTER(boundFn,         "__bound_fn__")
        REGISTER(boundThis,       "__bound_this__")
        REGISTER(bytecodeId,      "__bytecode_id__")
        REGISTER(errorCtor,       "__error_ctor__")
        REGISTER(externalPtrField,"_externalPtr")
        REGISTER(functionProto,   "__function_proto__")
        REGISTER(iterArr,         "__iter_arr__")
        REGISTER(iterIdx,         "__iter_idx__")
        REGISTER(iterKind,        "__iter_kind__")
        REGISTER(iterSlot,        "__iter_slot__")
        REGISTER(jsValuePtrField, "_jsValuePtr")
        REGISTER(jsValueTagField, "_jsValueTag")
        REGISTER(reBytecode,      "__re_bytecode__")
        REGISTER(regexpCtor,      "__regexp_ctor__")
        REGISTER(symbolMatch,     "Symbol.match")
        REGISTER(symbolReplace,   "Symbol.replace")
        REGISTER(symbolSearch,    "Symbol.search")
        REGISTER(symbolSplit,     "Symbol.split")
        REGISTER(groups,        "groups")
        REGISTER(hasIndices,    "hasIndices")
        REGISTER(indices,       "indices")
        REGISTER(symbolMatchAll,"Symbol.matchAll")
        REGISTER(iterDone,      "__iter_done__")
        REGISTER(iterRe,        "__iter_re__")
        REGISTER(iterStr,       "__iter_str__")
#undef REGISTER
    });
    auto it = s_map.find(hash);
    return it != s_map.end() ? it->second : std::string{};
}

} // namespace protojs::JSSymbols
