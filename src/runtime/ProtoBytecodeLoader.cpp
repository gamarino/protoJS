#include "ProtoBytecodeModule.h"
#include "QuickJSBytecodeExport.h"
#include "QuickJSOpcodeEnum.h"
#include "../JSSymbols.h"
#include "../TypeBridge.h"
#include "quickjs.h"
#include "headers/protoCore.h"

namespace protojs {

/**
 * Build opcode total-size table indexed by opcode byte value.
 * Must match QuickJS's OPCodeEnum (DEF-only, no def): sizes[byte] = total
 * instruction size (opcode + operands) for final-bytecode opcodes.
 * def() opcodes are phase-1 temporaries and never appear in final bytecode.
 */
static const uint8_t* getOpSizes() {
    static const uint8_t sizes[] = {
#define FMT(f)
#define DEF(id, size, n_pop, n_push, f) (uint8_t)(size),
#define def(id, size, n_pop, n_push, f)
#include "quickjs-opcode.h"
#undef def
#undef DEF
#undef FMT
        0 /* sentinel */
    };
    return sizes;
}

/**
 * Pre-resolve all atom operands in the bytecode into the module's atomToProto cache.
 * This must be called while ctx is still valid (at load time), so that the interpreter
 * never needs JSContext* for atom lookup at execution time.
 */
static void preResolveAllAtoms(JSContext* ctx, ProtoBytecodeModule* mod,
                                proto::ProtoContext* pContext) {
    if (!ctx || !mod || !pContext) return;
    const uint8_t* buf = mod->buf();
    int len = mod->bufLen();
    if (!buf || len <= 0) return;

    const uint8_t* sizes = getOpSizes();
    const int maxOp = static_cast<int>(OP_COUNT) - 1; /* last real opcode byte value */

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
                        op == OP_set_name         ||
                        op == OP_make_var_ref     ||
                        op == OP_delete_var       ||
                        op == OP_define_method    ||
                        op == OP_define_class     ||
                        op == OP_define_class_computed);
        if (hasAtom && pc + 5 <= len) {
            uint32_t atomIndex = (uint32_t)buf[pc+1] | ((uint32_t)buf[pc+2] << 8) |
                                 ((uint32_t)buf[pc+3] << 16) | ((uint32_t)buf[pc+4] << 24);
            if (mod->atomToProto.find(atomIndex) == mod->atomToProto.end()) {
                const char* str = JS_AtomToCString(ctx, (JSAtom)atomIndex);
                if (str) {
                    const proto::ProtoObject* so = pContext->fromUTF8String(str);
                    const proto::ProtoString* ps = so ? so->asString(pContext) : nullptr;
                    JS_FreeCString(ctx, str);
                    if (ps) mod->atomToProto[atomIndex] = ps;
                }
            }
        }
        /* Advance by opcode size (includes opcode byte itself). */
        uint8_t sz = (op <= static_cast<uint8_t>(maxOp)) ? sizes[op] : 0;
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
static bool loadBytecodeRecursive(JSContext* ctx,
                                  void* quickjsBytecode,
                                  proto::ProtoContext* pContext,
                                  ProtoBytecodeModule* out,
                                  std::vector<ProtoBytecodeModule>& allFunctions,
                                  size_t& nextGlobalId) {
    if (!ctx || !quickjsBytecode || !pContext || !out) return false;

    out->jsContext = ctx;

    // Copy raw bytecode buffer and metadata from the QuickJS function.
    const uint8_t* buf = protojs_bytecode_buf(quickjsBytecode);
    int len = protojs_bytecode_len(quickjsBytecode);
    if (!buf || len <= 0) return false;
    out->bytecode.assign(buf, buf + len);
    out->argCount_ = protojs_bytecode_arg_count(quickjsBytecode);
    out->varCount_ = protojs_bytecode_var_count(quickjsBytecode);
    out->stackSize_ = protojs_bytecode_stack_size(quickjsBytecode);
    out->isStrict = protojs_bytecode_is_strict(quickjsBytecode) != 0;
    out->isArrow  = protojs_bytecode_is_arrow(quickjsBytecode) != 0;
    const char* funcNameCstr = protojs_bytecode_func_name(ctx, quickjsBytecode);
    out->funcName = funcNameCstr ? funcNameCstr : "";
    if (funcNameCstr) JS_FreeCString(ctx, funcNameCstr);

    const int closureVarCount = protojs_bytecode_closure_var_count(quickjsBytecode);
    out->closureVarNames.clear();
    out->closureVarNames.reserve(static_cast<size_t>(closureVarCount));
    out->closureVarIsLexical.clear();
    out->closureVarIsLexical.reserve(static_cast<size_t>(closureVarCount));
    out->closureVarIsDeclared.clear();
    out->closureVarIsDeclared.reserve(static_cast<size_t>(closureVarCount));
    out->closureVarTypes.clear();
    out->closureVarTypes.reserve(static_cast<size_t>(closureVarCount));
    out->closureVarIndices.clear();
    out->closureVarIndices.reserve(static_cast<size_t>(closureVarCount));
    for (int i = 0; i < closureVarCount; i++) {
        const char* name = protojs_bytecode_closure_var_name(ctx, quickjsBytecode, static_cast<uint16_t>(i));
        out->closureVarNames.push_back(name ? name : "");
        out->closureVarIsLexical.push_back(
            protojs_bytecode_closure_var_is_lexical(quickjsBytecode, static_cast<uint16_t>(i)) != 0);
        // JS_CLOSURE_GLOBAL_DECL = 4: var-declared global; must be hoisted to undefined.
        int ctype = protojs_bytecode_closure_var_type(quickjsBytecode, static_cast<uint16_t>(i));
        out->closureVarIsDeclared.push_back(ctype == 4 /* JS_CLOSURE_GLOBAL_DECL */);
        out->closureVarTypes.push_back(ctype);
        out->closureVarIndices.push_back(
            protojs_bytecode_closure_var_idx(quickjsBytecode, static_cast<uint16_t>(i)));
        if (name) JS_FreeCString(ctx, name);
    }

    // Translate constant pool to ProtoObject values and nested functions.
    const int cpoolCount = protojs_bytecode_cpool_count(quickjsBytecode);
    const void* cpoolPtr = protojs_bytecode_cpool(quickjsBytecode);
    if (!cpoolPtr && cpoolCount > 0) return false;

    out->protoCpool.clear();
    out->protoCpool.reserve(static_cast<size_t>(cpoolCount));
    out->nestedFunctions.clear();

    const JSValue* cpool = static_cast<const JSValue*>(cpoolPtr);
    for (int i = 0; i < cpoolCount; i++) {
        JSValue v = cpool[i];
        /* Use protojs_get_function_bytecode directly: in compile-only mode, nested
         * functions in the cpool have tag JS_TAG_FUNCTION_BYTECODE (not JS_TAG_OBJECT),
         * so JS_IsFunction() returns FALSE for them. protojs_get_function_bytecode
         * handles both JS_TAG_FUNCTION_BYTECODE and JS_TAG_OBJECT (function) cases. */
        void* nestedQuickjsBytecode = protojs_get_function_bytecode(ctx, &v);
        if (nestedQuickjsBytecode) {
            // Post-order loading: recursively load all descendants into allFunctions
            // first (they get lower IDs), then assign this function's globally unique ID
            // and append it.  Loading into a local temp avoids any dangling-pointer
            // hazard from vector reallocation during the recursive call.
            ProtoBytecodeModule nestedMod;
            if (!loadBytecodeRecursive(ctx, nestedQuickjsBytecode, pContext,
                                       &nestedMod, allFunctions, nextGlobalId))
                return false;

            size_t globalId = nextGlobalId++;
            allFunctions.push_back(std::move(nestedMod));
            // allFunctions[globalId] is now valid (just appended).

            const proto::ProtoString* key = JSSymbols::bytecodeId(pContext);
            const proto::ProtoObject* placeholder = pContext->newObject(true);
            placeholder = placeholder->setAttribute(
                pContext, key, pContext->fromInteger(static_cast<long long>(globalId)));
            out->protoCpool.push_back(placeholder);
        } else {
            const proto::ProtoObject* obj = TypeBridge::fromJS(ctx, v, pContext);
            out->protoCpool.push_back(obj ? obj : PROTO_NONE);
        }
    }
    return true;
}

bool loadBytecode(JSContext* ctx, void* bytecode, proto::ProtoContext* pContext,
                  ProtoBytecodeModule* out) {
    if (!out) return false;
    // Pass out->nestedFunctions as the shared flat list.  All functions at every
    // depth end up there with globally unique IDs, so the interpreter can always
    // resolve bcId via t_rootModule->nestedFunctions[bcId].
    size_t nextGlobalId = 0;
    if (!loadBytecodeRecursive(ctx, bytecode, pContext, out, out->nestedFunctions, nextGlobalId))
        return false;
    /* Pre-resolve all atom operands so the interpreter never needs JSContext* at runtime. */
    preResolveAllAtoms(ctx, out, pContext);
    return true;
}

} // namespace protojs
