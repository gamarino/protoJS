#include "FunctionPrototype.h"
#include "JSContext.h"
#include "JSSymbols.h"
#include "ObjectPrototype.h"
#include "PrototypeUtils.h"
#include "ArrayElementsStorage.h"
#include "runtime/ProtoInterpreter.h"
#include "headers/protoCore.h"
#include <string>

namespace protojs {

namespace {

// ---------------------------------------------------------------------------
// Function.prototype.call(thisArg, arg0, arg1, ...)
// Calls `this` (the function) with the given this-binding and arguments.
// ---------------------------------------------------------------------------

// IsCallable check used by Function.prototype.{call,apply,bind} entry
// points. ECMA-262 §19.2.3.3/4/2 step 1: throw TypeError if 'this' is
// not callable. Accept protoCore methods, QuickJS-compiled closures
// (__bytecode_id__), native wrappers (__native_fn__), and native ctors
// (__construct__).
static bool fnIsCallable(proto::ProtoContext* ctx, const proto::ProtoObject* fn) {
    if (!fn || fn == PROTO_NONE) return false;
    if (fn->isMethod(ctx)) return true;
    const proto::ProtoString* bcKey = JSSymbols::bytecodeId(ctx);
    if (bcKey && fn->hasAttribute(ctx, bcKey) == PROTO_TRUE) return true;
    const proto::ProtoString* nfKey = JSSymbols::nativeFn(ctx);
    if (nfKey && fn->hasAttribute(ctx, nfKey) == PROTO_TRUE) return true;
    const proto::ProtoString* cKey = JSSymbols::construct(ctx);
    if (cKey && fn->hasAttribute(ctx, cKey) == PROTO_TRUE) return true;
    return false;
}

static const proto::ProtoObject* fnCall(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!fnIsCallable(ctx, self)) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Function.prototype.call called on non-callable"));
        return PROTO_NONE;
    }
    int argc = args ? args->getSize(ctx) : 0;
    const proto::ProtoObject* thisArg = (argc > 0) ? args->getAt(ctx, 0) : PROTO_NONE;
    if (!thisArg) thisArg = PROTO_NONE;

    const proto::ProtoList* callArgs = ctx->newList();
    for (int i = 1; i < argc; i++) {
        const proto::ProtoObject* a = args->getAt(ctx, i);
        callArgs = callArgs->appendLast(ctx, a ? a : PROTO_NONE);
    }
    return callJSFunction(ctx, self, thisArg, callArgs);
}

// ---------------------------------------------------------------------------
// Function.prototype.apply(thisArg, argsArray)
// Calls `this` with the given this-binding; spreads argsArray into arguments.
// ---------------------------------------------------------------------------

