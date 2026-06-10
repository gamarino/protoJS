#include "ProxyBuiltin.h"
#include "JSSymbols.h"
#include "runtime/ProtoInterpreter.h"

namespace protojs {

// Sidecar key names for the proxy markers.  Auto-interned by
// setAttribute on first use; subsequent lookups hit the symbol table
// directly.
static const proto::ProtoString* targetKey(proto::ProtoContext* ctx) {
    static const proto::ProtoString* k = nullptr;
    if (k) return k;
    const proto::ProtoObject* o = ctx->fromUTF8String("__proxy_target__");
    k = o ? o->asString(ctx) : nullptr;
    return k;
}
static const proto::ProtoString* handlerKey(proto::ProtoContext* ctx) {
    static const proto::ProtoString* k = nullptr;
    if (k) return k;
    const proto::ProtoObject* o = ctx->fromUTF8String("__proxy_handler__");
    k = o ? o->asString(ctx) : nullptr;
    return k;
}

bool isProxy(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    if (!obj || obj == PROTO_NONE) return false;
    const proto::ProtoString* tk = targetKey(ctx);
    if (!tk) return false;
    return obj->hasOwnAttribute(ctx, tk) == PROTO_TRUE;
}

const proto::ProtoObject* proxyTarget(proto::ProtoContext* ctx,
                                       const proto::ProtoObject* proxy) {
    const proto::ProtoString* tk = targetKey(ctx);
    if (!tk || !proxy) return nullptr;
    const proto::ProtoObject* t = proxy->getAttribute(ctx, tk, false);
    return (t && t != PROTO_NONE) ? t : nullptr;
}

const proto::ProtoObject* proxyHandler(proto::ProtoContext* ctx,
                                        const proto::ProtoObject* proxy) {
    const proto::ProtoString* hk = handlerKey(ctx);
    if (!hk || !proxy) return nullptr;
    const proto::ProtoObject* h = proxy->getAttribute(ctx, hk, false);
    return (h && h != PROTO_NONE) ? h : nullptr;
}

// Helper: look up a trap by name on the handler.  Returns the
// callable, or nullptr if absent / not callable.
static const proto::ProtoObject* lookupTrap(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* handler,
    const char* trapName)
{
    if (!handler || handler == PROTO_NONE) return nullptr;
    const proto::ProtoObject* nameObj = ctx->fromUTF8String(trapName);
    const proto::ProtoString* name = nameObj ? nameObj->asString(ctx) : nullptr;
    if (!name) return nullptr;
    const proto::ProtoObject* trap = handler->getAttribute(ctx, name, true);
    if (!trap || trap == PROTO_NONE) return nullptr;
    // Probe callability via the same markers IsCallable uses elsewhere.
    if (trap->isMethod(ctx)) return trap;
    const proto::ProtoString* bcK = JSSymbols::bytecodeId(ctx);
    if (bcK && trap->hasAttribute(ctx, bcK) == PROTO_TRUE) return trap;
    const proto::ProtoString* nfK = JSSymbols::nativeFn(ctx);
    if (nfK && trap->hasAttribute(ctx, nfK) == PROTO_TRUE) return trap;
    const proto::ProtoString* bfK = JSSymbols::boundFn(ctx);
    if (bfK && trap->hasAttribute(ctx, bfK) == PROTO_TRUE) return trap;
    return nullptr;
}

// Default [[Get]] on a non-proxy object: chain-walking getAttribute
// with accessor descriptor support.  Mirrors what L_OP_get_field
// would have done.
static const proto::ProtoObject* defaultGet(proto::ProtoContext* ctx,
                                              const proto::ProtoObject* target,
                                              const proto::ProtoString* key) {
    if (!target || target == PROTO_NONE || !key) return PROTO_NONE;
    // Accessor sidecar probe.
    std::string ks; key->toUTF8String(ctx, ks);
    const std::string gks = "__get_" + ks + "__";
    const proto::ProtoObject* gko = ctx->fromUTF8String(gks.c_str());
    const proto::ProtoString* gk = gko ? gko->asString(ctx) : nullptr;
    if (gk) {
        const proto::ProtoObject* getter = target->getAttribute(ctx, gk, true);
        if (getter && getter != PROTO_NONE) {
            const proto::ProtoList* a = ctx->newList();
            return callJSFunction(ctx, getter, target, a);
        }
    }
    const proto::ProtoObject* v = target->getAttribute(ctx, key, true);
    return v ? v : PROTO_NONE;
}

const proto::ProtoObject* proxyDispatchGet(proto::ProtoContext* ctx,
                                            const proto::ProtoObject* proxy,
                                            const proto::ProtoString* propKey,
                                            const proto::ProtoObject* receiver) {
    const proto::ProtoObject* target = proxyTarget(ctx, proxy);
    if (!target) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Cannot perform 'get' on a proxy that has been revoked"));
        return PROTO_NONE;
    }
    const proto::ProtoObject* handler = proxyHandler(ctx, proxy);
    const proto::ProtoObject* trap = lookupTrap(ctx, handler, "get");
    if (trap) {
        const proto::ProtoList* a = ctx->newList();
        a = a->appendLast(ctx, target);
        a = a->appendLast(ctx, propKey ? propKey->asObject(ctx) : PROTO_NONE);
        a = a->appendLast(ctx, receiver ? receiver : proxy);
        return callJSFunction(ctx, trap, handler, a);
    }
    return defaultGet(ctx, target, propKey);
}

