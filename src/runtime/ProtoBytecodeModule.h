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

    /** Native bytecode storage (GC-managed by protoCore). */
    const proto::ProtoByteBuffer* pBytecode{nullptr};

    /** Function metadata object holding all attributes and caching lists. */
    const proto::ProtoObject* metadata{nullptr};

    /** Pre-built positional parameter names for standard context init. */
    const proto::ProtoList* parameterNames{nullptr};

    /** Constant pool stored as a native ProtoList for GC safety. */
    const proto::ProtoList* constantPool{nullptr};

    /** Interned closure variable symbols for O(1) resolution. */
    const proto::ProtoList* closureSymbols{nullptr};

    /** Cached metadata values for fast interpreter access. */
    uint16_t argCount_{0};
    uint16_t varCount_{0};
    uint16_t stackSize_{0};

    /** Nested bytecode functions, fully translated to ProtoBytecodeModule. */
    std::vector<ProtoBytecodeModule> nestedFunctions;
    /** Lazily filled: atom index -> ProtoString (for get_field etc.).
     *  Note: atomToProto is still a map for O(1) resolution at runtime, 
     *  but we must ensure these symbols are rooted elsewhere (e.g. in metadata). */
    std::unordered_map<uint32_t, const proto::ProtoString*> atomToProto;

    /** Closure variable names for legacy/debugging. */
    std::vector<std::string> closureVarNames;
    /** Whether each closure var is lexical (const/let = true, var = false). */
    std::vector<bool> closureVarIsLexical;
    /** Whether each closure var is a declared global. */
    std::vector<bool> closureVarIsDeclared;
    /** Full closure type for each closure var. */
    std::vector<int> closureVarTypes;
    /** Index into the parent function's corresponding slot array. */
    std::vector<uint16_t> closureVarIndices;

    /** Boolean flags for quick access. */
    bool isStrict{false};
    bool isArrow{false};
    bool isAsync{false};
    bool isGenerator{false};

    /** True iff the bytecode reads the call-time argument list — set when
     *  the body contains OP_special_object kind=0/1 (arguments object),
     *  OP_rest (rest parameter), or OP_init_ctor (super(...) forwarding via
     *  t_activeArgs).  Computed once at module load.  When false, the
     *  caller can skip building argsList entirely and pass nullptr to
     *  runBytecode, saving one ProtoListSmallImpl allocation per call. */
    bool usesArguments{false};

    /** The function's declared name. */
    std::string funcName;

    /** The function's source text, populated at load time from the
     *  QuickJS bytecode's debug.source field.  Empty for top-level
     *  modules (and for functions whose bytecode lacks debug info).
     *  Used by Function.prototype.toString to surface the real
     *  source instead of the spec-default "[native code]" template.
     *
     *  For nested function declarations / expressions, QuickJS's
     *  compiler records the *entire* "function name(args) { body }"
     *  span (or the equivalent arrow / method short-form), so this
     *  string is directly usable as toString output. */
    std::string funcSource;

    unsigned argCount() const { return argCount_; }
    unsigned varCount() const { return varCount_; }
    unsigned stackSize() const { return stackSize_; }
    const uint8_t* buf(proto::ProtoContext* ctx) const {
        return pBytecode ? reinterpret_cast<const uint8_t*>(pBytecode->getBuffer(ctx)) : nullptr;
    }
    int bufLen(proto::ProtoContext* ctx) const {
        return pBytecode ? static_cast<int>(pBytecode->getSize(ctx)) : 0;
    }
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
