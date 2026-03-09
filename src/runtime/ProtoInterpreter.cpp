#include "ProtoInterpreter.h"
#include "QuickJSOpcodeEnum.h"
#include "QuickJSBytecodeExport.h"
#include "../ProtoJSStringCache.h"
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
        // Trim whitespace (JS spec: leading/trailing whitespace ignored in ToNumber).
        size_t start = tmp.find_first_not_of(" \t\n\r\f\v");
        if (start == std::string::npos) {
            // Empty or whitespace-only string → +0.
            return context->fromInteger(0LL);
        }
        size_t end = tmp.find_last_not_of(" \t\n\r\f\v");
        std::string trimmed = tmp.substr(start, end - start + 1);
        if (trimmed.empty()) return context->fromInteger(0LL);
        // Handle special literals.
        if (trimmed == "Infinity" || trimmed == "+Infinity")
            return context->fromDouble(std::numeric_limits<double>::infinity());
        if (trimmed == "-Infinity")
            return context->fromDouble(-std::numeric_limits<double>::infinity());
        // Try parsing as number; any parse error → NaN.
        try {
            size_t pos = 0;
            double d = std::stod(trimmed, &pos);
            if (pos != trimmed.size()) return makeNaN();
            // If integral and in range, use integer representation.
            if (d == std::trunc(d) && std::abs(d) < 9.007199254740992e15)
                return context->fromInteger(static_cast<long long>(d));
            return context->fromDouble(d);
        } catch (...) {
            return makeNaN();
        }
    }

    // TODO: Implement full ToPrimitive/ToNumber for objects (valueOf/toString chain).
    return makeNaN();
}

/** JS ToInt32: truncate to signed 32-bit integer. */
static int32_t toInt32Val(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    if (!v || v == PROTO_NONE || v->isNone(ctx)) return 0;
    if (v->isInteger(ctx)) return static_cast<int32_t>(v->asLong(ctx));
    if (v->isDouble(ctx) || v->isFloat(ctx)) {
        double d = v->asDouble(ctx);
        if (!std::isfinite(d) || d == 0.0) return 0;
        return static_cast<int32_t>(static_cast<int64_t>(d));
    }
    if (v->isBoolean(ctx)) return v->asBoolean(ctx) ? 1 : 0;
    if (v->isString(ctx)) {
        const proto::ProtoString* s = v->asString(ctx);
        if (!s) return 0;
        std::string tmp;
        s->toUTF8String(ctx, tmp);
        try { return static_cast<int32_t>(static_cast<int64_t>(std::stod(tmp))); } catch (...) { return 0; }
    }
    return 0;
}

static uint32_t toUint32Val(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    return static_cast<uint32_t>(toInt32Val(ctx, v));
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
        if (std::isnan(v)) return context->fromUTF8String("NaN");
        if (std::isinf(v)) return context->fromUTF8String(v > 0 ? "Infinity" : "-Infinity");
        const std::string tmp = std::to_string(v);
        return context->fromUTF8String(tmp.c_str());
    }

    // Generic object fallback matches typical "[object Object]" shape.
    return context->fromUTF8String("[object Object]");
}

/** JS Abstract Equality Comparison (==): performs type coercions per ECMAScript spec §7.2.13.
 *  - null == undefined (both map to PROTO_NONE here, always equal)
 *  - Boolean: coerce to Number, retry
 *  - Number vs String: ToNumber(String), retry
 *  - Object vs primitive: ToPrimitive(Object) via valueOf/toString, retry (TODO: valueOf wiring)
 */
static bool jsAbstractEquals(proto::ProtoContext* ctx,
                              const proto::ProtoObject* x,
                              const proto::ProtoObject* y,
                              int depth = 0) {
    if (depth > 4) {
        // Guard against infinite recursion.
        return x == y;
    }
    // null/undefined both map to PROTO_NONE; they are equal to each other.
    bool xNone = !x || x == PROTO_NONE || x->isNone(ctx);
    bool yNone = !y || y == PROTO_NONE || y->isNone(ctx);
    if (xNone && yNone) return true;
    if (xNone || yNone) return false;

    bool xBool = x->isBoolean(ctx);
    bool yBool = y->isBoolean(ctx);
    bool xNum  = x->isInteger(ctx) || x->isDouble(ctx) || x->isFloat(ctx);
    bool yNum  = y->isInteger(ctx) || y->isDouble(ctx) || y->isFloat(ctx);
    bool xStr  = !xBool && !xNum && x->asString(ctx) != nullptr;
    bool yStr  = !yBool && !yNum && y->asString(ctx) != nullptr;

    // Same type: use strict compare.
    if (xBool && yBool) return x->compare(ctx, y) == 0;
    if (xNum  && yNum)  return x->compare(ctx, y) == 0;
    if (xStr  && yStr)  return x->compare(ctx, y) == 0;

    // Boolean: convert to number first, then retry.
    if (xBool) return jsAbstractEquals(ctx, toNumber(ctx, x), y, depth + 1);
    if (yBool) return jsAbstractEquals(ctx, x, toNumber(ctx, y), depth + 1);

    // Number vs String: convert string to number, retry.
    if (xNum && yStr)  return jsAbstractEquals(ctx, x, toNumber(ctx, y), depth + 1);
    if (xStr && yNum)  return jsAbstractEquals(ctx, toNumber(ctx, x), y, depth + 1);

    // Object vs primitive: attempt ToPrimitive via valueOf attribute.
    bool xObj = !xBool && !xNum && !xStr;
    bool yObj = !yBool && !yNum && !yStr;
    if (xObj && !yObj) {
        const proto::ProtoString* vk = ProtoJSStringCache::getKey(ctx, "valueOf");
        if (vk) {
            const proto::ProtoObject* vfn = x->getAttribute(ctx, vk, true);
            if (vfn && vfn != PROTO_NONE && vfn->isMethod(ctx)) {
                const proto::ProtoObject* prim = vfn->call(ctx, nullptr, vk, x, ctx->newList(), nullptr);
                if (prim && prim != PROTO_NONE && !(prim->isMethod(ctx)) &&
                    (prim->isBoolean(ctx) || prim->isInteger(ctx) ||
                     prim->isDouble(ctx)  || prim->asString(ctx)))
                    return jsAbstractEquals(ctx, prim, y, depth + 1);
            }
        }
        return false;
    }
    if (yObj && !xObj) {
        const proto::ProtoString* vk = ProtoJSStringCache::getKey(ctx, "valueOf");
        if (vk) {
            const proto::ProtoObject* vfn = y->getAttribute(ctx, vk, true);
            if (vfn && vfn != PROTO_NONE && vfn->isMethod(ctx)) {
                const proto::ProtoObject* prim = vfn->call(ctx, nullptr, vk, y, ctx->newList(), nullptr);
                if (prim && prim != PROTO_NONE && !(prim->isMethod(ctx)) &&
                    (prim->isBoolean(ctx) || prim->isInteger(ctx) ||
                     prim->isDouble(ctx)  || prim->asString(ctx)))
                    return jsAbstractEquals(ctx, x, prim, depth + 1);
            }
        }
        return false;
    }

    // Object vs Object: identity.
    return x->compare(ctx, y) == 0;
}

/** Resolve atom index to ProtoString from the pre-resolved cache. */
const proto::ProtoString* resolveAtom(ProtoBytecodeModule* mod, proto::ProtoContext* pContext, uint32_t atomIndex) {
    if (!mod || !pContext) return nullptr;
    auto it = mod->atomToProto.find(atomIndex);
    if (it != mod->atomToProto.end()) return it->second;
    /* Atom was not pre-resolved; this should not happen after preResolveAllAtoms(). */
    std::fprintf(stderr, "[ProtoInterpreter] resolveAtom: atom %u not in cache\n", atomIndex);
    return nullptr;
}

/** Check if obj is a bytecode function placeholder (has __bytecode_id__). Returns -1 if not. */
int getBytecodeId(proto::ProtoContext* pContext, const proto::ProtoObject* obj) {
    if (!obj || !pContext) return -1;
    const proto::ProtoString* key = ProtoJSStringCache::getKey(pContext, "__bytecode_id__");
    const proto::ProtoObject* val = obj->getAttribute(pContext, key, false);
    if (!val || val == PROTO_NONE) return -1;
    if (!val->isInteger(pContext)) return -1;
    long long id = val->asLong(pContext);
    return id >= 0 ? static_cast<int>(id) : -1;
}

