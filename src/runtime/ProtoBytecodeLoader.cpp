#include "ProtoBytecodeModule.h"
#include "QuickJSBytecodeExport.h"
#include "../ProtoJSStringCache.h"
#include "../TypeBridge.h"
#include "quickjs.h"
#include "headers/protoCore.h"

namespace protojs {

static bool loadBytecodeRecursive(JSContext* ctx,
                                  void* quickjsBytecode,
                                  proto::ProtoContext* pContext,
                                  ProtoBytecodeModule* out) {
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
        if (JS_IsFunction(ctx, v)) {
            void* nestedQuickjsBytecode = protojs_get_function_bytecode(ctx, &v);
            if (!nestedQuickjsBytecode) {
                out->protoCpool.push_back(PROTO_NONE);
                continue;
            }

            // Recursively load nested function into its own module.
            ProtoBytecodeModule nestedMod;
            if (!loadBytecodeRecursive(ctx, nestedQuickjsBytecode, pContext, &nestedMod))
                return false;

            out->nestedFunctions.push_back(std::move(nestedMod));
            size_t id = out->nestedFunctions.size() - 1;

            const proto::ProtoString* key = ProtoJSStringCache::getKey(pContext, "__bytecode_id__");
            const proto::ProtoObject* placeholder = pContext->newObject(true);
            placeholder = placeholder->setAttribute(
                pContext, key, pContext->fromInteger(static_cast<long long>(id)));
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
    return loadBytecodeRecursive(ctx, bytecode, pContext, out);
}

} // namespace protojs
