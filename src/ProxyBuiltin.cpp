#include "ProxyBuiltin.h"
#include "ArrayPrototype.h"
#include "ArrayElementsStorage.h"
#include "ObjectPrototype.h"
#include "JSSymbols.h"
#include "JSContext.h"
#include "runtime/ProtoInterpreter.h"
#include <cmath>
#include <unordered_set>

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
    // §7.3.10 GetMethod step 3-5: null / undefined → return undefined
    // (no trap dispatch).  Absent → likewise.  Present but non-callable
    // → TypeError.  Pre-fix the non-callable path returned nullptr
    // identically to "absent", which silently fell through to the
    // default behaviour instead of surfacing the spec TypeError
    // (test262 Proxy/<trap>/trap-is-not-callable across get / set /
    // has / deleteProperty / apply / construct / etc.).
    if (!trap || trap == PROTO_NONE
        || trap == getUndefinedSentinel() || trap == getNullSentinel()) {
        return nullptr;
    }
    // Probe callability via the same markers IsCallable uses elsewhere.
    if (trap->isMethod(ctx)) return trap;
    const proto::ProtoString* bcK = JSSymbols::bytecodeId(ctx);
    if (bcK && trap->hasAttribute(ctx, bcK) == PROTO_TRUE) return trap;
    const proto::ProtoString* nfK = JSSymbols::nativeFn(ctx);
    if (nfK && trap->hasAttribute(ctx, nfK) == PROTO_TRUE) return trap;
    const proto::ProtoString* bfK = JSSymbols::boundFn(ctx);
    if (bfK && trap->hasAttribute(ctx, bfK) == PROTO_TRUE) return trap;
    // Present but non-callable.
    std::string msg = "Proxy handler's '";
    msg += trapName;
    msg += "' trap is not callable";
    signalNativeException(makeNativeError(ctx, "TypeError", msg.c_str()));
    return nullptr;
}

// ---------------------------------------------------------------------------
// Invariant helpers — used by the trap dispatchers to enforce the
// §10.5.* "validate" steps after the user handler returns.  We probe
// the target's own __pd_<key>__ descriptor sidecar (bit layout: 0x1 =
// writable, 0x2 = configurable, 0x4 = enumerable) and the
// NonExtensibleMarker the wrapper attaches via Object.preventExtensions /
// .seal / .freeze.

struct OwnDescriptor {
    bool present = false;            // target has this as an own property
    bool isAccessor = false;         // accessor (vs data) descriptor
    bool writable = true;            // data only — true if writable
    bool configurable = true;        // true if configurable
    bool hasGetter = false;          // accessor only — true if non-undefined
    bool hasSetter = false;          // accessor only — true if non-undefined
    const proto::ProtoObject* value = nullptr; // data: the value
};

static int probeDescriptorBits(proto::ProtoContext* ctx,
                                const proto::ProtoObject* target,
                                const proto::ProtoString* key,
                                const std::string& kstr) {
    // Default bits when no sidecar present: {writable, enumerable,
    // configurable} = 7.  Per defineProperty's normalisation path,
    // freshly created own data props that omit the descriptor have
    // ALL flags set.
    const std::string pdStr = "__pd_" + kstr + "__";
    const proto::ProtoObject* pdo = ctx->fromUTF8String(pdStr.c_str());
    const proto::ProtoString* pdk = pdo ? pdo->asString(ctx) : nullptr;
    if (!pdk) return 0x7;
    if (target->hasOwnAttribute(ctx, pdk) != PROTO_TRUE) return 0x7;
    const proto::ProtoObject* pdv = target->getAttribute(ctx, pdk, false);
    if (!pdv || pdv == PROTO_NONE || !pdv->isInteger(ctx)) return 0x7;
    return static_cast<int>(pdv->asLong(ctx));
}

static OwnDescriptor probeOwnDescriptor(proto::ProtoContext* ctx,
                                          const proto::ProtoObject* target,
                                          const proto::ProtoString* key) {
    OwnDescriptor d;
    if (!target || target == PROTO_NONE || !key) return d;
    std::string kstr; key->toUTF8String(ctx, kstr);

    // Accessor descriptor: own __get_<key>__ or __set_<key>__ present.
    const std::string gks = "__get_" + kstr + "__";
    const std::string sks = "__set_" + kstr + "__";
    const proto::ProtoObject* gko = ctx->fromUTF8String(gks.c_str());
    const proto::ProtoObject* sko = ctx->fromUTF8String(sks.c_str());
    const proto::ProtoString* gk = gko ? gko->asString(ctx) : nullptr;
    const proto::ProtoString* sk = sko ? sko->asString(ctx) : nullptr;
    bool hasOwnGetter = gk && target->hasOwnAttribute(ctx, gk) == PROTO_TRUE;
    bool hasOwnSetter = sk && target->hasOwnAttribute(ctx, sk) == PROTO_TRUE;
    if (hasOwnGetter || hasOwnSetter) {
        d.present = true;
        d.isAccessor = true;
        if (hasOwnGetter) {
            const proto::ProtoObject* gv = target->getAttribute(ctx, gk, false);
            d.hasGetter = gv && gv != PROTO_NONE;
        }
        if (hasOwnSetter) {
            const proto::ProtoObject* sv = target->getAttribute(ctx, sk, false);
            d.hasSetter = sv && sv != PROTO_NONE;
        }
        int bits = probeDescriptorBits(ctx, target, key, kstr);
        d.configurable = (bits & 0x2) != 0;
        return d;
    }

    // Data descriptor: probed via hasOwnAttribute on the key itself.
    if (target->hasOwnAttribute(ctx, key) == PROTO_TRUE) {
        const proto::ProtoObject* v = target->getAttribute(ctx, key, false);
        // PROTO_NONE-valued own data slot is the "deleted" sentinel
        // used by the array index delete path — treat as absent.
        if (v && v != PROTO_NONE) {
            d.present = true;
            d.value = v;
            int bits = probeDescriptorBits(ctx, target, key, kstr);
            d.writable = (bits & 0x1) != 0;
            d.configurable = (bits & 0x2) != 0;
        }
    }
    return d;
}

static bool isTargetNonExtensible(proto::ProtoContext* ctx,
                                    const proto::ProtoObject* target) {
    if (!target || target == PROTO_NONE) return false;
    JSContextWrapper* w = JSContextWrapper::current();
    if (!w) return false;
    const proto::ProtoObject* nem = w->getNonExtensibleMarker();
    return nem && target->hasParent(ctx, nem);
}

// SameValue per §7.2.10.  For the invariant checks we only need the
// primitive cases plus pointer-identity for objects.
static bool sameValue(proto::ProtoContext* ctx,
                       const proto::ProtoObject* a,
                       const proto::ProtoObject* b) {
    if (a == b) return true;
    if (!a || !b || a == PROTO_NONE || b == PROTO_NONE) return false;
    if (a->isInteger(ctx) && b->isInteger(ctx))
        return a->asLong(ctx) == b->asLong(ctx);
    if ((a->isDouble(ctx) || a->isFloat(ctx) || a->isInteger(ctx)) &&
        (b->isDouble(ctx) || b->isFloat(ctx) || b->isInteger(ctx))) {
        double da = a->isInteger(ctx) ? (double)a->asLong(ctx) : a->asDouble(ctx);
        double db = b->isInteger(ctx) ? (double)b->asLong(ctx) : b->asDouble(ctx);
        // NaN === NaN per SameValue.
        if (da != da && db != db) return true;
        // +0 vs -0 are distinct under SameValue.
        if (da == 0.0 && db == 0.0) return std::signbit(da) == std::signbit(db);
        return da == db;
    }
    const proto::ProtoString* sa = a->asString(ctx);
    const proto::ProtoString* sb = b->asString(ctx);
    if (sa && sb) {
        std::string xa, xb;
        sa->toUTF8String(ctx, xa); sb->toUTF8String(ctx, xb);
        return xa == xb;
    }
    return false;
}

