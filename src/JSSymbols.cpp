#include "JSSymbols.h"
#include <mutex>
#include <string>
#include <array>

namespace protojs::JSSymbols {

// Helper macro: define a lazy getter that interns "literal" on first call.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
#define DEFINE_SYMBOL(getter, literal)                                      \
    const proto::ProtoString* getter(proto::ProtoContext* ctx) {            \
        static const proto::ProtoString* s_sym = nullptr;                   \
        static std::once_flag s_flag;                                        \
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

// ---- Well-known JS protocol symbols -------------------------------------
DEFINE_SYMBOL(symbolMatch,   "Symbol.match")
DEFINE_SYMBOL(symbolReplace, "Symbol.replace")
DEFINE_SYMBOL(symbolSearch,  "Symbol.search")
DEFINE_SYMBOL(symbolSplit,   "Symbol.split")

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

} // namespace protojs::JSSymbols
