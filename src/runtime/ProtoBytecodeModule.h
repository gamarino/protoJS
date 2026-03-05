#ifndef PROTOJS_PROTO_BYTECODE_MODULE_H
#define PROTOJS_PROTO_BYTECODE_MODULE_H

/**
 * Loaded representation of QuickJS bytecode for the ProtoCore interpreter.
 * Holds bytecode pointer, proto constant pool, and optional atom cache.
 */

#include "headers/protoCore.h"
#include <vector>
#include <unordered_map>
#include <cstdint>

struct JSContext;

namespace protojs {

struct ProtoBytecodeModule {
    /** QuickJS bytecode (valid while compile JSContext is alive). */
    void* bytecode{nullptr};
    /** Compile context for atom resolution (JS_AtomToCString). */
    JSContext* jsContext{nullptr};
    /** Proto constant pool: cpool[i] -> ProtoObject. */
    std::vector<const proto::ProtoObject*> protoCpool;
    /** Nested bytecode functions: id -> (bytecode, protoCpool). Interpreter uses this for call. */
    std::vector<std::pair<void*, std::vector<const proto::ProtoObject*>>> nestedFunctions;
    /** Lazily filled: atom index -> ProtoString (for get_field etc.). */
    std::unordered_map<uint32_t, const proto::ProtoString*> atomToProto;

    unsigned argCount() const;
    unsigned varCount() const;
    unsigned stackSize() const;
    const uint8_t* buf() const;
    int bufLen() const;
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