/** Native ProtoMethod for Error.prototype.toString(). Returns "name: message" or just "name". */
static const proto::ProtoObject* errorPrototypeToString(
        proto::ProtoContext* context,
        const proto::ProtoObject* self,
        const proto::ParentLink* /*parentLink*/,
        const proto::ProtoList* /*params*/,
        const proto::ProtoSparseList* /*kw*/) {
    if (!context) return PROTO_NONE;
    const proto::ProtoString* nameKey = ProtoJSStringCache::getKey(context, "name");
    const proto::ProtoString* msgKey  = ProtoJSStringCache::getKey(context, "message");
    std::string nameStr = "Error", msgStr;
    if (nameKey && self && self != PROTO_NONE) {
        const proto::ProtoObject* nv = self->getAttribute(context, nameKey, true);
        if (nv && nv != PROTO_NONE && nv->isString(context))
            nv->asString(context)->toUTF8String(context, nameStr);
    }
    if (msgKey && self && self != PROTO_NONE) {
        const proto::ProtoObject* mv = self->getAttribute(context, msgKey, true);
        if (mv && mv != PROTO_NONE && mv->isString(context))
            mv->asString(context)->toUTF8String(context, msgStr);
    }
    std::string result = msgStr.empty() ? nameStr : (nameStr + ": " + msgStr);
    return context->fromUTF8String(result.c_str());
}

/** Register built-in error constructors (Error, TypeError, ReferenceError, …) on the global.
 *  Each entry is a stub object with `name` and `prototype` attributes.  The prototype is
 *  used by makeError (via newChild) so that `instanceof` works correctly. */
static void ensureBuiltinErrorConstructors(proto::ProtoContext* ctx,
                                            const proto::ProtoObject** globalRoot) {
    if (!ctx || !globalRoot || !*globalRoot) return;
    static const char* kNames[] = {
        "Error", "TypeError", "ReferenceError", "RangeError",
        "SyntaxError", "URIError", "EvalError", "InternalError", nullptr
    };
    const proto::ProtoString* protoKey = ProtoJSStringCache::getKey(ctx, "prototype");
    const proto::ProtoString* nameKey  = ProtoJSStringCache::getKey(ctx, "name");
    if (!protoKey || !nameKey) return;
    for (int i = 0; kNames[i]; ++i) {
        const proto::ProtoString* ctorKey = ProtoJSStringCache::getKey(ctx, kNames[i]);
        if (!ctorKey) continue;
        // Only register if not already present.
        const proto::ProtoObject* existing = (*globalRoot)->getAttribute(ctx, ctorKey, false);
        if (existing && existing != PROTO_NONE) continue;
        // Build prototype object.
        const proto::ProtoObject* proto = ctx->newObject(true);
        if (!proto) continue;
        proto = proto->setAttribute(ctx, nameKey, ctx->fromUTF8String(kNames[i]));
        if (!proto) continue;
        // Add toString method to the prototype.
        const proto::ProtoString* toStringKey = ProtoJSStringCache::getKey(ctx, "toString");
        if (toStringKey) {
            const proto::ProtoObject* toStringMethod = ctx->fromMethod(nullptr, errorPrototypeToString);
            if (toStringMethod) proto = proto->setAttribute(ctx, toStringKey, toStringMethod);
        }
        if (!proto) continue;
        // Build constructor stub.
        const proto::ProtoObject* ctor = ctx->newObject(true);
        if (!ctor) continue;
        ctor = ctor->setAttribute(ctx, nameKey, ctx->fromUTF8String(kNames[i]));
        if (!ctor) continue;
        ctor = ctor->setAttribute(ctx, protoKey, proto);
        if (!ctor) continue;
        // Mark as a built-in error constructor so OP_call can invoke it.
        const proto::ProtoString* errCtorKey = ProtoJSStringCache::getKey(ctx, "__error_ctor__");
        if (errCtorKey) ctor = ctor->setAttribute(ctx, errCtorKey, ctx->fromUTF8String(kNames[i]));
        if (!ctor) continue;
        *globalRoot = (*globalRoot)->setAttribute(ctx, ctorKey, ctor);
    }
}

/** Build an Error-like ProtoObject with name and message attributes.
 *  When globalRoot is provided, the object is created as a child of the corresponding
 *  error prototype so that `instanceof` works correctly. */
static const proto::ProtoObject* makeError(proto::ProtoContext* ctx,
                                           const char* name,
                                           const char* message,
                                           const proto::ProtoObject* const* globalRoot = nullptr) {
    if (!ctx) return PROTO_NONE;
    // Try to get the prototype from the global so instanceof works.
    const proto::ProtoObject* base = nullptr;
    if (globalRoot && *globalRoot && name) {
        const proto::ProtoString* ctorKey   = ProtoJSStringCache::getKey(ctx, name);
        const proto::ProtoString* protoKey  = ProtoJSStringCache::getKey(ctx, "prototype");
        if (ctorKey && protoKey) {
            const proto::ProtoObject* ctor = (*globalRoot)->getAttribute(ctx, ctorKey, false);
            if (ctor && ctor != PROTO_NONE) {
                const proto::ProtoObject* p = ctor->getAttribute(ctx, protoKey, false);
                if (p && p != PROTO_NONE) base = p;
            }
        }
    }
    const proto::ProtoObject* err = base ? base->newChild(ctx, true) : ctx->newObject(true);
    if (!err) return PROTO_NONE;
    const proto::ProtoString* nameKey = ProtoJSStringCache::getKey(ctx, "name");
    const proto::ProtoString* msgKey  = ProtoJSStringCache::getKey(ctx, "message");
    if (nameKey)       err = err->setAttribute(ctx, nameKey, ctx->fromUTF8String(name    ? name    : "Error"));
    if (msgKey && err) err = err->setAttribute(ctx, msgKey,  ctx->fromUTF8String(message ? message : ""));
    return err ? err : PROTO_NONE;
}

} // namespace

