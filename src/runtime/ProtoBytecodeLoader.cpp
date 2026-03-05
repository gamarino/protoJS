#include "ProtoBytecodeModule.h"
#include "QuickJSBytecodeExport.h"
#include "../ProtoJSStringCache.h"
#include "../TypeBridge.h"
#include "quickjs.h"
#include "headers/protoCore.h"

namespace protojs {

unsigned ProtoBytecodeModule::argCount() const {
    return bytecode ? protojs_bytecode_arg_count(bytecode) : 0;
}
unsigned ProtoBytecodeModule::varCount() const {
    return bytecode ? protojs_bytecode_var_count(bytecode) : 0;
}
unsigned ProtoBytecodeModule::stackSize() const {
    return bytecode ? protojs_bytecode_stack_size(bytecode) : 0;
}
const uint8_t* ProtoBytecodeModule::buf() const {
    return bytecode ? protojs_bytecode_buf(bytecode) : nullptr;
}
int ProtoBytecodeModule::bufLen() const {
    return bytecode ? protojs_bytecode_len(bytecode) : 0;
}

static bool loadBytecodeRecursive(JSContext* ctx, void* bytecode, proto::ProtoContext* pContext,
                                 ProtoBytecodeModule* out,
                                 std::vector<std::pair<void*, std::vector<const proto::ProtoObject*>>>* allNested) {
    if (!ctx || !bytecode || !pContext || !out) return false;
    const int cpoolCount = protojs_bytecode_cpool_count(bytecode);
    const void* cpoolPtr = protojs_bytecode_cpool(bytecode);
    if (!cpoolPtr && cpoolCount > 0) return false;

    out->bytecode = bytecode;
    out->jsContext = ctx;
    out->protoCpool.clear();
    out->protoCpool.reserve(static_cast<size_t>(cpoolCount));

    const JSValue* cpool = static_cast<const JSValue*>(cpoolPtr);
    for (int i = 0; i < cpoolCount; i++) {
        JSValue v = cpool[i];
        if (JS_IsFunction(ctx, v)) {
            void* nestedBc = protojs_get_function_bytecode(ctx, &v);
            if (!nestedBc) {
                out->protoCpool.push_back(PROTO_NONE);
                continue;
            }
            std::vector<const proto::ProtoObject*> nestedCpool;
            ProtoBytecodeModule nestedMod;
            nestedMod.bytecode = nestedBc;
            nestedMod.jsContext = ctx;
            if (!loadBytecodeRecursive(ctx, nestedBc, pContext, &nestedMod, allNested))
                return false;
            nestedCpool = std::move(nestedMod.protoCpool);
            allNested->push_back({nestedBc, std::move(nestedCpool)});
            size_t id = allNested->size() - 1;
            const proto::ProtoString* key = ProtoJSStringCache::getKey(pContext, "__bytecode_id__");
            const proto::ProtoObject* placeholder = pContext->newObject(true);
            placeholder = placeholder->setAttribute(pContext, key, pContext->fromInteger(static_cast<long long>(id)));
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
    out->nestedFunctions.clear();
    return loadBytecodeRecursive(ctx, bytecode, pContext, out, &out->nestedFunctions);
}

} // namespace protojs
