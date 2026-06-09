#ifndef PROTOJS_JSSYMBOLS_H
#define PROTOJS_JSSYMBOLS_H

#include "headers/protoCore.h"
#include <cstdint>
#include <string>

namespace protojs {

/**
 * Static attribute-key symbols for protoJS.
 *
 * Each getter returns the interned ProtoString symbol for a well-known
 * attribute name. Symbols are initialized lazily on first use via
 * std::call_once and cached globally; they are never GC-collected.
 *
 * For dynamic property names derived from JS values at runtime, use
 * ProtoString::fromUTF8() and let protoCore auto-intern at the
 * getAttribute/setAttribute boundary.
 */
namespace JSSymbols {

// ---- JS global constructor names ----------------------------------------
const proto::ProtoString* Array(proto::ProtoContext* ctx);          // "Array"
const proto::ProtoString* Function(proto::ProtoContext* ctx);       // "Function"
const proto::ProtoString* Math(proto::ProtoContext* ctx);           // "Math"
const proto::ProtoString* Number(proto::ProtoContext* ctx);         // "Number"
const proto::ProtoString* Object(proto::ProtoContext* ctx);         // "Object"
const proto::ProtoString* RegExp(proto::ProtoContext* ctx);         // "RegExp"
const proto::ProtoString* String(proto::ProtoContext* ctx);         // "String"

// ---- Common JS property names -------------------------------------------
const proto::ProtoString* apply(proto::ProtoContext* ctx);          // "apply"
const proto::ProtoString* bind(proto::ProtoContext* ctx);           // "bind"
const proto::ProtoString* call(proto::ProtoContext* ctx);           // "call"
const proto::ProtoString* constructor(proto::ProtoContext* ctx);    // "constructor"
const proto::ProtoString* createdTimestamp(proto::ProtoContext* ctx); // "createdTimestamp"
const proto::ProtoString* description(proto::ProtoContext* ctx);    // "description"
const proto::ProtoString* done(proto::ProtoContext* ctx);           // "done"
const proto::ProtoString* dotAll(proto::ProtoContext* ctx);         // "dotAll"
const proto::ProtoString* elements(proto::ProtoContext* ctx);       // "elements"
const proto::ProtoString* exports(proto::ProtoContext* ctx);        // "exports"
const proto::ProtoString* flags(proto::ProtoContext* ctx);          // "flags"
const proto::ProtoString* global(proto::ProtoContext* ctx);         // "global"
const proto::ProtoString* ignoreCase(proto::ProtoContext* ctx);     // "ignoreCase"
const proto::ProtoString* index(proto::ProtoContext* ctx);          // "index"
const proto::ProtoString* input(proto::ProtoContext* ctx);          // "input"
const proto::ProtoString* isRoot(proto::ProtoContext* ctx);         // "isRoot"
const proto::ProtoString* isWeakRef(proto::ProtoContext* ctx);      // "isWeakRef"
const proto::ProtoString* jsValueTag(proto::ProtoContext* ctx);     // "jsValueTag"
const proto::ProtoString* lastIndex(proto::ProtoContext* ctx);      // "lastIndex"
const proto::ProtoString* length(proto::ProtoContext* ctx);         // "length"
const proto::ProtoString* message(proto::ProtoContext* ctx);        // "message"
const proto::ProtoString* multiline(proto::ProtoContext* ctx);      // "multiline"
const proto::ProtoString* name(proto::ProtoContext* ctx);           // "name"
const proto::ProtoString* next(proto::ProtoContext* ctx);           // "next"
const proto::ProtoString* prototype(proto::ProtoContext* ctx);      // "prototype"
const proto::ProtoString* protoObj(proto::ProtoContext* ctx);       // "protoObj"
const proto::ProtoString* require(proto::ProtoContext* ctx);        // "require"
const proto::ProtoString* stringProtoBuild(proto::ProtoContext* ctx); // "__string_proto_built__"
const proto::ProtoString* source(proto::ProtoContext* ctx);         // "source"
const proto::ProtoString* sticky(proto::ProtoContext* ctx);         // "sticky"
const proto::ProtoString* toExponential(proto::ProtoContext* ctx);  // "toExponential"
const proto::ProtoString* toFixed(proto::ProtoContext* ctx);        // "toFixed"
const proto::ProtoString* toPrecision(proto::ProtoContext* ctx);    // "toPrecision"
const proto::ProtoString* toString(proto::ProtoContext* ctx);       // "toString"
const proto::ProtoString* unicode(proto::ProtoContext* ctx);        // "unicode"
const proto::ProtoString* value(proto::ProtoContext* ctx);          // "value"
const proto::ProtoString* valueOf(proto::ProtoContext* ctx);        // "valueOf"
const proto::ProtoString* values(proto::ProtoContext* ctx);         // "values"
const proto::ProtoString* groups(proto::ProtoContext* ctx);         // "groups"
const proto::ProtoString* hasIndices(proto::ProtoContext* ctx);     // "hasIndices"
const proto::ProtoString* indices(proto::ProtoContext* ctx);        // "indices"
const proto::ProtoString* isConcatSpreadable(proto::ProtoContext* ctx); // "Symbol.isConcatSpreadable"
const proto::ProtoString* getIsConcatSpreadable(proto::ProtoContext* ctx); // "__get_Symbol.isConcatSpreadable__"

// ---- Internal implementation keys (__ prefix stripped) ------------------
const proto::ProtoString* arrayCtor(proto::ProtoContext* ctx);      // "__array_ctor__"
const proto::ProtoString* arrayProto(proto::ProtoContext* ctx);     // "__array_proto__"
const proto::ProtoString* boundArgs(proto::ProtoContext* ctx);      // "__bound_args__"
const proto::ProtoString* boundFn(proto::ProtoContext* ctx);        // "__bound_fn__"
const proto::ProtoString* boundThis(proto::ProtoContext* ctx);      // "__bound_this__"
const proto::ProtoString* bytecodeId(proto::ProtoContext* ctx);     // "__bytecode_id__"
const proto::ProtoString* errorCtor(proto::ProtoContext* ctx);      // "__error_ctor__"
const proto::ProtoString* externalPtrField(proto::ProtoContext* ctx); // "_externalPtr"
const proto::ProtoString* functionProto(proto::ProtoContext* ctx);  // "__function_proto__"
const proto::ProtoString* iterArr(proto::ProtoContext* ctx);        // "__iter_arr__"
const proto::ProtoString* iterIdx(proto::ProtoContext* ctx);        // "__iter_idx__"
const proto::ProtoString* iterKind(proto::ProtoContext* ctx);       // "__iter_kind__"
const proto::ProtoString* iterSlot(proto::ProtoContext* ctx);       // "__iter_slot__"
const proto::ProtoString* jsValuePtrField(proto::ProtoContext* ctx);  // "_jsValuePtr"
const proto::ProtoString* jsValueTagField(proto::ProtoContext* ctx);  // "_jsValueTag"
const proto::ProtoString* reBytecode(proto::ProtoContext* ctx);     // "__re_bytecode__"
const proto::ProtoString* regexpCtor(proto::ProtoContext* ctx);     // "__regexp_ctor__"
const proto::ProtoString* iterDone(proto::ProtoContext* ctx);       // "__iter_done__"
const proto::ProtoString* iterRe(proto::ProtoContext* ctx);         // "__iter_re__"
const proto::ProtoString* iterStr(proto::ProtoContext* ctx);        // "__iter_str__"
const proto::ProtoString* isArray(proto::ProtoContext* ctx);        // "__is_array__"
const proto::ProtoString* stringCtor(proto::ProtoContext* ctx);     // "__string_ctor__"
const proto::ProtoString* primitiveValue(proto::ProtoContext* ctx); // "__primitive_value__"
const proto::ProtoString* arrowThis(proto::ProtoContext* ctx);      // "__arrow_this__"
const proto::ProtoString* closureModule(proto::ProtoContext* ctx);  // "__closure_module__"
const proto::ProtoString* argCount(proto::ProtoContext* ctx);       // "__arg_count__"
const proto::ProtoString* varCount(proto::ProtoContext* ctx);       // "__var_count__"
const proto::ProtoString* stackSize(proto::ProtoContext* ctx);      // "__stack_size__"
const proto::ProtoString* isStrict(proto::ProtoContext* ctx);       // "__is_strict__"
const proto::ProtoString* isArrow(proto::ProtoContext* ctx);        // "__is_arrow__"
const proto::ProtoString* isAsync(proto::ProtoContext* ctx);        // "__is_async__"
const proto::ProtoString* isGenerator(proto::ProtoContext* ctx);    // "__is_generator__"
const proto::ProtoString* constantPool(proto::ProtoContext* ctx);   // "__cpool__"
const proto::ProtoString* closureSymbols(proto::ProtoContext* ctx); // "__closure_symbols__"
const proto::ProtoString* parameterNames(proto::ProtoContext* ctx); // "__parameter_names__"
const proto::ProtoString* bytecode(proto::ProtoContext* ctx);       // "__bytecode__"
const proto::ProtoString* metadata(proto::ProtoContext* ctx);       // "__metadata__"
const proto::ProtoString* sourceText(proto::ProtoContext* ctx);     // "__source_text__" — original source for Function.prototype.toString
const proto::ProtoString* nativeFn(proto::ProtoContext* ctx);       // "__native_fn__"
const proto::ProtoString* arrayElements(proto::ProtoContext* ctx);  // "__elements__" — ProtoList storage for dense arrays
// Recurrent interpreter sentinels — added 2026-05-31 to eliminate
// per-dispatch ctx->fromUTF8String("…") + ->asString(ctx) cycles
// in hot bytecode handlers.  Each pair was previously re-interned
// twice per call site through the doubled `X ? X->asString : nullptr`
// pattern; now interned once via DEFINE_SYMBOL.
const proto::ProtoString* construct(proto::ProtoContext* ctx);      // "__construct__"
const proto::ProtoString* genSent(proto::ProtoContext* ctx);        // "__gen_sent__"
const proto::ProtoString* genThrowVal(proto::ProtoContext* ctx);    // "__gen_throw_val__"
const proto::ProtoString* pdPrototype(proto::ProtoContext* ctx);    // "__pd_prototype__"
const proto::ProtoString* jsNullSentinel(proto::ProtoContext* ctx); // "__js_null_sentinel__"
const proto::ProtoString* jsTdzSentinel(proto::ProtoContext* ctx);  // "__js_tdz_sentinel__"
const proto::ProtoString* isConstructor(proto::ProtoContext* ctx);  // "__is_constructor__"

// Property-descriptor writability sentinels — added 2026-06-06.
// Each "__pd_<name>__" key holds the writable bit for the matching
// public property.  Spec-compliance work in Array.prototype.* and
// friends reads these on every native call, so they must be
// strong-interned rather than rebuilt via fromUTF8String per dispatch.
const proto::ProtoString* pdLength(proto::ProtoContext* ctx);       // "__pd_length__"
const proto::ProtoString* pdName(proto::ProtoContext* ctx);         // "__pd_name__"
const proto::ProtoString* pdConstructor(proto::ProtoContext* ctx);  // "__pd_constructor__"
const proto::ProtoString* pdMessage(proto::ProtoContext* ctx);      // "__pd_message__"
const proto::ProtoString* pdSize(proto::ProtoContext* ctx);         // "__pd_size__"
// Other recurrent symbols from the same audit (2026-06-06):
const proto::ProtoString* isSymbol(proto::ProtoContext* ctx);       // "__is_symbol__"
const proto::ProtoString* fieldsInit(proto::ProtoContext* ctx);     // "__fields_init__"
const proto::ProtoString* isBigInt(proto::ProtoContext* ctx);       // "__is_bigint__"
const proto::ProtoString* bigIntValue(proto::ProtoContext* ctx);    // "__bigint_value__"

// Per-object hint flag (added 2026-06-06).  Set to PROTO_TRUE the first
// time Object.defineProperty installs an accessor descriptor (`__set_<i>__`)
// at a numeric-index key on the target.  When absent or PROTO_FALSE,
// arrSet may skip the per-element "__set_<idx>__" inherited-setter probe.
// Conservative: once set, it stays set — a later delete or descriptor
// rewrite does not clear it (false positive = pay the probe; false
// negative would break Array.prototype setter dispatch, which we must
// never do).
const proto::ProtoString* hasIndexedSetters(proto::ProtoContext* ctx); // "__has_indexed_setters__"

// Per-object hint flags for the OrdinarySet hot path (added 2026-06-07).
// resolvePutFieldOOP runs on every `obj[key] = val` and used to
// unconditionally construct "__set_<key>__"/"__get_<key>__"/"__pd_<key>__"
// rope keys per write to probe the chain for accessor descriptors and
// writable bits — even when no defineProperty ever touched the target.
// These flags let OrdinarySet skip the entire probe block when no
// non-default descriptor exists anywhere in the prototype chain.
// Conservative: once set, they stay set (false positive = pay the probe;
// false negative would miss a real accessor / writable=false).
const proto::ProtoString* hasAccessorProps(proto::ProtoContext* ctx);   // "__has_accessor_props__"
const proto::ProtoString* hasNonWritableProps(proto::ProtoContext* ctx); // "__has_nonwritable_props__"

// ---- TypedArray / ArrayBuffer / DataView internal keys ------------------
const proto::ProtoString* abData(proto::ProtoContext* ctx);         // "__ab_data__"
const proto::ProtoString* abDetached(proto::ProtoContext* ctx);     // "__ab_detached__"
const proto::ProtoString* taBuffer(proto::ProtoContext* ctx);       // "__ta_buffer__"
const proto::ProtoString* taElementType(proto::ProtoContext* ctx);  // "__ta_element_type__"
const proto::ProtoString* taByteOffset(proto::ProtoContext* ctx);   // "__ta_byte_offset__"
const proto::ProtoString* taCtor(proto::ProtoContext* ctx);         // "__typed_array_ctor__"
const proto::ProtoString* dvBuffer(proto::ProtoContext* ctx);       // "__dv_buffer__"
const proto::ProtoString* dvByteOffset(proto::ProtoContext* ctx);   // "__dv_byte_offset__"
const proto::ProtoString* dvByteLength(proto::ProtoContext* ctx);   // "__dv_byte_length__"

// ---- TypedArray / ArrayBuffer / DataView JS property names -------------
const proto::ProtoString* ArrayBuffer(proto::ProtoContext* ctx);    // "ArrayBuffer"
const proto::ProtoString* DataView(proto::ProtoContext* ctx);       // "DataView"
const proto::ProtoString* Int8Array(proto::ProtoContext* ctx);      // "Int8Array"
const proto::ProtoString* Uint8Array(proto::ProtoContext* ctx);     // "Uint8Array"
const proto::ProtoString* Uint8ClampedArray(proto::ProtoContext* ctx); // "Uint8ClampedArray"
const proto::ProtoString* Int16Array(proto::ProtoContext* ctx);     // "Int16Array"
const proto::ProtoString* Uint16Array(proto::ProtoContext* ctx);    // "Uint16Array"
const proto::ProtoString* Int32Array(proto::ProtoContext* ctx);     // "Int32Array"
const proto::ProtoString* Uint32Array(proto::ProtoContext* ctx);    // "Uint32Array"
const proto::ProtoString* Float32Array(proto::ProtoContext* ctx);   // "Float32Array"
const proto::ProtoString* Float64Array(proto::ProtoContext* ctx);   // "Float64Array"
const proto::ProtoString* BigInt64Array(proto::ProtoContext* ctx);  // "BigInt64Array"
const proto::ProtoString* BigUint64Array(proto::ProtoContext* ctx); // "BigUint64Array"
const proto::ProtoString* byteLength(proto::ProtoContext* ctx);     // "byteLength"
const proto::ProtoString* byteOffset(proto::ProtoContext* ctx);     // "byteOffset"
const proto::ProtoString* buffer(proto::ProtoContext* ctx);         // "buffer"
const proto::ProtoString* BYTES_PER_ELEMENT(proto::ProtoContext* ctx); // "BYTES_PER_ELEMENT"
const proto::ProtoString* slice(proto::ProtoContext* ctx);          // "slice"
const proto::ProtoString* isView(proto::ProtoContext* ctx);         // "isView"
const proto::ProtoString* fill(proto::ProtoContext* ctx);           // "fill"
const proto::ProtoString* forEach(proto::ProtoContext* ctx);        // "forEach"
const proto::ProtoString* map(proto::ProtoContext* ctx);            // "map"
const proto::ProtoString* filter(proto::ProtoContext* ctx);         // "filter"
const proto::ProtoString* indexOf(proto::ProtoContext* ctx);        // "indexOf"
const proto::ProtoString* lastIndexOf(proto::ProtoContext* ctx);    // "lastIndexOf"
const proto::ProtoString* includes(proto::ProtoContext* ctx);       // "includes"
const proto::ProtoString* find(proto::ProtoContext* ctx);           // "find"
const proto::ProtoString* findIndex(proto::ProtoContext* ctx);      // "findIndex"
const proto::ProtoString* every(proto::ProtoContext* ctx);          // "every"
const proto::ProtoString* some(proto::ProtoContext* ctx);           // "some"
const proto::ProtoString* join(proto::ProtoContext* ctx);           // "join"
const proto::ProtoString* reverse(proto::ProtoContext* ctx);        // "reverse"
const proto::ProtoString* sort(proto::ProtoContext* ctx);           // "sort"
const proto::ProtoString* copyWithin(proto::ProtoContext* ctx);     // "copyWithin"
const proto::ProtoString* subarray(proto::ProtoContext* ctx);       // "subarray"
const proto::ProtoString* at(proto::ProtoContext* ctx);             // "at"
const proto::ProtoString* set(proto::ProtoContext* ctx);            // "set"
const proto::ProtoString* reduce(proto::ProtoContext* ctx);         // "reduce"
const proto::ProtoString* reduceRight(proto::ProtoContext* ctx);    // "reduceRight"
const proto::ProtoString* from(proto::ProtoContext* ctx);           // "from"
const proto::ProtoString* of(proto::ProtoContext* ctx);             // "of"
const proto::ProtoString* keys(proto::ProtoContext* ctx);           // "keys"
const proto::ProtoString* entries(proto::ProtoContext* ctx);        // "entries"
const proto::ProtoString* getInt8(proto::ProtoContext* ctx);        // "getInt8"
const proto::ProtoString* getUint8(proto::ProtoContext* ctx);       // "getUint8"
const proto::ProtoString* getInt16(proto::ProtoContext* ctx);       // "getInt16"
const proto::ProtoString* getUint16(proto::ProtoContext* ctx);      // "getUint16"
const proto::ProtoString* getInt32(proto::ProtoContext* ctx);       // "getInt32"
const proto::ProtoString* getUint32(proto::ProtoContext* ctx);      // "getUint32"
const proto::ProtoString* getFloat32(proto::ProtoContext* ctx);     // "getFloat32"
const proto::ProtoString* getFloat64(proto::ProtoContext* ctx);     // "getFloat64"
const proto::ProtoString* getBigInt64(proto::ProtoContext* ctx);    // "getBigInt64"
const proto::ProtoString* getBigUint64(proto::ProtoContext* ctx);   // "getBigUint64"
const proto::ProtoString* setInt8(proto::ProtoContext* ctx);        // "setInt8"
const proto::ProtoString* setUint8(proto::ProtoContext* ctx);       // "setUint8"
const proto::ProtoString* setInt16(proto::ProtoContext* ctx);       // "setInt16"
const proto::ProtoString* setUint16(proto::ProtoContext* ctx);      // "setUint16"
const proto::ProtoString* setInt32(proto::ProtoContext* ctx);       // "setInt32"
const proto::ProtoString* setUint32(proto::ProtoContext* ctx);      // "setUint32"
const proto::ProtoString* setFloat32(proto::ProtoContext* ctx);     // "setFloat32"
const proto::ProtoString* setFloat64(proto::ProtoContext* ctx);     // "setFloat64"
const proto::ProtoString* setBigInt64(proto::ProtoContext* ctx);    // "setBigInt64"
const proto::ProtoString* setBigUint64(proto::ProtoContext* ctx);   // "setBigUint64"

// ---- Well-known JS protocol symbols -------------------------------------
const proto::ProtoString* symbolMatch(proto::ProtoContext* ctx);    // "Symbol.match"
const proto::ProtoString* symbolReplace(proto::ProtoContext* ctx);  // "Symbol.replace"
const proto::ProtoString* symbolSearch(proto::ProtoContext* ctx);   // "Symbol.search"
const proto::ProtoString* symbolSplit(proto::ProtoContext* ctx);    // "Symbol.split"
const proto::ProtoString* symbolMatchAll(proto::ProtoContext* ctx); // "Symbol.matchAll"
const proto::ProtoString* symbolIterator(proto::ProtoContext* ctx); // "Symbol.iterator"
const proto::ProtoString* symbolSpecies(proto::ProtoContext* ctx);  // "Symbol.species"
const proto::ProtoString* symbolHasInstance(proto::ProtoContext* ctx); // "Symbol.hasInstance"
const proto::ProtoString* symbolToStringTag(proto::ProtoContext* ctx); // "Symbol.toStringTag"
const proto::ProtoString* toStringTag(proto::ProtoContext* ctx);   // "__toStringTag__"

// ---- Numeric index symbols (0..N) ---------------------------------------
/**
 * Returns the symbol for a numeric array index ("0", "1", ...).
 * Indices 0..255 are cached; larger indices create a symbol on each call
 * (they are still globally unique due to createSymbol's interning).
 */
const proto::ProtoString* indexKey(proto::ProtoContext* ctx, uint32_t i);

// ---- Reverse hash lookup (debug / attribute iteration) ------------------
/**
 * Returns the string name for a known static symbol hash, or empty string
 * if the hash does not correspond to any registered JSSymbols key.
 * Useful for iterating ProtoSparseList attribute dictionaries.
 * Must be called after at least one symbol has been initialized.
 */
std::string getNameFromHash(proto::ProtoContext* ctx, unsigned long hash);

} // namespace JSSymbols
} // namespace protojs

#endif // PROTOJS_JSSYMBOLS_H