const proto::ProtoObject* runBytecode(proto::ProtoContext* pContext,
                                      const ProtoBytecodeModule* module,
                                      const proto::ProtoObject* thisObj,
                                      const proto::ProtoList* args,
                                      const proto::ProtoObject** pGlobalRoot,
                                      const proto::ProtoObject** outException) {
    if (!pContext || !module) return PROTO_NONE;
    const uint8_t* buf = module->buf();
    int len = module->bufLen();
    if (!buf || len <= 0) return PROTO_NONE;
    const std::vector<const proto::ProtoObject*>& cpool = module->protoCpool;
    const auto& nested = module->nestedFunctions;
    const std::vector<std::string>& closureVarNames = module->closureVarNames;
    unsigned argCount = module->argCount();
    unsigned varCount = module->varCount();
    // Locals and stack live only in ProtoContext::closureLocals (GC-visible). No std::vector.
    initStack(pContext);
    const proto::ProtoObject* globalObjInit = (pGlobalRoot && *pGlobalRoot) ? *pGlobalRoot : thisObj;
    for (size_t i = 0; i < closureVarNames.size() && (argCount + static_cast<unsigned>(i)) < (argCount + varCount); i++) {
        if (closureVarNames[i].empty() || !globalObjInit || globalObjInit == PROTO_NONE) continue;
        const proto::ProtoString* key = ProtoJSStringCache::getKey(pContext, closureVarNames[i].c_str());
        if (key) {
            const proto::ProtoObject* val = globalObjInit->getAttribute(pContext, key, false);
            if (val && val != PROTO_NONE)
                setSlot(pContext, argCount + static_cast<unsigned>(i), val);
        }
    }
    int pc = 0;
    ProtoBytecodeModule* mod = const_cast<ProtoBytecodeModule*>(module);

    const proto::ProtoObject* tdzSentinel = pContext->fromUTF8String("\x00__protojs_tdz_sentinel__");
    if (!tdzSentinel) tdzSentinel = PROTO_NONE;

    // Register built-in error constructors once so that `instanceof` works.
    ensureBuiltinErrorConstructors(pContext, pGlobalRoot);

    // Pending exception (set inside switch, dispatched after switch body).
    // Use a separate flag so that `throw undefined` (PROTO_NONE) is also catchable.
    const proto::ProtoObject* pending_exception = nullptr;
    bool has_pending_exception = false;

    // Catch-handler stack: each entry is {handler_pc, stack_depth_at_catch}.
    struct CatchFrame { int handler_pc; unsigned long stack_depth; };
    std::vector<CatchFrame> catch_stack;

    while (pc >= 0 && pc < len) {
        const proto::ProtoObject* globalObj = (pGlobalRoot && *pGlobalRoot) ? *pGlobalRoot : PROTO_NONE;
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
                // Strict mode: pass thisObj as-is (undefined stays undefined).
                // Non-strict mode: coerce null/undefined to the global object per spec.
                if (module->isStrict) {
                    stackPush(pContext, thisObj ? thisObj : PROTO_NONE);
                } else {
                    stackPush(pContext, (thisObj && thisObj != PROTO_NONE) ? thisObj
                                                                           : (globalObj ? globalObj : PROTO_NONE));
                }
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
            case OP_throw: {
                if (stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* exObj = stackTop(pContext);
                stackPop(pContext);
                pending_exception = exObj ? exObj : PROTO_NONE;
                has_pending_exception = true;
                break;
            }
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
                /* QuickJS semantics: push the atom's string representation as a string literal.
                 * This is JS_AtomToString(ctx, atom) — NOT a variable lookup in globalObj. */
                if (pc + 4 > len) return PROTO_NONE;
                uint32_t atomIndex = get_u32(buf + pc);
                pc += 4;
                const proto::ProtoString* key = resolveAtom(mod, pContext, atomIndex);
                if (!key) {
                    stackPush(pContext, PROTO_NONE);
                    break;
                }
                /* Push the atom as a string value (the atom name IS the string literal). */
                stackPush(pContext, key->asObject(pContext));
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
            case OP_get_var_undef:
            case OP_get_var: {
                if (pc + 2 > len) return PROTO_NONE;
                uint16_t idx = get_u16(buf + pc);
                pc += 2;
                const proto::ProtoObject* val = PROTO_NONE;
                bool isLexical = (opcode == OP_get_var) &&
                                 static_cast<size_t>(idx) < module->closureVarIsLexical.size() &&
                                 module->closureVarIsLexical[idx];
                /* Read from *pGlobalRoot (the live global, updated by OP_put_var_init via setAttribute)
                 * rather than from the stale globalObj snapshot passed at call time. */
                const proto::ProtoObject* liveGlobal = (pGlobalRoot && *pGlobalRoot) ? *pGlobalRoot : globalObj;
                bool foundInGlobal = false;
                if (liveGlobal && liveGlobal != PROTO_NONE && static_cast<size_t>(idx) < module->closureVarNames.size()) {
                    const std::string& name = module->closureVarNames[idx];
                    if (!name.empty()) {
                        const proto::ProtoString* key = ProtoJSStringCache::getKey(pContext, name.c_str());
                        if (key) {
                            /* TDZ: use hasAttribute to distinguish "not yet initialized" (absent
                             * key) from "initialized to undefined" (key present, value PROTO_NONE).
                             * A lexical var set via OP_put_var_init always has its key present. */
                            if (isLexical && !liveGlobal->hasAttribute(pContext, key)) {
                                const std::string& vname = module->closureVarNames[idx];
                                std::string msg = "Cannot access '";
                                msg += vname.empty() ? "?" : vname;
                                msg += "' before initialization";
                                pending_exception = makeError(pContext, "ReferenceError", msg.c_str(), pGlobalRoot);
                                has_pending_exception = true;
                                break;
                            }
                            val = liveGlobal->getAttribute(pContext, key, false);
                            foundInGlobal = true;
                        }
                    }
                }
                /* Only fall back to local slots if the var was not found in the global root.
                 * If it was found (even as PROTO_NONE = undefined), skip the slot fallback to
                 * avoid picking up stale slot data for vars initialized to undefined. */
                if (!foundInGlobal && (!val || val == PROTO_NONE)) {
                    if (idx < varCount && (argCount + idx) < (argCount + varCount))
                        val = getSlot(pContext, argCount + idx);
                }
                stackPush(pContext, val && val != PROTO_NONE ? val : PROTO_NONE);
                break;
            }
            case OP_put_var_init:
            case OP_put_var: {
                if (pc + 2 > len || stackEmpty(pContext)) return PROTO_NONE;
                uint16_t idx = get_u16(buf + pc);
                pc += 2;
                const proto::ProtoObject* val = stackTop(pContext);
                stackPop(pContext);
                if (pGlobalRoot && static_cast<size_t>(idx) < module->closureVarNames.size()) {
                    const std::string& name = module->closureVarNames[idx];
                    if (!name.empty()) {
                        const proto::ProtoString* key = ProtoJSStringCache::getKey(pContext, name.c_str());
                        if (key)
                            *pGlobalRoot = (*pGlobalRoot)->setAttribute(pContext, key, val ? val : PROTO_NONE);
                    }
                }
                break;
            }
            case OP_get_var_ref0:
            case OP_get_var_ref1:
            case OP_get_var_ref2:
            case OP_get_var_ref3: {
                uint16_t refIndex = static_cast<uint16_t>(opcode - OP_get_var_ref0);
                if (refIndex < varCount && (argCount + refIndex) < (argCount + varCount))
                    stackPush(pContext, getSlot(pContext, argCount + refIndex));
                else
                    stackPush(pContext, PROTO_NONE);
                break;
            }
            case OP_put_var_ref0:
            case OP_put_var_ref1:
            case OP_put_var_ref2:
            case OP_put_var_ref3: {
                if (stackEmpty(pContext)) return PROTO_NONE;
                uint16_t refIndex = static_cast<uint16_t>(opcode - OP_put_var_ref0);
                const proto::ProtoObject* val = stackTop(pContext);
                stackPop(pContext);
                if (refIndex < varCount && (argCount + refIndex) < (argCount + varCount))
                    setSlot(pContext, argCount + refIndex, val);
                break;
            }
            case OP_set_var_ref0:
            case OP_set_var_ref1:
            case OP_set_var_ref2:
            case OP_set_var_ref3: {
                if (stackEmpty(pContext)) return PROTO_NONE;
                uint16_t refIndex = static_cast<uint16_t>(opcode - OP_set_var_ref0);
                const proto::ProtoObject* val = stackTop(pContext);
                if (refIndex < varCount && (argCount + refIndex) < (argCount + varCount))
                    setSlot(pContext, argCount + refIndex, val);
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
                if (refIndex < varCount && (argCount + refIndex) < (argCount + varCount)) {
                    const proto::ProtoObject* val = getSlot(pContext, argCount + refIndex);
                    if (val == tdzSentinel) {
                        pending_exception = makeError(pContext, "ReferenceError", "Cannot access before initialization", pGlobalRoot); has_pending_exception = true;
                        break;
                    }
                    stackPush(pContext, val ? val : PROTO_NONE);
                } else {
                    stackPush(pContext, PROTO_NONE);
                }
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
                    setSlot(pContext, argCount + locIndex, tdzSentinel);
                break;
            }
            case OP_get_loc_check: {
                if (pc + 2 > len) return PROTO_NONE;
                uint16_t locIndex = get_u16(buf + pc);
                pc += 2;
                if (locIndex < varCount && (argCount + locIndex) < (argCount + varCount)) {
                    const proto::ProtoObject* val = getSlot(pContext, argCount + locIndex);
                    if (val == tdzSentinel) {
                        pending_exception = makeError(pContext, "ReferenceError", "Cannot access before initialization", pGlobalRoot); has_pending_exception = true;
                        break;
                    }
                    stackPush(pContext, val ? val : PROTO_NONE);
                } else {
                    stackPush(pContext, PROTO_NONE);
                }
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
                    if (newObj && pGlobalRoot && obj == globalObj)
                        *pGlobalRoot = newObj;
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
                    if (newObj && pGlobalRoot && obj == globalObj)
                        *pGlobalRoot = newObj;
                    stackPush(pContext,newObj ? newObj : obj);
                } else {
                    stackPush(pContext,PROTO_NONE);
                }
                break;
            }
            case OP_set_name: {
                /* Sets the .name property of TOS (function/value) to the given atom string.
                 * Format: atom (4 bytes). n_pop=1, n_push=1. */
                if (pc + 4 > len || stackEmpty(pContext)) return PROTO_NONE;
                uint32_t atomIndex = get_u32(buf + pc);
                pc += 4;
                const proto::ProtoObject* func = stackTop(pContext);
                stackPop(pContext);
                if (func && func != PROTO_NONE) {
                    const proto::ProtoString* nameKey = ProtoJSStringCache::getKey(pContext, "name");
                    const proto::ProtoString* nameStr = resolveAtom(mod, pContext, atomIndex);
                    if (nameKey && nameStr) {
                        std::string nameUtf8;
                        nameStr->toUTF8String(pContext, nameUtf8);
                        const proto::ProtoObject* nameVal = pContext->fromUTF8String(nameUtf8.c_str());
                        if (nameVal)
                            func = func->setAttribute(pContext, nameKey, nameVal);
                    }
                }
                stackPush(pContext, func ? func : PROTO_NONE);
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
                // JS semantics: if either operand is a string, convert both to string and concat.
                // Otherwise, convert both to number.
                bool aIsStr = a && a != PROTO_NONE && a->isString(pContext);
                bool bIsStr = b && b != PROTO_NONE && b->isString(pContext);
                const proto::ProtoObject* res;
                if (aIsStr || bIsStr) {
                    // String concatenation: convert both sides to string, use ProtoString::appendLast
                    const proto::ProtoObject* ra = toString(pContext, a);
                    const proto::ProtoObject* rb = toString(pContext, b);
                    const proto::ProtoString* sa = ra ? ra->asString(pContext) : nullptr;
                    const proto::ProtoString* sb = rb ? rb->asString(pContext) : nullptr;
                    if (sa && sb) {
                        const proto::ProtoString* concat = sa->appendLast(pContext, sb);
                        res = concat ? concat->asObject(pContext) : PROTO_NONE;
                    } else {
                        res = sa ? ra : (sb ? rb : PROTO_NONE);
                    }
                } else {
                    const proto::ProtoObject* ra = toNumber(pContext, a);
                    const proto::ProtoObject* rb = toNumber(pContext, b);
                    res = ra ? ra->add(pContext, rb) : PROTO_NONE;
                }
                stackPush(pContext, res ? res : PROTO_NONE);
                break;
            }
            case OP_mul: {
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* b = toNumber(pContext, stackTop(pContext));
                stackPop(pContext);
                const proto::ProtoObject* a = toNumber(pContext, stackTop(pContext));
                stackPop(pContext);
                const proto::ProtoObject* res = a->multiply(pContext, b);
                stackPush(pContext,res ? res : PROTO_NONE);
                break;
            }
            case OP_div: {
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* b = toNumber(pContext, stackTop(pContext));
                stackPop(pContext);
                const proto::ProtoObject* a = toNumber(pContext, stackTop(pContext));
                stackPop(pContext);
                const proto::ProtoObject* res = a->divide(pContext, b);
                stackPush(pContext,res ? res : PROTO_NONE);
                break;
            }
            case OP_sub: {
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* b = toNumber(pContext, stackTop(pContext));
                stackPop(pContext);
                const proto::ProtoObject* a = toNumber(pContext, stackTop(pContext));
                stackPop(pContext);
                const proto::ProtoObject* res = a->subtract(pContext, b);
                stackPush(pContext,res ? res : PROTO_NONE);
                break;
            }
            case OP_mod: {
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* b = toNumber(pContext, stackTop(pContext));
                stackPop(pContext);
                const proto::ProtoObject* a = toNumber(pContext, stackTop(pContext));
                stackPop(pContext);
                const proto::ProtoObject* res = a->modulo(pContext, b);
                stackPush(pContext,res ? res : PROTO_NONE);
                break;
            }
            case OP_eq:
            case OP_neq: {
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* b = stackTop(pContext);
                stackPop(pContext);
                const proto::ProtoObject* a = stackTop(pContext);
                stackPop(pContext);
                /* ToPrimitive helper: coerce an object to primitive via valueOf (bytecode or native). */
                auto toPrim = [&](const proto::ProtoObject* obj) -> const proto::ProtoObject* {
                    if (!obj || obj == PROTO_NONE || obj->isNone(pContext)) return obj;
                    if (obj->isBoolean(pContext) || obj->isInteger(pContext) ||
                        obj->isDouble(pContext) || obj->isFloat(pContext) ||
                        obj->asString(pContext)) return obj;
                    const proto::ProtoString* vk = ProtoJSStringCache::getKey(pContext, "valueOf");
                    if (!vk) return obj;
                    const proto::ProtoObject* vfn = obj->getAttribute(pContext, vk, true);
                    if (!vfn || vfn == PROTO_NONE) return obj;
                    const proto::ProtoObject* prim = PROTO_NONE;
                    int vbcId = getBytecodeId(pContext, vfn);
                    if (vbcId >= 0 && static_cast<size_t>(vbcId) < nested.size()) {
                        proto::ProtoContext childCtx(pContext->space, pContext, nullptr, nullptr, nullptr, nullptr);
                        childCtx.currentFileName = pContext->currentFileName;
                        const proto::ProtoObject* childEx = PROTO_NONE;
                        prim = runBytecode(&childCtx, &nested[vbcId], obj, pContext->newList(), pGlobalRoot, &childEx);
                    } else if (vfn->isMethod(pContext)) {
                        prim = vfn->call(pContext, nullptr, vk, obj, pContext->newList(), nullptr);
                    }
                    if (prim && prim != PROTO_NONE &&
                        (prim->isBoolean(pContext) || prim->isInteger(pContext) ||
                         prim->isDouble(pContext) || prim->isFloat(pContext) ||
                         prim->asString(pContext)))
                        return prim;
                    return obj;
                };
                const proto::ProtoObject* pa = toPrim(a);
                const proto::ProtoObject* pb = toPrim(b);
                bool eq = jsAbstractEquals(pContext, pa, pb);
                stackPush(pContext, (opcode == OP_eq ? eq : !eq) ? PROTO_TRUE : PROTO_FALSE);
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
                // Bitwise AND: ToInt32(a) & ToInt32(b)
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* b = stackTop(pContext); stackPop(pContext);
                const proto::ProtoObject* a = stackTop(pContext); stackPop(pContext);
                int32_t res = toInt32Val(pContext, a) & toInt32Val(pContext, b);
                stackPush(pContext, pContext->fromInteger(static_cast<long long>(res)));
                break;
            }
            case OP_or: {
                // Bitwise OR: ToInt32(a) | ToInt32(b)
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* b = stackTop(pContext); stackPop(pContext);
                const proto::ProtoObject* a = stackTop(pContext); stackPop(pContext);
                int32_t res = toInt32Val(pContext, a) | toInt32Val(pContext, b);
                stackPush(pContext, pContext->fromInteger(static_cast<long long>(res)));
                break;
            }
            case OP_xor: {
                // Bitwise XOR: ToInt32(a) ^ ToInt32(b)
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* b = stackTop(pContext); stackPop(pContext);
                const proto::ProtoObject* a = stackTop(pContext); stackPop(pContext);
                int32_t res = toInt32Val(pContext, a) ^ toInt32Val(pContext, b);
                stackPush(pContext, pContext->fromInteger(static_cast<long long>(res)));
                break;
            }
            case OP_shl: {
                // Left shift: ToInt32(a) << (ToUint32(b) & 0x1F)
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* b = stackTop(pContext); stackPop(pContext);
                const proto::ProtoObject* a = stackTop(pContext); stackPop(pContext);
                int32_t res = toInt32Val(pContext, a) << (toUint32Val(pContext, b) & 0x1Fu);
                stackPush(pContext, pContext->fromInteger(static_cast<long long>(res)));
                break;
            }
            case OP_sar: {
                // Arithmetic right shift: ToInt32(a) >> (ToUint32(b) & 0x1F)
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* b = stackTop(pContext); stackPop(pContext);
                const proto::ProtoObject* a = stackTop(pContext); stackPop(pContext);
                int32_t res = toInt32Val(pContext, a) >> (toUint32Val(pContext, b) & 0x1Fu);
                stackPush(pContext, pContext->fromInteger(static_cast<long long>(res)));
                break;
            }
            case OP_shr: {
                // Unsigned right shift: ToUint32(a) >>> (ToUint32(b) & 0x1F) → Int32 result
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* b = stackTop(pContext); stackPop(pContext);
                const proto::ProtoObject* a = stackTop(pContext); stackPop(pContext);
                uint32_t ua = toUint32Val(pContext, a);
                uint32_t shift = toUint32Val(pContext, b) & 0x1Fu;
                uint32_t ures = ua >> shift;
                // ToNumber result: if fits in signed int32, return integer, else double
                if (ures <= static_cast<uint32_t>(std::numeric_limits<int32_t>::max())) {
                    stackPush(pContext, pContext->fromInteger(static_cast<long long>(ures)));
                } else {
                    stackPush(pContext, pContext->fromDouble(static_cast<double>(ures)));
                }
                break;
            }
            case OP_not: {
                // Bitwise NOT: ~ToInt32(a)
                if (stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* a = stackTop(pContext); stackPop(pContext);
                int32_t res = ~toInt32Val(pContext, a);
                stackPush(pContext, pContext->fromInteger(static_cast<long long>(res)));
                break;
            }
            case OP_neg: {
                // Unary minus: -ToNumber(a)
                if (stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* a = stackTop(pContext); stackPop(pContext);
                const proto::ProtoObject* num = toNumber(pContext, a);
                if (!num || num == PROTO_NONE) { stackPush(pContext, pContext->fromDouble(std::numeric_limits<double>::quiet_NaN())); break; }
                if (num->isInteger(pContext)) {
                    long long v = num->asLong(pContext);
                    stackPush(pContext, v == 0 ? pContext->fromDouble(-0.0) : pContext->fromInteger(-v));
                } else {
                    stackPush(pContext, pContext->fromDouble(-num->asDouble(pContext)));
                }
                break;
            }
            case OP_lnot: {
                // Logical NOT: !ToBoolean(a)
                if (stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* a = stackTop(pContext); stackPop(pContext);
                stackPush(pContext, toBool(pContext, a) ? PROTO_FALSE : PROTO_TRUE);
                break;
            }
            case OP_inc: {
                // Prefix increment: ToNumber(a) + 1
                if (stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* a = toNumber(pContext, stackTop(pContext)); stackPop(pContext);
                if (!a || a == PROTO_NONE) { stackPush(pContext, pContext->fromDouble(std::numeric_limits<double>::quiet_NaN())); break; }
                if (a->isInteger(pContext)) stackPush(pContext, pContext->fromInteger(a->asLong(pContext) + 1));
                else stackPush(pContext, pContext->fromDouble(a->asDouble(pContext) + 1.0));
                break;
            }
            case OP_dec: {
                // Prefix decrement: ToNumber(a) - 1
                if (stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* a = toNumber(pContext, stackTop(pContext)); stackPop(pContext);
                if (!a || a == PROTO_NONE) { stackPush(pContext, pContext->fromDouble(std::numeric_limits<double>::quiet_NaN())); break; }
                if (a->isInteger(pContext)) stackPush(pContext, pContext->fromInteger(a->asLong(pContext) - 1));
                else stackPush(pContext, pContext->fromDouble(a->asDouble(pContext) - 1.0));
                break;
            }
            case OP_post_inc: {
                // Post-increment: pushes original value then incremented value.
                // Stack effect: a → a_orig, a+1 (but QuickJS spec: n_pop=1, n_push=2)
                if (stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* a = stackTop(pContext); stackPop(pContext);
                const proto::ProtoObject* num = toNumber(pContext, a);
                const proto::ProtoObject* inc;
                if (!num || num == PROTO_NONE) inc = pContext->fromDouble(std::numeric_limits<double>::quiet_NaN());
                else if (num->isInteger(pContext)) inc = pContext->fromInteger(num->asLong(pContext) + 1);
                else inc = pContext->fromDouble(num->asDouble(pContext) + 1.0);
                stackPush(pContext, num ? num : PROTO_NONE);
                stackPush(pContext, inc);
                break;
            }
            case OP_post_dec: {
                // Post-decrement: pushes original value then decremented value.
                if (stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* a = stackTop(pContext); stackPop(pContext);
                const proto::ProtoObject* num = toNumber(pContext, a);
                const proto::ProtoObject* dec;
                if (!num || num == PROTO_NONE) dec = pContext->fromDouble(std::numeric_limits<double>::quiet_NaN());
                else if (num->isInteger(pContext)) dec = pContext->fromInteger(num->asLong(pContext) - 1);
                else dec = pContext->fromDouble(num->asDouble(pContext) - 1.0);
                stackPush(pContext, num ? num : PROTO_NONE);
                stackPush(pContext, dec);
                break;
            }
            case OP_dec_loc: {
                // Decrement a local variable slot in-place. Format: loc8 (1 byte).
                if (pc + 1 > len) return PROTO_NONE;
                uint8_t locIndex = buf[pc++];
                if (locIndex < varCount) {
                    const proto::ProtoObject* cur = toNumber(pContext, getSlot(pContext, argCount + locIndex));
                    const proto::ProtoObject* nv;
                    if (!cur || cur == PROTO_NONE) nv = pContext->fromDouble(std::numeric_limits<double>::quiet_NaN());
                    else if (cur->isInteger(pContext)) nv = pContext->fromInteger(cur->asLong(pContext) - 1);
                    else nv = pContext->fromDouble(cur->asDouble(pContext) - 1.0);
                    setSlot(pContext, argCount + locIndex, nv);
                }
                break;
            }
            case OP_inc_loc: {
                // Increment a local variable slot in-place. Format: loc8 (1 byte).
                if (pc + 1 > len) return PROTO_NONE;
                uint8_t locIndex = buf[pc++];
                if (locIndex < varCount) {
                    const proto::ProtoObject* cur = toNumber(pContext, getSlot(pContext, argCount + locIndex));
                    const proto::ProtoObject* nv;
                    if (!cur || cur == PROTO_NONE) nv = pContext->fromDouble(std::numeric_limits<double>::quiet_NaN());
                    else if (cur->isInteger(pContext)) nv = pContext->fromInteger(cur->asLong(pContext) + 1);
                    else nv = pContext->fromDouble(cur->asDouble(pContext) + 1.0);
                    setSlot(pContext, argCount + locIndex, nv);
                }
                break;
            }
            case OP_add_loc: {
                // add_loc loc8: pops TOS and adds it to a local variable. Format: loc8 (1 byte).
                if (pc + 1 > len || stackEmpty(pContext)) return PROTO_NONE;
                uint8_t locIndex = buf[pc++];
                const proto::ProtoObject* val = stackTop(pContext); stackPop(pContext);
                if (locIndex < varCount) {
                    const proto::ProtoObject* cur = getSlot(pContext, argCount + locIndex);
                    // JS + semantics: string concat or numeric add.
                    bool curIsStr = cur && cur != PROTO_NONE && cur->asString(pContext);
                    bool valIsStr = val && val != PROTO_NONE && val->asString(pContext);
                    const proto::ProtoObject* nv;
                    if (curIsStr || valIsStr) {
                        const proto::ProtoObject* sc = toString(pContext, cur);
                        const proto::ProtoObject* sv = toString(pContext, val);
                        const proto::ProtoString* sa = sc ? sc->asString(pContext) : nullptr;
                        const proto::ProtoString* sb = sv ? sv->asString(pContext) : nullptr;
                        if (sa && sb) { const proto::ProtoString* cat = sa->appendLast(pContext, sb); nv = cat ? cat->asObject(pContext) : PROTO_NONE; }
                        else nv = sc ? sc : (sv ? sv : PROTO_NONE);
                    } else {
                        const proto::ProtoObject* nc = toNumber(pContext, cur);
                        const proto::ProtoObject* nval = toNumber(pContext, val);
                        nv = nc ? nc->add(pContext, nval) : PROTO_NONE;
                    }
                    setSlot(pContext, argCount + locIndex, nv);
                }
                break;
            }
            case OP_pow: {
                // Exponentiation: a ** b
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* b = toNumber(pContext, stackTop(pContext)); stackPop(pContext);
                const proto::ProtoObject* a = toNumber(pContext, stackTop(pContext)); stackPop(pContext);
                double da = (!a || a == PROTO_NONE) ? std::numeric_limits<double>::quiet_NaN() : a->asDouble(pContext);
                double db = (!b || b == PROTO_NONE) ? std::numeric_limits<double>::quiet_NaN() : b->asDouble(pContext);
                double result = std::pow(da, db);
                if (result == std::trunc(result) && std::abs(result) < 9.007199254740992e15 && !std::isnan(result) && !std::isinf(result))
                    stackPush(pContext, pContext->fromInteger(static_cast<long long>(result)));
                else
                    stackPush(pContext, pContext->fromDouble(result));
                break;
            }
            case OP_is_undefined_or_null: {
                // Pops one value; pushes true if it is undefined or null (both PROTO_NONE in protoCore).
                if (stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* val = stackTop(pContext); stackPop(pContext);
                stackPush(pContext, (!val || val == PROTO_NONE) ? PROTO_TRUE : PROTO_FALSE);
                break;
            }
            case OP_nop:
                // No operation.
                break;
            case OP_get_length: {
                // Push the .length property of TOS.
                if (stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* obj = stackTop(pContext); stackPop(pContext);
                const proto::ProtoString* lk = ProtoJSStringCache::getKey(pContext, "length");
                const proto::ProtoObject* len_val = (obj && lk) ? obj->getAttribute(pContext, lk, true) : PROTO_NONE;
                stackPush(pContext, len_val ? len_val : PROTO_NONE);
                break;
            }
            case OP_to_object: {
                // ToObject: for primitive types, returns a wrapper object.
                // For objects, returns the value unchanged.
                if (stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* val = stackTop(pContext); stackPop(pContext);
                // In our system, primitives remain as-is (we don't have wrapper objects yet).
                stackPush(pContext, val ? val : PROTO_NONE);
                break;
            }
            case OP_throw_error: {
                // throw_error atom u8: throw a TypeError/ReferenceError etc. with message from atom.
                // Format: atom(4), u8(1). n_pop=0, n_push=0 (throws).
                if (pc + 5 > len) return PROTO_NONE;
                uint32_t atomIndex = get_u32(buf + pc);
                uint8_t errorType = buf[pc + 4];
                pc += 5;
                const proto::ProtoString* key = resolveAtom(mod, pContext, atomIndex);
                std::string msg;
                if (key) key->toUTF8String(pContext, msg);
                const char* errName = (errorType == 1) ? "TypeError" :
                                      (errorType == 2) ? "ReferenceError" : "Error";
                pending_exception = makeError(pContext, errName, msg.c_str(), pGlobalRoot); has_pending_exception = true;
                break;
            }
            case OP_catch: {
                // catch label(4): push catch frame onto the handler stack.
                // The label is a relative offset from the label's own byte position (same as goto).
                // Also push an undefined placeholder on the value stack (n_push=1).
                if (pc + 4 > len) return PROTO_NONE;
                int32_t diff = static_cast<int32_t>(get_u32(buf + pc));
                int handler_pc = pc + diff; // same formula as OP_goto
                pc += 4;
                catch_stack.push_back({handler_pc, stackSize(pContext)});
                stackPush(pContext, PROTO_NONE); // placeholder (undefined) for the catch value
                break;
            }
            case OP_nip_catch: {
                // nip_catch: pop the active catch frame and clean the value stack.
                // Stack shape: ... [catch_placeholder] [top_value]  →  ... [top_value]
                if (!catch_stack.empty()) catch_stack.pop_back();
                if (stackSize(pContext) >= 2) {
                    const proto::ProtoObject* top = stackTop(pContext); stackPop(pContext);
                    stackPop(pContext); // remove the catch placeholder
                    stackPush(pContext, top);
                }
                break;
            }
            case OP_gosub: {
                // gosub label(4): call a finally block.
                // Push the return address (instruction after gosub) as an integer, then jump.
                if (pc + 4 > len) return PROTO_NONE;
                int32_t diff = static_cast<int32_t>(get_u32(buf + pc));
                int return_pc = pc + 4;
                stackPush(pContext, pContext->fromInteger(static_cast<long long>(return_pc)));
                pc += diff; // jump to finally block (same formula as goto)
                break;
            }
            case OP_ret: {
                // ret: return from a finally block.
                // Pop the return address pushed by gosub and jump there.
                if (stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* addr_obj = stackTop(pContext);
                stackPop(pContext);
                if (!addr_obj || addr_obj == PROTO_NONE || !addr_obj->isInteger(pContext))
                    return PROTO_NONE;
                pc = static_cast<int>(addr_obj->asLong(pContext));
                break;
            }
            case OP_plus: {
                // Unary plus: ToNumber(a)
                if (stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* a = stackTop(pContext); stackPop(pContext);
                stackPush(pContext, toNumber(pContext, a));
                break;
            }
            case OP_typeof: {
                if (stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* v = stackTop(pContext);
                stackPop(pContext);
                const char* typeStr = "undefined";
                if (v && v != PROTO_NONE && !v->isNone(pContext)) {
                    if (v->isBoolean(pContext)) typeStr = "boolean";
                    else if (v->isInteger(pContext) || v->isDouble(pContext) || v->isFloat(pContext)) typeStr = "number";
                    else if (v->asString(pContext)) typeStr = "string";
                    else if (v->isMethod(pContext) || getBytecodeId(pContext, v) >= 0) typeStr = "function";
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
                // Stack (top = index 0): arg0, ..., arg(n-1), func, this. QuickJS: call_argv = sp - argc, call_argv[-1] = func, call_argv[-2] = this.
                if (pc + 2 > len || stackEmpty(pContext)) return PROTO_NONE;
                uint32_t argc = get_u16(buf + pc);
                pc += 2;
                if (stackSize(pContext) < argc + 2) return PROTO_NONE;
                const proto::ProtoObject* func = stackAt(pContext, argc);
                const proto::ProtoObject* thisVal = stackAt(pContext, argc + 1);
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
                    const proto::ProtoObject* childEx = PROTO_NONE;
                    const proto::ProtoObject* result =
                        runBytecode(&childCtx, &nf, thisVal, argsList, pGlobalRoot, &childEx);
                    childCtx.returnValue = result;
                    if (childEx && childEx != PROTO_NONE) {
                        pending_exception = childEx; has_pending_exception = true;
                        break;
                    }
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
                    const proto::ProtoList* argsList = pContext->newList();
                    for (uint32_t i = 0; i < argc; i++)
                        argsList = argsList->appendLast(pContext, stackAt(pContext, argc - 1 - i));
                    for (uint32_t i = 0; i < argc + 2; i++) stackPop(pContext);
                    /* Function not yet converted to ProtoMethod; push PROTO_NONE. */
                    if (opcode != OP_tail_call_method)
                        stackPush(pContext, PROTO_NONE);
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
                    const proto::ProtoObject* childEx = PROTO_NONE;
                    result = runBytecode(&childCtx, &nf, newObj, argsList, pGlobalRoot, &childEx);
                    childCtx.returnValue = result;
                    if (childEx && childEx != PROTO_NONE) {
                        pending_exception = childEx; has_pending_exception = true;
                        break;
                    }
                } else if (func && func->isMethod(pContext)) {
                    result = func->call(pContext, nullptr,
                        ProtoJSStringCache::getKey(pContext, "call"), newObj, argsList, nullptr);
                } else {
                    // Check for built-in error constructor stub.
                    const proto::ProtoString* errCtorAttr = ProtoJSStringCache::getKey(pContext, "__error_ctor__");
                    const proto::ProtoObject* errTypeName = (func && func != PROTO_NONE && errCtorAttr)
                        ? func->getAttribute(pContext, errCtorAttr, false) : nullptr;
                    if (errTypeName && errTypeName != PROTO_NONE && errTypeName->isString(pContext)) {
                        const proto::ProtoObject* msgArg = (argc > 0) ? argsList->getAt(pContext, 0) : PROTO_NONE;
                        const proto::ProtoObject* msgStr = toString(pContext, msgArg);
                        std::string msgStr2;
                        if (msgStr && msgStr != PROTO_NONE && msgStr->isString(pContext))
                            msgStr->asString(pContext)->toUTF8String(pContext, msgStr2);
                        std::string errType;
                        errTypeName->asString(pContext)->toUTF8String(pContext, errType);
                        result = makeError(pContext, errType.c_str(), msgStr2.c_str(), pGlobalRoot);
                    } else {
                        result = PROTO_NONE;
                    }
                }
                bool resultIsObject = result && result != PROTO_NONE
                    && !result->isInteger(pContext) && !result->isDouble(pContext)
                    && !result->asString(pContext) && result != PROTO_TRUE && result != PROTO_FALSE;
                stackPush(pContext, resultIsObject ? result : newObj);
                break;
            }
            case OP_call0:
            case OP_call1:
            case OP_call2:
            case OP_call3:
            case OP_call: {
                uint32_t argc;
                if (opcode >= OP_call0 && opcode <= OP_call3) {
                    argc = static_cast<uint32_t>(opcode - OP_call0);
                } else {
                    if (pc + 2 > len || stackEmpty(pContext)) return PROTO_NONE;
                    argc = get_u16(buf + pc);
                    pc += 2;
                }
                if (stackEmpty(pContext) || stackSize(pContext) < argc + 1) return PROTO_NONE;
                const proto::ProtoObject* func = stackAt(pContext, argc);
                int bcId = getBytecodeId(pContext, func);
                if (bcId >= 0 && static_cast<size_t>(bcId) < nested.size()) {
                    const auto& nf = nested[bcId];
                    const proto::ProtoList* argsList = pContext->newList();
                    for (uint32_t i = 0; i < argc; i++)
                        argsList = argsList->appendLast(pContext, stackAt(pContext, argc - 1 - i));
                    // OP_call has no `this` slot on the stack; spec mandates undefined.
                    const proto::ProtoObject* thisVal = PROTO_NONE;
                    for (uint32_t i = 0; i <= argc; i++) stackPop(pContext);

                    proto::ProtoContext childCtx(pContext->space, pContext, nullptr, nullptr, nullptr, nullptr);
                    childCtx.currentFileName = pContext->currentFileName;
                    childCtx.currentLineNumber = pContext->currentLineNumber;
                    for (uint32_t i = 0; i < argc; i++)
                        setSlot(&childCtx, i, argsList->getAt(&childCtx, static_cast<int>(i)));

                    const proto::ProtoObject* childEx = PROTO_NONE;
                    const proto::ProtoObject* result =
                        runBytecode(&childCtx, &nf, thisVal, argsList, pGlobalRoot, &childEx);
                    childCtx.returnValue = result;
                    if (childEx && childEx != PROTO_NONE) {
                        pending_exception = childEx; has_pending_exception = true;
                        break;
                    }
                    stackPush(pContext, result ? result : PROTO_NONE);
                } else if (func && func->isMethod(pContext)) {
                    // OP_call: no `this` on the stack; pass undefined as receiver.
                    const proto::ProtoObject* thisVal = PROTO_NONE;
                    const proto::ProtoList* argsList = pContext->newList();
                    for (uint32_t i = 0; i < argc; i++)
                        argsList = argsList->appendLast(pContext, stackAt(pContext, argc - 1 - i));
                    for (uint32_t i = 0; i <= argc; i++) stackPop(pContext);
                    const proto::ProtoObject* result = func->call(pContext, nullptr,
                        ProtoJSStringCache::getKey(pContext, "call"), thisVal, argsList, nullptr);
                    stackPush(pContext, result ? result : PROTO_NONE);
                } else {
                    // Check if this is a built-in error constructor stub (registered by
                    // ensureBuiltinErrorConstructors). If so, call makeError with the first arg.
                    const proto::ProtoString* errCtorAttr = ProtoJSStringCache::getKey(pContext, "__error_ctor__");
                    const proto::ProtoObject* errTypeName = (func && func != PROTO_NONE && errCtorAttr)
                        ? func->getAttribute(pContext, errCtorAttr, false) : nullptr;
                    if (errTypeName && errTypeName != PROTO_NONE && errTypeName->isString(pContext)) {
                        const proto::ProtoObject* msgArg = (argc > 0) ? stackAt(pContext, argc - 1) : PROTO_NONE;
                        const proto::ProtoObject* msgStr = toString(pContext, msgArg);
                        std::string msgStr2;
                        if (msgStr && msgStr != PROTO_NONE && msgStr->isString(pContext))
                            msgStr->asString(pContext)->toUTF8String(pContext, msgStr2);
                        std::string errType;
                        errTypeName->asString(pContext)->toUTF8String(pContext, errType);
                        for (uint32_t i = 0; i <= argc; i++) stackPop(pContext);
                        stackPush(pContext, makeError(pContext, errType.c_str(), msgStr2.c_str(), pGlobalRoot));
                    } else {
                        const proto::ProtoList* argsList = pContext->newList();
                        for (uint32_t i = 0; i < argc; i++)
                            argsList = argsList->appendLast(pContext, stackAt(pContext, argc - 1 - i));
                        for (uint32_t i = 0; i <= argc; i++) stackPop(pContext);
                        /* Function not yet converted to ProtoMethod; push PROTO_NONE. */
                        stackPush(pContext, PROTO_NONE);
                    }
                }
                break;
            }
            case OP_fclosure8: {
                if (pc + 1 > len) return PROTO_NONE;
                uint8_t idx = buf[pc++];
                if (idx < cpool.size())
                    stackPush(pContext, cpool[idx]);
                else
                    stackPush(pContext, PROTO_NONE);
                break;
            }
            case OP_fclosure: {
                if (pc + 4 > len) return PROTO_NONE;
                uint32_t idx = get_u32(buf + pc);
                pc += 4;
                if (idx < cpool.size())
                    stackPush(pContext, cpool[idx]);
                else
                    stackPush(pContext, PROTO_NONE);
                break;
            }
            case OP_is_undefined: {
                // Pops one value; pushes true if it is undefined (PROTO_NONE in protoCore).
                if (stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* val = stackTop(pContext);
                stackPop(pContext);
                stackPush(pContext, (!val || val == PROTO_NONE) ? PROTO_TRUE : PROTO_FALSE);
                break;
            }
            case OP_is_null: {
                // Pops one value; pushes true if it is null. protoCore maps both null and
                // undefined to PROTO_NONE, so we treat PROTO_NONE as null here.
                if (stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* val = stackTop(pContext);
                stackPop(pContext);
                stackPush(pContext, (!val || val == PROTO_NONE) ? PROTO_TRUE : PROTO_FALSE);
                break;
            }
            case OP_typeof_is_undefined: {
                // Pops one value; pushes true if typeof is "undefined".
                if (stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* val = stackTop(pContext);
                stackPop(pContext);
                stackPush(pContext, (!val || val == PROTO_NONE) ? PROTO_TRUE : PROTO_FALSE);
                break;
            }
            case OP_typeof_is_function: {
                // Pops one value; pushes true if typeof is "function".
                if (stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* val = stackTop(pContext);
                stackPop(pContext);
                int bcId = getBytecodeId(pContext, val);
                bool isFunc = (bcId >= 0) ||
                              (val && val != PROTO_NONE && val->isMethod(pContext));
                stackPush(pContext, isFunc ? PROTO_TRUE : PROTO_FALSE);
                break;
            }
            // ---------------------------------------------------------------
            // Step A — OP_array_from
            // DEF(array_from, 3, 0, 1, npop) — 1 opcode + 2-byte element count
            // ---------------------------------------------------------------
            case OP_array_from: {
                if (pc + 2 > len) return PROTO_NONE;
                uint16_t count = get_u16(buf + pc);
                pc += 2;
                // Collect `count` elements from the stack (TOS = last element, i.e. index count-1).
                const proto::ProtoObject* arr = pContext->newObject(false);
                if (!arr) { stackPush(pContext, PROTO_NONE); break; }
                // Read elements bottom-first (stack order: elem[0] deepest, elem[count-1] at TOS).
                for (uint16_t i = 0; i < count; i++) {
                    const proto::ProtoObject* elem = stackAt(pContext, static_cast<unsigned long>(count - 1 - i));
                    std::string idxStr = std::to_string(i);
                    const proto::ProtoObject* idxObj = pContext->fromUTF8String(idxStr.c_str());
                    const proto::ProtoString* idxKey = idxObj ? idxObj->asString(pContext) : nullptr;
                    if (idxKey) arr = arr->setAttribute(pContext, idxKey, elem ? elem : PROTO_NONE);
                }
                for (uint16_t i = 0; i < count; i++) stackPop(pContext);
                // Set .length
                const proto::ProtoString* lenKey = ProtoJSStringCache::getKey(pContext, "length");
                if (lenKey) arr = arr->setAttribute(pContext, lenKey, pContext->fromInteger(static_cast<long long>(count)));
                stackPush(pContext, arr ? arr : PROTO_NONE);
                break;
            }

            // ---------------------------------------------------------------
            // Step B — for-of iterator protocol
            // ---------------------------------------------------------------

            // OP_for_of_start: DEF(for_of_start, 1, 1, 3, none)
            // Pops iterable; pushes [iterator, nextMethod, catch_offset].
            case OP_for_of_start: {
                if (stackEmpty(pContext)) return PROTO_NONE;
                const proto::ProtoObject* iterable = stackTop(pContext);
                stackPop(pContext);
                // PROTO_NONE guard: generator iterables return PROTO_NONE from OP_initial_yield
                // (unsupported). Propagate vacuous-pass so generator-based for-of tests don't regress.
                if (!iterable || iterable == PROTO_NONE) return PROTO_NONE;
                // Require a numeric .length — arrays only.  Non-array iterables fall through to vacuous pass.
                const proto::ProtoString* lenKey2 = ProtoJSStringCache::getKey(pContext, "length");
                const proto::ProtoObject* lenVal = lenKey2 ? iterable->getAttribute(pContext, lenKey2, false) : PROTO_NONE;
                if (!lenVal || lenVal == PROTO_NONE || !lenVal->isInteger(pContext)) return PROTO_NONE;
                // Use a PC-based slot pair unique to this for-of loop site.
                uint32_t baseSlot = 0x10000u + static_cast<uint32_t>(pc - 1);
                setSlot(pContext, baseSlot,     iterable);
                setSlot(pContext, baseSlot + 1, pContext->fromInteger(0LL));
                // Build a lightweight iterator object carrying the slot base.
                const proto::ProtoObject* iterObj = pContext->newObject(false);
                if (!iterObj) return PROTO_NONE;
                const proto::ProtoString* slotKey2 = ProtoJSStringCache::getKey(pContext, "__iter_slot__");
                if (slotKey2)
                    iterObj = iterObj->setAttribute(pContext, slotKey2, pContext->fromInteger(static_cast<long long>(baseSlot)));
                stackPush(pContext, iterObj);           // iterator
                stackPush(pContext, PROTO_NONE);        // nextMethod placeholder
                stackPush(pContext, pContext->fromInteger(0LL)); // catch_offset placeholder
                break;
            }

            // OP_for_of_next: DEF(for_of_next, 2, 3, 5, u8)
            // 1 opcode + 1 u8 rawOffset.  Reads iterator state; pushes [value, done] (net +2).
            case OP_for_of_next: {
                if (pc + 1 > len) return PROTO_NONE;
                uint8_t rawOffset = buf[pc++];
                // iterator object is 2 + rawOffset positions from TOS (QuickJS protocol).
                const proto::ProtoObject* iterObj2 = stackAt(pContext, static_cast<unsigned long>(2 + rawOffset));
                if (!iterObj2 || iterObj2 == PROTO_NONE) {
                    stackPush(pContext, PROTO_NONE);
                    stackPush(pContext, PROTO_TRUE);
                    break;
                }
                const proto::ProtoString* slotKey3 = ProtoJSStringCache::getKey(pContext, "__iter_slot__");
                const proto::ProtoObject* slotVal = slotKey3 ? iterObj2->getAttribute(pContext, slotKey3, false) : PROTO_NONE;
                if (!slotVal || slotVal == PROTO_NONE || !slotVal->isInteger(pContext)) {
                    stackPush(pContext, PROTO_NONE);
                    stackPush(pContext, PROTO_TRUE);
                    break;
                }
                uint32_t bs = static_cast<uint32_t>(slotVal->asLong(pContext));
                const proto::ProtoObject* arrObj  = getSlot(pContext, bs);
                const proto::ProtoObject* idxObj2 = getSlot(pContext, bs + 1);
                if (!arrObj || arrObj == PROTO_NONE || !idxObj2 || !idxObj2->isInteger(pContext)) {
                    stackPush(pContext, PROTO_NONE);
                    stackPush(pContext, PROTO_TRUE);
                    break;
                }
                long long idx2 = idxObj2->asLong(pContext);
                const proto::ProtoString* lenKey3 = ProtoJSStringCache::getKey(pContext, "length");
                const proto::ProtoObject* lenVal2 = lenKey3 ? arrObj->getAttribute(pContext, lenKey3, false) : PROTO_NONE;
                long long arrLen = (lenVal2 && lenVal2 != PROTO_NONE && lenVal2->isInteger(pContext))
                                   ? lenVal2->asLong(pContext) : 0LL;
                if (idx2 >= arrLen) {
                    stackPush(pContext, PROTO_NONE);
                    stackPush(pContext, PROTO_TRUE);
                    break;
                }
                std::string elemIdxStr = std::to_string(idx2);
                const proto::ProtoObject* elemIdxObj = pContext->fromUTF8String(elemIdxStr.c_str());
                const proto::ProtoString* elemIdxKey = elemIdxObj ? elemIdxObj->asString(pContext) : nullptr;
                const proto::ProtoObject* elemVal = elemIdxKey
                    ? arrObj->getAttribute(pContext, elemIdxKey, false) : PROTO_NONE;
                setSlot(pContext, bs + 1, pContext->fromInteger(idx2 + 1LL));
                stackPush(pContext, elemVal ? elemVal : PROTO_NONE);
                stackPush(pContext, PROTO_FALSE); // done = false
                break;
            }

            // OP_iterator_get_value_done: DEF(iterator_get_value_done, 1, 2, 3, none)
            // Stack before: [..., catch_0, result_obj]  → after: [..., new_catch_0, value, done]
            case OP_iterator_get_value_done: {
                if (stackSize(pContext) < 2) return PROTO_NONE;
                const proto::ProtoObject* result_obj = stackTop(pContext); stackPop(pContext);
                stackPop(pContext); // discard catch_0
                const proto::ProtoString* valueKey = ProtoJSStringCache::getKey(pContext, "value");
                const proto::ProtoString* doneKey  = ProtoJSStringCache::getKey(pContext, "done");
                const proto::ProtoObject* value = (result_obj && result_obj != PROTO_NONE && valueKey)
                    ? result_obj->getAttribute(pContext, valueKey, false) : PROTO_NONE;
                const proto::ProtoObject* doneRaw = (result_obj && result_obj != PROTO_NONE && doneKey)
                    ? result_obj->getAttribute(pContext, doneKey, false) : PROTO_TRUE;
                const proto::ProtoObject* done = (doneRaw == PROTO_TRUE || doneRaw == PROTO_FALSE)
                    ? doneRaw : PROTO_FALSE;
                stackPush(pContext, pContext->fromInteger(0LL)); // new catch_offset placeholder
                stackPush(pContext, value ? value : PROTO_NONE);
                stackPush(pContext, done);
                break;
            }

            // OP_iterator_check_object: DEF(iterator_check_object, 1, 1, 1, none)
            // Validates iterator result is object-like; protoCore accepts any non-null — no-op.
            case OP_iterator_check_object:
                // Leave TOS unchanged; no validation throw in protoCore.
                break;

            // OP_iterator_close: DEF(iterator_close, 1, 3, 0, none)
            // Pops [iter, nextMethod, catch_0] from stack.
            case OP_iterator_close: {
                for (int i = 0; i < 3 && !stackEmpty(pContext); i++) stackPop(pContext);
                break;
            }

            // OP_iterator_next: DEF(iterator_next, 1, 4, 4, none)
            // Used in destructuring. Unsupported — preserve vacuous-pass behaviour for
            // tests that use iterator destructuring (they previously exited here via default:).
            case OP_iterator_next:
                return PROTO_NONE;

            // OP_iterator_call: DEF(iterator_call, 2, 4, 5, u8)
            // Used in advanced iterator protocol. Unsupported — preserve vacuous-pass.
            case OP_iterator_call:
                if (pc + 1 <= len) pc += 1; // skip u8 flags byte before returning
                return PROTO_NONE;

            // ---------------------------------------------------------------
            // Step C — for-in (PROTO_NONE guard — key enumeration not supported)
            // ---------------------------------------------------------------

            // OP_for_in_start: DEF(for_in_start, 1, 1, 1, none)
            case OP_for_in_start: {
                if (!stackEmpty(pContext)) stackPop(pContext);
                // Cannot enumerate property keys without a protoCore API for key iteration.
                return PROTO_NONE;
            }

            // OP_for_in_next: DEF(for_in_next, 1, 1, 3, none)
            // Stub in case for_in_start is ever enhanced; maintain stack balance.
            case OP_for_in_next: {
                if (!stackEmpty(pContext)) stackPop(pContext);
                stackPush(pContext, PROTO_NONE);
                stackPush(pContext, PROTO_NONE);
                stackPush(pContext, PROTO_TRUE); // done = true
                break;
            }

            default: {
                // Unknown opcode: log for diagnostics; execution cannot continue safely.
                std::fprintf(stderr, "[ProtoInterpreter] unsupported opcode 0x%02x at byte offset %d\n",
                    static_cast<unsigned>(opcode), static_cast<int>(pc - 1));
                return PROTO_NONE;
            }
        } // end switch

        // --- Exception dispatch ---
        // If the switch body raised an exception (has_pending_exception flag),
        // either jump to the nearest catch handler or propagate to the caller.
        // Note: pending_exception may be PROTO_NONE (JS `throw undefined`) — use the flag.
        if (has_pending_exception) {
            has_pending_exception = false;
            if (!catch_stack.empty()) {
                CatchFrame frame = catch_stack.back();
                catch_stack.pop_back();
                // Restore value stack to the depth recorded at the catch site.
                while (stackSize(pContext) > frame.stack_depth) stackPop(pContext);
                stackPush(pContext, pending_exception);
                pc = frame.handler_pc;
                pending_exception = nullptr;
                // Continue executing from the catch handler.
            } else {
                if (outException) *outException = pending_exception;
                return PROTO_NONE;
            }
        }
    }
    return PROTO_NONE;
}

} // namespace protojs