// Default [[Get]] on a non-proxy object: chain-walking getAttribute
// with accessor descriptor support.  Mirrors what L_OP_get_field
// would have done.
static const proto::ProtoObject* defaultGet(proto::ProtoContext* ctx,
                                              const proto::ProtoObject* target,
                                              const proto::ProtoString* key,
                                              const proto::ProtoObject* receiver = nullptr) {
    if (!target || target == PROTO_NONE || !key) return PROTO_NONE;
    // Accessor sidecar probe.  §10.1.8.1 [[Get]] step 8.c: when the
    // own property is an accessor, invoke its getter with `Receiver`
    // (not target) as `this`.  Pre-fix the bind always used target,
    // so `var p = new Proxy(t, {}); p.attr` returned t instead of p
    // when t had `get attr(){ return this }`.
    std::string ks; key->toUTF8String(ctx, ks);
    const std::string gks = "__get_" + ks + "__";
    const proto::ProtoObject* gko = ctx->fromUTF8String(gks.c_str());
    const proto::ProtoString* gk = gko ? gko->asString(ctx) : nullptr;
    if (gk) {
        const proto::ProtoObject* getter = target->getAttribute(ctx, gk, true);
        if (getter && getter != PROTO_NONE) {
            const proto::ProtoList* a = ctx->newList();
            const proto::ProtoObject* recv = (receiver && receiver != PROTO_NONE) ? receiver : target;
            return callJSFunction(ctx, getter, recv, a);
        }
    }
    // Array exotic: §10.4.2 [[Get]] for an integer-indexed key reads
    // from the internal element storage, not from the attribute slot
    // (protoJS keeps dense elements in __array_elements__).  Pre-fix
    // defaultGet's getAttribute-only probe returned PROTO_NONE for
    // every numeric index on an Array target — `new Proxy([1,2,3],
    // {})[0]` surfaced undefined and every Array.prototype.* method
    // routed through the proxy iterated over empty slots.
    {
        std::string keyStr; key->toUTF8String(ctx, keyStr);
        bool numeric = !keyStr.empty();
        for (char c : keyStr) { if (c < '0' || c > '9') { numeric = false; break; } }
        if (numeric && (keyStr.size() == 1 || keyStr[0] != '0')) {
            char* endp = nullptr;
            unsigned long idx = std::strtoul(keyStr.c_str(), &endp, 10);
            if (endp && *endp == '\0') {
                const proto::ProtoObject* v = protojs::arrayTryFastGet(ctx, target, idx);
                if (v && v != PROTO_NONE) return v;
            }
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
        const proto::ProtoObject* res = callJSFunction(ctx, trap, handler, a);
        if (hasCallException()) return PROTO_NONE;
        // §10.5.8 step 9: validate the trap result against the target's
        // own descriptor when it is non-configurable.
        OwnDescriptor od = probeOwnDescriptor(ctx, target, propKey);
        if (od.present && !od.configurable) {
            if (!od.isAccessor) {
                // 9.a: non-configurable, non-writable data → result must
                // SameValue target's value.
                if (!od.writable && !sameValue(ctx, res, od.value)) {
                    signalNativeException(makeNativeError(ctx, "TypeError",
                        "'get' on proxy: property descriptor is "
                        "non-configurable and non-writable but trap "
                        "returned a different value"));
                    return PROTO_NONE;
                }
            } else if (!od.hasGetter) {
                // 9.b: accessor with undefined [[Get]] → trap must return undefined.
                if (res && res != PROTO_NONE) {
                    signalNativeException(makeNativeError(ctx, "TypeError",
                        "'get' on proxy: property descriptor is a "
                        "non-configurable accessor with undefined getter "
                        "but trap returned a value"));
                    return PROTO_NONE;
                }
            }
        }
        return res;
    }
    // §10.5.8 step 7: trap is undefined → forward to target.[[Get]](P, R).
    // Recurse when the target itself is a Proxy.
    if (isProxy(ctx, target)) return proxyDispatchGet(ctx, target, propKey, receiver);
    return defaultGet(ctx, target, propKey, receiver ? receiver : proxy);
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
        if (hasCallException()) return PROTO_NONE;
        // §10.5.9 step 9 ToBoolean(trapResult): null, undefined, +0/-0,
        // NaN and "" are all falsy.  Pre-fix the check accepted any
        // non-PROTO_FALSE / non-nullptr return as truthy, so a `null`
        // /`0`/`""` from the user trap was silently treated as "set
        // succeeded".  test262 set/boolean-trap-result-is-{false,null,
        // 0, NaN, ""}-return-false.
        bool truthy;
        {
            if (r == nullptr || r == PROTO_NONE
                || r == PROTO_FALSE || r == getNullSentinel()
                || r == getUndefinedSentinel()) truthy = false;
            else if (r->isBoolean(ctx)) truthy = r->asBoolean(ctx);
            else if (r->isInteger(ctx)) truthy = r->asLong(ctx) != 0;
            else if (r->isDouble(ctx)) { double d = r->asDouble(ctx); truthy = d != 0.0 && d == d; }
            else if (r->isString(ctx)) {
                std::string s; r->asString(ctx)->toUTF8String(ctx, s);
                truthy = !s.empty();
            } else truthy = true;
        }
        // §10.5.9 step 11: if trap returned a truthy result, validate
        // against the target's own descriptor when non-configurable.
        if (truthy) {
            OwnDescriptor od = probeOwnDescriptor(ctx, target, propKey);
            if (od.present && !od.configurable) {
                if (!od.isAccessor) {
                    if (!od.writable && !sameValue(ctx, value, od.value)) {
                        signalNativeException(makeNativeError(ctx, "TypeError",
                            "'set' on proxy: trap returned truthy for "
                            "non-configurable, non-writable property with "
                            "different value"));
                        return PROTO_FALSE;
                    }
                } else if (!od.hasSetter) {
                    signalNativeException(makeNativeError(ctx, "TypeError",
                        "'set' on proxy: trap returned truthy for "
                        "non-configurable accessor property with no setter"));
                    return PROTO_FALSE;
                }
            }
        }
        return truthy ? PROTO_TRUE : PROTO_FALSE;
    }
    // §10.5.9 step 7: trap is undefined → forward to target.[[Set]](P, V, R).
    // Recurse when the target is a Proxy.
    if (isProxy(ctx, target)) return proxyDispatchSet(ctx, target, propKey, value, receiver);
    // OrdinarySet step 2.b — when the property is inherited as an
    // accessor, the spec requires invoking the setter with [[Receiver]]
    // bound to the original receiver (the proxy here).  Routing every
    // inherited accessor would require a full chain walk; for the
    // load-bearing case the test262 set-abrupt cases pin (the
    // `__proto__` accessor on Object.prototype), short-circuit to
    // [[SetPrototypeOf]] on the receiver so the proxy's setPrototypeOf
    // trap fires per §B.2.2.1 step 4.  Other inherited accessors fall
    // back to the raw setAttribute below as before — the wider chain-
    // walking [[Set]] is tracked separately.
    if (propKey) {
        std::string ks; propKey->toUTF8String(ctx, ks);
        if (ks == "__proto__" && receiver) {
            const proto::ProtoObject* r2 =
                proxyDispatchSetPrototypeOf(ctx, receiver, value);
            if (hasCallException()) return PROTO_NONE;
            if (r2 == PROTO_FALSE) {
                signalNativeException(makeNativeError(ctx, "TypeError",
                    "Proxy setPrototypeOf returned false"));
                return PROTO_NONE;
            }
            if (r2 == PROTO_TRUE) return PROTO_TRUE;
            // r2 == nullptr — no trap; fall through to default raw write.
        }
    }
    // §10.5.9 step 7 forwarded to target.[[Set]](P, V, Receiver).
    // Per §10.1.9 OrdinarySet step 3 / OrdinarySetWithOwnDescriptor:
    //   - if target has a data ownDesc → use Receiver.[[DefineOwnProperty]]
    //     (§10.1.9.2 step 3.d.iv).  When Receiver is the original
    //     proxy, that dispatches the proxy's defineProperty trap.
    //   - if target has an accessor ownDesc → call its setter with
    //     `this = Receiver` (§10.1.9.2 step 4.c).
    //   - if target has NO ownDesc → CreateDataProperty on Receiver
    //     (§10.1.9.2 step 3.e.ii) → again dispatches proxy's dP trap
    //     when Receiver is the proxy.
    // Pre-fix the no-trap fallback raw-setAttribute'd the target,
    // which (a) skipped the proxy's dP trap and (b) wrote into target
    // instead of Receiver when those identities differ.
    if (propKey && receiver) {
        OwnDescriptor od = probeOwnDescriptor(ctx, target, propKey);
        // Build a {value: V, writable:true, enumerable:true,
        // configurable:true} descriptor for CreateDataProperty.
        JSContextWrapper* w = JSContextWrapper::current();
        const proto::ProtoObject* objProto = w ? w->getJSObjectPrototype() : nullptr;
        auto newDataDesc = [&](const proto::ProtoObject* v) -> const proto::ProtoObject* {
            const proto::ProtoObject* d = objProto
                ? objProto->newChild(ctx, true) : ctx->newObject(true);
            if (!d) return nullptr;
            auto set = [&](const char* name, const proto::ProtoObject* slot) {
                const proto::ProtoObject* ko = ctx->fromUTF8String(name);
                const proto::ProtoString* ks = ko ? ko->asString(ctx) : nullptr;
                if (ks) d = d->setAttribute(ctx, ks, slot ? slot : getUndefinedSentinel());
            };
            set("value", v);
            set("writable",     PROTO_TRUE);
            set("enumerable",   PROTO_TRUE);
            set("configurable", PROTO_TRUE);
            return d;
        };
        // (a) accessor on target → invoke the setter with `this=Receiver`.
        if (od.present && od.isAccessor) {
            if (od.hasSetter) {
                std::string ks; propKey->toUTF8String(ctx, ks);
                std::string sks = "__set_" + ks + "__";
                const proto::ProtoObject* sko = ctx->fromUTF8String(sks.c_str());
                const proto::ProtoString* skKey = sko ? sko->asString(ctx) : nullptr;
                const proto::ProtoObject* setter = skKey
                    ? target->getAttribute(ctx, skKey, true) : nullptr;
                if (setter && setter != PROTO_NONE
                    && setter != getUndefinedSentinel()) {
                    const proto::ProtoList* setArgs = ctx->newList();
                    setArgs = setArgs->appendLast(ctx, value ? value : PROTO_NONE);
                    (void)callJSFunction(ctx, setter, receiver, setArgs);
                    if (hasCallException()) return PROTO_NONE;
                    return PROTO_TRUE;
                }
            }
            return PROTO_FALSE;  // accessor with no setter
        }
        // (b) data descriptor on target (writable case) → CreateDataProperty
        //     on Receiver, which dispatches Receiver.[[DefineOwnProperty]].
        //     A non-writable data descriptor rejects.
        if (od.present && !od.isAccessor && !od.writable) {
            return PROTO_FALSE;
        }
        // Build the value descriptor and CreateDataProperty on Receiver.
        const proto::ProtoObject* desc = newDataDesc(value);
        if (!desc) {
            const_cast<proto::ProtoObject*>(target)->setAttribute(ctx, propKey,
                value ? value : PROTO_NONE);
            return PROTO_TRUE;
        }
        // When receiver IS a proxy with a defineProperty trap, route
        // via proxyDispatchDefineProperty so the dP trap fires.  When
        // there is no dP trap, the spec forwards to the unwrapped
        // target's [[DefineOwnProperty]] — for a plain Object that's a
        // raw setAttribute on the deepest non-proxy target.
        if (isProxy(ctx, receiver)) {
            const proto::ProtoObject* r =
                proxyDispatchDefineProperty(ctx, receiver, propKey, desc);
            if (hasCallException()) return PROTO_NONE;
            if (r == PROTO_FALSE) return PROTO_FALSE;
            if (r == PROTO_TRUE)  return PROTO_TRUE;
            // r == nullptr (no dP trap) — recurse via proxyTarget so a
            // proxy-of-proxy receiver dispatches its inner dP trap if
            // present; otherwise land on the underlying target and
            // setAttribute there.
            const proto::ProtoObject* deepest = receiver;
            while (isProxy(ctx, deepest)) {
                const proto::ProtoObject* t = proxyTarget(ctx, deepest);
                if (!t) return PROTO_FALSE;
                deepest = t;
            }
            const_cast<proto::ProtoObject*>(deepest)->setAttribute(ctx, propKey,
                value ? value : PROTO_NONE);
            return PROTO_TRUE;
        }
        // Non-Proxy Receiver — simple raw write on Receiver.
        const_cast<proto::ProtoObject*>(receiver)->setAttribute(ctx, propKey,
            value ? value : PROTO_NONE);
        return PROTO_TRUE;
    }
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
        if (hasCallException()) return PROTO_NONE;
        bool truthy;
        {
            if (r == nullptr || r == PROTO_NONE
                || r == PROTO_FALSE || r == getNullSentinel()
                || r == getUndefinedSentinel()) truthy = false;
            else if (r->isBoolean(ctx)) truthy = r->asBoolean(ctx);
            else if (r->isInteger(ctx)) truthy = r->asLong(ctx) != 0;
            else if (r->isDouble(ctx)) { double d = r->asDouble(ctx); truthy = d != 0.0 && d == d; }
            else if (r->isString(ctx)) {
                std::string s; r->asString(ctx)->toUTF8String(ctx, s);
                truthy = !s.empty();
            } else truthy = true;
        }
        // §10.5.7 step 9: if trap returned falsy, validate against the
        // target's own descriptor.  Non-configurable owns and owns on
        // non-extensible targets cannot be hidden by the trap.
        if (!truthy) {
            OwnDescriptor od = probeOwnDescriptor(ctx, target, propKey);
            if (od.present && !od.configurable) {
                signalNativeException(makeNativeError(ctx, "TypeError",
                    "'has' on proxy: trap returned falsy for "
                    "non-configurable own property"));
                return PROTO_FALSE;
            }
            if (od.present && isTargetNonExtensible(ctx, target)) {
                signalNativeException(makeNativeError(ctx, "TypeError",
                    "'has' on proxy: trap returned falsy for "
                    "an own property of a non-extensible target"));
                return PROTO_FALSE;
            }
        }
        return truthy ? PROTO_TRUE : PROTO_FALSE;
    }
    // §10.5.7 step 7: trap is undefined → forward to target.[[HasProperty]](P).
    // When the target is itself a Proxy, that means recursing into its
    // dispatcher. Pre-fix the default branch only consulted
    // target->hasAttribute, which on a proxy-cell sees only the
    // __proxy_target__ / __proxy_handler__ sidecars and returns false
    // for every real property.
    if (isProxy(ctx, target)) return proxyDispatchHas(ctx, target, propKey);
    if (propKey && target->hasAttribute(ctx, propKey) == PROTO_TRUE)
        return PROTO_TRUE;
    // Array exotic dense-element fallback — §10.4.2 [[HasProperty]]
    // returns true for any integer index in the __array_elements__
    // storage.  Pre-fix hasAttribute alone returned false for every
    // dense index, so a Proxy of an Array reported `Reflect.has(px,
    // '0')` as false and the Array.prototype.* iteration helpers
    // skipped every slot.
    if (propKey) {
        std::string keyStr; propKey->toUTF8String(ctx, keyStr);
        bool numeric = !keyStr.empty();
        for (char c : keyStr) { if (c < '0' || c > '9') { numeric = false; break; } }
        if (numeric && (keyStr.size() == 1 || keyStr[0] != '0')) {
            char* endp = nullptr;
            unsigned long idx = std::strtoul(keyStr.c_str(), &endp, 10);
            if (endp && *endp == '\0') {
                const proto::ProtoObject* v = protojs::arrayTryFastGet(ctx, target, idx);
                if (v && v != PROTO_NONE) return PROTO_TRUE;
            }
        }
    }
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
        if (hasCallException()) return PROTO_NONE;
        bool truthy;
        {
            if (r == nullptr || r == PROTO_NONE
                || r == PROTO_FALSE || r == getNullSentinel()
                || r == getUndefinedSentinel()) truthy = false;
            else if (r->isBoolean(ctx)) truthy = r->asBoolean(ctx);
            else if (r->isInteger(ctx)) truthy = r->asLong(ctx) != 0;
            else if (r->isDouble(ctx)) { double d = r->asDouble(ctx); truthy = d != 0.0 && d == d; }
            else if (r->isString(ctx)) {
                std::string s; r->asString(ctx)->toUTF8String(ctx, s);
                truthy = !s.empty();
            } else truthy = true;
        }
        // §10.5.10 step 9: if trap returned truthy, validate against the
        // target's own descriptor.  Non-configurable owns cannot be
        // deleted, and owns on non-extensible targets cannot vanish.
        if (truthy) {
            OwnDescriptor od = probeOwnDescriptor(ctx, target, propKey);
            if (od.present && !od.configurable) {
                signalNativeException(makeNativeError(ctx, "TypeError",
                    "'deleteProperty' on proxy: trap returned truthy for "
                    "non-configurable own property"));
                return PROTO_FALSE;
            }
            if (od.present && isTargetNonExtensible(ctx, target)) {
                signalNativeException(makeNativeError(ctx, "TypeError",
                    "'deleteProperty' on proxy: trap returned truthy for "
                    "an own property of a non-extensible target"));
                return PROTO_FALSE;
            }
        }
        return truthy ? PROTO_TRUE : PROTO_FALSE;
    }
    // §10.5.10 step 7: trap is undefined → forward to target.[[Delete]](P).
    if (isProxy(ctx, target)) return proxyDispatchDelete(ctx, target, propKey);
    // §10.1.10 OrdinaryDelete on a plain Object target: a
    // non-configurable own descriptor cannot be deleted — return
    // false.  Frozen / sealed receivers similarly reject.  Pre-fix
    // the fallback raw-setAttribute'd PROTO_NONE without consulting
    // the descriptor bits, so a Proxy whose target had a non-
    // configurable own incorrectly reported delete=true (test262
    // Proxy/deleteProperty/trap-is-undefined-strict.js, trap-is-
    // null-target-is-proxy.js).
    if (propKey) {
        OwnDescriptor od = probeOwnDescriptor(ctx, target, propKey);
        if (od.present && !od.configurable) return PROTO_FALSE;
        // Pass nullptr (not PROTO_NONE) so protoCore's setAttribute
        // routes to implRemoveAt — actually removing the entry from
        // the own-attribute sparse list.  Pre-fix PROTO_NONE was
        // silently treated as "present but valueless", so
        // hasOwnProperty still returned true and the inherited
        // hasAttribute walk continued reporting the property.
        const_cast<proto::ProtoObject*>(target)->setAttribute(ctx, propKey, nullptr);
    }
    return PROTO_TRUE;
}

