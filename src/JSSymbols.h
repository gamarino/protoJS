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

// ---- Well-known JS protocol symbols -------------------------------------
const proto::ProtoString* symbolMatch(proto::ProtoContext* ctx);    // "Symbol.match"
const proto::ProtoString* symbolReplace(proto::ProtoContext* ctx);  // "Symbol.replace"
const proto::ProtoString* symbolSearch(proto::ProtoContext* ctx);   // "Symbol.search"
const proto::ProtoString* symbolSplit(proto::ProtoContext* ctx);    // "Symbol.split"
const proto::ProtoString* symbolMatchAll(proto::ProtoContext* ctx); // "Symbol.matchAll"

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
