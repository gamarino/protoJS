#include "../ProtoCoreTypes.h"
#include "ProtoBytecodeModule.h"
#include "BytecodeSpecialiser.h"
#include "QuickJSBytecodeExport.h"
#include "QuickJSOpcodeEnum.h"
#include "../JSSymbols.h"
#include "../debugger/DebugHooks.h"
#include "PropertyDictionary.h"
#include "JSStringBytes.h"
#include <cstring>
#include "../TypeBridge.h"
#include "quickjs.h"
#include "protoCore.h"

namespace protojs {

/// Instruction size of every opcode byte; the table is shared with the
/// specialiser and the disassembler (BytecodeSpecialiser.h).
static const uint8_t* getOpSizes() { return bytecodeOpcodeSizes(); }

/**
 * Pre-resolve all atom operands in the bytecode into the module's atomToProto cache.
 * This must be called while ctx is still valid (at load time), so that the interpreter
 * never needs JSContext* for atom lookup at execution time.
 */
static void preResolveAllAtoms(JSContext* ctx, ProtoBytecodeModule* mod,
                                proto::ProtoContext* pContext) {
    if (!ctx || !mod || !pContext) return;
    const uint8_t* buf = mod->buf(pContext);
    int len = mod->bufLen(pContext);
    if (!buf || len <= 0) return;

    const uint8_t* sizes = getOpSizes();
    const int maxOp = static_cast<int>(OP_COUNT) - 1; /* last real opcode byte value */

    /* Write groups carry their atoms in the module, not in the bytecode. */
    for (const PutFieldGroup& g : mod->putFieldGroups) {
        for (uint32_t atomIndex : g.atoms) {
            if (mod->atomToProto.find(atomIndex) != mod->atomToProto.end()) continue;
            size_t strLen = 0;
            const char* str = JS_AtomToCStringLen(ctx, &strLen, (JSAtom)atomIndex);
            if (!str) continue;
            const proto::ProtoString* ps = protojs::jsSymbolFromUTF8(pContext, str, strLen);
            protojs::dict::noteVocabulary(str, strLen);  // program vocabulary
            JS_FreeCString(ctx, str);
            if (ps) mod->atomToProto[atomIndex] = ps;
        }
        for (const auto& r : g.reads) {
            if (mod->atomToProto.find(r.atom) != mod->atomToProto.end()) continue;
            size_t strLen = 0;
            const char* str = JS_AtomToCStringLen(ctx, &strLen, (JSAtom)r.atom);
            if (!str) continue;
            const proto::ProtoString* ps = protojs::jsSymbolFromUTF8(pContext, str, strLen);
            protojs::dict::noteVocabulary(str, strLen);  // program vocabulary
            JS_FreeCString(ctx, str);
            if (ps) mod->atomToProto[r.atom] = ps;
        }
    }

    int pc = 0;
    while (pc < len) {
        uint8_t op = buf[pc];
        /* Atom-carrying opcodes (4-byte atom at offset 1 from opcode byte): */
        bool hasAtom = (op == OP_push_atom_value ||
                        op == OP_private_symbol   ||
                        op == OP_get_field        ||
                        op == OP_get_field2       ||
                        op == OP_put_field        ||
                        op == OP_define_field     ||
                        op == 248 /* OP_PROTO_DEFINE_FIELD_LAST */ ||
                        op == OP_set_name         ||
                        op == OP_make_var_ref     ||
                        op == OP_delete_var       ||
                        op == OP_define_method    ||
                        op == OP_define_class     ||
                        op == OP_define_class_computed ||
                        op == OP_with_get_var     ||
                        op == OP_with_put_var     ||
                        op == OP_with_delete_var  ||
                        op == OP_with_make_ref    ||
                        op == OP_with_get_ref     ||
                        op == OP_make_loc_ref     ||
                        op == OP_make_arg_ref     ||
                        op == OP_make_var_ref_ref);
        if (hasAtom && pc + 5 <= len) {
            uint32_t atomIndex = (uint32_t)buf[pc+1] | ((uint32_t)buf[pc+2] << 8) |
                                 ((uint32_t)buf[pc+3] << 16) | ((uint32_t)buf[pc+4] << 24);
            if (mod->atomToProto.find(atomIndex) == mod->atomToProto.end()) {
                size_t strLen = 0;
                const char* str = JS_AtomToCStringLen(ctx, &strLen, (JSAtom)atomIndex);
                if (str) {
                    const proto::ProtoString* ps = protojs::jsSymbolFromUTF8(pContext, str, strLen);
                    protojs::dict::noteVocabulary(str, strLen);  // program vocabulary
                    JS_FreeCString(ctx, str);
                    if (ps) mod->atomToProto[atomIndex] = ps;
                }
            }
        }
        /* Detect opcodes that read the call-time `args` ProtoList or
         * t_activeArgs.  Setting mod->usesArguments lets callers skip
         * the newList(argc, slice) allocation entirely when the callee
         * never touches the call-time args. */
        if (!mod->usesArguments) {
            if (op == OP_rest || op == OP_init_ctor) {
                mod->usesArguments = true;
            } else if (op == OP_special_object && pc + 1 < len) {
                /* kinds 0 (ARGUMENTS) and 1 (MAPPED_ARGUMENTS) need args.
                 * Other kinds (THIS_FUNC, NEW_TARGET, HOME_OBJECT, ...)
                 * read thread-locals that don't depend on argsList. */
                uint8_t kind = buf[pc + 1];
                if (kind == 0 || kind == 1) mod->usesArguments = true;
            }
        }
        /* Advance by opcode size (includes opcode byte itself).  Bytes
         * past OP_COUNT carry the BytecodeSpecialiser's fused
         * super-instructions — getOpSizes() populates their sizes too,
         * so trust the table for the full 256-entry range instead of
         * stopping at maxOp.  Pre-fix this aborted the atom walk the
         * moment a fused opcode appeared, so every get_field that
         * followed missed its atom-to-symbol cache entry and the
         * runtime crashed with `resolveAtom: atom N not in cache`
         * the first time the bench called a method. */
        (void)maxOp;
        uint8_t sz = sizes[op];
        if (sz == 0) break; /* unknown opcode - stop */
        pc += sz;
    }

    /* Recurse for nested functions. */
    for (auto& nf : mod->nestedFunctions) {
        preResolveAllAtoms(ctx, &nf, pContext);
    }
}

/**
 * Load a QuickJS bytecode function into `out`, appending ALL nested functions
 * (at any depth) to `allFunctions` with globally unique IDs.
 *
 * The IDs are assigned in post-order (children before parents), so each function
 * is appended to `allFunctions` only after all its own descendants have been
 * appended.  This means we never hold a pointer into `allFunctions` while
 * executing a push_back that could trigger reallocation.
 *
 * After loadBytecode completes, `root->nestedFunctions` is the flat list of ALL
 * functions at every depth.  Each placeholder object in any module's protoCpool
 * carries a `__bytecode_id__` that is a valid index into that flat list.
 */
/**
 * Make every class constructor a closure.
 *
 * QuickJS emits a class definition as
 *
 *     push_const <ctor cpool idx>     (push_const8 after the short-opcode pass)
 *     define_class <atom> <flags>     (or define_class_computed)
 *
 * and its OP_define_class builds the constructor closure itself (js_closure2
 * over the running frame's var_refs). protoJS's OP_define_class instead
 * consumes an already-built closure, exactly as OP_fclosure produces for every
 * other function. Without this lowering it received the raw constant-pool
 * object, which is shared by every evaluation of the class definition, captures
 * no variables, and carries no __closure_module__ stamp -- so `new` on a class
 * exported by a required module resolved the constructor's bytecode ID against
 * the requiring script's function table and ran an unrelated function.
 *
 * Rewriting the push to the same-sized closure opcode (push_const and fclosure
 * are both 5 bytes with a u32 cpool index; push_const8 and fclosure8 are both
 * 2 bytes with a u8 index) routes the constructor through the one closure path
 * the interpreter already has. Only a push that immediately precedes a
 * define_class is rewritten; no other push_const is touched.
 */
static void lowerClassConstructorPush(std::vector<uint8_t>& code) {
    const uint8_t* sizes = getOpSizes();
    const size_t len = code.size();
    size_t pc = 0;
    size_t prevPc = len; // no previous instruction yet
    while (pc < len) {
        const uint8_t op = code[pc];
        if ((op == OP_define_class || op == OP_define_class_computed) && prevPc < len) {
            if (code[prevPc] == OP_push_const)
                code[prevPc] = OP_fclosure;
            else if (code[prevPc] == OP_push_const8)
                code[prevPc] = OP_fclosure8;
        }
        const uint8_t sz = sizes[op];
        if (sz == 0) return; // unknown opcode: stop rather than lose alignment
        prevPc = pc;
        pc += sz;
    }
}

// Names a write group may carry (markPutFieldGroups).  Rejected: names the
// runtime gives a meaning beyond a data property -- `length` (arrays,
// functions, String wrappers), `prototype` (created lazily on functions),
// array indices, and protoJS's own `__..__` sidecars and markers, which user
// code can name too (`o.__proto__`) and which the per-write path interprets.
static bool putFieldGroupNameEligible(void* user, uint32_t atom) {
    JSContext* ctx = static_cast<JSContext*>(user);
    size_t strLen = 0;
    const char* str = JS_AtomToCStringLen(ctx, &strLen, (JSAtom)atom);
    if (!str) return false;
    const std::string name(str, strLen);
    JS_FreeCString(ctx, str);
    if (name.empty() || name == "length" || name == "prototype") return false;
    if (name.size() >= 2 && name[0] == '_' && name[1] == '_') return false;
    bool digits = true;
    for (char c : name) if (c < '0' || c > '9') { digits = false; break; }
    return !digits;
}

static bool loadBytecodeRecursive(JSContext* ctx,
                                  void* quickjsBytecode,
                                  proto::ProtoContext* pContext,
                                  ProtoBytecodeModule* out,
                                  std::vector<ProtoBytecodeModule>& allFunctions,
                                  size_t& nextGlobalId) {
    if (!ctx || !quickjsBytecode || !pContext || !out) return false;

    out->jsContext = ctx;
    out->quickjsBytecode = quickjsBytecode;

    // Copy raw bytecode buffer and metadata from the QuickJS function.
    const uint8_t* buf = protojs_bytecode_buf(quickjsBytecode);
    int len = protojs_bytecode_len(quickjsBytecode);
    if (!buf || len <= 0) return false;

    // The loader's rewrites, in order: object literals, write groups, the
    // specialiser (on by default; see BytecodeSpecialiser.h), and the class
    // constructor push.  Only the specialiser changes instruction offsets,
    // and it re-encodes every code offset it moves.
    // Object literals are built immutable and made mutable once
    // (markObjectLiterals); the pass needs QuickJS's stack levels, so it
    // runs on the unmodified bytecode, before the specialiser.
    std::vector<uint8_t> litBuf(buf, buf + len);
    {
        std::vector<uint16_t> levels(static_cast<size_t>(len));
        if (protojs_bytecode_stack_levels(ctx, quickjsBytecode, levels.data()) == 0)
            markObjectLiterals(litBuf, levels.data());
    }
    // Runs of writes to one object published once (markPutFieldGroups).
    // The run keeps its values on the operand stack until its last write,
    // so the frame needs that much more stack.
    int putFieldGroupExtraStack = 0;
    out->putFieldGroups.clear();
    markPutFieldGroups(litBuf, putFieldGroupNameEligible, ctx,
                       out->putFieldGroups, putFieldGroupExtraStack);
    std::vector<uint8_t> specBuf = specialise(litBuf.data(), len, getSpecialiseMode());
    lowerClassConstructorPush(specBuf);
    {
        const uint8_t* sizes = getOpSizes();
        out->forOfStartPcs.clear();
        for (size_t pc = 0; pc < specBuf.size(); ) {
            const uint8_t op = specBuf[pc];
            if (op == OP_for_of_start) out->forOfStartPcs.push_back(static_cast<uint32_t>(pc));
            const uint8_t sz = sizes[op];
            if (sz == 0) { out->forOfStartPcs.clear(); break; }
            pc += sz;
        }
    }
    out->pBytecode = pContext->newByteBuffer(
        reinterpret_cast<const char*>(specBuf.data()),
        static_cast<proto::proto_ulong>(specBuf.size()));
    
    out->argCount_ = protojs_bytecode_arg_count(quickjsBytecode);
    out->varCount_ = protojs_bytecode_var_count(quickjsBytecode);
    out->stackSize_ = static_cast<uint16_t>(
        protojs_bytecode_stack_size(quickjsBytecode) + putFieldGroupExtraStack);
    if (getenv("PROTO_DEBUG_LOAD")) {
        const char* fname = protojs_bytecode_func_name(ctx, quickjsBytecode);
        printf("[DEBUG] Loading function '%s': argCount=%u varCount=%u\n", fname ? fname : "<anon>", out->argCount_, out->varCount_);
        if (fname) JS_FreeCString(ctx, fname);
    }
    out->isStrict = protojs_bytecode_is_strict(quickjsBytecode) != 0;
    out->isArrow  = protojs_bytecode_is_arrow(quickjsBytecode) != 0;
    out->isConstructor = protojs_bytecode_is_constructor(quickjsBytecode) != 0;
    out->hasLazyPrototype = protojs_bytecode_has_prototype(quickjsBytecode) != 0;
    {
        // func_kind: 0=normal, 1=generator, 2=async, 3=async-generator
        int fk = protojs_bytecode_func_kind(quickjsBytecode);
        out->isAsync     = (fk & 2) != 0; // JS_FUNC_ASYNC bit
        out->isGenerator = (fk & 1) != 0; // JS_FUNC_GENERATOR bit
    }

    // [NEW] Create metadata object with interned keys.
    const proto::ProtoObject* meta = pContext->newObject(true);
    meta = meta->setAttribute(pContext, JSSymbols::argCount(pContext), pContext->fromInteger(out->argCount_));
    meta = meta->setAttribute(pContext, JSSymbols::varCount(pContext), pContext->fromInteger(out->varCount_));
    meta = meta->setAttribute(pContext, JSSymbols::stackSize(pContext), pContext->fromInteger(out->stackSize_));
    meta = meta->setAttribute(pContext, JSSymbols::isStrict(pContext), out->isStrict ? PROTO_TRUE : PROTO_FALSE);
    meta = meta->setAttribute(pContext, JSSymbols::isArrow(pContext), out->isArrow ? PROTO_TRUE : PROTO_FALSE);
    meta = meta->setAttribute(pContext, JSSymbols::isAsync(pContext), out->isAsync ? PROTO_TRUE : PROTO_FALSE);
    meta = meta->setAttribute(pContext, JSSymbols::isGenerator(pContext), out->isGenerator ? PROTO_TRUE : PROTO_FALSE);
    out->metadata = meta;

    const char* funcNameCstr = protojs_bytecode_func_name(ctx, quickjsBytecode);
    out->funcName = funcNameCstr ? funcNameCstr : "";
    if (funcNameCstr) JS_FreeCString(ctx, funcNameCstr);

    // Capture the function's source text from the QuickJS bytecode's
    // debug info (b->debug.source).  Empty when the bytecode was
    // built without debug info, which is the case for top-level
    // module / script frames.  Function.prototype.toString reads
    // this back via the __source_text__ attribute stamped by
    // OP_fclosure.
    const char* srcPtr = protojs_bytecode_source(quickjsBytecode);
    const int srcLen = protojs_bytecode_source_len(quickjsBytecode);
    if (srcPtr && srcLen > 0) {
        out->funcSource.assign(srcPtr, static_cast<size_t>(srcLen));
    } else {
        out->funcSource.clear();
    }

    const int closureVarCount = protojs_bytecode_closure_var_count(quickjsBytecode);
    out->closureVarNames.clear();
    out->closureVarNames.reserve(static_cast<size_t>(closureVarCount));
    
    // [NEW] Interned closure symbols as ProtoList for GC safety.
    const proto::ProtoList* cSymbols = pContext->newList();

    // [NEW] Build parameter list (first argCount vars in QuickJS).
    const proto::ProtoList* pNames = pContext->newList();
    for (uint16_t i = 0; i < out->argCount_; i++) {
        const char* name = protojs_bytecode_var_name(ctx, quickjsBytecode, i);
        const std::string nameStr = name ? name : "";
        const proto::ProtoString* sym = proto::ProtoString::createSymbol(pContext, nameStr);
        protojs::dict::noteVocabulary(nameStr);  // program vocabulary
        pNames = pNames->appendLast(pContext, sym->asObject(pContext));
        if (name) JS_FreeCString(ctx, name);
    }
    out->parameterNames = pNames;

    out->closureVarIsLexical.clear();
    out->closureVarIsLexical.reserve(static_cast<size_t>(closureVarCount));
    out->closureVarIsConst.clear();
    out->closureVarIsConst.reserve(static_cast<size_t>(closureVarCount));
    out->closureVarIsDeclared.clear();
    out->closureVarIsDeclared.reserve(static_cast<size_t>(closureVarCount));
    out->closureVarIsFunctionDecl.clear();
    out->closureVarIsFunctionDecl.reserve(static_cast<size_t>(closureVarCount));
    out->closureVarTypes.clear();
    out->closureVarTypes.reserve(static_cast<size_t>(closureVarCount));
    out->closureVarIndices.clear();
    out->closureVarIndices.reserve(static_cast<size_t>(closureVarCount));
    for (int i = 0; i < closureVarCount; i++) {
        const char* name = protojs_bytecode_closure_var_name(ctx, quickjsBytecode, static_cast<uint16_t>(i));
        const std::string nameStr = name ? name : "";
        out->closureVarNames.push_back(nameStr);
        
        // [NEW] Pre-intern the name as a symbol for O(1) resolution.
        const proto::ProtoString* sym = proto::ProtoString::createSymbol(pContext, nameStr);
        protojs::dict::noteVocabulary(nameStr);  // program vocabulary
        cSymbols = cSymbols->appendLast(pContext, sym->asObject(pContext));
        
        out->closureVarIsLexical.push_back(
            protojs_bytecode_closure_var_is_lexical(quickjsBytecode, static_cast<uint16_t>(i)) != 0);
        out->closureVarIsConst.push_back(
            protojs_bytecode_closure_var_is_const(quickjsBytecode, static_cast<uint16_t>(i)) != 0);
        int ctype = protojs_bytecode_closure_var_type(quickjsBytecode, static_cast<uint16_t>(i));
        out->closureVarIsDeclared.push_back(ctype == 4 /* JS_CLOSURE_GLOBAL_DECL */);
        out->closureVarIsFunctionDecl.push_back(
            protojs_bytecode_closure_var_is_function_decl(quickjsBytecode, static_cast<uint16_t>(i)) != 0);
        out->closureVarTypes.push_back(ctype);
        out->closureVarIndices.push_back(
            protojs_bytecode_closure_var_idx(quickjsBytecode, static_cast<uint16_t>(i)));
        if (name) JS_FreeCString(ctx, name);
    }
    out->closureSymbols = cSymbols;

    // The debugger's line table, slot names and file name (DebugHooks.h).
    out->debugLinePcs.clear();
    out->debugLines.clear();
    out->debugVarNames.clear();
    out->debugFile.clear();
    if (debughooks::enabled()) {
        const int n = protojs_bytecode_pc2line(quickjsBytecode, nullptr, nullptr, 0);
        if (n > 0) {
            out->debugLinePcs.resize(static_cast<size_t>(n));
            out->debugLines.resize(static_cast<size_t>(n));
            protojs_bytecode_pc2line(quickjsBytecode, out->debugLinePcs.data(),
                                     out->debugLines.data(), n);
        }
        const unsigned slots = static_cast<unsigned>(out->argCount_) + out->varCount_;
        out->debugVarNames.resize(slots);
        for (unsigned i = 0; i < slots; i++) {
            const char* name = protojs_bytecode_var_name(ctx, quickjsBytecode, static_cast<uint16_t>(i));
            if (name) {
                out->debugVarNames[i] = name;
                JS_FreeCString(ctx, name);
            }
        }
        const char* file = protojs_bytecode_filename(ctx, quickjsBytecode);
        if (file) {
            out->debugFile = file;
            JS_FreeCString(ctx, file);
        }
    }

    // Translate constant pool to ProtoObject values and nested functions.
    const int cpoolCount = protojs_bytecode_cpool_count(quickjsBytecode);
    const void* cpoolPtr = protojs_bytecode_cpool(quickjsBytecode);
    if (!cpoolPtr && cpoolCount > 0) return false;

    const proto::ProtoList* cpList = pContext->newList();
    out->nestedFunctions.clear();

    const JSValue* cpool = static_cast<const JSValue*>(cpoolPtr);
    for (int i = 0; i < cpoolCount; i++) {
        JSValue v = cpool[i];
        void* nestedQuickjsBytecode = protojs_get_function_bytecode(ctx, &v);
        if (nestedQuickjsBytecode) {
            ProtoBytecodeModule nestedMod;
            if (!loadBytecodeRecursive(ctx, nestedQuickjsBytecode, pContext,
                                       &nestedMod, allFunctions, nextGlobalId))
                return false;

            size_t globalId = nextGlobalId++;
            allFunctions.push_back(std::move(nestedMod));

            const proto::ProtoString* key = JSSymbols::bytecodeId(pContext);
            const proto::ProtoObject* placeholder = pContext->newObject(true);
            placeholder = placeholder->setAttribute(
                pContext, key, pContext->fromInteger(static_cast<long long>(globalId)));
            // Link nested metadata to ensure it's rooted via the parent's constant pool.
            placeholder = placeholder->setAttribute(
                pContext, JSSymbols::metadata(pContext), nestedMod.metadata);
            cpList = cpList->appendLast(pContext, placeholder);
        } else {
            const proto::ProtoObject* obj = TypeBridge::fromJS(ctx, v, pContext);
            cpList = cpList->appendLast(pContext, obj ? obj : PROTO_NONE);
        }
    }
    out->constantPool = cpList;
    
    // Attach cache lists to metadata for persistent rooting.
    meta = meta->setAttribute(pContext, JSSymbols::constantPool(pContext), cpList->asObject(pContext));
    meta = meta->setAttribute(pContext, JSSymbols::closureSymbols(pContext), out->closureSymbols->asObject(pContext));
    meta = meta->setAttribute(pContext, JSSymbols::parameterNames(pContext), out->parameterNames->asObject(pContext));
    meta = meta->setAttribute(pContext, JSSymbols::bytecode(pContext), out->pBytecode->asObject(pContext));
    // The function's name and source text as strings, made once here instead
    // of once per closure; the metadata keeps them alive.
    out->funcNameValue = out->funcName.empty()
        ? nullptr : protojs::jsStringFromUTF8(pContext, out->funcName);
    if (out->funcNameValue)
        meta = meta->setAttribute(pContext, JSSymbols::name(pContext), out->funcNameValue);
    out->funcSourceValue = out->funcSource.empty()
        ? nullptr : protojs::jsStringFromUTF8(pContext, out->funcSource);
    if (out->funcSourceValue)
        meta = meta->setAttribute(pContext, JSSymbols::sourceText(pContext), out->funcSourceValue);
    out->metadata = meta;

    return true;
}

bool loadBytecode(JSContext* ctx, void* bytecode, proto::ProtoContext* pContext,
                  ProtoBytecodeModule* out) {
    if (!out) return false;
    size_t nextGlobalId = 0;
    if (!loadBytecodeRecursive(ctx, bytecode, pContext, out, out->nestedFunctions, nextGlobalId))
        return false;
    /* Every nested function's bytecode IDs index `out->nestedFunctions`, the
     * one flat table built above.  Record that here, now that the vector has
     * stopped growing, so the interpreter can resolve an ID against the module
     * that actually owns it instead of inferring it from thread-local state.
     * `out` keeps ownerRoot null: it already owns the table. */
    for (auto& nf : out->nestedFunctions) nf.ownerRoot = out;
    /* Pre-resolve all atom operands so the interpreter never needs JSContext* at runtime. */
    preResolveAllAtoms(ctx, out, pContext);
    if (debughooks::enabled()) debughooks::moduleLoaded(*out);
    return true;
}

} // namespace protojs