const proto::ProtoObject* proxyLookupTrap(proto::ProtoContext* ctx,
                                           const proto::ProtoObject* proxy,
                                           const char* trapName) {
    const proto::ProtoObject* handler = proxyHandler(ctx, proxy);
    return lookupTrap(ctx, handler, trapName);
}

// §10.5.11 [[OwnPropertyKeys]]: handler.ownKeys(target).
// CreateListFromArrayLike per §7.3.18 — read length, then 0..length-1
// entries.  Each entry must be a String or a Symbol object.  Returns
// a fresh JS Array carrying the keys; nullptr means "no trap, forward
// to default".
const proto::ProtoObject* proxyDispatchOwnKeys(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* proxy) {
    if (!ctx || !proxy) return nullptr;
    const proto::ProtoObject* target = proxyTarget(ctx, proxy);
    if (!target) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Cannot perform 'ownKeys' on a proxy that has been revoked"));
        return PROTO_NONE;
    }
    const proto::ProtoObject* handler = proxyHandler(ctx, proxy);
    const proto::ProtoObject* trap = lookupTrap(ctx, handler, "ownKeys");
    if (!trap) {
        if (isProxy(ctx, target)) return proxyDispatchOwnKeys(ctx, target);
        return nullptr;  // forward to default
    }
    const proto::ProtoList* a = ctx->newList();
    a = a->appendLast(ctx, target);
    const proto::ProtoObject* trapResult = callJSFunction(ctx, trap, handler, a);
    if (hasCallException()) return PROTO_NONE;
    // §7.3.18 CreateListFromArrayLike step 2: trapResult must be
    // Object — primitives (null / undefined / number / string /
    // boolean / Symbol) raise TypeError before length is read.
    bool resultIsSymbol = false;
    {
        const proto::ProtoString* symK = JSSymbols::isSymbol(ctx);
        if (symK && trapResult && trapResult != PROTO_NONE
            && trapResult->getAttribute(ctx, symK, true) == PROTO_TRUE)
            resultIsSymbol = true;
    }
    if (!trapResult || trapResult == PROTO_NONE
        || trapResult == getUndefinedSentinel()
        || trapResult == getNullSentinel()
        || trapResult == PROTO_TRUE || trapResult == PROTO_FALSE
        || trapResult->isInteger(ctx) || trapResult->isDouble(ctx)
        || trapResult->isFloat(ctx)   || trapResult->isString(ctx)
        || resultIsSymbol) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "'ownKeys' on proxy: trap returned a non-Object"));
        return PROTO_NONE;
    }
    // §7.3.18 step 3: read length via Get(obj, "length").
    const proto::ProtoString* lenK = JSSymbols::length(ctx);
    const proto::ProtoObject* lenV = lenK ? trapResult->getAttribute(ctx, lenK, true) : nullptr;
    long long len = 0;
    if (lenV && lenV->isInteger(ctx)) len = lenV->asLong(ctx);
    else if (lenV && lenV->isDouble(ctx)) len = static_cast<long long>(lenV->asDouble(ctx));
    if (len < 0) len = 0;
    const proto::ProtoList* fastEls = getArrayElements(ctx, trapResult);
    const proto::ProtoList* outEls = ctx->newList();
    // §10.5.11 step 9: duplicate-detect each emitted key by string
    // identity; a duplicate raises TypeError.  Hash by the UTF-8
    // bytes — symbol-keyed entries hash by their underlying
    // ProtoString identity, which the symbol stores as its UTF-8.
    std::unordered_set<std::string> seenKeys;
    for (long long i = 0; i < len; i++) {
        const proto::ProtoObject* kv = nullptr;
        if (fastEls && i < static_cast<long long>(fastEls->getSize(ctx))) {
            kv = fastEls->getAt(ctx, static_cast<size_t>(i));
        } else {
            std::string idx = std::to_string(i);
            const proto::ProtoObject* idxKey = ctx->fromUTF8String(idx.c_str());
            const proto::ProtoString* ik = idxKey ? idxKey->asString(ctx) : nullptr;
            if (ik) kv = trapResult->getAttribute(ctx, ik, true);
        }
        if (!kv || kv == PROTO_NONE) kv = getUndefinedSentinel();
        // Each entry must be a String or a Symbol object — per
        // §7.3.18 step 5.f the check is a runtime TypeError.
        bool isSymEntry = false;
        const proto::ProtoString* symK = JSSymbols::isSymbol(ctx);
        if (symK && kv != PROTO_NONE
            && kv->getAttribute(ctx, symK, false) == PROTO_TRUE)
            isSymEntry = true;
        bool ok = isSymEntry || kv->isString(ctx);
        if (!ok) {
            signalNativeException(makeNativeError(ctx, "TypeError",
                "'ownKeys' on proxy: trap returned an entry that is neither String nor Symbol"));
            return PROTO_NONE;
        }
        // §10.5.11 step 9: duplicate-entries → TypeError.  Symbol
        // entries dedupe by their underlying ProtoString identity
        // (protoJS stores symbol primitives as ProtoString-backed
        // strong-tagged objects).
        std::string keyStr;
        if (kv->isString(ctx)) {
            kv->asString(ctx)->toUTF8String(ctx, keyStr);
            keyStr = "S:" + keyStr;  // namespace string keys
        } else {
            // Symbol — use the object identity as a printable key
            // (Symbols stringify to "Symbol.<wkn>" / "Symbol(<desc>)").
            const proto::ProtoString* ks = kv->asString(ctx);
            if (ks) {
                ks->toUTF8String(ctx, keyStr);
                keyStr = "Y:" + keyStr;  // namespace symbol keys
            } else {
                keyStr = "Y:@" + std::to_string(reinterpret_cast<uintptr_t>(kv));
            }
        }
        if (!seenKeys.insert(keyStr).second) {
            signalNativeException(makeNativeError(ctx, "TypeError",
                "'ownKeys' on proxy: trap result contains duplicate entries"));
            return PROTO_NONE;
        }
        outEls = outEls->appendLast(ctx, kv);
    }
    const proto::ProtoObject* arr = createNewArray(ctx, nullptr);
    setArrayElements(ctx, arr, outEls);
    if (lenK) arr = arr->setAttribute(ctx, lenK, ctx->fromInteger(len));
    return arr;
}