static const proto::ProtoObject* fnApply(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!fnIsCallable(ctx, self)) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Function.prototype.apply called on non-callable"));
        return PROTO_NONE;
    }
    int argc = args ? args->getSize(ctx) : 0;
    const proto::ProtoObject* thisArg = (argc > 0) ? args->getAt(ctx, 0) : PROTO_NONE;
    if (!thisArg) thisArg = PROTO_NONE;
    const proto::ProtoObject* argsArray = (argc > 1) ? args->getAt(ctx, 1) : nullptr;
    // §20.2.3.1 step 4 + §7.3.18 CreateListFromArrayLike: argArray must
    // be an Object — primitives (boolean, number, string, symbol) and
    // null throw TypeError before length is read.  Pre-fix the
    // null/undefined branch silently produced an empty arg list and the
    // primitive branch fell through to a length read that returned 0,
    // so fn.apply(null, true) / .apply(null, NaN) / etc. called fn
    // with no args instead of throwing.
    if (argsArray && argsArray != PROTO_NONE && argsArray != getUndefinedSentinel()) {
        if (argsArray == getNullSentinel()
            || argsArray->isInteger(ctx) || argsArray->isDouble(ctx)
            || argsArray->isFloat(ctx)   || argsArray->isBoolean(ctx)
            || argsArray->isString(ctx)) {
            signalNativeException(makeNativeError(ctx, "TypeError",
                "Function.prototype.apply argsArray must be an Object"));
            return PROTO_NONE;
        }
        const proto::ProtoObject* isSymKo = ctx->fromUTF8String("__is_symbol__");
        const proto::ProtoString* isSymK = isSymKo ? isSymKo->asString(ctx) : nullptr;
        if (isSymK && argsArray->hasAttribute(ctx, isSymK) == PROTO_TRUE) {
            const proto::ProtoObject* mv = argsArray->getAttribute(ctx, isSymK, true);
            if (mv == PROTO_TRUE) {
                signalNativeException(makeNativeError(ctx, "TypeError",
                    "Function.prototype.apply argsArray must be an Object"));
                return PROTO_NONE;
            }
        }
    }

    const proto::ProtoList* callArgs = ctx->newList();
    if (argsArray && argsArray != PROTO_NONE) {
        const proto::ProtoString* lenKey = JSSymbols::length(ctx);
        long long alen = 0;
        if (lenKey) {
            const proto::ProtoObject* lo = argsArray->getAttribute(ctx, lenKey, false);
            if (lo && lo != PROTO_NONE) {
                if (lo->isInteger(ctx))      alen = lo->asLong(ctx);
                else if (lo->isDouble(ctx))  alen = static_cast<long long>(lo->asDouble(ctx));
            }
        }
        for (long long i = 0; i < alen; i++) {
            // Read via arrayTryFastGet first (arrays store elements in
            // __elements__ ProtoList) — falling back to indexed-attribute
            // lookup keeps legacy array-likes working.
            const proto::ProtoObject* av = arrayTryFastGet(ctx, argsArray, static_cast<unsigned long>(i));
            if (!av) {
                const proto::ProtoString* ik =
                    JSSymbols::indexKey(ctx, static_cast<uint32_t>(i));
                av = ik ? argsArray->getAttribute(ctx, ik, false) : PROTO_NONE;
            }
            callArgs = callArgs->appendLast(ctx, av ? av : PROTO_NONE);
        }
    }
    return callJSFunction(ctx, self, thisArg, callArgs);
}

// ---------------------------------------------------------------------------
// Function.prototype.bind(thisArg, arg0, arg1, ...)
// Returns a new "bound function" object carrying __bound_fn__, __bound_this__,
// and __bound_args__.  The OP_call / OP_call_method handlers in ProtoInterpreter
// detect these sentinels and dispatch accordingly.
// ---------------------------------------------------------------------------

