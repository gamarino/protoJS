#include "ProtoInterpreter.h"
#include "QuickJSOpcodeEnum.h"
#include "QuickJSBytecodeExport.h"
#include "../ProtoJSStringCache.h"
#include "../JSContext.h"
#include "quickjs.h"
#include "headers/protoCore.h"
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <limits>

namespace protojs {

namespace {

static inline uint32_t get_u32(const uint8_t* p) {
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline uint16_t get_u16(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

/** JS-style truthiness, implemented on top of protoCore primitives. */
static bool toBool(proto::ProtoContext* context, const proto::ProtoObject* value) {
    if (!context) return false;
    if (!value || value == PROTO_NONE || value->isNone(context)) return false;
    if (value == PROTO_TRUE) return true;
    if (value == PROTO_FALSE) return false;
    if (value->isBoolean(context)) return value->asBoolean(context);

    if (value->isInteger(context)) {
        return value->asLong(context) != 0;
    }

    if (value->isDouble(context) || value->isFloat(context)) {
        const double v = value->asDouble(context);
        if (v == 0.0 || std::isnan(v)) return false;
        return true;
    }

    if (value->isString(context)) {
        const proto::ProtoString* s = value->asString(context);
        if (!s) return false;
        return s->getSize(context) != 0;
    }

    // Objects, lists, sets, etc. are always truthy.
    return true;
}

/** JS-style ToNumber conversion, returning a protoCore number object. */
static const proto::ProtoObject* toNumber(proto::ProtoContext* context,
                                          const proto::ProtoObject* value) {
    if (!context) return PROTO_NONE;

    auto makeNaN = [&]() -> const proto::ProtoObject* {
        const double nan = std::numeric_limits<double>::quiet_NaN();
        return context->fromDouble(nan);
    };

    if (!value || value == PROTO_NONE || value->isNone(context)) {
        // ToNumber(undefined) is NaN; ToNumber(null) is +0. We currently
        // represent both as PROTO_NONE; prefer NaN for safety.
        return makeNaN();
    }

    if (value->isInteger(context) || value->isDouble(context) || value->isFloat(context)) {
        // Already a numeric primitive.
        return value;
    }

    if (value->isBoolean(context)) {
        const bool b = value->asBoolean(context);
        return context->fromInteger(b ? 1LL : 0LL);
    }

    if (value->isString(context)) {
        const proto::ProtoString* s = value->asString(context);
        if (!s) return makeNaN();
        std::string tmp;
        s->toUTF8String(context, tmp);
        return context->fromString(tmp.c_str(), 10);
    }

    // TODO: Implement full ToPrimitive/ToNumber for objects (valueOf/toString chain).
    return makeNaN();
}

/** JS-style ToString conversion, returning a protoCore string object. */
static const proto::ProtoObject* toString(proto::ProtoContext* context,
                                          const proto::ProtoObject* value) {
    if (!context) return PROTO_NONE;

    if (!value || value == PROTO_NONE || value->isNone(context)) {
        // We currently map "no value" to "undefined" in string context.
        return context->fromUTF8String("undefined");
    }

    if (value->isString(context)) {
        const proto::ProtoString* s = value->asString(context);
        return s ? s->asObject(context) : context->fromUTF8String("");
    }

    if (value->isBoolean(context)) {
        return context->fromUTF8String(value->asBoolean(context) ? "true" : "false");
    }

    if (value->isInteger(context)) {
        const long long v = value->asLong(context);
        const std::string tmp = std::to_string(v);
        return context->fromUTF8String(tmp.c_str());
    }

    if (value->isDouble(context) || value->isFloat(context)) {
        const double v = value->asDouble(context);
        const std::string tmp = std::to_string(v);
        return context->fromUTF8String(tmp.c_str());
    }

    // Generic object fallback matches typical "[object Object]" shape.
    return context->fromUTF8String("[object Object]");
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
                                      const proto::ProtoObject* thisObj,
                                      const proto::ProtoList* args,
                                      const proto::ProtoObject* globalObj,
                                      JSContext* jsContextForAtoms) {
    if (!pContext || !module) return PROTO_NONE;
    const uint8_t* buf = module->buf();
    int len = module->bufLen();
    if (!buf || len <= 0) return PROTO_NONE;
    const std::vector<const proto::ProtoObject*>& cpool = module->protoCpool;
    const auto& nested = module->nestedFunctions;
    unsigned stackSize = module->stackSize();
    unsigned argCount = module->argCount();
    unsigned varCount = module->varCount();
    unsigned numLocals = argCount + varCount;
    std::vector<const proto::ProtoObject*> stack;
    stack.reserve(stackSize + 16);
    std::vector<const proto::ProtoObject*> locals(numLocals, PROTO_NONE);
    // Bind formal parameters from the provided argument list.
    if (args) {
        for (unsigned i = 0; i < argCount; ++i) {
            const proto::ProtoObject* v = args->getAt(pContext, static_cast<int>(i));
            locals[i] = v ? v : PROTO_NONE;
        }
    }
    int pc = 0;
    ProtoBytecodeModule* mod = const_cast<ProtoBytecodeModule*>(module);

    while (pc >= 0 && pc < len) {
        int opcode = buf[pc++];
        switch (opcode) {
            // --- Constant and immediate pushes ---
            case OP_push_minus1:
                stack.push_back(pContext->fromInteger(-1));
                break;
            case OP_push_0:
                stack.push_back(pContext->fromInteger(0));
                break;
            case OP_push_1:
                stack.push_back(pContext->fromInteger(1));
                break;
            case OP_push_2:
                stack.push_back(pContext->fromInteger(2));
                break;
            case OP_push_3:
                stack.push_back(pContext->fromInteger(3));
                break;
            case OP_push_4:
                stack.push_back(pContext->fromInteger(4));
                break;
            case OP_push_5:
                stack.push_back(pContext->fromInteger(5));
                break;
            case OP_push_6:
                stack.push_back(pContext->fromInteger(6));
                break;
            case OP_push_7:
                stack.push_back(pContext->fromInteger(7));
                break;
            case OP_push_i8: {
                if (pc + 1 > len) return PROTO_NONE;
                int8_t v = static_cast<int8_t>(buf[pc++]);
                stack.push_back(pContext->fromInteger(static_cast<long long>(v)));
                break;
            }
            case OP_push_i16: {
                if (pc + 2 > len) return PROTO_NONE;
                int16_t v = static_cast<int16_t>(get_u16(buf + pc));
                pc += 2;
                stack.push_back(pContext->fromInteger(static_cast<long long>(v)));
                break;
            }
            case OP_push_i32: {
                // push_i32 encodes a 32-bit signed immediate.
                if (pc + 4 > len) return PROTO_NONE;
                int32_t v = (int32_t)get_u32(buf + pc);
                pc += 4;
                stack.push_back(pContext->fromInteger(static_cast<long long>(v)));
                break;
            }
            case OP_push_const8: {
                if (pc + 1 > len) return PROTO_NONE;
                uint8_t idx = buf[pc++];
                if (idx < cpool.size())
                    stack.push_back(cpool[idx]);
                else
                    stack.push_back(PROTO_NONE);
                break;
            }
            case OP_push_empty_string:
                stack.push_back(pContext->fromUTF8String(""));
                break;
            case OP_push_this:
                // Use the current frame's `this` binding; fall back to global object.
                stack.push_back(thisObj ? thisObj : (globalObj ? globalObj : PROTO_NONE));
                break;
            case OP_special_object: {
                // TODO: Implement arguments/new.target/etc. wiring.
                if (pc + 1 > len) return PROTO_NONE;
                pc += 1; // skip kind
                stack.push_back(PROTO_NONE);
                break;
            }
            case OP_rest: {
                // TODO: Implement rest parameter materialization once call/arg opcodes are wired.
                if (pc + 2 > len) return PROTO_NONE;
                pc += 2; // skip u16 argument index
                stack.push_back(PROTO_NONE);
                break;
            }
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
            case OP_nip:
                if (stack.size() < 2) return PROTO_NONE;
                // a b -> b (drop next-to-top)
                stack[stack.size() - 2] = stack.back();
                stack.pop_back();
                break;
            case OP_nip1:
                if (stack.size() < 3) return PROTO_NONE;
                // a b c -> b c (drop bottom of top-3 window)
                stack.erase(stack.end() - 3);
                break;
            case OP_dup:
                if (!stack.empty()) stack.push_back(stack.back());
                break;
            case OP_dup1:
                if (stack.size() < 2) return PROTO_NONE;
                // a b -> a a b
                stack.insert(stack.end() - 1, stack[stack.size() - 2]);
                break;
            case OP_dup2:
                if (stack.size() < 2) return PROTO_NONE;
                // a b -> a b a b
                stack.push_back(stack[stack.size() - 2]);
                stack.push_back(stack[stack.size() - 2]); // last element before push is original b
                break;
            case OP_dup3:
                if (stack.size() < 3) return PROTO_NONE;
                // a b c -> a b c a b c
                stack.push_back(stack[stack.size() - 3]);
                stack.push_back(stack[stack.size() - 3]); // now original b
                stack.push_back(stack[stack.size() - 3]); // now original c
                break;
            case OP_insert2:
                if (stack.size() < 2) return PROTO_NONE;
                // obj a -> a obj a
                {
                    const proto::ProtoObject* a = stack.back();
                    stack.pop_back();
                    const proto::ProtoObject* obj = stack.back();
                    stack.pop_back();
                    stack.push_back(a);
                    stack.push_back(obj);
                    stack.push_back(a);
                }
                break;
            case OP_insert3:
                if (stack.size() < 3) return PROTO_NONE;
                // obj prop a -> a obj prop a
                {
                    const proto::ProtoObject* a = stack.back();
                    stack.pop_back();
                    const proto::ProtoObject* prop = stack.back();
                    stack.pop_back();
                    const proto::ProtoObject* obj = stack.back();
                    stack.pop_back();
                    stack.push_back(a);
                    stack.push_back(obj);
                    stack.push_back(prop);
                    stack.push_back(a);
                }
                break;
            case OP_insert4:
                if (stack.size() < 4) return PROTO_NONE;
                // this obj prop a -> a this obj prop a
                {
                    const proto::ProtoObject* a = stack.back();
                    stack.pop_back();
                    const proto::ProtoObject* prop = stack.back();
                    stack.pop_back();
                    const proto::ProtoObject* obj = stack.back();
                    stack.pop_back();
                    const proto::ProtoObject* thisVal = stack.back();
                    stack.pop_back();
                    stack.push_back(a);
                    stack.push_back(thisVal);
                    stack.push_back(obj);
                    stack.push_back(prop);
                    stack.push_back(a);
                }
                break;
            case OP_perm3:
                if (stack.size() < 3) return PROTO_NONE;
                // obj a b -> a obj b
                {
                    const proto::ProtoObject* b = stack.back();
                    stack.pop_back();
                    const proto::ProtoObject* a = stack.back();
                    stack.pop_back();
                    const proto::ProtoObject* obj = stack.back();
                    stack.pop_back();
                    stack.push_back(a);
                    stack.push_back(obj);
                    stack.push_back(b);
                }
                break;
            case OP_perm4:
                if (stack.size() < 4) return PROTO_NONE;
                // obj prop a b -> a obj prop b
                {
                    const proto::ProtoObject* b = stack.back();
                    stack.pop_back();
                    const proto::ProtoObject* a = stack.back();
                    stack.pop_back();
                    const proto::ProtoObject* prop = stack.back();
                    stack.pop_back();
                    const proto::ProtoObject* obj = stack.back();
                    stack.pop_back();
                    stack.push_back(a);
                    stack.push_back(obj);
                    stack.push_back(prop);
                    stack.push_back(b);
                }
                break;
            case OP_perm5:
                if (stack.size() < 5) return PROTO_NONE;
                // this obj prop a b -> a this obj prop b
                {
                    const proto::ProtoObject* b = stack.back();
                    stack.pop_back();
                    const proto::ProtoObject* a = stack.back();
                    stack.pop_back();
                    const proto::ProtoObject* prop = stack.back();
                    stack.pop_back();
                    const proto::ProtoObject* obj = stack.back();
                    stack.pop_back();
                    const proto::ProtoObject* thisVal = stack.back();
                    stack.pop_back();
                    stack.push_back(a);
                    stack.push_back(thisVal);
                    stack.push_back(obj);
                    stack.push_back(prop);
                    stack.push_back(b);
                }
                break;
            case OP_swap:
                if (stack.size() < 2) return PROTO_NONE;
                std::swap(stack[stack.size() - 1], stack[stack.size() - 2]);
                break;
            case OP_swap2:
                if (stack.size() < 4) return PROTO_NONE;
                // a b c d -> c d a b
                {
                    const proto::ProtoObject* d = stack.back();
                    stack.pop_back();
                    const proto::ProtoObject* c = stack.back();
                    stack.pop_back();
                    const proto::ProtoObject* b = stack.back();
                    stack.pop_back();
                    const proto::ProtoObject* a = stack.back();
                    stack.pop_back();
                    stack.push_back(c);
                    stack.push_back(d);
                    stack.push_back(a);
                    stack.push_back(b);
                }
                break;
            case OP_rot3l:
                if (stack.size() < 3) return PROTO_NONE;
                // x a b -> a b x
                {
                    const proto::ProtoObject* b = stack.back();
                    stack.pop_back();
                    const proto::ProtoObject* a = stack.back();
                    stack.pop_back();
                    const proto::ProtoObject* x = stack.back();
                    stack.pop_back();
                    stack.push_back(a);
                    stack.push_back(b);
                    stack.push_back(x);
                }
                break;
            case OP_rot3r:
                if (stack.size() < 3) return PROTO_NONE;
                // a b x -> x a b
                {
                    const proto::ProtoObject* x = stack.back();
                    stack.pop_back();
                    const proto::ProtoObject* b = stack.back();
                    stack.pop_back();
                    const proto::ProtoObject* a = stack.back();
                    stack.pop_back();
                    stack.push_back(x);
                    stack.push_back(a);
                    stack.push_back(b);
                }
                break;
            case OP_rot4l:
                if (stack.size() < 4) return PROTO_NONE;
                // x a b c -> a b c x
                {
                    const proto::ProtoObject* c = stack.back();
                    stack.pop_back();
                    const proto::ProtoObject* b = stack.back();
                    stack.pop_back();
                    const proto::ProtoObject* a = stack.back();
                    stack.pop_back();
                    const proto::ProtoObject* x = stack.back();
                    stack.pop_back();
                    stack.push_back(a);
                    stack.push_back(b);
                    stack.push_back(c);
                    stack.push_back(x);
                }
                break;
            case OP_rot5l:
                if (stack.size() < 5) return PROTO_NONE;
                // x a b c d -> a b c d x
                {
                    const proto::ProtoObject* d = stack.back();
                    stack.pop_back();
                    const proto::ProtoObject* c = stack.back();
                    stack.pop_back();
                    const proto::ProtoObject* b = stack.back();
                    stack.pop_back();
                    const proto::ProtoObject* a = stack.back();
                    stack.pop_back();
                    const proto::ProtoObject* x = stack.back();
                    stack.pop_back();
                    stack.push_back(a);
                    stack.push_back(b);
                    stack.push_back(c);
                    stack.push_back(d);
                    stack.push_back(x);
                }
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
            case OP_push_atom_value: {
                if (pc + 4 > len) return PROTO_NONE;
                uint32_t atomIndex = get_u32(buf + pc);
                pc += 4;
                const proto::ProtoString* key = resolveAtom(mod, pContext, atomIndex);
                if (!key || !globalObj) {
                    stack.push_back(PROTO_NONE);
                    break;
                }
                const proto::ProtoObject* val = globalObj->getAttribute(pContext, key, true);
                stack.push_back(val ? val : PROTO_NONE);
                break;
            }
            // Short local/arg accessors (loc8/arg8 and loc0-3/arg0-3)
            case OP_get_loc8: {
                if (pc + 1 > len) return PROTO_NONE;
                uint8_t locIndex = buf[pc++];
                if (locIndex < varCount && (argCount + locIndex) < locals.size())
                    stack.push_back(locals[argCount + locIndex]);
                else
                    stack.push_back(PROTO_NONE);
                break;
            }
            case OP_put_loc8: {
                if (pc + 1 > len || stack.empty()) return PROTO_NONE;
                uint8_t locIndex = buf[pc++];
                const proto::ProtoObject* val = stack.back();
                stack.pop_back();
                if (locIndex < varCount && (argCount + locIndex) < locals.size())
                    locals[argCount + locIndex] = val;
                break;
            }
            case OP_set_loc8: {
                if (pc + 1 > len || stack.empty()) return PROTO_NONE;
                uint8_t locIndex = buf[pc++];
                const proto::ProtoObject* val = stack.back();
                if (locIndex < varCount && (argCount + locIndex) < locals.size())
                    locals[argCount + locIndex] = val;
                break;
            }
            case OP_get_arg0:
            case OP_get_arg1:
            case OP_get_arg2:
            case OP_get_arg3: {
                unsigned idx = static_cast<unsigned>(opcode - OP_get_arg0);
                if (idx < argCount && idx < locals.size())
                    stack.push_back(locals[idx]);
                else
                    stack.push_back(PROTO_NONE);
                break;
            }
            case OP_put_arg0:
            case OP_put_arg1:
            case OP_put_arg2:
            case OP_put_arg3: {
                if (stack.empty()) return PROTO_NONE;
                unsigned idx = static_cast<unsigned>(opcode - OP_put_arg0);
                const proto::ProtoObject* val = stack.back();
                stack.pop_back();
                if (idx < argCount && idx < locals.size())
                    locals[idx] = val;
                break;
            }
            case OP_set_arg0:
            case OP_set_arg1:
            case OP_set_arg2:
            case OP_set_arg3: {
                if (stack.empty()) return PROTO_NONE;
                unsigned idx = static_cast<unsigned>(opcode - OP_set_arg0);
                const proto::ProtoObject* val = stack.back();
                if (idx < argCount && idx < locals.size())
                    locals[idx] = val;
                break;
            }
            case OP_get_var_ref: {
                if (pc + 2 > len) return PROTO_NONE;
                uint16_t refIndex = get_u16(buf + pc);
                pc += 2;
                if (refIndex < varCount && (argCount + refIndex) < locals.size())
                    stack.push_back(locals[argCount + refIndex]);
                else
                    stack.push_back(PROTO_NONE);
                break;
            }
            case OP_put_var_ref: {
                if (pc + 2 > len || stack.empty()) return PROTO_NONE;
                uint16_t refIndex = get_u16(buf + pc);
                pc += 2;
                const proto::ProtoObject* val = stack.back();
                stack.pop_back();
                if (refIndex < varCount && (argCount + refIndex) < locals.size())
                    locals[argCount + refIndex] = val;
                break;
            }
            case OP_set_var_ref: {
                if (pc + 2 > len || stack.empty()) return PROTO_NONE;
                uint16_t refIndex = get_u16(buf + pc);
                pc += 2;
                const proto::ProtoObject* val = stack.back();
                if (refIndex < varCount && (argCount + refIndex) < locals.size())
                    locals[argCount + refIndex] = val;
                break;
            }
            case OP_get_var_ref_check: {
                if (pc + 2 > len) return PROTO_NONE;
                uint16_t refIndex = get_u16(buf + pc);
                pc += 2;
                if (refIndex < varCount && (argCount + refIndex) < locals.size())
                    stack.push_back(locals[argCount + refIndex]);
                else
                    stack.push_back(PROTO_NONE);
                break;
            }
            case OP_put_var_ref_check:
            case OP_put_var_ref_check_init: {
                if (pc + 2 > len || stack.empty()) return PROTO_NONE;
                uint16_t refIndex = get_u16(buf + pc);
                pc += 2;
                const proto::ProtoObject* val = stack.back();
                stack.pop_back();
                if (refIndex < varCount && (argCount + refIndex) < locals.size())
                    locals[argCount + refIndex] = val;
                break;
            }
            case OP_close_loc: {
                // Notification for closing an environment slot; no-op in this interpreter.
                if (pc + 2 > len) return PROTO_NONE;
                pc += 2;
                break;
            }
            case OP_get_loc0:
            case OP_get_loc1:
            case OP_get_loc2:
            case OP_get_loc3: {
                unsigned idx = static_cast<unsigned>(opcode - OP_get_loc0);
                if (idx < varCount && (argCount + idx) < locals.size())
                    stack.push_back(locals[argCount + idx]);
                else
                    stack.push_back(PROTO_NONE);
                break;
            }
            case OP_put_loc0:
            case OP_put_loc1:
            case OP_put_loc2:
            case OP_put_loc3: {
                if (stack.empty()) return PROTO_NONE;
                unsigned idx = static_cast<unsigned>(opcode - OP_put_loc0);
                const proto::ProtoObject* val = stack.back();
                stack.pop_back();
                if (idx < varCount && (argCount + idx) < locals.size())
                    locals[argCount + idx] = val;
                break;
            }
            case OP_set_loc0:
            case OP_set_loc1:
            case OP_set_loc2:
            case OP_set_loc3: {
                if (stack.empty()) return PROTO_NONE;
                unsigned idx = static_cast<unsigned>(opcode - OP_set_loc0);
                const proto::ProtoObject* val = stack.back();
                if (idx < varCount && (argCount + idx) < locals.size())
                    locals[argCount + idx] = val;
                break;
            }
            // --- Locals, arguments, and variable references ---
            case OP_get_loc: {
                if (pc + 2 > len) return PROTO_NONE;
                uint16_t locIndex = get_u16(buf + pc);
                pc += 2;
                if (locIndex < varCount && (argCount + locIndex) < locals.size())
                    stack.push_back(locals[argCount + locIndex]);
                else
                    stack.push_back(PROTO_NONE);
                break;
            }
            case OP_put_loc: {
                if (pc + 2 > len || stack.empty()) return PROTO_NONE;
                uint16_t locIndex = get_u16(buf + pc);
                pc += 2;
                const proto::ProtoObject* val = stack.back();
                stack.pop_back();
                if (locIndex < varCount && (argCount + locIndex) < locals.size())
                    locals[argCount + locIndex] = val;
                break;
            }
            case OP_set_loc: {
                if (pc + 2 > len || stack.empty()) return PROTO_NONE;
                uint16_t locIndex = get_u16(buf + pc);
                pc += 2;
                const proto::ProtoObject* val = stack.back();
                if (locIndex < varCount && (argCount + locIndex) < locals.size())
                    locals[argCount + locIndex] = val;
                break;
            }
            case OP_get_arg: {
                if (pc + 2 > len) return PROTO_NONE;
                uint16_t argIndex = get_u16(buf + pc);
                pc += 2;
                if (argIndex < argCount && argIndex < locals.size())
                    stack.push_back(locals[argIndex]);
                else
                    stack.push_back(PROTO_NONE);
                break;
            }
            case OP_put_arg: {
                if (pc + 2 > len || stack.empty()) return PROTO_NONE;
                uint16_t argIndex = get_u16(buf + pc);
                pc += 2;
                const proto::ProtoObject* val = stack.back();
                stack.pop_back();
                if (argIndex < argCount && argIndex < locals.size())
                    locals[argIndex] = val;
                break;
            }
            case OP_set_arg: {
                if (pc + 2 > len || stack.empty()) return PROTO_NONE;
                uint16_t argIndex = get_u16(buf + pc);
                pc += 2;
                const proto::ProtoObject* val = stack.back();
                if (argIndex < argCount && argIndex < locals.size())
                    locals[argIndex] = val;
                break;
            }
            case OP_set_loc_uninitialized: {
                if (pc + 2 > len) return PROTO_NONE;
                uint16_t locIndex = get_u16(buf + pc);
                pc += 2;
                if (locIndex < varCount && (argCount + locIndex) < locals.size())
                    locals[argCount + locIndex] = PROTO_NONE;
                break;
            }
            case OP_get_loc_check: {
                if (pc + 2 > len) return PROTO_NONE;
                uint16_t locIndex = get_u16(buf + pc);
                pc += 2;
                if (locIndex < varCount && (argCount + locIndex) < locals.size())
                    stack.push_back(locals[argCount + locIndex]);
                else
                    stack.push_back(PROTO_NONE);
                break;
            }
            case OP_put_loc_check: {
                if (pc + 2 > len || stack.empty()) return PROTO_NONE;
                uint16_t locIndex = get_u16(buf + pc);
                pc += 2;
                const proto::ProtoObject* val = stack.back();
                stack.pop_back();
                if (locIndex < varCount && (argCount + locIndex) < locals.size())
                    locals[argCount + locIndex] = val;
                break;
            }
            case OP_set_loc_check: {
                if (pc + 2 > len || stack.empty()) return PROTO_NONE;
                uint16_t locIndex = get_u16(buf + pc);
                pc += 2;
                const proto::ProtoObject* val = stack.back();
                if (locIndex < varCount && (argCount + locIndex) < locals.size())
                    locals[argCount + locIndex] = val;
                break;
            }
            case OP_put_loc_check_init: {
                if (pc + 2 > len || stack.empty()) return PROTO_NONE;
                uint16_t locIndex = get_u16(buf + pc);
                pc += 2;
                const proto::ProtoObject* val = stack.back();
                stack.pop_back();
                if (locIndex < varCount && (argCount + locIndex) < locals.size())
                    locals[argCount + locIndex] = val;
                break;
            }
            case OP_get_loc_checkthis: {
                if (pc + 2 > len) return PROTO_NONE;
                uint16_t locIndex = get_u16(buf + pc);
                pc += 2;
                if (locIndex < varCount && (argCount + locIndex) < locals.size())
                    stack.push_back(locals[argCount + locIndex]);
                else
                    stack.push_back(PROTO_NONE);
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
            case OP_get_field2: {
                if (pc + 4 > len || stack.empty()) return PROTO_NONE;
                uint32_t atomIndex = get_u32(buf + pc);
                pc += 4;
                const proto::ProtoObject* obj = stack.back();
                const proto::ProtoString* key = resolveAtom(mod, pContext, atomIndex);
                const proto::ProtoObject* val =
                    (obj && key) ? obj->getAttribute(pContext, key, true) : PROTO_NONE;
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
            case OP_define_field: {
                if (pc + 4 > len || stack.size() < 2) return PROTO_NONE;
                uint32_t atomIndex = get_u32(buf + pc);
                pc += 4;
                const proto::ProtoObject* value = stack.back();
                stack.pop_back();
                const proto::ProtoObject* obj = stack.back();
                stack.pop_back();
                const proto::ProtoString* key = resolveAtom(mod, pContext, atomIndex);
                if (key && obj) {
                    const proto::ProtoObject* newObj = obj->setAttribute(pContext, key, value);
                    stack.push_back(newObj ? newObj : obj);
                } else {
                    stack.push_back(PROTO_NONE);
                }
                break;
            }
            case OP_object: {
                const proto::ProtoObject* newObj = pContext->newObject(true);
                stack.push_back(newObj);
                break;
            }
            // --- Array element helpers (implemented via property semantics) ---
            case OP_get_array_el: {
                if (stack.size() < 2) return PROTO_NONE;
                const proto::ProtoObject* index = stack.back();
                stack.pop_back();
                const proto::ProtoObject* obj = stack.back();
                stack.pop_back();
                const proto::ProtoObject* keyObj = toString(pContext, index);
                const proto::ProtoString* key =
                    keyObj ? keyObj->asString(pContext) : nullptr;
                const proto::ProtoObject* val =
                    (obj && key) ? obj->getAttribute(pContext, key, true) : PROTO_NONE;
                stack.push_back(val && val != PROTO_NONE ? val : PROTO_NONE);
                break;
            }
            case OP_get_array_el2: {
                if (stack.size() < 2) return PROTO_NONE;
                const proto::ProtoObject* index = stack.back();
                const proto::ProtoObject* obj = stack[stack.size() - 2];
                const proto::ProtoObject* keyObj = toString(pContext, index);
                const proto::ProtoString* key =
                    keyObj ? keyObj->asString(pContext) : nullptr;
                const proto::ProtoObject* val =
                    (obj && key) ? obj->getAttribute(pContext, key, true) : PROTO_NONE;
                stack.push_back(val && val != PROTO_NONE ? val : PROTO_NONE);
                break;
            }
            case OP_get_array_el3: {
                if (stack.size() < 2) return PROTO_NONE;
                const proto::ProtoObject* index = stack.back();
                const proto::ProtoObject* obj = stack[stack.size() - 2];
                const proto::ProtoObject* keyObj = toString(pContext, index);
                const proto::ProtoString* key =
                    keyObj ? keyObj->asString(pContext) : nullptr;
                const proto::ProtoObject* val =
                    (obj && key) ? obj->getAttribute(pContext, key, true) : PROTO_NONE;
                stack.push_back(index);
                stack.push_back(val && val != PROTO_NONE ? val : PROTO_NONE);
                break;
            }
            case OP_put_array_el: {
                if (stack.size() < 3) return PROTO_NONE;
                const proto::ProtoObject* value = stack.back();
                stack.pop_back();
                const proto::ProtoObject* index = stack.back();
                stack.pop_back();
                const proto::ProtoObject* obj = stack.back();
                stack.pop_back();
                const proto::ProtoObject* keyObj = toString(pContext, index);
                const proto::ProtoString* key =
                    keyObj ? keyObj->asString(pContext) : nullptr;
                if (obj && key) {
                    const proto::ProtoObject* newObj = obj->setAttribute(pContext, key, value);
                    (void)newObj; // result ignored per QuickJS semantics
                }
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
            // --- Control flow ---
            case OP_goto: {
                if (pc + 4 > len) return PROTO_NONE;
                int32_t diff = static_cast<int32_t>(get_u32(buf + pc));
                pc += diff;
                break;
            }
            case OP_goto16: {
                if (pc + 2 > len) return PROTO_NONE;
                int16_t diff = static_cast<int16_t>(get_u16(buf + pc));
                pc += diff;
                break;
            }
            case OP_goto8: {
                if (pc + 1 > len) return PROTO_NONE;
                int8_t diff = static_cast<int8_t>(buf[pc]);
                pc += diff;
                break;
            }
            case OP_if_true: {
                if (pc + 4 > len || stack.empty()) return PROTO_NONE;
                const proto::ProtoObject* cond = stack.back();
                stack.pop_back();
                int32_t diff = static_cast<int32_t>(get_u32(buf + pc));
                pc += 4;
                if (toBool(pContext, cond)) {
                    pc += diff - 4;
                }
                break;
            }
            case OP_if_false: {
                if (pc + 4 > len || stack.empty()) return PROTO_NONE;
                const proto::ProtoObject* cond = stack.back();
                stack.pop_back();
                int32_t diff = static_cast<int32_t>(get_u32(buf + pc));
                pc += 4;
                if (!toBool(pContext, cond)) {
                    pc += diff - 4;
                }
                break;
            }
            case OP_if_true8: {
                if (pc + 1 > len || stack.empty()) return PROTO_NONE;
                const proto::ProtoObject* cond = stack.back();
                stack.pop_back();
                int8_t off = static_cast<int8_t>(buf[pc]);
                if (toBool(pContext, cond)) {
                    pc += off;
                } else {
                    pc += 1;
                }
                break;
            }
            case OP_if_false8: {
                if (pc + 1 > len || stack.empty()) return PROTO_NONE;
                const proto::ProtoObject* cond = stack.back();
                stack.pop_back();
                int8_t off = static_cast<int8_t>(buf[pc]);
                if (!toBool(pContext, cond)) {
                    pc += off;
                } else {
                    pc += 1;
                }
                break;
            }
            case OP_add: {
                if (stack.size() < 2) return PROTO_NONE;
                const proto::ProtoObject* b = stack.back();
                stack.pop_back();
                const proto::ProtoObject* a = stack.back();
                stack.pop_back();
                const proto::ProtoObject* res = a ? a->add(pContext, b) : PROTO_NONE;
                stack.push_back(res ? res : PROTO_NONE);
                break;
            }
            case OP_mul: {
                if (stack.size() < 2) return PROTO_NONE;
                const proto::ProtoObject* b = stack.back();
                stack.pop_back();
                const proto::ProtoObject* a = stack.back();
                stack.pop_back();
                const proto::ProtoObject* res = a ? a->multiply(pContext, b) : PROTO_NONE;
                stack.push_back(res ? res : PROTO_NONE);
                break;
            }
            case OP_div: {
                if (stack.size() < 2) return PROTO_NONE;
                const proto::ProtoObject* b = stack.back();
                stack.pop_back();
                const proto::ProtoObject* a = stack.back();
                stack.pop_back();
                const proto::ProtoObject* res = a ? a->divide(pContext, b) : PROTO_NONE;
                stack.push_back(res ? res : PROTO_NONE);
                break;
            }
            case OP_sub: {
                if (stack.size() < 2) return PROTO_NONE;
                const proto::ProtoObject* b = stack.back();
                stack.pop_back();
                const proto::ProtoObject* a = stack.back();
                stack.pop_back();
                const proto::ProtoObject* res = a ? a->subtract(pContext, b) : PROTO_NONE;
                stack.push_back(res ? res : PROTO_NONE);
                break;
            }
            case OP_mod: {
                if (stack.size() < 2) return PROTO_NONE;
                const proto::ProtoObject* b = stack.back();
                stack.pop_back();
                const proto::ProtoObject* a = stack.back();
                stack.pop_back();
                const proto::ProtoObject* res = a ? a->modulo(pContext, b) : PROTO_NONE;
                stack.push_back(res ? res : PROTO_NONE);
                break;
            }
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
                    const proto::ProtoObject* thisVal =
                        argc > 0 ? (stack.empty() ? globalObj : stack.back()) : globalObj;
                    const proto::ProtoObject* result =
                        runBytecode(pContext, &nf, thisVal, args, globalObj, jsContextForAtoms);
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