// §10.5.1 [[GetPrototypeOf]]: handler.getPrototypeOf(target).  Trap
// result must be Object or Null; on non-extensible target the result
// must SameValue target.[[GetPrototypeOf]]() (§10.5.1 step 11).
const proto::ProtoObject* proxyDispatchGetPrototypeOf(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* proxy) {
    if (!ctx || !proxy) return PROTO_NONE;
    const proto::ProtoObject* target = proxyTarget(ctx, proxy);
    if (!target) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Cannot perform 'getPrototypeOf' on a proxy that has been revoked"));
        return PROTO_NONE;
    }
    const proto::ProtoObject* handler = proxyHandler(ctx, proxy);
    const proto::ProtoObject* trap = lookupTrap(ctx, handler, "getPrototypeOf");
    if (trap) {
        const proto::ProtoList* a = ctx->newList();
        a = a->appendLast(ctx, target);
        const proto::ProtoObject* r = callJSFunction(ctx, trap, handler, a);
        if (hasCallException()) return PROTO_NONE;
        // §10.5.1 step 7: trap result must be Object or Null —
        // Symbol primitives (Object-tagged with `__is_symbol__`)
        // also reject here.  Pre-fix the rejector missed Symbol
        // and the `r->isString` check alone let through anything
        // whose asString returned null (the Symbol case).
        if (r != getNullSentinel()) {
            bool resIsSymbol = false;
            {
                const proto::ProtoString* isSymK = JSSymbols::isSymbol(ctx);
                if (isSymK && r && r != PROTO_NONE
                    && r->getAttribute(ctx, isSymK, true) == PROTO_TRUE)
                    resIsSymbol = true;
            }
            if (!r || r == PROTO_NONE || r == getUndefinedSentinel()
                || r->isInteger(ctx) || r->isDouble(ctx) || r->isFloat(ctx)
                || r == PROTO_TRUE || r == PROTO_FALSE || r->isString(ctx)
                || resIsSymbol) {
                signalNativeException(makeNativeError(ctx, "TypeError",
                    "'getPrototypeOf' on proxy: trap returned neither Object nor Null"));
                return PROTO_NONE;
            }
        }
        return r;
    }
    // No trap — forward.  When target is itself a Proxy, recurse;
    // otherwise consult getJSProtoOverride (Object.create(null) /
    // Object.setPrototypeOf rebinds) before falling back to the C++
    // parent — pre-fix getPrototype on an Object.create(null)
    // receiver returned the protoCore default parent (Object.prototype),
    // which contradicts the JS-side null override and broke every
    // gPO-chained `null prototype` test (Proxy/getPrototypeOf/trap-
    // is-null-target-is-proxy.js).
    if (isProxy(ctx, target)) return proxyDispatchGetPrototypeOf(ctx, target);
    const proto::ProtoObject* over = getJSProtoOverride(target);
    if (over) return over;
    const proto::ProtoObject* p = target->getPrototype(ctx);
    return (p && p != PROTO_NONE) ? p : getNullSentinel();
}