static const proto::ProtoObject* fnBind(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    // ECMA-262 §20.2.3.2 step 1: if Function.prototype.bind is invoked
    // on a non-callable receiver, throw TypeError. Pre-fix bind
    // accepted any receiver and returned a sentinel that broke later
    // calls.
    if (!fnIsCallable(ctx, self)) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Function.prototype.bind called on non-callable"));
        return PROTO_NONE;
    }
    int argc = args ? args->getSize(ctx) : 0;
    const proto::ProtoObject* thisArg = (argc > 0) ? args->getAt(ctx, 0) : PROTO_NONE;
    if (!thisArg) thisArg = PROTO_NONE;

    // Build a JS-array-like object for the pre-bound arguments.
    const proto::ProtoObject* boundArgsArr = ctx->newObject(true);
    const proto::ProtoString* lenKey = JSSymbols::length(ctx);
    long long bcount = 0;
    for (int i = 1; i < argc; i++) {
        const proto::ProtoString* ik =
            JSSymbols::indexKey(ctx, static_cast<uint32_t>(i - 1));
        if (ik) boundArgsArr = boundArgsArr->setAttribute(ctx, ik, args->getAt(ctx, i));
        bcount++;
    }
    if (lenKey)
        boundArgsArr = boundArgsArr->setAttribute(ctx, lenKey, ctx->fromInteger(bcount));

    // Build the bound function sentinel object. Parent at
    // Function.prototype so `bound instanceof Function` holds and
    // `boundFn.call / .apply / .bind` resolve through the chain.
    // Pre-fix bound = ctx->newObject(true) produced an orphan object
    // that instanceof Function returned false for and whose own .call
    // was undefined.
    const proto::ProtoObject* fpParent =
        ctx->space ? ctx->space->methodPrototype : nullptr;
    const proto::ProtoObject* bound = fpParent
        ? fpParent->newChild(ctx, true)
        : ctx->newObject(true);
    const proto::ProtoString* bfKey = JSSymbols::boundFn(ctx);
    const proto::ProtoString* btKey = JSSymbols::boundThis(ctx);
    const proto::ProtoString* baKey = JSSymbols::boundArgs(ctx);
    if (bfKey) bound = bound->setAttribute(ctx, bfKey, self);
    if (btKey) bound = bound->setAttribute(ctx, btKey, thisArg);
    if (baKey) bound = bound->setAttribute(ctx, baKey, boundArgsArr);

    // Set bound.length = max(0, target.length - pre_bound_arg_count).
    const proto::ProtoString* lenKey2 = JSSymbols::length(ctx);
    if (lenKey2) {
        long long targetLen = 0;
        const proto::ProtoObject* lo = self->getAttribute(ctx, lenKey2, false);
        if (lo && lo != PROTO_NONE) {
            if (lo->isInteger(ctx))     targetLen = lo->asLong(ctx);
            else if (lo->isDouble(ctx)) targetLen = static_cast<long long>(lo->asDouble(ctx));
        }
        long long boundLen = targetLen - bcount;
        if (boundLen < 0) boundLen = 0;
        bound = bound->setAttribute(ctx, lenKey2, ctx->fromInteger(boundLen));
    }
    // Set bound.name = "bound " + target.name.
    const proto::ProtoString* nameKey2 = JSSymbols::name(ctx);
    if (nameKey2) {
        std::string targetName;
        const proto::ProtoObject* no = self->getAttribute(ctx, nameKey2, false);
        if (no && no != PROTO_NONE) {
            const proto::ProtoString* ns = no->asString(ctx);
            if (ns) targetName = ns->toStdString(ctx);
        }
        std::string boundName = "bound " + targetName;
        const proto::ProtoObject* bnVal = ctx->fromUTF8String(boundName.c_str());
        if (bnVal) bound = bound->setAttribute(ctx, nameKey2, bnVal);
    }
    return bound;
}

// ---------------------------------------------------------------------------
// Function.prototype.toString() — returns generic function source string
// ---------------------------------------------------------------------------

static const proto::ProtoObject* fnToString(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*,
    const proto::ProtoSparseList*)
{
    // Per ES2019 §19.2.3.5: must be called on a callable; otherwise TypeError.
    // A callable is: a raw native method, a JS closure (has __bytecode_id__),
    // a wrapNativeFunction wrapper (has __native_fn__), or a bound function
    // (has __bound_fn__).
    bool isCallable = false;
    if (self && self != PROTO_NONE) {
        if (self->isMethod(ctx)) {
            isCallable = true;
        } else {
            // JS closure: has __bytecode_id__ integer attribute.
            const proto::ProtoString* bcKey = JSSymbols::bytecodeId(ctx);
            if (bcKey) {
                const proto::ProtoObject* bcVal = self->getAttribute(ctx, bcKey, false);
                if (bcVal && bcVal != PROTO_NONE && bcVal->isInteger(ctx))
                    isCallable = true;
            }
            // wrapNativeFunction wrapper: has __native_fn__ method attribute.
            if (!isCallable) {
                const proto::ProtoString* nfKey = JSSymbols::nativeFn(ctx);
                if (nfKey) {
                    const proto::ProtoObject* nfVal = self->getAttribute(ctx, nfKey, false);
                    if (nfVal && nfVal != PROTO_NONE && nfVal->isMethod(ctx))
                        isCallable = true;
                }
            }
            // Bound function: has __bound_fn__ attribute.
            if (!isCallable) {
                const proto::ProtoString* bfKey = JSSymbols::boundFn(ctx);
                if (bfKey) {
                    const proto::ProtoObject* bfVal = self->getAttribute(ctx, bfKey, false);
                    if (bfVal && bfVal != PROTO_NONE)
                        isCallable = true;
                }
            }
        }
    }
    if (!isCallable) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Function.prototype.toString requires a callable"));
        return PROTO_NONE;
    }

    // Extract the function's name attribute.
    std::string fnName;
    const proto::ProtoString* nameKey = JSSymbols::name(ctx);
    if (nameKey) {
        const proto::ProtoObject* nameVal = self->getAttribute(ctx, nameKey, false);
        if (nameVal && nameVal != PROTO_NONE && nameVal->isString(ctx)) {
            nameVal->asString(ctx)->toUTF8String(ctx, fnName);
        }
    }

    std::string result = "function " + fnName + "() { [native code] }";
    return ctx->fromUTF8String(result.c_str());
}

} // anonymous namespace

