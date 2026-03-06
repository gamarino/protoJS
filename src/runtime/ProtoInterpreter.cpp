#include "ProtoInterpreter.h"
#include "QuickJSOpcodeEnum.h"
#include "QuickJSBytecodeExport.h"
#include "../ProtoJSStringCache.h"
#include "../JSContext.h"
#include "../GCBridge.h"
#include "../TypeBridge.h"
#include "quickjs.h"
#include "headers/protoCore.h"
#include <cmath>
#include <cstring>
#include <cstdlib>
#include <limits>
#include <string>
#include <cstdio>

namespace protojs {

namespace {

/** Slot and stack storage use ProtoContext::closureLocals only (no std::vector); GC sees all references. */

static unsigned long slotKey(proto::ProtoContext* ctx, unsigned int index) {
    if (!ctx) return 0;
    std::string s = std::to_string(index);
    const proto::ProtoObject* o = ctx->fromUTF8String(s.c_str());
    const proto::ProtoString* ps = o ? o->asString(ctx) : nullptr;
    return ps ? static_cast<unsigned long>(ps->getHash(ctx)) : 0;
}

static unsigned long stackKey(proto::ProtoContext* ctx) {
    if (!ctx) return 0;
    const proto::ProtoObject* o = ctx->fromUTF8String("__stack__");
    const proto::ProtoString* ps = o ? o->asString(ctx) : nullptr;
    return ps ? static_cast<unsigned long>(ps->getHash(ctx)) : 0;
}

static const proto::ProtoObject* getSlot(proto::ProtoContext* ctx, unsigned int index) {
    if (!ctx || !ctx->closureLocals) return PROTO_NONE;
    const proto::ProtoObject* v = ctx->closureLocals->getAt(ctx, slotKey(ctx, index));
    return (v && v != PROTO_NONE) ? v : PROTO_NONE;
}

static void setSlot(proto::ProtoContext* ctx, unsigned int index, const proto::ProtoObject* value) {
    if (!ctx || !ctx->closureLocals) return;
    const proto::ProtoObject* val = value ? value : PROTO_NONE;
    ctx->closureLocals = ctx->closureLocals->setAt(ctx, slotKey(ctx, index), val);
}

static void initStack(proto::ProtoContext* ctx) {
    if (!ctx || !ctx->closureLocals) return;
    const proto::ProtoList* empty = ctx->newList();
    ctx->closureLocals = ctx->closureLocals->setAt(ctx, stackKey(ctx), empty ? empty->asObject(ctx) : PROTO_NONE);
}

static void stackPush(proto::ProtoContext* ctx, const proto::ProtoObject* value) {
    if (!ctx || !ctx->closureLocals) return;
    unsigned long sk = stackKey(ctx);
    const proto::ProtoObject* obj = ctx->closureLocals->getAt(ctx, sk);
    const proto::ProtoList* list = obj && obj != PROTO_NONE ? obj->asList(ctx) : nullptr;
    if (!list) list = ctx->newList();
    const proto::ProtoList* next = list->appendLast(ctx, value ? value : PROTO_NONE);
    ctx->closureLocals = ctx->closureLocals->setAt(ctx, sk, next ? next->asObject(ctx) : (list ? list->asObject(ctx) : PROTO_NONE));
}

static void stackPop(proto::ProtoContext* ctx) {
    if (!ctx || !ctx->closureLocals) return;
    unsigned long sk = stackKey(ctx);
    const proto::ProtoObject* obj = ctx->closureLocals->getAt(ctx, sk);
    const proto::ProtoList* list = obj && obj != PROTO_NONE ? obj->asList(ctx) : nullptr;
    if (!list) return;
    unsigned long n = list->getSize(ctx);
    if (n == 0) return;
    const proto::ProtoList* next = list->getSlice(ctx, 0, static_cast<int>(n - 1));
    ctx->closureLocals = ctx->closureLocals->setAt(ctx, sk, next ? next->asObject(ctx) : ctx->newList()->asObject(ctx));
}

static const proto::ProtoObject* stackTop(proto::ProtoContext* ctx) {
    if (!ctx || !ctx->closureLocals) return PROTO_NONE;
    unsigned long sk = stackKey(ctx);
    const proto::ProtoObject* obj = ctx->closureLocals->getAt(ctx, sk);
    const proto::ProtoList* list = obj && obj != PROTO_NONE ? obj->asList(ctx) : nullptr;
    if (!list) return PROTO_NONE;
    unsigned long n = list->getSize(ctx);
    if (n == 0) return PROTO_NONE;
    return list->getAt(ctx, static_cast<int>(n - 1));
}

static unsigned long stackSize(proto::ProtoContext* ctx) {
    if (!ctx || !ctx->closureLocals) return 0;
    unsigned long sk = stackKey(ctx);
    const proto::ProtoObject* obj = ctx->closureLocals->getAt(ctx, sk);
    const proto::ProtoList* list = obj && obj != PROTO_NONE ? obj->asList(ctx) : nullptr;
    return list ? list->getSize(ctx) : 0;
}

static bool stackEmpty(proto::ProtoContext* ctx) {
    return stackSize(ctx) == 0;
}

/** Get stack element by 0-based index from top (0 = top, 1 = next, ...). */
static const proto::ProtoObject* stackAt(proto::ProtoContext* ctx, unsigned long fromTop) {
    if (!ctx || !ctx->closureLocals) return PROTO_NONE;
    unsigned long sk = stackKey(ctx);
    const proto::ProtoObject* obj = ctx->closureLocals->getAt(ctx, sk);
    const proto::ProtoList* list = obj && obj != PROTO_NONE ? obj->asList(ctx) : nullptr;
    if (!list) return PROTO_NONE;
    unsigned long n = list->getSize(ctx);
    if (fromTop >= n) return PROTO_NONE;
    return list->getAt(ctx, static_cast<int>(n - 1 - fromTop));
}

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

/**
 * Host function bridge: call a QuickJS-backed function from the interpreter.
 * If func maps to a JSValue function via GCBridge, convert this+args to JSValue,
 * run JS_Call, convert result back to ProtoObject. Returns PROTO_NONE if not a host function.
 */
static const proto::ProtoObject* hostCall(JSContext* ctx, proto::ProtoContext* pContext,
    const proto::ProtoObject* func, const proto::ProtoObject* thisVal,
    const proto::ProtoList* argsList) {
    if (!ctx || !pContext || !func) return PROTO_NONE;
    JSValue jFunc = GCBridge::getJSValue(func, ctx);
    if (JS_IsNull(jFunc) || JS_IsUndefined(jFunc) || !JS_IsFunction(ctx, jFunc)) {
        if (!JS_IsNull(jFunc) && !JS_IsUndefined(jFunc)) JS_FreeValue(ctx, jFunc);
        return PROTO_NONE;
    }
    JSValue jThis = TypeBridge::toJS(ctx, thisVal ? thisVal : PROTO_NONE, pContext);
    int argc = argsList ? static_cast<int>(argsList->getSize(pContext)) : 0;
    std::vector<JSValue> argv(static_cast<size_t>(argc));
    for (int i = 0; i < argc; i++) {
        const proto::ProtoObject* arg = argsList->getAt(pContext, i);
        argv[static_cast<size_t>(i)] = TypeBridge::toJS(ctx, arg ? arg : PROTO_NONE, pContext);
    }
    JSValue result = JS_Call(ctx, jFunc, jThis, argc, argc > 0 ? argv.data() : nullptr);
    JS_FreeValue(ctx, jThis);
    for (int i = 0; i < argc; i++)
        JS_FreeValue(ctx, argv[static_cast<size_t>(i)]);
    JS_FreeValue(ctx, jFunc);
    const proto::ProtoObject* resultProto = PROTO_NONE;
    if (!JS_IsException(result)) {
        resultProto = TypeBridge::fromJS(ctx, result, pContext);
        JS_FreeValue(ctx, result);
    } else {
        JSValue ex = JS_GetException(ctx);
        resultProto = TypeBridge::fromJS(ctx, ex, pContext);
        JS_FreeValue(ctx, ex);
        JS_FreeValue(ctx, result);
    }
    return resultProto;
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
    unsigned argCount = module->argCount();
    unsigned varCount = module->varCount();
    // Locals and stack live only in ProtoContext::closureLocals (GC-visible). No std::vector.
    initStack(pContext);
    int pc = 0;
    ProtoBytecodeModule* mod = const_cast<ProtoBytecodeModule*>(module);

    while (pc >= 0 && pc < len) {
        int opcode = buf[pc++];
        switch (opcode) {
            // --- Constant and immediate pushes ---
            case OP_push_minus1:
                stackPush(pContext,pContext->fromInteger(-1));
                break;
            case OP_push_0:
                stackPush(pContext,pContext->fromInteger(0));
                break;
            case OP_push_1:
                stackPush(pContext,pContext->fromInteger(1));
                break;
            case OP_push_2:
                stackPush(pContext,pContext->fromInteger(2));
                break;
            case OP_push_3:
                stackPush(pContext,pContext->fromInteger(3));
                break;
            case OP_push_4:
                stackPush(pContext,pContext->fromInteger(4));
                break;
            case OP_push_5:
                stackPush(pContext,pContext->fromInteger(5));
                break;
            case OP_push_6:
                stackPush(pContext,pContext->fromInteger(6));
                break;
            case OP_push_7:
                stackPush(pContext,pContext->fromInteger(7));
                break;
            case OP_push_i8: {
                if (pc + 1 > len) return PROTO_NONE;
                int8_t v = static_cast<int8_t>(buf[pc++]);
                stackPush(pContext,pContext->fromInteger(static_cast<long long>(v)));
                break;
            }
            case OP_push_i16: {
                if (pc + 2 > len) return PROTO_NONE;
                int16_t v = static_cast<int16_t>(get_u16(buf + pc));
                pc += 2;
                stackPush(pContext,pContext->fromInteger(static_cast<long long>(v)));
                break;
            }
            case OP_push_i32: {
                // push_i32 encodes a 32-bit signed immediate.
                if (pc + 4 > len) return PROTO_NONE;
                int32_t v = (int32_t)get_u32(buf + pc);
                pc += 4;
                stackPush(pContext,pContext->fromInteger(static_cast<long long>(v)));
                break;
            }
            case OP_push_const8: {
                if (pc + 1 > len) return PROTO_NONE;
                uint8_t idx = buf[pc++];
                if (idx < cpool.size())
                    stackPush(pContext,cpool[idx]);
                else
                    stackPush(pContext,PROTO_NONE);
                break;
            }
            case OP_push_empty_string:
                stackPush(pContext,pContext->fromUTF8String(""));
                break;
            case OP_push_this:
                // Use the current frame's `this` binding; fall back to global object.
                stackPush(pContext,thisObj ? thisObj : (globalObj ? globalObj : PROTO_NONE));
                break;
            case OP_special_object: {
                // TODO: Implement arguments/new.target/etc. wiring.
                if (pc + 1 > len) return PROTO_NONE;
                pc += 1; // skip kind
                stackPush(pContext,PROTO_NONE);
                break;
            }
            case OP_rest: {
                // TODO: Implement rest parameter materialization once call/arg opcodes are wired.
                if (pc + 2 > len) return PROTO_NONE;
                pc += 2; // skip u16 argument index
                stackPush(pContext,PROTO_NONE);
                break;
            }
            case OP_return: {
                if (stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* result = stackTop(pContext);
                return result;
            }
            case OP_return_undef:
                return PROTO_NONE;
            case OP_drop:
                if (!stackEmpty(pContext)) stackPop(pContext);
                break;
            case OP_nip:
                if (stackSize(pContext) < 2) return PROTO_NONE;
                { const proto::ProtoObject* top = stackTop(pContext); stackPop(pContext); stackPop(pContext); stackPush(pContext, top); }
                break;
            case OP_nip1:
                if (stackSize(pContext) < 3) return PROTO_NONE;
                { const proto::ProtoObject* c = stackTop(pContext); stackPop(pContext); const proto::ProtoObject* b = stackTop(pContext); stackPop(pContext); stackPop(pContext); stackPush(pContext, b); stackPush(pContext, c); }
                break;
            case OP_dup:
                if (!stackEmpty(pContext)) stackPush(pContext, stackTop(pContext));
                break;
            case OP_dup1:
                if (stackSize(pContext) < 2) return PROTO_NONE;
                { const proto::ProtoObject* top = stackTop(pContext); const proto::ProtoObject* second = stackAt(pContext, 1); stackPush(pContext, second); stackPush(pContext, top); }
                break;
            case OP_dup2:
                if (stackSize(pContext) < 2) return PROTO_NONE;
                stackPush(pContext, stackAt(pContext, 1));
                stackPush(pContext, stackAt(pContext, 1));
                break;
            case OP_dup3:
                if (stackSize(pContext) < 3) return PROTO_NONE;
                stackPush(pContext, stackAt(pContext, 2));
                stackPush(pContext, stackAt(pContext, 2));
                stackPush(pContext, stackAt(pContext, 2));
                break;
            case OP_insert2:
                if (stackSize(pContext) < 2) return PROTO_NONE;
                // obj a -> a obj a
                {
                    const proto::ProtoObject* a = stackTop(pContext);
                    stackPop(pContext);
                    const proto::ProtoObject* obj = stackTop(pContext);
                    stackPop(pContext);
                    stackPush(pContext,a);
                    stackPush(pContext,obj);
                    stackPush(pContext,a);
                }
                break;
            case OP_insert3:
                if (stackSize(pContext) < 3) return PROTO_NONE;
                // obj prop a -> a obj prop a
                {
                    const proto::ProtoObject* a = stackTop(pContext);
                    stackPop(pContext);
                    const proto::ProtoObject* prop = stackTop(pContext);
                    stackPop(pContext);
                    const proto::ProtoObject* obj = stackTop(pContext);
                    stackPop(pContext);
                    stackPush(pContext,a);
                    stackPush(pContext,obj);
                    stackPush(pContext,prop);
                    stackPush(pContext,a);
                }
                break;
            case OP_insert4:
                if (stackSize(pContext) < 4) return PROTO_NONE;
                // this obj prop a -> a this obj prop a
                {
                    const proto::ProtoObject* a = stackTop(pContext);
                    stackPop(pContext);
                    const proto::ProtoObject* prop = stackTop(pContext);
                    stackPop(pContext);
                    const proto::ProtoObject* obj = stackTop(pContext);
                    stackPop(pContext);
                    const proto::ProtoObject* thisVal = stackTop(pContext);
                    stackPop(pContext);
                    stackPush(pContext,a);
                    stackPush(pContext,thisVal);
                    stackPush(pContext,obj);
                    stackPush(pContext,prop);
                    stackPush(pContext,a);
                }
                break;
            case OP_perm3:
                if (stackSize(pContext) < 3) return PROTO_NONE;
                // obj a b -> a obj b
                {
                    const proto::ProtoObject* b = stackTop(pContext);
                    stackPop(pContext);
                    const proto::ProtoObject* a = stackTop(pContext);
                    stackPop(pContext);
                    const proto::ProtoObject* obj = stackTop(pContext);
                    stackPop(pContext);
                    stackPush(pContext,a);
                    stackPush(pContext,obj);
                    stackPush(pContext,b);
                }
                break;
            case OP_perm4:
                if (stackSize(pContext) < 4) return PROTO_NONE;
                // obj prop a b -> a obj prop b
                {
                    const proto::ProtoObject* b = stackTop(pContext);
                    stackPop(pContext);
                    const proto::ProtoObject* a = stackTop(pContext);
                    stackPop(pContext);
                    const proto::ProtoObject* prop = stackTop(pContext);
                    stackPop(pContext);
                    const proto::ProtoObject* obj = stackTop(pContext);
                    stackPop(pContext);
                    stackPush(pContext,a);
                    stackPush(pContext,obj);
                    stackPush(pContext,prop);
                    stackPush(pContext,b);
                }
                break;
            case OP_perm5:
                if (stackSize(pContext) < 5) return PROTO_NONE;
                // this obj prop a b -> a this obj prop b
                {
                    const proto::ProtoObject* b = stackTop(pContext);
                    stackPop(pContext);
                    const proto::ProtoObject* a = stackTop(pContext);
                    stackPop(pContext);
                    const proto::ProtoObject* prop = stackTop(pContext);
                    stackPop(pContext);
                    const proto::ProtoObject* obj = stackTop(pContext);
                    stackPop(pContext);
                    const proto::ProtoObject* thisVal = stackTop(pContext);
                    stackPop(pContext);
                    stackPush(pContext,a);
                    stackPush(pContext,thisVal);
                    stackPush(pContext,obj);
                    stackPush(pContext,prop);
                    stackPush(pContext,b);
                }
                break;
            case OP_swap:
                if (stackSize(pContext) < 2) return PROTO_NONE;
                { const proto::ProtoObject* a = stackTop(pContext); stackPop(pContext); const proto::ProtoObject* b = stackTop(pContext); stackPop(pContext); stackPush(pContext, a); stackPush(pContext, b); }
                break;
            case OP_swap2:
                if (stackSize(pContext) < 4) return PROTO_NONE;
                // a b c d -> c d a b
                {
                    const proto::ProtoObject* d = stackTop(pContext);
                    stackPop(pContext);
                    const proto::ProtoObject* c = stackTop(pContext);
                    stackPop(pContext);
                    const proto::ProtoObject* b = stackTop(pContext);
                    stackPop(pContext);
                    const proto::ProtoObject* a = stackTop(pContext);
                    stackPop(pContext);
                    stackPush(pContext,c);
                    stackPush(pContext,d);
                    stackPush(pContext,a);
                    stackPush(pContext,b);
                }
                break;
            case OP_rot3l:
                if (stackSize(pContext) < 3) return PROTO_NONE;
                // x a b -> a b x
                {
                    const proto::ProtoObject* b = stackTop(pContext);
                    stackPop(pContext);
                    const proto::ProtoObject* a = stackTop(pContext);
                    stackPop(pContext);
                    const proto::ProtoObject* x = stackTop(pContext);
                    stackPop(pContext);
                    stackPush(pContext,a);
                    stackPush(pContext,b);
                    stackPush(pContext,x);
                }
                break;
            case OP_rot3r:
                if (stackSize(pContext) < 3) return PROTO_NONE;
                // a b x -> x a b
                {
                    const proto::ProtoObject* x = stackTop(pContext);
                    stackPop(pContext);
                    const proto::ProtoObject* b = stackTop(pContext);
                    stackPop(pContext);
                    const proto::ProtoObject* a = stackTop(pContext);
                    stackPop(pContext);
                    stackPush(pContext,x);
                    stackPush(pContext,a);
                    stackPush(pContext,b);
                }
                break;
            case OP_rot4l:
                if (stackSize(pContext) < 4) return PROTO_NONE;
                // x a b c -> a b c x
                {
                    const proto::ProtoObject* c = stackTop(pContext);
                    stackPop(pContext);
                    const proto::ProtoObject* b = stackTop(pContext);
                    stackPop(pContext);
                    const proto::ProtoObject* a = stackTop(pContext);
                    stackPop(pContext);
                    const proto::ProtoObject* x = stackTop(pContext);
                    stackPop(pContext);
                    stackPush(pContext,a);
                    stackPush(pContext,b);
                    stackPush(pContext,c);
                    stackPush(pContext,x);
                }
                break;
            case OP_rot5l:
                if (stackSize(pContext) < 5) return PROTO_NONE;
                // x a b c d -> a b c d x
                {
                    const proto::ProtoObject* d = stackTop(pContext);
                    stackPop(pContext);
                    const proto::ProtoObject* c = stackTop(pContext);
                    stackPop(pContext);
                    const proto::ProtoObject* b = stackTop(pContext);
                    stackPop(pContext);
                    const proto::ProtoObject* a = stackTop(pContext);
                    stackPop(pContext);
                    const proto::ProtoObject* x = stackTop(pContext);
                    stackPop(pContext);
                    stackPush(pContext,a);
                    stackPush(pContext,b);
                    stackPush(pContext,c);
                    stackPush(pContext,d);
                    stackPush(pContext,x);
                }
                break;
            case OP_push_const: {
                if (pc + 4 > len) return PROTO_NONE;
                uint32_t idx = get_u32(buf + pc);
                pc += 4;
                if (idx < cpool.size())
                    stackPush(pContext,cpool[idx]);
                else
                    stackPush(pContext,PROTO_NONE);
                break;
            }
            case OP_push_atom_value: {
                if (pc + 4 > len) return PROTO_NONE;
                uint32_t atomIndex = get_u32(buf + pc);
                pc += 4;
                const proto::ProtoString* key = resolveAtom(mod, pContext, atomIndex);
                if (!key || !globalObj) {
                    stackPush(pContext,PROTO_NONE);
                    break;
                }
                const proto::ProtoObject* val = globalObj->getAttribute(pContext, key, true);
                stackPush(pContext,val ? val : PROTO_NONE);
                break;
            }
            // Short local/arg accessors (loc8/arg8 and loc0-3/arg0-3)
            case OP_get_loc8: {
                if (pc + 1 > len) return PROTO_NONE;
                uint8_t locIndex = buf[pc++];
                if (locIndex < varCount && (argCount + locIndex) < (argCount + varCount))
                    stackPush(pContext, getSlot(pContext, argCount + locIndex));
                else
                    stackPush(pContext,PROTO_NONE);
                break;
            }
            case OP_put_loc8: {
                if (pc + 1 > len || stackEmpty(pContext)) return PROTO_NONE;
                uint8_t locIndex = buf[pc++];
                const proto::ProtoObject* val = stackTop(pContext);
                stackPop(pContext);
                if (locIndex < varCount && (argCount + locIndex) < (argCount + varCount))
                    setSlot(pContext, argCount + locIndex, val);
                break;
            }
            case OP_set_loc8: {
                if (pc + 1 > len || stackEmpty(pContext)) return PROTO_NONE;
                uint8_t locIndex = buf[pc++];
                const proto::ProtoObject* val = stackTop(pContext);
                if (locIndex < varCount && (argCount + locIndex) < (argCount + varCount))
                    setSlot(pContext, argCount + locIndex, val);
                break;
            }
            case OP_get_arg0:
            case OP_get_arg1:
            case OP_get_arg2:
            case OP_get_arg3: {
                unsigned idx = static_cast<unsigned>(opcode - OP_get_arg0);
                if (idx < argCount && idx < (argCount + varCount))
                    stackPush(pContext, getSlot(pContext, idx));
                else
                    stackPush(pContext,PROTO_NONE);
                break;
            }
            case OP_put_arg0:
            case OP_put_arg1:
            case OP_put_arg2:
            case OP_put_arg3: {
                if (stackEmpty(pContext)) return PROTO_NONE;
                unsigned idx = static_cast<unsigned>(opcode - OP_put_arg0);
                const proto::ProtoObject* val = stackTop(pContext);
                stackPop(pContext);
                if (idx < argCount && idx < (argCount + varCount))
                    setSlot(pContext, idx, val);
                break;
            }
            case OP_set_arg0:
            case OP_set_arg1:
            case OP_set_arg2:
            case OP_set_arg3: {
                if (stackEmpty(pContext)) return PROTO_NONE;
                unsigned idx = static_cast<unsigned>(opcode - OP_set_arg0);
                const proto::ProtoObject* val = stackTop(pContext);
                if (idx < argCount && idx < (argCount + varCount))
                    setSlot(pContext, idx, val);
                break;
            }
            case OP_get_var_ref: {
                if (pc + 2 > len) return PROTO_NONE;
                uint16_t refIndex = get_u16(buf + pc);
                pc += 2;
                if (refIndex < varCount && (argCount + refIndex) < (argCount + varCount))
                    stackPush(pContext, getSlot(pContext, argCount + refIndex));
                else
                    stackPush(pContext,PROTO_NONE);
                break;
            }
            case OP_put_var_ref: {
                if (pc + 2 > len || stackEmpty(pContext)) return PROTO_NONE;
                uint16_t refIndex = get_u16(buf + pc);
                pc += 2;
                const proto::ProtoObject* val = stackTop(pContext);
                stackPop(pContext);
                if (refIndex < varCount && (argCount + refIndex) < (argCount + varCount))
                    setSlot(pContext, argCount + refIndex, val);
                break;
            }
            case OP_set_var_ref: {
                if (pc + 2 > len || stackEmpty(pContext)) return PROTO_NONE;
                uint16_t refIndex = get_u16(buf + pc);
                pc += 2;
                const proto::ProtoObject* val = stackTop(pContext);
                if (refIndex < varCount && (argCount + refIndex) < (argCount + varCount))
                    setSlot(pContext, argCount + refIndex, val);
                break;
            }
            case OP_get_var_ref_check: {
                if (pc + 2 > len) return PROTO_NONE;
                uint16_t refIndex = get_u16(buf + pc);
                pc += 2;
                if (refIndex < varCount && (argCount + refIndex) < (argCount + varCount))
                    stackPush(pContext, getSlot(pContext, argCount + refIndex));
                else
                    stackPush(pContext,PROTO_NONE);
                break;
            }
            case OP_put_var_ref_check:
            case OP_put_var_ref_check_init: {
                if (pc + 2 > len || stackEmpty(pContext)) return PROTO_NONE;
                uint16_t refIndex = get_u16(buf + pc);
                pc += 2;
                const proto::ProtoObject* val = stackTop(pContext);
                stackPop(pContext);
                if (refIndex < varCount && (argCount + refIndex) < (argCount + varCount))
                    setSlot(pContext, argCount + refIndex, val);
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
                if (idx < varCount && (argCount + idx) < (argCount + varCount))
                    stackPush(pContext, getSlot(pContext, argCount + idx));
                else
                    stackPush(pContext,PROTO_NONE);
                break;
            }
            case OP_put_loc0:
            case OP_put_loc1:
            case OP_put_loc2:
            case OP_put_loc3: {
                if (stackEmpty(pContext)) return PROTO_NONE;
                unsigned idx = static_cast<unsigned>(opcode - OP_put_loc0);
                const proto::ProtoObject* val = stackTop(pContext);
                stackPop(pContext);
                if (idx < varCount && (argCount + idx) < (argCount + varCount))
                    setSlot(pContext, argCount + idx, val);
                break;
            }
            case OP_set_loc0:
            case OP_set_loc1:
            case OP_set_loc2:
            case OP_set_loc3: {
                if (stackEmpty(pContext)) return PROTO_NONE;
                unsigned idx = static_cast<unsigned>(opcode - OP_set_loc0);
                const proto::ProtoObject* val = stackTop(pContext);
                if (idx < varCount && (argCount + idx) < (argCount + varCount))
                    setSlot(pContext, argCount + idx, val);
                break;
            }
            // --- Locals, arguments, and variable references ---
            case OP_get_loc: {
                if (pc + 2 > len) return PROTO_NONE;
                uint16_t locIndex = get_u16(buf + pc);
                pc += 2;
                if (locIndex < varCount && (argCount + locIndex) < (argCount + varCount))
                    stackPush(pContext, getSlot(pContext, argCount + locIndex));
                else
                    stackPush(pContext,PROTO_NONE);
                break;
            }
            case OP_put_loc: {
                if (pc + 2 > len || stackEmpty(pContext)) return PROTO_NONE;
                uint16_t locIndex = get_u16(buf + pc);
                pc += 2;
                const proto::ProtoObject* val = stackTop(pContext);
                stackPop(pContext);
                if (locIndex < varCount && (argCount + locIndex) < (argCount + varCount))
                    setSlot(pContext, argCount + locIndex, val);
                break;
            }
            case OP_set_loc: {
                if (pc + 2 > len || stackEmpty(pContext)) return PROTO_NONE;
                uint16_t locIndex = get_u16(buf + pc);
                pc += 2;
                const proto::ProtoObject* val = stackTop(pContext);
                if (locIndex < varCount && (argCount + locIndex) < (argCount + varCount))
                    setSlot(pContext, argCount + locIndex, val);
                break;
            }
            case OP_get_arg: {
                if (pc + 2 > len) return PROTO_NONE;
                uint16_t argIndex = get_u16(buf + pc);
                pc += 2;
                if (argIndex < argCount && argIndex < (argCount + varCount))
                    stackPush(pContext, getSlot(pContext, argIndex));
                else
                    stackPush(pContext,PROTO_NONE);
                break;
            }
            case OP_put_arg: {
                if (pc + 2 > len || stackEmpty(pContext)) return PROTO_NONE;
                uint16_t argIndex = get_u16(buf + pc);
                pc += 2;
                const proto::ProtoObject* val = stackTop(pContext);
                stackPop(pContext);
                if (argIndex < argCount && argIndex < (argCount + varCount))
                    setSlot(pContext, argIndex, val);
                break;
            }
            case OP_set_arg: {
                if (pc + 2 > len || stackEmpty(pContext)) return PROTO_NONE;
                uint16_t argIndex = get_u16(buf + pc);
                pc += 2;
                const proto::ProtoObject* val = stackTop(pContext);
                if (argIndex < argCount && argIndex < (argCount + varCount))
                    setSlot(pContext, argIndex, val);
                break;
            }
            case OP_set_loc_uninitialized: {
                if (pc + 2 > len) return PROTO_NONE;
                uint16_t locIndex = get_u16(buf + pc);
                pc += 2;
                if (locIndex < varCount && (argCount + locIndex) < (argCount + varCount))
                    setSlot(pContext, argCount + locIndex, PROTO_NONE);
                break;
            }
            case OP_get_loc_check: {
                if (pc + 2 > len) return PROTO_NONE;
                uint16_t locIndex = get_u16(buf + pc);
                pc += 2;
                if (locIndex < varCount && (argCount + locIndex) < (argCount + varCount))
                    stackPush(pContext, getSlot(pContext, argCount + locIndex));
                else
                    stackPush(pContext,PROTO_NONE);
                break;
            }
            case OP_put_loc_check: {
                if (pc + 2 > len || stackEmpty(pContext)) return PROTO_NONE;
                uint16_t locIndex = get_u16(buf + pc);
                pc += 2;
                const proto::ProtoObject* val = stackTop(pContext);
                stackPop(pContext);
                if (locIndex < varCount && (argCount + locIndex) < (argCount + varCount))
                    setSlot(pContext, argCount + locIndex, val);
                break;
            }
            case OP_set_loc_check: {
                if (pc + 2 > len || stackEmpty(pContext)) return PROTO_NONE;
                uint16_t locIndex = get_u16(buf + pc);
                pc += 2;
                const proto::ProtoObject* val = stackTop(pContext);
                if (locIndex < varCount && (argCount + locIndex) < (argCount + varCount))
                    setSlot(pContext, argCount + locIndex, val);
                break;
            }
            case OP_put_loc_check_init: {
                if (pc + 2 > len || stackEmpty(pContext)) return PROTO_NONE;
                uint16_t locIndex = get_u16(buf + pc);
                pc += 2;
                const proto::ProtoObject* val = stackTop(pContext);
                stackPop(pContext);
                if (locIndex < varCount && (argCount + locIndex) < (argCount + varCount))
                    setSlot(pContext, argCount + locIndex, val);
                break;
            }
            case OP_get_loc_checkthis: {
                if (pc + 2 > len) return PROTO_NONE;
                uint16_t locIndex = get_u16(buf + pc);
                pc += 2;
                if (locIndex < varCount && (argCount + locIndex) < (argCount + varCount))
                    stackPush(pContext, getSlot(pContext, argCount + locIndex));
                else
                    stackPush(pContext,PROTO_NONE);
                break;
            }
            case OP_get_field: {
                if (pc + 4 > len || stackEmpty(pContext)) return PROTO_NONE;
                uint32_t atomIndex = get_u32(buf + pc);
                pc += 4;
                const proto::ProtoObject* obj = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoString* key = resolveAtom(mod, pContext, atomIndex);
                if (!key) { stackPush(pContext,PROTO_NONE); break; }
                const proto::ProtoObject* val = obj ? obj->getAttribute(pContext, key, true) : PROTO_NONE;
                stackPush(pContext,val && val != PROTO_NONE ? val : PROTO_NONE);
                break;
            }
            case OP_get_field2: {
                if (pc + 4 > len || stackEmpty(pContext)) return PROTO_NONE;
                uint32_t atomIndex = get_u32(buf + pc);
                pc += 4;
                const proto::ProtoObject* obj = stackTop(pContext);
                const proto::ProtoString* key = resolveAtom(mod, pContext, atomIndex);
                const proto::ProtoObject* val =
                    (obj && key) ? obj->getAttribute(pContext, key, true) : PROTO_NONE;
                stackPush(pContext,val && val != PROTO_NONE ? val : PROTO_NONE);
                break;
            }
            case OP_put_field: {
                if (pc + 4 > len || stackSize(pContext) < 2) return PROTO_NONE;
                uint32_t atomIndex = get_u32(buf + pc);
                pc += 4;
                const proto::ProtoObject* val = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoObject* obj = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoString* key = resolveAtom(mod, pContext, atomIndex);
                if (key && obj) {
                    const proto::ProtoObject* newObj = obj->setAttribute(pContext, key, val);
                    stackPush(pContext,newObj ? newObj : obj);
                }
                break;
            }
            case OP_define_field: {
                if (pc + 4 > len || stackSize(pContext) < 2) return PROTO_NONE;
                uint32_t atomIndex = get_u32(buf + pc);
                pc += 4;
                const proto::ProtoObject* value = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoObject* obj = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoString* key = resolveAtom(mod, pContext, atomIndex);
                if (key && obj) {
                    const proto::ProtoObject* newObj = obj->setAttribute(pContext, key, value);
                    stackPush(pContext,newObj ? newObj : obj);
                } else {
                    stackPush(pContext,PROTO_NONE);
                }
                break;
            }
            case OP_object: {
                const proto::ProtoObject* newObj = pContext->newObject(true);
                stackPush(pContext,newObj);
                break;
            }
            // --- Array element helpers (implemented via property semantics) ---
            case OP_get_array_el: {
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* index = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoObject* obj = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoObject* keyObj = toString(pContext, index);
                const proto::ProtoString* key =
                    keyObj ? keyObj->asString(pContext) : nullptr;
                const proto::ProtoObject* val =
                    (obj && key) ? obj->getAttribute(pContext, key, true) : PROTO_NONE;
                stackPush(pContext,val && val != PROTO_NONE ? val : PROTO_NONE);
                break;
            }
            case OP_get_array_el2: {
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* index = stackTop(pContext);
                const proto::ProtoObject* obj = stackAt(pContext, 1);
                const proto::ProtoObject* keyObj = toString(pContext, index);
                const proto::ProtoString* key =
                    keyObj ? keyObj->asString(pContext) : nullptr;
                const proto::ProtoObject* val =
                    (obj && key) ? obj->getAttribute(pContext, key, true) : PROTO_NONE;
                stackPush(pContext,val && val != PROTO_NONE ? val : PROTO_NONE);
                break;
            }
            case OP_get_array_el3: {
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* index = stackTop(pContext);
                const proto::ProtoObject* obj = stackAt(pContext, 1);
                const proto::ProtoObject* keyObj = toString(pContext, index);
                const proto::ProtoString* key =
                    keyObj ? keyObj->asString(pContext) : nullptr;
                const proto::ProtoObject* val =
                    (obj && key) ? obj->getAttribute(pContext, key, true) : PROTO_NONE;
                stackPush(pContext,index);
                stackPush(pContext,val && val != PROTO_NONE ? val : PROTO_NONE);
                break;
            }
            case OP_put_array_el: {
                if (stackSize(pContext) < 3) return PROTO_NONE;
                const proto::ProtoObject* value = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoObject* index = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoObject* obj = stackTop(pContext);
                stackPop(pContext);
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
                stackPush(pContext,PROTO_NONE);
                break;
            case OP_null:
                stackPush(pContext,PROTO_NONE);
                break;
            case OP_push_false:
                stackPush(pContext,PROTO_FALSE);
                break;
            case OP_push_true:
                stackPush(pContext,PROTO_TRUE);
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
                if (pc + 4 > len || stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* cond = stackTop(pContext);
                stackPop(pContext);
                int32_t diff = static_cast<int32_t>(get_u32(buf + pc));
                pc += 4;
                if (toBool(pContext, cond)) {
                    pc += diff - 4;
                }
                break;
            }
            case OP_if_false: {
                if (pc + 4 > len || stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* cond = stackTop(pContext);
                stackPop(pContext);
                int32_t diff = static_cast<int32_t>(get_u32(buf + pc));
                pc += 4;
                if (!toBool(pContext, cond)) {
                    pc += diff - 4;
                }
                break;
            }
            case OP_if_true8: {
                if (pc + 1 > len || stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* cond = stackTop(pContext);
                stackPop(pContext);
                int8_t off = static_cast<int8_t>(buf[pc]);
                if (toBool(pContext, cond)) {
                    pc += off;
                } else {
                    pc += 1;
                }
                break;
            }
            case OP_if_false8: {
                if (pc + 1 > len || stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* cond = stackTop(pContext);
                stackPop(pContext);
                int8_t off = static_cast<int8_t>(buf[pc]);
                if (!toBool(pContext, cond)) {
                    pc += off;
                } else {
                    pc += 1;
                }
                break;
            }
            case OP_add: {
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* b = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoObject* a = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoObject* res = a ? a->add(pContext, b) : PROTO_NONE;
                stackPush(pContext,res ? res : PROTO_NONE);
                break;
            }
            case OP_mul: {
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* b = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoObject* a = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoObject* res = a ? a->multiply(pContext, b) : PROTO_NONE;
                stackPush(pContext,res ? res : PROTO_NONE);
                break;
            }
            case OP_div: {
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* b = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoObject* a = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoObject* res = a ? a->divide(pContext, b) : PROTO_NONE;
                stackPush(pContext,res ? res : PROTO_NONE);
                break;
            }
            case OP_sub: {
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* b = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoObject* a = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoObject* res = a ? a->subtract(pContext, b) : PROTO_NONE;
                stackPush(pContext,res ? res : PROTO_NONE);
                break;
            }
            case OP_mod: {
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* b = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoObject* a = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoObject* res = a ? a->modulo(pContext, b) : PROTO_NONE;
                stackPush(pContext,res ? res : PROTO_NONE);
                break;
            }
            case OP_eq: {
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* b = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoObject* a = stackTop(pContext);
                stackPop(pContext);
                int cmp = (a && b) ? a->compare(pContext, b) : ((!a && !b) ? 0 : 1);
                stackPush(pContext, (cmp == 0) ? PROTO_TRUE : PROTO_FALSE);
                break;
            }
            case OP_neq: {
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* b = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoObject* a = stackTop(pContext);
                stackPop(pContext);
                int cmp = (a && b) ? a->compare(pContext, b) : ((!a && !b) ? 0 : 1);
                stackPush(pContext, (cmp != 0) ? PROTO_TRUE : PROTO_FALSE);
                break;
            }
            case OP_strict_eq: {
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* b = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoObject* a = stackTop(pContext);
                stackPop(pContext);
                int cmp = (a && b) ? a->compare(pContext, b) : ((!a && !b) ? 0 : 1);
                stackPush(pContext, (cmp == 0) ? PROTO_TRUE : PROTO_FALSE);
                break;
            }
            case OP_strict_neq: {
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* b = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoObject* a = stackTop(pContext);
                stackPop(pContext);
                int cmp = (a && b) ? a->compare(pContext, b) : ((!a && !b) ? 0 : 1);
                stackPush(pContext, (cmp != 0) ? PROTO_TRUE : PROTO_FALSE);
                break;
            }
            case OP_lt: {
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* b = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoObject* a = stackTop(pContext);
                stackPop(pContext);
                int cmp = (a && b) ? a->compare(pContext, b) : 0;
                stackPush(pContext, (cmp < 0) ? PROTO_TRUE : PROTO_FALSE);
                break;
            }
            case OP_lte: {
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* b = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoObject* a = stackTop(pContext);
                stackPop(pContext);
                int cmp = (a && b) ? a->compare(pContext, b) : 0;
                stackPush(pContext, (cmp <= 0) ? PROTO_TRUE : PROTO_FALSE);
                break;
            }
            case OP_gt: {
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* b = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoObject* a = stackTop(pContext);
                stackPop(pContext);
                int cmp = (a && b) ? a->compare(pContext, b) : 0;
                stackPush(pContext, (cmp > 0) ? PROTO_TRUE : PROTO_FALSE);
                break;
            }
            case OP_gte: {
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* b = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoObject* a = stackTop(pContext);
                stackPop(pContext);
                int cmp = (a && b) ? a->compare(pContext, b) : 0;
                stackPush(pContext, (cmp >= 0) ? PROTO_TRUE : PROTO_FALSE);
                break;
            }
            case OP_and: {
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* b = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoObject* a = stackTop(pContext);
                stackPop(pContext);
                bool va = toBool(pContext, a);
                stackPush(pContext, va ? (b ? b : PROTO_NONE) : (a ? a : PROTO_NONE));
                break;
            }
            case OP_or: {
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* b = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoObject* a = stackTop(pContext);
                stackPop(pContext);
                bool va = toBool(pContext, a);
                stackPush(pContext, va ? (a ? a : PROTO_NONE) : (b ? b : PROTO_NONE));
                break;
            }
            case OP_typeof: {
                if (stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* v = stackTop(pContext);
                stackPop(pContext);
                const char* typeStr = "undefined";
                if (v && v != PROTO_NONE && !v->isNone(pContext)) {
                    if (v->isBoolean(pContext)) typeStr = "boolean";
                    else if (v->isInteger(pContext) || v->isDouble(pContext)) typeStr = "number";
                    else if (v->asString(pContext)) typeStr = "string";
                    else if (v->isMethod(pContext)) typeStr = "function";
                    else typeStr = "object";
                }
                stackPush(pContext, pContext->fromUTF8String(typeStr));
                break;
            }
            case OP_instanceof: {
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* func = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoObject* obj = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoString* protoKey = ProtoJSStringCache::getKey(pContext, "prototype");
                const proto::ProtoObject* protoObj = func ? func->getAttribute(pContext, protoKey, false) : nullptr;
                const proto::ProtoObject* res = (obj && protoObj && protoObj != PROTO_NONE) ? obj->isInstanceOf(pContext, protoObj) : PROTO_FALSE;
                stackPush(pContext, (res == PROTO_TRUE) ? PROTO_TRUE : PROTO_FALSE);
                break;
            }
            case OP_in: {
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* keyVal = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoObject* obj = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoObject* keyObj = toString(pContext, keyVal);
                const proto::ProtoString* key = keyObj ? keyObj->asString(pContext) : nullptr;
                bool has = (obj && key && obj->hasAttribute(pContext, key));
                stackPush(pContext, has ? PROTO_TRUE : PROTO_FALSE);
                break;
            }
            case OP_delete: {
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* keyVal = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoObject* obj = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoObject* keyObj = toString(pContext, keyVal);
                const proto::ProtoString* key = keyObj ? keyObj->asString(pContext) : nullptr;
                if (obj && key) {
                    const proto::ProtoObject* prev = obj->getAttribute(pContext, key, false);
                    (void)obj->setAttribute(pContext, key, PROTO_NONE);
                    (void)prev;
                }
                stackPush(pContext, PROTO_TRUE);
                break;
            }
            case OP_call_method:
            case OP_tail_call_method: {
                // Stack: ... this, func, arg0, ..., arg(argc-1). this = stackAt(argc), func = stackAt(argc+1).
                if (pc + 2 > len || stackEmpty(pContext)) return PROTO_NONE;
                uint32_t argc = get_u16(buf + pc);
                pc += 2;
                if (stackSize(pContext) < argc + 2) return PROTO_NONE;
                const proto::ProtoObject* thisVal = stackAt(pContext, argc);
                const proto::ProtoObject* func = stackAt(pContext, argc + 1);
                int bcId = getBytecodeId(pContext, func);
                if (bcId >= 0 && static_cast<size_t>(bcId) < nested.size()) {
                    const auto& nf = nested[bcId];
                    const proto::ProtoList* argsList = pContext->newList();
                    for (uint32_t i = 0; i < argc; i++)
                        argsList = argsList->appendLast(pContext, stackAt(pContext, argc - 1 - i));
                    for (uint32_t i = 0; i < argc + 2; i++) stackPop(pContext);
                    proto::ProtoContext childCtx(pContext->space, pContext, nullptr, nullptr, nullptr, nullptr);
                    childCtx.currentFileName = pContext->currentFileName;
                    childCtx.currentLineNumber = pContext->currentLineNumber;
                    for (uint32_t i = 0; i < argc; i++)
                        setSlot(&childCtx, i, argsList->getAt(&childCtx, static_cast<int>(i)));
                    const proto::ProtoObject* result =
                        runBytecode(&childCtx, &nf, thisVal, argsList, globalObj, jsContextForAtoms);
                    childCtx.returnValue = result;
                    if (opcode != OP_tail_call_method)
                        stackPush(pContext, result ? result : PROTO_NONE);
                } else if (func && func->isMethod(pContext)) {
                    const proto::ProtoList* argsList = pContext->newList();
                    for (uint32_t i = 0; i < argc; i++)
                        argsList = argsList->appendLast(pContext, stackAt(pContext, argc - 1 - i));
                    for (uint32_t i = 0; i < argc + 2; i++) stackPop(pContext);
                    const proto::ProtoObject* result = func->call(pContext, nullptr,
                        ProtoJSStringCache::getKey(pContext, "call"), thisVal, argsList, nullptr);
                    if (opcode != OP_tail_call_method)
                        stackPush(pContext, result ? result : PROTO_NONE);
                } else {
                    const proto::ProtoObject* thisValM = stackAt(pContext, argc);
                    const proto::ProtoList* argsList = pContext->newList();
                    for (uint32_t i = 0; i < argc; i++)
                        argsList = argsList->appendLast(pContext, stackAt(pContext, argc - 1 - i));
                    for (uint32_t i = 0; i < argc + 2; i++) stackPop(pContext);
                    const proto::ProtoObject* result = hostCall(jsContextForAtoms, pContext, func, thisValM, argsList);
                    if (opcode != OP_tail_call_method)
                        stackPush(pContext, result ? result : PROTO_NONE);
                }
                break;
            }
            case OP_call_constructor: {
                // Stack: ... func, newTarget, arg0, ..., arg(argc-1). Create new object, call func as ctor, return object or result.
                if (pc + 2 > len || stackEmpty(pContext)) return PROTO_NONE;
                uint32_t argc = get_u16(buf + pc);
                pc += 2;
                if (stackSize(pContext) < argc + 2) return PROTO_NONE;
                const proto::ProtoObject* func = stackAt(pContext, argc + 1);
                const proto::ProtoObject* newTarget = stackAt(pContext, argc);
                const proto::ProtoObject* newObj = pContext->newObject(true);
                if (!newObj) { for (uint32_t i = 0; i < argc + 2; i++) stackPop(pContext); stackPush(pContext, PROTO_NONE); break; }
                const proto::ProtoList* argsList = pContext->newList();
                for (uint32_t i = 0; i < argc; i++)
                    argsList = argsList->appendLast(pContext, stackAt(pContext, argc - 1 - i));
                for (uint32_t i = 0; i < argc + 2; i++) stackPop(pContext);
                const proto::ProtoObject* result = PROTO_NONE;
                int bcId = getBytecodeId(pContext, func);
                if (bcId >= 0 && static_cast<size_t>(bcId) < nested.size()) {
                    const auto& nf = nested[bcId];
                    proto::ProtoContext childCtx(pContext->space, pContext, nullptr, nullptr, nullptr, nullptr);
                    childCtx.currentFileName = pContext->currentFileName;
                    childCtx.currentLineNumber = pContext->currentLineNumber;
                    for (uint32_t i = 0; i < argc; i++)
                        setSlot(&childCtx, i, argsList->getAt(&childCtx, static_cast<int>(i)));
                    result = runBytecode(&childCtx, &nf, newObj, argsList, globalObj, jsContextForAtoms);
                    childCtx.returnValue = result;
                } else if (func && func->isMethod(pContext)) {
                    result = func->call(pContext, nullptr,
                        ProtoJSStringCache::getKey(pContext, "call"), newObj, argsList, nullptr);
                } else {
                    result = hostCall(jsContextForAtoms, pContext, func, newObj, argsList);
                }
                bool resultIsObject = result && result != PROTO_NONE
                    && !result->isInteger(pContext) && !result->isDouble(pContext)
                    && !result->asString(pContext) && result != PROTO_TRUE && result != PROTO_FALSE;
                stackPush(pContext, resultIsObject ? result : newObj);
                break;
            }
            case OP_call: {
                if (pc + 2 > len || stackEmpty(pContext)) return PROTO_NONE;
                uint32_t argc = get_u16(buf + pc);
                pc += 2;
                if (stackSize(pContext) < argc + 1) return PROTO_NONE;
                const proto::ProtoObject* func = stackAt(pContext, argc);
                int bcId = getBytecodeId(pContext, func);
                if (bcId >= 0 && static_cast<size_t>(bcId) < nested.size()) {
                    const auto& nf = nested[bcId];
                    const proto::ProtoList* argsList = pContext->newList();
                    for (uint32_t i = 0; i < argc; i++)
                        argsList = argsList->appendLast(pContext, stackAt(pContext, argc - 1 - i));
                    const proto::ProtoObject* thisVal =
                        argc > 0 ? stackAt(pContext, argc) : globalObj;
                    for (uint32_t i = 0; i <= argc; i++) stackPop(pContext);

                    // Execute nested bytecode in a child ProtoContext frame so that
                    // allocations are tied to the call frame and can be handed back
                    // to the parent context on return. Bind args into child's closureLocals
                    // (GC-visible) so getSlot/get_arg see them.
                    proto::ProtoContext childCtx(pContext->space, pContext, nullptr, nullptr, nullptr, nullptr);
                    childCtx.currentFileName = pContext->currentFileName;
                    childCtx.currentLineNumber = pContext->currentLineNumber;
                    for (uint32_t i = 0; i < argc; i++)
                        setSlot(&childCtx, i, argsList->getAt(&childCtx, static_cast<int>(i)));

                    const proto::ProtoObject* result =
                        runBytecode(&childCtx, &nf, thisVal, argsList, globalObj, jsContextForAtoms);
                    // Preserve the result for GC: the destructor will create a
                    // ReturnReference in the parent context.
                    childCtx.returnValue = result;

                    stackPush(pContext, result ? result : PROTO_NONE);
                } else if (func && func->isMethod(pContext)) {
                    const proto::ProtoObject* thisVal = stackAt(pContext, argc);
                    const proto::ProtoList* argsList = pContext->newList();
                    for (uint32_t i = 0; i < argc; i++)
                        argsList = argsList->appendLast(pContext, stackAt(pContext, argc - 1 - i));
                    for (uint32_t i = 0; i <= argc; i++) stackPop(pContext);
                    const proto::ProtoObject* result = func->call(pContext, nullptr,
                        ProtoJSStringCache::getKey(pContext, "call"), thisVal, argsList, nullptr);
                    stackPush(pContext,result ? result : PROTO_NONE);
                } else {
                    const proto::ProtoObject* thisVal = argc > 0 ? stackAt(pContext, argc) : globalObj;
                    const proto::ProtoList* argsList = pContext->newList();
                    for (uint32_t i = 0; i < argc; i++)
                        argsList = argsList->appendLast(pContext, stackAt(pContext, argc - 1 - i));
                    for (uint32_t i = 0; i <= argc; i++) stackPop(pContext);
                    const proto::ProtoObject* result = hostCall(jsContextForAtoms, pContext, func, thisVal, argsList);
                    stackPush(pContext, result ? result : PROTO_NONE);
                }
                break;
            }
            default: {
                // Unknown opcode: log for diagnostics; execution cannot continue safely.
                std::fprintf(stderr, "[ProtoInterpreter] unsupported opcode 0x%02x at byte offset %d\n",
                    static_cast<unsigned>(opcode), static_cast<int>(pc - 1));
                return PROTO_NONE;
            }
        }
    }
    return PROTO_NONE;
}

} // namespace protojs