// §10.5.2 [[SetPrototypeOf]]: handler.setPrototypeOf(target, V).
const proto::ProtoObject* proxyDispatchSetPrototypeOf(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* proxy,
    const proto::ProtoObject* newProto) {
    if (!ctx || !proxy) return PROTO_NONE;
    const proto::ProtoObject* target = proxyTarget(ctx, proxy);
    if (!target) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Cannot perform 'setPrototypeOf' on a proxy that has been revoked"));
        return PROTO_NONE;
    }
    const proto::ProtoObject* handler = proxyHandler(ctx, proxy);
    const proto::ProtoObject* trap = lookupTrap(ctx, handler, "setPrototypeOf");
    if (!trap) {
        if (isProxy(ctx, target))
            return proxyDispatchSetPrototypeOf(ctx, target, newProto);
        return nullptr;  // forward to default
    }
    const proto::ProtoList* a = ctx->newList();
    a = a->appendLast(ctx, target);
    a = a->appendLast(ctx, newProto ? newProto : getNullSentinel());
    const proto::ProtoObject* r = callJSFunction(ctx, trap, handler, a);
    if (hasCallException()) return PROTO_NONE;
    bool truthy;
    {
        if (r == nullptr || r == PROTO_NONE
            || r == PROTO_FALSE || r == getNullSentinel()
            || r == getUndefinedSentinel()) truthy = false;
        else if (r->isBoolean(ctx)) truthy = r->asBoolean(ctx);
        else if (r->isInteger(ctx)) truthy = r->asLong(ctx) != 0;
        else if (r->isDouble(ctx)) { double d = r->asDouble(ctx); truthy = d != 0.0 && d == d; }
        else if (r->isString(ctx)) {
            std::string s; r->asString(ctx)->toUTF8String(ctx, s);
            truthy = !s.empty();
        } else truthy = true;
    }
    if (!truthy) return PROTO_FALSE;
    // §10.5.2 step 10-13: when trap returns truthy, check
    // [[IsExtensible]](target).  If extensible, return true.  If
    // non-extensible, target.[[GetPrototypeOf]]() must SameValue V.
    JSContextWrapper* w = JSContextWrapper::current();
    // §10.5.2 step 10: IsExtensible(target) — for a Proxy target,
    // dispatches the isExtensible trap; an abrupt must propagate.
    // For nested proxies we walk to the deepest non-Proxy underlying
    // object and check ITS NonExtensibleMarker so the test262
    // setPrototypeOf/return-abrupt-from-target-getprototypeof.js path
    // (where Object.preventExtensions(target) set the marker on the
    // inner ProxyTarget) correctly sees non-extensible.
    bool targetNonExt = false;
    {
        const proto::ProtoObject* probe = target;
        while (isProxy(ctx, probe)) {
            const proto::ProtoObject* iext =
                lookupTrap(ctx, proxyHandler(ctx, probe), "isExtensible");
            if (iext) {
                const proto::ProtoList* iea = ctx->newList();
                iea = iea->appendLast(ctx, proxyTarget(ctx, probe));
                (void)callJSFunction(ctx, iext, proxyHandler(ctx, probe), iea);
                if (hasCallException()) return PROTO_NONE;
            }
            const proto::ProtoObject* inner = proxyTarget(ctx, probe);
            if (!inner) return PROTO_FALSE;
            probe = inner;
        }
        if (probe && w && w->getNonExtensibleMarker()
            && probe->hasParent(ctx, w->getNonExtensibleMarker()))
            targetNonExt = true;
    }
    if (!targetNonExt) return PROTO_TRUE;
    // §10.5.2 step 12: target.[[GetPrototypeOf]]() — may abrupt.
    const proto::ProtoObject* targetProto = nullptr;
    if (isProxy(ctx, target)) {
        targetProto = proxyDispatchGetPrototypeOf(ctx, target);
        if (hasCallException()) return PROTO_NONE;
    } else {
        const proto::ProtoObject* over = getJSProtoOverride(target);
        if (over) targetProto = over;
        else {
            const proto::ProtoObject* tp = target->getPrototype(ctx);
            targetProto = (tp && tp != PROTO_NONE) ? tp : getNullSentinel();
        }
    }
    const proto::ProtoObject* requested = newProto
        ? newProto : getNullSentinel();
    if (!sameValue(ctx, requested, targetProto ? targetProto : getNullSentinel())) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "'setPrototypeOf' on proxy: trap returned truthy for a "
            "non-extensible target with a different proposed prototype"));
        return PROTO_FALSE;
    }
    return PROTO_TRUE;
}

