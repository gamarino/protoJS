#include "ProtoCoreTypes.h"
#include "JSSymbols.h"
#include "runtime/PropertyDictionary.h"
#include "runtime/JSStringBytes.h"
#include <atomic>
#include <cstdint>
#include <cstring>
#include <mutex>
#include <string>
#include <array>

namespace protojs::JSSymbols {

// Helper macro: define a lazy getter that interns "literal" on first call.
// NOLINTNEXTLINE(cppcoreguidelines-macro-usage)
/* Magic-static (C++11 thread-safe static initialisation) instead of
 * std::call_once + lambda.  The std::call_once version compiled to a
 * non-inlined function with a lambda capture and an atomic load every
 * call; perf annotate on object_read_only's OP_get_array_el path
 * showed `JSSymbols::hasAccessorProps` at ~0.8 % CPU on its own and
 * the JSSymbols family adding up to ~3 %.
 * The compiler can fully inline a function whose body is `static const
 * Y v = expr; return v;` — the fast path after init is a single
 * guard-byte test + load, optimised down to a couple of cycles.  Any
 * valid ctx suffices: createSymbol interns at ProtoSpace level, so
 * the result is globally unique regardless of which ctx wins the
 * initialisation race. */
// Every name defined here is also program vocabulary for the property
// dictionary (runtime/PropertyDictionary.h): a computed key with one of these
// spellings stays a named attribute.
static const proto::ProtoString* internVocabulary(proto::ProtoContext* ctx, const char* literal) {
    const proto::ProtoString* sym = proto::ProtoString::createSymbol(ctx, literal);
    dict::noteVocabulary(literal, std::strlen(literal));
    return sym;
}

#define DEFINE_SYMBOL(getter, literal)                                      \
    const proto::ProtoString* getter(proto::ProtoContext* ctx) {            \
        static const proto::ProtoString* const s_sym =                      \
            internVocabulary(ctx, literal);                                  \
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
DEFINE_SYMBOL(isConcatSpreadable,    "Symbol.isConcatSpreadable")
DEFINE_SYMBOL(getIsConcatSpreadable, "__get_Symbol.isConcatSpreadable__")
DEFINE_SYMBOL(getSymbolSpecies, "__get_Symbol.species__")
DEFINE_SYMBOL(pdSymbolIterator, "__pd_Symbol.iterator__")

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
DEFINE_SYMBOL(isArray,          "__is_array__")
DEFINE_SYMBOL(stringCtor,       "__string_ctor__")
DEFINE_SYMBOL(primitiveValue,   "__primitive_value__")
DEFINE_SYMBOL(arrowThis,        "__arrow_this__")
DEFINE_SYMBOL(closureModule,    "__closure_module__")
DEFINE_SYMBOL(evalCacheEntries, "__eval_cache__")
DEFINE_SYMBOL(nativeFn,         "__native_fn__")
DEFINE_SYMBOL(arrayElements,    "__elements__")
DEFINE_SYMBOL(argCount,         "__arg_count__")
DEFINE_SYMBOL(varCount,         "__var_count__")
DEFINE_SYMBOL(stackSize,        "__stack_size__")
DEFINE_SYMBOL(isStrict,         "__is_strict__")
DEFINE_SYMBOL(isArrow,          "__is_arrow__")
DEFINE_SYMBOL(isAsync,          "__is_async__")
DEFINE_SYMBOL(isGenerator,      "__is_generator__")
DEFINE_SYMBOL(isClassCtor,      "__is_class_ctor__")
DEFINE_SYMBOL(constantPool,     "__cpool__")
DEFINE_SYMBOL(closureSymbols,   "__closure_symbols__")
DEFINE_SYMBOL(parameterNames,   "__parameter_names__")
DEFINE_SYMBOL(bytecode,         "__bytecode__")
DEFINE_SYMBOL(metadata,         "__metadata__")
DEFINE_SYMBOL(sourceText,       "__source_text__")

// Recurrent interpreter sentinels (added 2026-05-31).
DEFINE_SYMBOL(construct,        "__construct__")
DEFINE_SYMBOL(genSent,          "__gen_sent__")
DEFINE_SYMBOL(genThrowVal,      "__gen_throw_val__")
DEFINE_SYMBOL(pdPrototype,      "__pd_prototype__")
DEFINE_SYMBOL(jsNullSentinel,   "__js_null_sentinel__")
DEFINE_SYMBOL(jsTdzSentinel,    "__js_tdz_sentinel__")
DEFINE_SYMBOL(isConstructor,    "__is_constructor__")

// Property-descriptor writability sentinels (added 2026-06-06).
DEFINE_SYMBOL(pdLength,         "__pd_length__")
DEFINE_SYMBOL(pdName,           "__pd_name__")
DEFINE_SYMBOL(pdConstructor,    "__pd_constructor__")
DEFINE_SYMBOL(pdMessage,        "__pd_message__")
DEFINE_SYMBOL(pdSize,           "__pd_size__")
DEFINE_SYMBOL(isSymbol,         "__is_symbol__")
DEFINE_SYMBOL(fieldsInit,       "__fields_init__")
DEFINE_SYMBOL(isBigInt,         "__is_bigint__")
DEFINE_SYMBOL(bigIntValue,      "__bigint_value__")
DEFINE_SYMBOL(hasIndexedSetters,"__has_indexed_setters__")
DEFINE_SYMBOL(hasIndexedAccessors,"__has_indexed_accessors__")
DEFINE_SYMBOL(hasAccessorsBeyondProto,"__has_accessors_beyond_proto__")
DEFINE_SYMBOL(hasIndexDescriptors,"__has_index_descriptors__")
DEFINE_SYMBOL(hasAccessorProps, "__has_accessor_props__")
DEFINE_SYMBOL(hasNonWritableProps,"__has_nonwritable_props__")
DEFINE_SYMBOL(integrity,        "__integrity__")
DEFINE_SYMBOL(propsDict,        "__props_dict__")
DEFINE_SYMBOL(protoOverride,    "__js_proto_override__")
DEFINE_SYMBOL(lazyPrototype,    "__lazy_prototype__")
DEFINE_SYMBOL(getLength,        "__get_length__")
DEFINE_SYMBOL(setLength,        "__set_length__")
DEFINE_SYMBOL(toJSON,           "toJSON")
DEFINE_SYMBOL(getToJSON,        "__get_toJSON__")

// ---- TypedArray / ArrayBuffer / DataView internal keys ------------------
DEFINE_SYMBOL(abData,        "__ab_data__")
DEFINE_SYMBOL(abDetached,    "__ab_detached__")
DEFINE_SYMBOL(taBuffer,      "__ta_buffer__")
DEFINE_SYMBOL(taElementType, "__ta_element_type__")
DEFINE_SYMBOL(taByteOffset,  "__ta_byte_offset__")
DEFINE_SYMBOL(taCtor,        "__typed_array_ctor__")
DEFINE_SYMBOL(dvBuffer,      "__dv_buffer__")
DEFINE_SYMBOL(dvByteOffset,  "__dv_byte_offset__")
DEFINE_SYMBOL(dvByteLength,  "__dv_byte_length__")

// ---- TypedArray / ArrayBuffer / DataView JS property names -------------
DEFINE_SYMBOL(ArrayBuffer,         "ArrayBuffer")
DEFINE_SYMBOL(DataView,            "DataView")
DEFINE_SYMBOL(Int8Array,           "Int8Array")
DEFINE_SYMBOL(Uint8Array,          "Uint8Array")
DEFINE_SYMBOL(Uint8ClampedArray,   "Uint8ClampedArray")
DEFINE_SYMBOL(Int16Array,          "Int16Array")
DEFINE_SYMBOL(Uint16Array,         "Uint16Array")
DEFINE_SYMBOL(Int32Array,          "Int32Array")
DEFINE_SYMBOL(Uint32Array,         "Uint32Array")
DEFINE_SYMBOL(Float32Array,        "Float32Array")
DEFINE_SYMBOL(Float64Array,        "Float64Array")
DEFINE_SYMBOL(BigInt64Array,       "BigInt64Array")
DEFINE_SYMBOL(BigUint64Array,      "BigUint64Array")
DEFINE_SYMBOL(byteLength,          "byteLength")
DEFINE_SYMBOL(byteOffset,          "byteOffset")
DEFINE_SYMBOL(buffer,              "buffer")
DEFINE_SYMBOL(BYTES_PER_ELEMENT,   "BYTES_PER_ELEMENT")
DEFINE_SYMBOL(slice,               "slice")
DEFINE_SYMBOL(isView,              "isView")
DEFINE_SYMBOL(fill,                "fill")
DEFINE_SYMBOL(forEach,             "forEach")
DEFINE_SYMBOL(map,                 "map")
DEFINE_SYMBOL(filter,              "filter")
DEFINE_SYMBOL(indexOf,             "indexOf")
DEFINE_SYMBOL(lastIndexOf,         "lastIndexOf")
DEFINE_SYMBOL(includes,            "includes")
DEFINE_SYMBOL(find,                "find")
DEFINE_SYMBOL(findIndex,           "findIndex")
DEFINE_SYMBOL(every,               "every")
DEFINE_SYMBOL(some,                "some")
DEFINE_SYMBOL(join,                "join")
DEFINE_SYMBOL(reverse,             "reverse")
DEFINE_SYMBOL(sort,                "sort")
DEFINE_SYMBOL(copyWithin,          "copyWithin")
DEFINE_SYMBOL(subarray,            "subarray")
DEFINE_SYMBOL(at,                  "at")
DEFINE_SYMBOL(set,                 "set")
DEFINE_SYMBOL(get,                 "get")
DEFINE_SYMBOL(enumerable,          "enumerable")
DEFINE_SYMBOL(configurable,        "configurable")
DEFINE_SYMBOL(writable,            "writable")
DEFINE_SYMBOL(returnSym,           "return")
DEFINE_SYMBOL(is,                  "is")
DEFINE_SYMBOL(Symbol,              "Symbol")
DEFINE_SYMBOL(getProto,            "__get___proto____")
DEFINE_SYMBOL(setProto,            "__set___proto____")
DEFINE_SYMBOL(pdIs,                "__pd_is__")
DEFINE_SYMBOL(isBigIntKey,         "__is_bigint__")
DEFINE_SYMBOL(reduce,              "reduce")
DEFINE_SYMBOL(reduceRight,         "reduceRight")
DEFINE_SYMBOL(from,                "from")
DEFINE_SYMBOL(of,                  "of")
DEFINE_SYMBOL(keys,                "keys")
DEFINE_SYMBOL(entries,             "entries")
DEFINE_SYMBOL(getInt8,             "getInt8")
DEFINE_SYMBOL(getUint8,            "getUint8")
DEFINE_SYMBOL(getInt16,            "getInt16")
DEFINE_SYMBOL(getUint16,           "getUint16")
DEFINE_SYMBOL(getInt32,            "getInt32")
DEFINE_SYMBOL(getUint32,           "getUint32")
DEFINE_SYMBOL(getFloat32,          "getFloat32")
DEFINE_SYMBOL(getFloat64,          "getFloat64")
DEFINE_SYMBOL(getBigInt64,         "getBigInt64")
DEFINE_SYMBOL(getBigUint64,        "getBigUint64")
DEFINE_SYMBOL(setInt8,             "setInt8")
DEFINE_SYMBOL(setUint8,            "setUint8")
DEFINE_SYMBOL(setInt16,            "setInt16")
DEFINE_SYMBOL(setUint16,           "setUint16")
DEFINE_SYMBOL(setInt32,            "setInt32")
DEFINE_SYMBOL(setUint32,           "setUint32")
DEFINE_SYMBOL(setFloat32,          "setFloat32")
DEFINE_SYMBOL(setFloat64,          "setFloat64")
DEFINE_SYMBOL(setBigInt64,         "setBigInt64")
DEFINE_SYMBOL(setBigUint64,        "setBigUint64")

// ---- Well-known JS protocol symbols -------------------------------------
DEFINE_SYMBOL(symbolMatch,   "Symbol.match")
DEFINE_SYMBOL(symbolReplace, "Symbol.replace")
DEFINE_SYMBOL(symbolSearch,  "Symbol.search")
DEFINE_SYMBOL(symbolSplit,   "Symbol.split")
DEFINE_SYMBOL(symbolMatchAll,   "Symbol.matchAll")
DEFINE_SYMBOL(symbolIterator,   "Symbol.iterator")
DEFINE_SYMBOL(symbolSpecies,    "Symbol.species")
DEFINE_SYMBOL(symbolHasInstance,"Symbol.hasInstance")
DEFINE_SYMBOL(symbolToStringTag,"Symbol.toStringTag")
DEFINE_SYMBOL(toStringTag,       "__toStringTag__")

#undef DEFINE_SYMBOL

// ---- Numeric index symbols ----------------------------------------------
//
// Cache size 4096 covers most practical array workloads (push/index loops,
// JSON arrays).  The benchmark suite pushes 100 K elements; without a
// large-enough cache, 99 % of pushes per iteration construct a std::string
// and go through SymbolTable::lookupByContent on every call.  4096 strikes
// a balance: 32 KB of pointer storage, no measurable startup cost from
// std::once_flag init (lazy per slot).
namespace {
    constexpr uint32_t kIndexCacheSize = 4096;
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

const proto::ProtoString* internName(proto::ProtoContext* ctx, const char* literal) {
    return internVocabulary(ctx, literal);
}

namespace {

// One open-addressing table per sidecar kind: key address -> sidecar symbol.
// A slot's key is claimed once with a CAS and never changes; its value is
// published after it, so a reader that sees the key but no value yet builds
// the (same) symbol itself.  A full probe window falls back to building the
// name uncached.
struct SidecarSlot {
    std::atomic<std::uintptr_t> key{0};
    std::atomic<const proto::ProtoString*> value{nullptr};
};
constexpr unsigned kSidecarBits = 14;
constexpr std::size_t kSidecarSlots = std::size_t(1) << kSidecarBits;
constexpr std::size_t kSidecarProbe = 16;
SidecarSlot g_sidecars[3][kSidecarSlots];
const char* const kSidecarPrefix[3] = {"__get_", "__set_", "__pd_"};

std::string sidecarSpelling(proto::ProtoContext* ctx, Sidecar kind, const proto::ProtoString* key) {
    std::string name;
    key->toUTF8String(ctx, name);
    return kSidecarPrefix[static_cast<unsigned>(kind)] + name + "__";
}

}  // namespace

const proto::ProtoString* sidecarName(proto::ProtoContext* ctx, Sidecar kind,
                                      const proto::ProtoString* key, bool programName) {
    if (!ctx || !key) return nullptr;
    const std::uintptr_t k = reinterpret_cast<std::uintptr_t>(key);
    const bool cacheable = key->isSymbol() || (programName && !dict::isHeapStringKey(key));
    if (!cacheable) {
        const proto::ProtoObject* o = jsStringFromUTF8(ctx, sidecarSpelling(ctx, kind, key));
        return o ? o->asString(ctx) : nullptr;
    }
    SidecarSlot* table = g_sidecars[static_cast<unsigned>(kind)];
    const std::size_t h = static_cast<std::size_t>((k * 0x9E3779B97F4A7C15ull) >> (64 - kSidecarBits));
    for (std::size_t i = 0; i < kSidecarProbe; ++i) {
        SidecarSlot& slot = table[(h + i) & (kSidecarSlots - 1)];
        std::uintptr_t seen = slot.key.load(std::memory_order_acquire);
        if (seen == k) {
            if (const proto::ProtoString* v = slot.value.load(std::memory_order_acquire)) return v;
            break;  // being published by another thread
        }
        if (seen == 0) {
            const std::string spelling = sidecarSpelling(ctx, kind, key);
            const proto::ProtoString* sym = internVocabulary(ctx, spelling.c_str());
            if (slot.key.compare_exchange_strong(seen, k, std::memory_order_acq_rel))
                slot.value.store(sym, std::memory_order_release);
            return sym;
        }
    }
    return internVocabulary(ctx, sidecarSpelling(ctx, kind, key).c_str());
}

} // namespace protojs::JSSymbols
