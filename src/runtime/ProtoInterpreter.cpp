#include "ProtoInterpreter.h"
#include "QuickJSOpcodeEnum.h"
#include "QuickJSBytecodeExport.h"
#include "../ProtoJSStringCache.h"
#include "../JSContext.h"
#include "quickjs.h"
#include "headers/protoCore.h"
#include <cstring>
#include <cstdlib>

namespace protojs {

namespace {

static inline uint32_t get_u32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline uint16_t get_u16(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

/** Resolve atom index to ProtoString using JSContext; cache in module. */
const proto::ProtoString* resolveAtom(ProtoBytecodeModule* mod, proto::ProtoContext* pContext, uint32_t atomIndex) {
    if (!mod || !mod->jsContext || !pContext) return nullptr;
    auto it = mod->atomToProto.find(atomIndex);
    if (it != mod->atomToProto.end()) return it->second;
    const char* str = JS_AtomToCString(mod->jsContext, (JSAtom)atomIndex);
    if (!str) return nullptr;
    const proto::ProtoString* ps = pContext->fromUTF8String(str)->asString(pContext);
    JS_FreeCString(mod->jsContext, str);
    if (ps) mod->atomToProto[atomIndex] = ps;
    return ps;
}

/** Check if obj is a bytecode function placeholder (has __bytecode_id__). Returns -1 if not. */
int getBytecodeId(proto::ProtoContext* pContext, const proto::ProtoObject* obj) {
    if (!obj || !pContext) return -1;
    const proto::ProtoString* key = ProtoJSStringCache::getKey(pContext, "__bytecode_id__");
    const proto::ProtoObject* val = obj->getAttribute(pContext, key, false);
    if (!val || val == PROTO_NONE || !val->isInteger(pContext)) return -1;
    long long id = val->asLong(pContext);
    return id >= 0 ? static_cast<int>(id) : -1;
}

} // namespace

const proto::ProtoObject* runBytecode(proto::ProtoContext* pContext,
                                      const ProtoBytecodeModule* module,
                                      const proto::ProtoObject* globalObj,
                                      JSContext* jsContextForAtoms) {
    if (!pContext || !module || !globalObj || !module->bytecode) return PROTO_NONE;
    const uint8_t* buf = module->buf();
    int len = module->bufLen();
    if (!buf || len <= 0) return PROTO_NONE;
    const std::vector<const proto::ProtoObject*>& cpool = module->protoCpool;
    const auto& nested = module->nestedFunctions;
    unsigned stackSize = module->stackSize();
    unsigned numLocals = module->argCount() + module->varCount();
    std::vector<const proto::ProtoObject*> stack;
    stack.reserve(stackSize + 16);
    std::vector<const proto::ProtoObject*> locals(numLocals, PROTO_NONE);
    int pc = 0;
    ProtoBytecodeModule* mod = const_cast<ProtoBytecodeModule*>(module);

    while (pc >= 0 && pc < len) {
        int opcode = buf[pc++];
        switch (opcode) {
            case OP_return: {
                if (stack.empty()) return PROTO_NONE;
                const proto::ProtoObject* result = stack.back();
                return result;
            }
            case OP_return_undef:
                return PROTO_NONE;
            case OP_drop:
                if (!stack.empty()) stack.pop_back();
                break;
            case OP_dup:
                if (!stack.empty()) stack.push_back(stack.back());
                break;
            case OP_push_const: {
                if (pc + 4 > len) return PROTO_NONE;
                uint32_t idx = get_u32(buf + pc);
                pc += 4;
                if (idx < cpool.size())
                    stack.push_back(cpool[idx]);
                else
                    stack.push_back(PROTO_NONE);
                break;
            }
            case OP_get_loc: {
                if (pc + 2 > len) return PROTO_NONE;
                uint16_t loc = get_u16(buf + pc);
                pc += 2;
                if (loc < locals.size())
                    stack.push_back(locals[loc]);
                else
                    stack.push_back(PROTO_NONE);
                break;
            }
            case OP_put_loc: {
                if (pc + 2 > len || stack.empty()) return PROTO_NONE;
                uint16_t loc = get_u16(buf + pc);
                pc += 2;
                const proto::ProtoObject* val = stack.back();
                stack.pop_back();
                if (loc < locals.size()) locals[loc] = val;
                break;
            }
            case OP_get_field: {
                if (pc + 4 > len || stack.empty()) return PROTO_NONE;
                uint32_t atomIndex = get_u32(buf + pc);
                pc += 4;
                const proto::ProtoObject* obj = stack.back();
                stack.pop_back();
                const proto::ProtoString* key = resolveAtom(mod, pContext, atomIndex);
                if (!key) { stack.push_back(PROTO_NONE); break; }
                const proto::ProtoObject* val = obj ? obj->getAttribute(pContext, key, true) : PROTO_NONE;
                stack.push_back(val && val != PROTO_NONE ? val : PROTO_NONE);
                break;
            }
            case OP_put_field: {
                if (pc + 4 > len || stack.size() < 2) return PROTO_NONE;
                uint32_t atomIndex = get_u32(buf + pc);
                pc += 4;
                const proto::ProtoObject* val = stack.back();
                stack.pop_back();
                const proto::ProtoObject* obj = stack.back();
                stack.pop_back();
                const proto::ProtoString* key = resolveAtom(mod, pContext, atomIndex);
                if (key && obj) {
                    const proto::ProtoObject* newObj = obj->setAttribute(pContext, key, val);
                    stack.push_back(newObj ? newObj : obj);
                }
                break;
            }
            case OP_object: {
                const proto::ProtoObject* newObj = pContext->newObject(true);
                stack.push_back(newObj);
                break;
            }
            case OP_undefined:
                stack.push_back(PROTO_NONE);
                break;
            case OP_null:
                stack.push_back(PROTO_NONE);
                break;
            case OP_push_false:
                stack.push_back(PROTO_FALSE);
                break;
            case OP_push_true:
                stack.push_back(PROTO_TRUE);
                break;
            case OP_call: {
                if (pc + 2 > len || stack.empty()) return PROTO_NONE;
                uint32_t argc = get_u16(buf + pc);
                pc += 2;
                if (stack.size() < argc + 1) return PROTO_NONE;
                const proto::ProtoObject* func = stack[stack.size() - argc - 1];
                int bcId = getBytecodeId(pContext, func);
                if (bcId >= 0 && static_cast<size_t>(bcId) < nested.size()) {
                    const auto& nf = nested[bcId];
                    const proto::ProtoList* args = pContext->newList();
                    for (uint32_t i = 0; i < argc; i++) {
                        args = args->appendLast(pContext, stack[stack.size() - argc + i]);
                    }
                    for (uint32_t i = 0; i <= argc; i++) stack.pop_back();
                    const proto::ProtoObject* thisVal = argc > 0 ? stack.empty() ? globalObj : stack.back() : globalObj;
                    ProtoBytecodeModule nestedMod;
                    nestedMod.bytecode = nf.first;
                    nestedMod.jsContext = module->jsContext;
                    nestedMod.protoCpool = nf.second;
                    nestedMod.nestedFunctions = nested;
                    const proto::ProtoObject* result = runBytecode(pContext, &nestedMod, thisVal, jsContextForAtoms);
                    stack.push_back(result ? result : PROTO_NONE);
                } else if (func && func->isMethod(pContext)) {
                    const proto::ProtoObject* thisVal = stack[stack.size() - argc - 1];
                    const proto::ProtoList* args = pContext->newList();
                    for (uint32_t i = 0; i < argc; i++)
                        args = args->appendLast(pContext, stack[stack.size() - argc + i]);
                    for (uint32_t i = 0; i <= argc; i++) stack.pop_back();
                    const proto::ProtoObject* result = func->call(pContext, nullptr,
                        ProtoJSStringCache::getKey(pContext, "call"), thisVal, args, nullptr);
                    stack.push_back(result ? result : PROTO_NONE);
                } else {
                    for (uint32_t i = 0; i <= argc; i++) stack.pop_back();
                    stack.push_back(PROTO_NONE);
                }
                break;
            }
            default:
                return PROTO_NONE;
        }
    }
    return PROTO_NONE;
}

} // namespace protojs