// §10.5.6 [[DefineOwnProperty]]: handler.defineProperty(target, P, Desc).
// When the trap is absent, returns nullptr so the caller can perform
// the standard define-on-target work (we don't recurse into Object.
// defineProperty from ProxyBuiltin to avoid a circular include — the
// hook in objectDefineProperty handles the fall-through).  When the
// trap is present we call it, ToBoolean the result, and run the
// non-configurable invariants from §10.5.6 steps 17-19.
const proto::ProtoObject* proxyDispatchDefineProperty(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* proxy,
    const proto::ProtoString* propKey,
    const proto::ProtoObject* descriptor) {
    if (!ctx || !proxy || !propKey) return nullptr;
    const proto::ProtoObject* target = proxyTarget(ctx, proxy);
    if (!target) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Cannot perform 'defineProperty' on a proxy that has been revoked"));
        return PROTO_NONE;
    }
    const proto::ProtoObject* handler = proxyHandler(ctx, proxy);
    const proto::ProtoObject* trap = lookupTrap(ctx, handler, "defineProperty");
    if (!trap) {
        if (isProxy(ctx, target))
            return proxyDispatchDefineProperty(ctx, target, propKey, descriptor);
        return nullptr;  // forward to default
    }
    const proto::ProtoList* a = ctx->newList();
    a = a->appendLast(ctx, target);
    a = a->appendLast(ctx, propKey->asObject(ctx));
    a = a->appendLast(ctx, descriptor ? descriptor : PROTO_NONE);
    const proto::ProtoObject* r = callJSFunction(ctx, trap, handler, a);
    if (hasCallException()) return PROTO_NONE;
    bool truthy;
    {
        if (r == nullptr || r == PROTO_NONE
            || r == PROTO_FALSE || r == getNullSentinel()
            || r == getUndefinedSentinel()) truthy = false;
        else if (r->isBoolean(ctx)) truthy = r->asBoolean(ctx);
        else if (r->isInteger(ctx)) truthy = r->asLong(ctx) != 0;
        else if (r->isDouble(ctx)) { double d = r->asDouble(ctx); truthy = d != 0.0 && d == d; }
        else if (r->isString(ctx)) {
            std::string s; r->asString(ctx)->toUTF8String(ctx, s);
            truthy = !s.empty();
        } else truthy = true;
    }
    if (truthy) {
        OwnDescriptor od = probeOwnDescriptor(ctx, target, propKey);
        // §10.5.6 step 16: trap accepted but the target is non-extensible
        // and there is no existing own descriptor for P — TypeError.
        if (!od.present && isTargetNonExtensible(ctx, target)) {
            signalNativeException(makeNativeError(ctx, "TypeError",
                "'defineProperty' on proxy: trap returned truthy adding "
                "a property to a non-extensible target"));
            return PROTO_FALSE;
        }
        // Inspect the Desc the trap saw to enforce the remaining
        // §10.5.6 step 17–20 invariants.  Desc is the JS-side
        // descriptor object the caller passed.
        bool descHasConfigurable = false;
        bool descConfigurable    = true;
        bool descHasWritable     = false;
        bool descWritable        = true;
        if (descriptor && descriptor != PROTO_NONE) {
            const proto::ProtoObject* cko = ctx->fromUTF8String("configurable");
            const proto::ProtoString* cks = cko ? cko->asString(ctx) : nullptr;
            if (cks && descriptor->hasAttribute(ctx, cks) == PROTO_TRUE) {
                descHasConfigurable = true;
                const proto::ProtoObject* cv = descriptor->getAttribute(ctx, cks, false);
                descConfigurable = (cv == PROTO_TRUE);
            }
            const proto::ProtoObject* wko = ctx->fromUTF8String("writable");
            const proto::ProtoString* wks = wko ? wko->asString(ctx) : nullptr;
            if (wks && descriptor->hasAttribute(ctx, wks) == PROTO_TRUE) {
                descHasWritable = true;
                const proto::ProtoObject* wv = descriptor->getAttribute(ctx, wks, false);
                descWritable = (wv == PROTO_TRUE);
            }
        }
        // §10.5.6 step 17: targetDesc is undefined && (Desc has any
        // configurable:false) → TypeError.
        if (!od.present && descHasConfigurable && !descConfigurable) {
            signalNativeException(makeNativeError(ctx, "TypeError",
                "'defineProperty' on proxy: trap returned truthy adding "
                "a non-configurable property to a target without one"));
            return PROTO_FALSE;
        }
        // §10.5.6 step 20.a: targetDesc present && Desc not compatible
        // → TypeError.  Conservative check: when targetDesc is non-
        // configurable, any Desc that wants to flip configurable to
        // false-vs-true OR change writable on a data slot raises.
        if (od.present && !od.configurable
            && descHasConfigurable && descConfigurable) {
            signalNativeException(makeNativeError(ctx, "TypeError",
                "'defineProperty' on proxy: trap returned truthy making "
                "a non-configurable property configurable"));
            return PROTO_FALSE;
        }
        if (od.present && !od.configurable && !od.isAccessor
            && !od.writable && descHasWritable && descWritable) {
            signalNativeException(makeNativeError(ctx, "TypeError",
                "'defineProperty' on proxy: trap returned truthy making "
                "a non-configurable non-writable property writable"));
            return PROTO_FALSE;
        }
        // §10.5.6 step 20.b: settingConfigFalse on a configurable
        // targetDesc → TypeError.
        if (od.present && od.configurable
            && descHasConfigurable && !descConfigurable) {
            signalNativeException(makeNativeError(ctx, "TypeError",
                "'defineProperty' on proxy: trap returned truthy making "
                "a configurable property non-configurable when target has it as configurable"));
            return PROTO_FALSE;
        }
    }
    return truthy ? PROTO_TRUE : PROTO_FALSE;
}

