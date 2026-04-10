#ifndef PROTOJS_PROTO_BYTECODE_MODULE_H
#define PROTOJS_PROTO_BYTECODE_MODULE_H

/**
 * Loaded representation of compiled JavaScript for the ProtoCore interpreter.
 * Holds a copy of the bytecode buffer, proto constant pool, and optional atom cache.
 */

#include "headers/protoCore.h"
#include <vector>
#include <unordered_map>
#include <cstdint>

struct JSContext;

namespace protojs {

struct ProtoBytecodeModule {
    /** Compile context for atom resolution (JS_AtomToCString). */
    JSContext* jsContext{nullptr};

    /** Copy of the bytecode buffer (no dependency on JSFunctionBytecode lifetime). */
    std::vector<uint8_t> bytecode;

    /** Function metadata copied from the frontend. */
    uint16_t argCount_{0};
    uint16_t varCount_{0};
    uint16_t stackSize_{0};

    /** Proto constant pool: cpool[i] -> ProtoObject. */
    std::vector<const proto::ProtoObject*> protoCpool;

    /** Nested bytecode functions, fully translated to ProtoBytecodeModule. */
    std::vector<ProtoBytecodeModule> nestedFunctions;
    /** Lazily filled: atom index -> ProtoString (for get_field etc.). */
    std::unordered_map<uint32_t, const proto::ProtoString*> atomToProto;
    /** Closure variable names for OP_get_var / OP_put_var (index -> name). */
    std::vector<std::string> closureVarNames;
    /** Whether each closure var is lexical (const/let = true, var = false). */
    std::vector<bool> closureVarIsLexical;
    /** Whether each closure var is a declared global (var x; hoistable to undefined).
     *  True when closure_type == JS_CLOSURE_GLOBAL_DECL (4). */
    std::vector<bool> closureVarIsDeclared;
    /** Full closure type for each closure var (JSClosureTypeEnum values):
     *  0=LOCAL (parent local), 1=ARG (parent arg), 2=REF (parent closure var),
     *  3=GLOBAL_REF, 4=GLOBAL_DECL, 5=GLOBAL, 6=MODULE_DECL, 7=MODULE_IMPORT. */
    std::vector<int> closureVarTypes;
    /** Index into the parent function's corresponding slot array (var_idx field).
     *  Meaning depends on closureVarTypes[i]:
     *  type 0 (LOCAL)  → parent local var slot index (relative to argCount)
     *  type 1 (ARG)    → parent arg slot index (0-based)
     *  type 2 (REF)    → parent closure var index (relative to argCount+varCount) */
    std::vector<uint16_t> closureVarIndices;
    /** True when the function was compiled with "use strict" (JS_MODE_STRICT). */
    bool isStrict{false};
    /** The function's declared name (empty for anonymous functions). */
    std::string funcName;
    /** True when this function is an arrow function (no own this/arguments). */
    bool isArrow{false};

    unsigned argCount() const { return argCount_; }
    unsigned varCount() const { return varCount_; }
    unsigned stackSize() const { return stackSize_; }
    const uint8_t* buf() const { return bytecode.empty() ? nullptr : bytecode.data(); }
    int bufLen() const { return static_cast<int>(bytecode.size()); }
};

/**
 * Load QuickJS bytecode into a ProtoBytecodeModule. Converts cpool to ProtoObject
 * using pContext (fromInteger, fromDouble, fromUTF8String, etc.). Nested functions
 * are recursively loaded and stored in nestedFunctions; their cpool index is
 * replaced by a placeholder ProtoObject that the interpreter recognizes.
 */
bool loadBytecode(JSContext* ctx, void* bytecode, proto::ProtoContext* pContext,
                  ProtoBytecodeModule* out);

} // namespace protojs

#endif /* PROTOJS_PROTO_BYTECODE_MODULE_H */