void ensureFunctionPrototype(proto::ProtoContext* ctx,
                              const proto::ProtoObject** globalRoot)
{
    if (!ctx || !globalRoot || !*globalRoot) return;

    // Idempotency guard: store/check under internal key "__function_proto__".
    const proto::ProtoString* fpKey = JSSymbols::functionProto(ctx);
    if (!fpKey) return;
    const proto::ProtoObject* existing = (*globalRoot)->getAttribute(ctx, fpKey, false);
    if (existing && existing != PROTO_NONE) return;

    // Build Function.prototype as a child of Object.prototype so that the full
    // chain  fn → Function.prototype → Object.prototype  is in place.
    // This gives every function instance access to hasOwnProperty, toString
    // (Object's), valueOf, etc., matching the ES spec prototype hierarchy.
    // fp is created mutable so that the recursive
    // Function.prototype.constructor === Function backref can be
    // installed later in this function without triggering a copy that
    // would split fp into two distinct objects (the test262 identity
    // check would then fail). The Error / Boolean / Number constructors
    // use the same mutable strategy for their prototypes.
    const proto::ProtoObject* objProto =
        ctx->space ? ctx->space->objectPrototype : nullptr;
    const proto::ProtoObject* fp = (objProto && objProto != PROTO_NONE)
        ? objProto->newChild(ctx, true)
        : ctx->newObject(true);
    if (!fp) return;

    // Install Function.prototype methods as non-enumerable, configurable, writable.
    // These are special: they ARE Function.prototype, so they cannot use the
    // installNonEnumerableMethod wrapper pattern (which requires methodPrototype to
    // already point to fp).  Instead install the raw ProtoMethod with descriptor
    // sidecars directly.  The descriptor bits 0x3 = writable|configurable|!enumerable.
    auto installFpMethod = [&](const char* methodName, proto::ProtoMethod fn, int argc) {
        const proto::ProtoObject* mko = ctx->fromUTF8String(methodName);
        const proto::ProtoString* mk = mko ? mko->asString(ctx) : nullptr;
        if (!mk) return;

        // Raw method object. Wrapping with name/length would orphan the
        // wrapper from the final Function.prototype (parents get snapshotted
        // pre-install), so 'Function.prototype.call.bind(...)' would lose
        // .bind. Raw method handles inherit from methodPrototype lazily —
        // wrapping breaks that.
        const proto::ProtoObject* rawMethod = ctx->fromMethod(nullptr, fn);
        if (!rawMethod) return;
        fp = fp->setAttribute(ctx, mk, rawMethod);

        // Method descriptor: {writable:true, enumerable:false, configurable:true} → 0x3
        std::string pdStr = std::string("__pd_") + methodName + "__";
        const proto::ProtoObject* pdko = ctx->fromUTF8String(pdStr.c_str());
        const proto::ProtoString* pdks = pdko ? pdko->asString(ctx) : nullptr;
        if (pdks) fp = fp->setAttribute(ctx, pdks, ctx->fromInteger(0x3LL));
        (void)argc;
    };

    installFpMethod("call",     fnCall,     1);
    installFpMethod("apply",    fnApply,    2);
    installFpMethod("bind",     fnBind,     1);
    installFpMethod("toString", fnToString, 0);

    // §20.2.3: Function.prototype itself is a function (calling it
    // returns undefined). Object.prototype.toString.call(Function
    // .prototype) must therefore yield "[object Function]". Because
    // Function.prototype is built as a plain newChild of Object
    // .prototype, it carries none of the standard callable markers
    // (__bytecode_id__ / __native_fn__ / __is_constructor__) and
    // toString fell through to "[object Object]" (Sputnik S15.3.4_A1).
    // Stamp __is_function_prototype__ so objectToString can dispatch.
    {
        const proto::ProtoObject* fpmo = ctx->fromUTF8String("__is_function_prototype__");
        const proto::ProtoString* fpms = fpmo ? fpmo->asString(ctx) : nullptr;
        if (fpms) fp = fp->setAttribute(ctx, fpms, PROTO_TRUE);
    }

    // §20.2.3: Function.prototype carries length === 0 and name === ""
    // with the standard built-in descriptor 0x2 (configurable,
    // non-writable, non-enumerable).
    {
        const proto::ProtoString* lenKey = JSSymbols::length(ctx);
        if (lenKey) {
            fp = fp->setAttribute(ctx, lenKey, ctx->fromInteger(0LL));
            const proto::ProtoObject* pdo = ctx->fromUTF8String("__pd_length__");
            const proto::ProtoString* pdk = pdo ? pdo->asString(ctx) : nullptr;
            if (pdk) fp = fp->setAttribute(ctx, pdk, ctx->fromInteger(0x2LL));
        }
        const proto::ProtoString* nmKey = JSSymbols::name(ctx);
        if (nmKey) {
            fp = fp->setAttribute(ctx, nmKey, ctx->fromUTF8String(""));
            const proto::ProtoObject* pdno = ctx->fromUTF8String("__pd_name__");
            const proto::ProtoString* pdnk = pdno ? pdno->asString(ctx) : nullptr;
            if (pdnk) fp = fp->setAttribute(ctx, pdnk, ctx->fromInteger(0x2LL));
        }
    }

    // Register fp as the ProtoSpace method prototype so that ALL native ProtoMethod
    // objects (e.g. Array.prototype.join, Function.prototype.call itself) inherit
    // from Function.prototype.  This is what makes `fn.bind(...)`, `fn.call(...)`,
    // and `fn.apply(...)` work on any native function, matching JS semantics where
    // every callable has Function.prototype in its [[Prototype]] chain.
    if (ctx->space) ctx->space->methodPrototype = const_cast<proto::ProtoObject*>(fp);

    // Store at __function_proto__ so the interpreter can look it up.
    *globalRoot = (*globalRoot)->setAttribute(ctx, fpKey, fp);

    // Expose as Function.prototype under the "Function" constructor if not yet set.
    const proto::ProtoString* keyFunction = JSSymbols::Function(ctx);
    if (keyFunction) {
        const proto::ProtoObject* existingFnCtor =
            (*globalRoot)->getAttribute(ctx, keyFunction, false);
        if (!existingFnCtor || existingFnCtor == PROTO_NONE) {
            const proto::ProtoObject* fnCtor = ctx->newObject(false);
            if (fnCtor) {
                const proto::ProtoString* nameKey = JSSymbols::name(ctx);
                const proto::ProtoString* protoKey = JSSymbols::prototype(ctx);
                const proto::ProtoString* lenKey = JSSymbols::length(ctx);

                // Function.name = "Function", {writable:false, enumerable:false, configurable:true} → 0x2
                if (nameKey) {
                    fnCtor = fnCtor->setAttribute(ctx, nameKey, ctx->fromUTF8String("Function"));
                    const proto::ProtoObject* pdnko = ctx->fromUTF8String("__pd_name__");
                    const proto::ProtoString* pdnk = pdnko ? pdnko->asString(ctx) : nullptr;
                    if (pdnk) fnCtor = fnCtor->setAttribute(ctx, pdnk, ctx->fromInteger(0x2));
                }

                // Function.length = 1, {writable:false, enumerable:false, configurable:true} → 0x2
                if (lenKey) {
                    fnCtor = fnCtor->setAttribute(ctx, lenKey, ctx->fromInteger(1LL));
                    const proto::ProtoObject* pdlko = ctx->fromUTF8String("__pd_length__");
                    const proto::ProtoString* pdlk = pdlko ? pdlko->asString(ctx) : nullptr;
                    if (pdlk) fnCtor = fnCtor->setAttribute(ctx, pdlk, ctx->fromInteger(0x2));
                }

                if (protoKey) fnCtor = fnCtor->setAttribute(ctx, protoKey, fp);

                // Function.prototype.constructor === Function per
                // §20.2.4.1. Use the canonical interned constructor
                // symbol so the runtime's getAttribute("constructor")
                // calls resolve to this exact attribute (an ad-hoc
                // fromUTF8String key would be a different ProtoString
                // identity and would not collide on lookup).
                {
                    const proto::ProtoString* ctorWordKey = JSSymbols::constructor(ctx);
                    if (ctorWordKey) {
                        const proto::ProtoObject* updatedFp =
                            fp->setAttribute(ctx, ctorWordKey, fnCtor);
                        // Non-enumerable per §20.2.4.1 (0x3 = w+c).
                        if (updatedFp && updatedFp != PROTO_NONE) {
                            const proto::ProtoObject* pdo = ctx->fromUTF8String("__pd_constructor__");
                            const proto::ProtoString* pdk = pdo ? pdo->asString(ctx) : nullptr;
                            if (pdk) updatedFp = updatedFp->setAttribute(ctx, pdk,
                                ctx->fromInteger(0x3LL));
                        }
                        if (updatedFp && updatedFp != PROTO_NONE) {
                            fp = updatedFp;
                            *globalRoot = (*globalRoot)->setAttribute(ctx, fpKey, fp);
                            if (ctx->space) ctx->space->methodPrototype =
                                const_cast<proto::ProtoObject*>(fp);
                        }
                    }
                }
                *globalRoot = (*globalRoot)->setAttribute(ctx, keyFunction, fnCtor);
                // §17 globalThis.Function descriptor 0x3.
                const proto::ProtoObject* pdo = ctx->fromUTF8String("__pd_Function__");
                const proto::ProtoString* pdk = pdo ? pdo->asString(ctx) : nullptr;
                if (pdk) *globalRoot = (*globalRoot)->setAttribute(ctx, pdk,
                    ctx->fromInteger(0x3LL));
            }
        }
    }

    // Re-parent Object.prototype's instance method wrappers at fp so
    // their .call / .apply / .bind resolve via the chain. These were
    // installed during JSPrototypes::BootstrapJSPrototypes — earlier
    // than ensureFunctionPrototype — when space->methodPrototype still
    // equalled space->objectPrototype (an empty snapshot), so each
    // wrapper has the snapshot as its only parent and finds nothing
    // when the user looks up .call. addParent(fp) leaves the original
    // parent in place but adds fp to the chain, so attribute lookup
    // now walks into Function.prototype after Object.prototype.
    // The wrappers were created with newChild(ctx, true) (mutable), so
    // addParent mutates them in place.
    {
        const proto::ProtoObject* objProto =
            ctx->space ? ctx->space->objectPrototype : nullptr;
        if (objProto && objProto != PROTO_NONE) {
            static const char* kReparentNames[] = {
                "hasOwnProperty", "isPrototypeOf", "propertyIsEnumerable",
                "toLocaleString", nullptr
            };
            for (int i = 0; kReparentNames[i]; ++i) {
                const proto::ProtoObject* nko = ctx->fromUTF8String(kReparentNames[i]);
                const proto::ProtoString* nk = nko ? nko->asString(ctx) : nullptr;
                if (!nk) continue;
                const proto::ProtoObject* wrapper =
                    objProto->getAttribute(ctx, nk, false);
                if (wrapper && wrapper != PROTO_NONE) {
                    wrapper->addParent(ctx, fp);
                }
            }
        }

        // Same re-parenting pass for the JSON namespace's stringify /
        // parse wrappers. ProtoNativeModule::buildModule allocated them
        // through methodPrototype which, at JSONBuiltin::init time,
        // was still objectPrototype. Without this fixup,
        // Object.getPrototypeOf(JSON.stringify) returned the empty
        // objectPrototype snapshot (and typeof JSON.stringify.call was
        // undefined), so the test262 'builtin.js' check failed.
        //
        // addParent fixes the chain walk so .call/.apply/.bind resolve;
        // setJSProtoOverride aligns Object.getPrototypeOf(...) with
        // Function.prototype, which the spec / test262 check directly.
        const proto::ProtoString* jsonKey =
            ctx->fromUTF8String("JSON") ? ctx->fromUTF8String("JSON")->asString(ctx) : nullptr;
        if (jsonKey && globalRoot && *globalRoot) {
            const proto::ProtoObject* jsonNs =
                (*globalRoot)->getAttribute(ctx, jsonKey, false);
            if (jsonNs && jsonNs != PROTO_NONE) {
                static const char* kJsonNames[] = { "stringify", "parse", nullptr };
                for (int i = 0; kJsonNames[i]; ++i) {
                    const proto::ProtoObject* mko = ctx->fromUTF8String(kJsonNames[i]);
                    const proto::ProtoString* mk = mko ? mko->asString(ctx) : nullptr;
                    if (!mk) continue;
                    const proto::ProtoObject* w =
                        jsonNs->getAttribute(ctx, mk, false);
                    if (w && w != PROTO_NONE) {
                        w->addParent(ctx, fp);
                        protojs::setJSProtoOverride(w, fp);
                    }
                }
            }
        }
    }
}