// §10.5.5 [[GetOwnProperty]]: handler.getOwnPropertyDescriptor(target, P)
// — if absent forward to target.[[GetOwnProperty]](P).  The trap result
// must be either an Object (descriptor) or undefined; anything else is
// a TypeError.  We then normalise via ToPropertyDescriptor +
// FromPropertyDescriptor by constructing a fresh ordinary object with
// the spec-mandated slot set ({value, writable, enumerable,
// configurable} for data or {get, set, enumerable, configurable} for
// accessor) per §6.2.5.5.
const proto::ProtoObject* proxyDispatchGetOwnPropertyDescriptor(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* proxy,
    const proto::ProtoString* propKey) {
    if (!ctx || !proxy || !propKey) return PROTO_NONE;
    const proto::ProtoObject* target = proxyTarget(ctx, proxy);
    if (!target) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Cannot perform 'getOwnPropertyDescriptor' on a proxy that has been revoked"));
        return PROTO_NONE;
    }
    const proto::ProtoObject* handler = proxyHandler(ctx, proxy);
    const proto::ProtoObject* trap = lookupTrap(ctx, handler, "getOwnPropertyDescriptor");
    if (trap) {
        const proto::ProtoList* a = ctx->newList();
        a = a->appendLast(ctx, target);
        a = a->appendLast(ctx, propKey->asObject(ctx));
        const proto::ProtoObject* res = callJSFunction(ctx, trap, handler, a);
        if (hasCallException()) return PROTO_NONE;
        // §10.5.5 step 7: trapResultObj must be Object or undefined.
        if (res == nullptr || res == PROTO_NONE
            || res == getUndefinedSentinel() || res == getNullSentinel()) {
            // §10.5.5 step 11.a/b: when the trap reports absence the
            // own descriptor must either be missing on target or the
            // target must allow hiding it.  A non-configurable own
            // can't be hidden (step 11.b.i), and an own on a
            // non-extensible target can't be hidden either (step 11.b.iv).
            OwnDescriptor od = probeOwnDescriptor(ctx, target, propKey);
            if (od.present && !od.configurable) {
                signalNativeException(makeNativeError(ctx, "TypeError",
                    "'getOwnPropertyDescriptor' on proxy: trap reported "
                    "undefined for non-configurable own property"));
                return PROTO_NONE;
            }
            if (od.present && isTargetNonExtensible(ctx, target)) {
                signalNativeException(makeNativeError(ctx, "TypeError",
                    "'getOwnPropertyDescriptor' on proxy: trap reported "
                    "undefined for an own property of a non-extensible target"));
                return PROTO_NONE;
            }
            return PROTO_NONE;
        }
        // §10.5.5 step 7 — Must be an Object or Undefined.  Primitives
        // (number, string, boolean, symbol) all reject.  Pre-fix the
        // Symbol case slipped past the rejector because Symbol
        // primitives are Object-tagged with `__is_symbol__` and
        // asString returns nullptr for them.
        bool resIsSymbol = false;
        {
            const proto::ProtoString* isSymK = JSSymbols::isSymbol(ctx);
            if (isSymK && res && res != PROTO_NONE
                && res->getAttribute(ctx, isSymK, true) == PROTO_TRUE)
                resIsSymbol = true;
        }
        if (res->isInteger(ctx) || res->isDouble(ctx) || res->isFloat(ctx)
            || res->isBoolean(ctx) || res->isString(ctx)
            || resIsSymbol) {
            signalNativeException(makeNativeError(ctx, "TypeError",
                "'getOwnPropertyDescriptor' on proxy: trap returned non-Object"));
            return PROTO_NONE;
        }
        // §10.5.5 step 17 — when trap returned a descriptor object
        // but the target has no own descriptor for P AND target is
        // non-extensible, raise TypeError (test262 gOPD/resultdesc-
        // is-not-configurable-targetdesc-is-undefined.js, resultdesc-
        // is-invalid-descriptor.js).
        {
            OwnDescriptor odPre = probeOwnDescriptor(ctx, target, propKey);
            if (!odPre.present && isTargetNonExtensible(ctx, target)) {
                signalNativeException(makeNativeError(ctx, "TypeError",
                    "'getOwnPropertyDescriptor' on proxy: trap returned a "
                    "descriptor for an absent property on a non-extensible target"));
                return PROTO_NONE;
            }
            // §10.5.5 step 21 (subset of IsCompatiblePropertyDescriptor):
            // when targetDesc is present and non-configurable, the trap
            // result must (a) also be non-configurable, and (b) for a
            // non-writable data targetDesc, also be non-writable.
            // Helper: read a boolean slot from the trap result.
            auto readBool = [&](const char* name) -> int {
                // -1 = absent / undefined, 0 = false, 1 = true.
                const proto::ProtoObject* ko = ctx->fromUTF8String(name);
                const proto::ProtoString* ks = ko ? ko->asString(ctx) : nullptr;
                if (!ks) return -1;
                const proto::ProtoObject* v = res->getAttribute(ctx, ks, true);
                if (!v || v == PROTO_NONE
                    || v == getUndefinedSentinel() || v == getNullSentinel()) return -1;
                if (v == PROTO_TRUE) return 1;
                if (v == PROTO_FALSE) return 0;
                if (v->isBoolean(ctx)) return v->asBoolean(ctx) ? 1 : 0;
                if (v->isInteger(ctx)) return v->asLong(ctx) != 0 ? 1 : 0;
                return 1;  // any other value → truthy
            };
            int resConfigBit = readBool("configurable");
            bool resConfig = resConfigBit != 0;
            if (odPre.present && !odPre.configurable) {
                if (resConfig) {
                    signalNativeException(makeNativeError(ctx, "TypeError",
                        "'getOwnPropertyDescriptor' on proxy: trap reported "
                        "configurable for a non-configurable own property"));
                    return PROTO_NONE;
                }
                if (!odPre.isAccessor && !odPre.writable) {
                    int resWritableBit = readBool("writable");
                    if (resWritableBit != 0) {
                        signalNativeException(makeNativeError(ctx, "TypeError",
                            "'getOwnPropertyDescriptor' on proxy: trap reported "
                            "writable for a non-configurable non-writable own property"));
                        return PROTO_NONE;
                    }
                }
            }
            // §10.5.5 step 21 reverse — when target's descriptor IS
            // configurable but the trap result says non-configurable,
            // the trap is attempting to tighten configurability
            // illegally.  Pre-fix only the target-non-configurable
            // direction was checked (test262 resultdesc-is-not-
            // configurable-targetdesc-is-configurable.js).
            if (odPre.present && odPre.configurable && resConfigBit == 0) {
                signalNativeException(makeNativeError(ctx, "TypeError",
                    "'getOwnPropertyDescriptor' on proxy: trap reported "
                    "non-configurable for a configurable own property"));
                return PROTO_NONE;
            }
            // §10.5.5 step 21 — data target with writable=true should
            // not be tightened by the trap to writable=false + non-
            // configurable (resultdesc-is-not-configurable-not-
            // writable-targetdesc-is-writable.js).
            if (odPre.present && !odPre.isAccessor && odPre.writable) {
                int resWritableBit = readBool("writable");
                if (resWritableBit == 0 && resConfigBit == 0) {
                    signalNativeException(makeNativeError(ctx, "TypeError",
                        "'getOwnPropertyDescriptor' on proxy: trap reported "
                        "non-configurable non-writable for a writable own property"));
                    return PROTO_NONE;
                }
            }
        }
        // FromPropertyDescriptor — rebuild the canonical 4-slot
        // descriptor object so the caller can read each slot in the
        // order the spec mandates.  We honour both data and accessor
        // shapes; if the trap returned a mixed object (rare), prefer
        // accessor.
        JSContextWrapper* w = JSContextWrapper::current();
        const proto::ProtoObject* op = w ? w->getJSObjectPrototype() : nullptr;
        const proto::ProtoObject* desc = op ? op->newChild(ctx, true) : ctx->newObject(true);
        auto put = [&](const char* name, const proto::ProtoObject* v){
            const proto::ProtoObject* nk = ctx->fromUTF8String(name);
            const proto::ProtoString* ks = nk ? nk->asString(ctx) : nullptr;
            if (ks) desc = desc->setAttribute(ctx, ks, v ? v : getUndefinedSentinel());
        };
        auto read = [&](const char* name) -> const proto::ProtoObject* {
            const proto::ProtoObject* nk = ctx->fromUTF8String(name);
            const proto::ProtoString* ks = nk ? nk->asString(ctx) : nullptr;
            if (!ks) return PROTO_NONE;
            return res->getAttribute(ctx, ks, true);
        };
        const proto::ProtoObject* getter = read("get");
        const proto::ProtoObject* setter = read("set");
        bool hasGetter = getter && getter != PROTO_NONE && getter != getUndefinedSentinel();
        bool hasSetter = setter && setter != PROTO_NONE && setter != getUndefinedSentinel();
        const proto::ProtoObject* enumerable   = read("enumerable");
        const proto::ProtoObject* configurable = read("configurable");
        auto toBool = [&](const proto::ProtoObject* v) -> const proto::ProtoObject* {
            if (!v || v == PROTO_NONE || v == getUndefinedSentinel()) return PROTO_FALSE;
            if (v == PROTO_TRUE || v == PROTO_FALSE) return v;
            if (v->isBoolean(ctx)) return v->asBoolean(ctx) ? PROTO_TRUE : PROTO_FALSE;
            return PROTO_TRUE;
        };
        if (hasGetter || hasSetter) {
            put("get",          hasGetter ? getter : getUndefinedSentinel());
            put("set",          hasSetter ? setter : getUndefinedSentinel());
            put("enumerable",   toBool(enumerable));
            put("configurable", toBool(configurable));
        } else {
            const proto::ProtoObject* value    = read("value");
            const proto::ProtoObject* writable = read("writable");
            put("value",        value ? value : getUndefinedSentinel());
            put("writable",     toBool(writable));
            put("enumerable",   toBool(enumerable));
            put("configurable", toBool(configurable));
        }
        return desc;
    }
    // No trap — forward to target.  If target is itself a Proxy,
    // recurse; otherwise build a descriptor by reading the target's
    // own attribute layer.  We defer to objectGetOwnPropertyDescriptor's
    // caller path by signalling "no proxy override" via PROTO_NONE +
    // a sentinel handled by the caller.  Simpler: probe own descriptor
    // here and synthesise.
    if (isProxy(ctx, target))
        return proxyDispatchGetOwnPropertyDescriptor(ctx, target, propKey);
    OwnDescriptor od = probeOwnDescriptor(ctx, target, propKey);
    if (!od.present) return PROTO_NONE;
    JSContextWrapper* w = JSContextWrapper::current();
    const proto::ProtoObject* op = w ? w->getJSObjectPrototype() : nullptr;
    const proto::ProtoObject* desc = op ? op->newChild(ctx, true) : ctx->newObject(true);
    auto putB = [&](const char* name, bool b){
        const proto::ProtoObject* nk = ctx->fromUTF8String(name);
        const proto::ProtoString* ks = nk ? nk->asString(ctx) : nullptr;
        if (ks) desc = desc->setAttribute(ctx, ks, b ? PROTO_TRUE : PROTO_FALSE);
    };
    auto putV = [&](const char* name, const proto::ProtoObject* v){
        const proto::ProtoObject* nk = ctx->fromUTF8String(name);
        const proto::ProtoString* ks = nk ? nk->asString(ctx) : nullptr;
        if (ks) desc = desc->setAttribute(ctx, ks, v ? v : getUndefinedSentinel());
    };
    if (od.isAccessor) {
        std::string kstr; propKey->toUTF8String(ctx, kstr);
        const std::string gks = "__get_" + kstr + "__";
        const std::string sks = "__set_" + kstr + "__";
        const proto::ProtoObject* gko = ctx->fromUTF8String(gks.c_str());
        const proto::ProtoObject* sko = ctx->fromUTF8String(sks.c_str());
        const proto::ProtoString* gk = gko ? gko->asString(ctx) : nullptr;
        const proto::ProtoString* sk = sko ? sko->asString(ctx) : nullptr;
        putV("get",          gk ? target->getAttribute(ctx, gk, false) : nullptr);
        putV("set",          sk ? target->getAttribute(ctx, sk, false) : nullptr);
        putB("enumerable",   true);
        putB("configurable", od.configurable);
    } else {
        putV("value",        od.value);
        putB("writable",     od.writable);
        putB("enumerable",   true);
        putB("configurable", od.configurable);
    }
    return desc;
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
    // Both arguments must be Objects (not null / undefined / number /
    // boolean / string / symbol).  Per §28.2.1.1 step 1+3 the type
    // check happens BEFORE allocation; primitives must reject.  Pre-fix
    // the lambda accepted getNullSentinel / getUndefinedSentinel /
    // Symbol primitives because none of them carry the integer / float
    // / string / boolean markers — only the asString/isInteger probes
    // were consulted.
    auto isObj = [&](const proto::ProtoObject* o) {
        if (!o || o == PROTO_NONE) return false;
        if (o == getNullSentinel() || o == getUndefinedSentinel()) return false;
        if (o == PROTO_TRUE || o == PROTO_FALSE) return false;
        if (o->isInteger(ctx) || o->isDouble(ctx) || o->isFloat(ctx)) return false;
        if (o->isBoolean(ctx) || o->asString(ctx)) return false;
        // Symbol primitives are tagged with __is_symbol__ — reject.
        const proto::ProtoString* symK = JSSymbols::isSymbol(ctx);
        if (symK && o->getAttribute(ctx, symK, false) == PROTO_TRUE) return false;
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

// §28.2.2.1 Proxy.revocable(target, handler) — returns {proxy, revoke}
// where revoke() poisons the proxy by clearing __proxy_target__.
static const proto::ProtoObject* proxyRevocable(
    proto::ProtoContext* ctx, const proto::ProtoObject* /*self*/,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!ctx) return PROTO_NONE;
    // Build the proxy via the normal constructor — same arg validation.
    const proto::ProtoObject* proxy = proxyConstructor(ctx, nullptr, nullptr, args, nullptr);
    if (!proxy || proxy == PROTO_NONE) return PROTO_NONE;

    // Build revoke: a closure that clears the proxy's target / handler
    // sidecars so subsequent dispatches see the revoked state.  The
    // proxy reference is captured via a sidecar on the revoke function
    // itself (no real closures here since we're in C++).
    const proto::ProtoString* fp = ctx->space
        ? reinterpret_cast<const proto::ProtoString*>(ctx->space->methodPrototype)
        : nullptr;
    const proto::ProtoObject* fpObj = ctx->space ? ctx->space->methodPrototype : nullptr;
    const proto::ProtoObject* revoke = fpObj
        ? fpObj->newChild(ctx, true) : ctx->newObject(true);
    if (!revoke) return PROTO_NONE;
    // Stash the proxy on the revoke fn.
    const proto::ProtoObject* pko = ctx->fromUTF8String("__proxy_revoke_target__");
    const proto::ProtoString* pk = pko ? pko->asString(ctx) : nullptr;
    if (pk) revoke = revoke->setAttribute(ctx, pk, proxy);
    // Install __native_fn__ that clears the proxy's sidecars.
    // The revoke function looks up __proxy_revoke_target__ on its
    // own this-binding.  Because `r.revoke()` calls with this = r (the
    // wrapping object), not this = revoke, we ALSO stash the proxy on
    // the wrapping object so the lookup succeeds either way (whether
    // the user calls `r.revoke()` or `r.revoke.call(r.revoke)` or
    // detaches with `const rv = r.revoke; rv()`).
    static const proto::ProtoMethod revokeFn = [](
        proto::ProtoContext* ictx,
        const proto::ProtoObject* self,
        const proto::ParentLink*,
        const proto::ProtoList*,
        const proto::ProtoSparseList*) -> const proto::ProtoObject* {
        if (!ictx || !self) return PROTO_NONE;
        const proto::ProtoObject* pko = ictx->fromUTF8String("__proxy_revoke_target__");
        const proto::ProtoString* pk = pko ? pko->asString(ictx) : nullptr;
        if (!pk) return PROTO_NONE;
        // Search self (the call receiver) and, if not found, walk
        // through the receiver's properties looking for revoke
        // candidates.  Spec semantics rely on internal slots so this
        // multi-site stash is the protoJS-flavoured equivalent.
        const proto::ProtoObject* p = self->getAttribute(ictx, pk, true);
        if (p && p != PROTO_NONE) {
            // Mark the proxy as revoked by setting target/handler to
            // PROTO_NONE (not nullptr — nullptr removes the attribute,
            // which makes isProxy return false and the dispatch fall
            // through to a normal-object read).  PROTO_NONE keeps the
            // brand sidecars in place, isProxy stays true,
            // proxyDispatchGet sees proxyTarget == nullptr and throws.
            const proto::ProtoString* tk = targetKey(ictx);
            const proto::ProtoString* hk = handlerKey(ictx);
            if (tk) p->setAttribute(ictx, tk, PROTO_NONE);
            if (hk) p->setAttribute(ictx, hk, PROTO_NONE);
        }
        return PROTO_NONE;
    };
    const proto::ProtoString* nfK = JSSymbols::nativeFn(ctx);
    if (nfK) revoke = revoke->setAttribute(ctx, nfK, ctx->fromMethod(nullptr, revokeFn));
    const proto::ProtoString* nameK = JSSymbols::name(ctx);
    if (nameK) {
        revoke = revoke->setAttribute(ctx, nameK, ctx->fromUTF8String(""));
        // Per §28.2.2.1.1 step 7: name descriptor is
        // {writable: false, enumerable: false, configurable: true} = 0x2.
        const proto::ProtoString* pdnK = JSSymbols::pdName(ctx);
        if (pdnK) revoke = revoke->setAttribute(ctx, pdnK, ctx->fromInteger(0x2LL));
    }
    const proto::ProtoString* lenK = JSSymbols::length(ctx);
    if (lenK) {
        revoke = revoke->setAttribute(ctx, lenK, ctx->fromInteger(0LL));
        // §28.2.2.1.1 step 8: length descriptor is
        // {writable: false, enumerable: false, configurable: true} = 0x2.
        const proto::ProtoString* pdlK = JSSymbols::pdLength(ctx);
        if (pdlK) revoke = revoke->setAttribute(ctx, pdlK, ctx->fromInteger(0x2LL));
    }

    // Return { proxy, revoke } object.  ALSO stash the proxy on the
    // result under __proxy_revoke_target__ so `r.revoke()` (which
    // binds this = r) can find it.  The user can't see this sidecar
    // through enumeration because it isn't a "normal" key.
    const proto::ProtoObject* result = ctx->newObject(true);
    const proto::ProtoObject* proxyKo = ctx->fromUTF8String("proxy");
    const proto::ProtoString* proxyK = proxyKo ? proxyKo->asString(ctx) : nullptr;
    const proto::ProtoObject* revokeKo = ctx->fromUTF8String("revoke");
    const proto::ProtoString* revokeK = revokeKo ? revokeKo->asString(ctx) : nullptr;
    if (proxyK) result = result->setAttribute(ctx, proxyK, proxy);
    if (revokeK) result = result->setAttribute(ctx, revokeK, revoke);
    if (pk) result = result->setAttribute(ctx, pk, proxy);
    return result;
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
    // Proxy.revocable static method.
    {
        const proto::ProtoObject* revFn = installCallable(ctx, proxyRevocable, "revocable", 2);
        if (revFn) {
            const proto::ProtoString* rk = ctx->fromUTF8String("revocable")->asString(ctx);
            if (rk) ctor = ctor->setAttribute(ctx, rk, revFn);
        }
    }
    const proto::ProtoString* k = ctx->fromUTF8String("Proxy")->asString(ctx);
    if (k) *globalRoot = (*globalRoot)->setAttribute(ctx, k, ctor);
}

} // namespace protojs