const proto::ProtoObject* proxyDispatchSet(proto::ProtoContext* ctx,
                                            const proto::ProtoObject* proxy,
                                            const proto::ProtoString* propKey,
                                            const proto::ProtoObject* value,
                                            const proto::ProtoObject* receiver) {
    const proto::ProtoObject* target = proxyTarget(ctx, proxy);
    if (!target) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Cannot perform 'set' on a proxy that has been revoked"));
        return PROTO_FALSE;
    }
    const proto::ProtoObject* handler = proxyHandler(ctx, proxy);
    const proto::ProtoObject* trap = lookupTrap(ctx, handler, "set");
    if (trap) {
        const proto::ProtoList* a = ctx->newList();
        a = a->appendLast(ctx, target);
        a = a->appendLast(ctx, propKey ? propKey->asObject(ctx) : PROTO_NONE);
        a = a->appendLast(ctx, value ? value : PROTO_NONE);
        a = a->appendLast(ctx, receiver ? receiver : proxy);
        const proto::ProtoObject* r = callJSFunction(ctx, trap, handler, a);
        // ECMA-262 §9.5.9 step 9: if the trap returns falsy in strict
        // mode the assignment must throw a TypeError; out here we
        // surface the boolean and let the caller decide.
        if (!r || r == PROTO_NONE) return PROTO_FALSE;
        if (r == PROTO_FALSE || r == PROTO_TRUE) return r;
        // Truthy non-boolean → PROTO_TRUE; falsy → PROTO_FALSE.
        return r ? PROTO_TRUE : PROTO_FALSE;
    }
    // No trap: write directly to target.
    if (propKey) {
        const_cast<proto::ProtoObject*>(target)->setAttribute(ctx, propKey,
            value ? value : PROTO_NONE);
    }
    return PROTO_TRUE;
}

const proto::ProtoObject* proxyDispatchHas(proto::ProtoContext* ctx,
                                            const proto::ProtoObject* proxy,
                                            const proto::ProtoString* propKey) {
    const proto::ProtoObject* target = proxyTarget(ctx, proxy);
    if (!target) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Cannot perform 'has' on a proxy that has been revoked"));
        return PROTO_FALSE;
    }
    const proto::ProtoObject* handler = proxyHandler(ctx, proxy);
    const proto::ProtoObject* trap = lookupTrap(ctx, handler, "has");
    if (trap) {
        const proto::ProtoList* a = ctx->newList();
        a = a->appendLast(ctx, target);
        a = a->appendLast(ctx, propKey ? propKey->asObject(ctx) : PROTO_NONE);
        const proto::ProtoObject* r = callJSFunction(ctx, trap, handler, a);
        if (!r || r == PROTO_NONE || r == PROTO_FALSE) return PROTO_FALSE;
        return PROTO_TRUE;
    }
    // Default: walk the chain like the `in` operator.
    if (propKey && target->hasAttribute(ctx, propKey) == PROTO_TRUE)
        return PROTO_TRUE;
    return PROTO_FALSE;
}

const proto::ProtoObject* proxyDispatchDelete(proto::ProtoContext* ctx,
                                               const proto::ProtoObject* proxy,
                                               const proto::ProtoString* propKey) {
    const proto::ProtoObject* target = proxyTarget(ctx, proxy);
    if (!target) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Cannot perform 'deleteProperty' on a proxy that has been revoked"));
        return PROTO_FALSE;
    }
    const proto::ProtoObject* handler = proxyHandler(ctx, proxy);
    const proto::ProtoObject* trap = lookupTrap(ctx, handler, "deleteProperty");
    if (trap) {
        const proto::ProtoList* a = ctx->newList();
        a = a->appendLast(ctx, target);
        a = a->appendLast(ctx, propKey ? propKey->asObject(ctx) : PROTO_NONE);
        const proto::ProtoObject* r = callJSFunction(ctx, trap, handler, a);
        if (!r || r == PROTO_NONE || r == PROTO_FALSE) return PROTO_FALSE;
        return PROTO_TRUE;
    }
    if (propKey) {
        const_cast<proto::ProtoObject*>(target)->setAttribute(ctx, propKey, PROTO_NONE);
    }
    return PROTO_TRUE;
}