const proto::ProtoObject* wrapNativeFunction(proto::ProtoContext* ctx,
                                              proto::ProtoMethod fn,
                                              const char* name,
                                              long long length,
                                              const proto::ProtoObject** globalRoot)
{
    if (!ctx || !fn) return PROTO_NONE;

    // Determine parent: prefer Function.prototype so the wrapper inherits
    // .call / .apply / .bind, exactly matching ES semantics.
    const proto::ProtoObject* parent = nullptr;
    if (globalRoot && *globalRoot) {
        const proto::ProtoString* fpKey = JSSymbols::functionProto(ctx);
        if (fpKey) {
            const proto::ProtoObject* fp = (*globalRoot)->getAttribute(ctx, fpKey, false);
            if (fp && fp != PROTO_NONE) parent = fp;
        }
    }
    if (!parent && ctx->space) parent = ctx->space->methodPrototype;

    const proto::ProtoObject* wrapper = parent
        ? parent->newChild(ctx, true)
        : ctx->newObject(true);
    if (!wrapper) return PROTO_NONE;

    // Store the raw method pointer as a ProtoMethod-tagged pointer under __native_fn__.
    const proto::ProtoString* nfKey  = JSSymbols::nativeFn(ctx);
    const proto::ProtoString* lenKey = JSSymbols::length(ctx);
    const proto::ProtoString* nmKey  = JSSymbols::name(ctx);

    const proto::ProtoObject* rawMethod = ctx->fromMethod(nullptr, fn);
    if (nfKey && rawMethod) wrapper = wrapper->setAttribute(ctx, nfKey, rawMethod);

    if (lenKey) {
        wrapper = wrapper->setAttribute(ctx, lenKey, ctx->fromInteger(length));
        // length: {writable: false, enumerable: false, configurable: true} → bits = 0x2
        const proto::ProtoObject* pdlko = ctx->fromUTF8String("__pd_length__");
        const proto::ProtoString* pdlk = pdlko ? pdlko->asString(ctx) : nullptr;
        if (pdlk) wrapper = wrapper->setAttribute(ctx, pdlk, ctx->fromInteger(0x2));
    }

    if (nmKey) {
        wrapper = wrapper->setAttribute(ctx, nmKey, ctx->fromUTF8String(name ? name : ""));
        // name: {writable: false, enumerable: false, configurable: true} → bits = 0x2
        const proto::ProtoObject* pdnko = ctx->fromUTF8String("__pd_name__");
        const proto::ProtoString* pdnk = pdnko ? pdnko->asString(ctx) : nullptr;
        if (pdnk) wrapper = wrapper->setAttribute(ctx, pdnk, ctx->fromInteger(0x2));
    }

    return wrapper ? wrapper : PROTO_NONE;
}

} // namespace protojs