const proto::ProtoObject* proxyLookupTrap(proto::ProtoContext* ctx,
                                           const proto::ProtoObject* proxy,
                                           const char* trapName) {
    const proto::ProtoObject* handler = proxyHandler(ctx, proxy);
    return lookupTrap(ctx, handler, trapName);
}

// new Proxy(target, handler) — §28.2.1.
const proto::ProtoObject* proxyConstructor(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!ctx) return PROTO_NONE;
    if (!args || args->getSize(ctx) < 2) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Cannot create proxy with a non-object as target or handler"));
        return PROTO_NONE;
    }
    const proto::ProtoObject* target = args->getAt(ctx, 0);
    const proto::ProtoObject* handler = args->getAt(ctx, 1);
    // Both arguments must be objects (not null / undefined / primitive).
    auto isObj = [&](const proto::ProtoObject* o) {
        if (!o || o == PROTO_NONE) return false;
        if (o->isInteger(ctx) || o->isDouble(ctx) || o->isFloat(ctx)) return false;
        if (o->isBoolean(ctx) || o->asString(ctx)) return false;
        return true;
    };
    if (!isObj(target) || !isObj(handler)) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Cannot create proxy with a non-object as target or handler"));
        return PROTO_NONE;
    }
    const proto::ProtoObject* proxy = (self && self != PROTO_NONE)
        ? self : ctx->newObject(true);
    if (!proxy) return PROTO_NONE;
    const proto::ProtoString* tk = targetKey(ctx);
    const proto::ProtoString* hk = handlerKey(ctx);
    if (tk) proxy = proxy->setAttribute(ctx, tk, target);
    if (hk) proxy = proxy->setAttribute(ctx, hk, handler);
    return proxy;
}

// ---------------------------------------------------------------------------
// Installation entry point — register Proxy constructor on the global
// root, replacing the bare unimplementedCtor stub.  The Reflect.*
// helpers are already wired in ProtoInterpreter.cpp; they call into
// proxyDispatch* via isProxy() when the target is a proxy.
// ---------------------------------------------------------------------------

static const proto::ProtoObject* installCallable(
    proto::ProtoContext* ctx,
    proto::ProtoMethod method,
    const char* name,
    long long length)
{
    if (!ctx) return PROTO_NONE;
    const proto::ProtoObject* fp = ctx->space ? ctx->space->methodPrototype : nullptr;
    const proto::ProtoObject* fn = fp ? fp->newChild(ctx, true) : ctx->newObject(true);
    if (!fn) return PROTO_NONE;
    const proto::ProtoString* nfk = JSSymbols::nativeFn(ctx);
    if (nfk) fn = fn->setAttribute(ctx, nfk, ctx->fromMethod(nullptr, method));
    const proto::ProtoString* nk = JSSymbols::name(ctx);
    if (nk) {
        fn = fn->setAttribute(ctx, nk, ctx->fromUTF8String(name));
        const proto::ProtoString* pdnk = JSSymbols::pdName(ctx);
        if (pdnk) fn = fn->setAttribute(ctx, pdnk, ctx->fromInteger(0x2LL));
    }
    const proto::ProtoString* lk = JSSymbols::length(ctx);
    if (lk) {
        fn = fn->setAttribute(ctx, lk, ctx->fromInteger(length));
        const proto::ProtoString* pdlk = JSSymbols::pdLength(ctx);
        if (pdlk) fn = fn->setAttribute(ctx, pdlk, ctx->fromInteger(0x2LL));
    }
    return fn;
}

void installProxyAndReflect(proto::ProtoContext* ctx,
                             const proto::ProtoObject** globalRoot) {
    if (!ctx || !globalRoot || !*globalRoot) return;

    // Proxy constructor: native fn that builds the proxy via
    // proxyConstructor when invoked via OP_call_constructor.  The
    // constructor marker is set so isConstructor(Proxy) is true.
    const proto::ProtoObject* ctor = installCallable(ctx, proxyConstructor, "Proxy", 2);
    if (!ctor) return;
    const proto::ProtoString* icK = JSSymbols::isConstructor(ctx);
    if (icK) ctor = ctor->setAttribute(ctx, icK, PROTO_TRUE);
    const proto::ProtoString* ctorMK = JSSymbols::construct(ctx);
    if (ctorMK) ctor = ctor->setAttribute(ctx, ctorMK,
        ctx->fromMethod(nullptr, proxyConstructor));
    const proto::ProtoString* k = ctx->fromUTF8String("Proxy")->asString(ctx);
    if (k) *globalRoot = (*globalRoot)->setAttribute(ctx, k, ctor);
}

} // namespace protojs
