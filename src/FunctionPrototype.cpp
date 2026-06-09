#include "FunctionPrototype.h"
#include "JSContext.h"
#include "JSSymbols.h"
#include "ObjectPrototype.h"
#include "PrototypeUtils.h"
#include "ArrayElementsStorage.h"
#include "runtime/ProtoInterpreter.h"
#include "headers/protoCore.h"
#include <cstdio>
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
    // Built-in constructors (Array, String, Number, Boolean, RegExp,
    // Error, TypedArray, ...) carry constructor-marker attributes
    // instead of __native_fn__ on the constructor object itself.
    // typeof returns "function" for them via the same probes (see
    // OP_typeof), and arrayThrowIfCallbackNotCallable already accepts
    // them — so Function.prototype.{call, apply, bind} must too,
    // otherwise Array.apply(this, args) / String.call(...) etc. throw
    // \"called on non-callable\" against the spec.  Pre-fix
    // built-ins/Array/prototype/concat/Array.prototype.concat_non-array
    // (which uses Array.apply(this, arguments) inside a class
    // constructor) raised this TypeError.
    const proto::ProtoString* acK = JSSymbols::arrayCtor(ctx);
    if (acK && fn->getAttribute(ctx, acK, false) == PROTO_TRUE) return true;
    const proto::ProtoString* ecK = JSSymbols::errorCtor(ctx);
    if (ecK && fn->hasAttribute(ctx, ecK) == PROTO_TRUE) return true;
    const proto::ProtoString* reK = JSSymbols::regexpCtor(ctx);
    if (reK && fn->getAttribute(ctx, reK, false) == PROTO_TRUE) return true;
    const proto::ProtoString* taK = JSSymbols::taCtor(ctx);
    if (taK && fn->hasAttribute(ctx, taK) == PROTO_TRUE) return true;
    const proto::ProtoString* scK = JSSymbols::stringCtor(ctx);
    if (scK && fn->getAttribute(ctx, scK, false) == PROTO_TRUE) return true;
    // Bound functions wrap a target callable behind __bound_fn__.
    // callJSFunction unwraps them transparently.
    const proto::ProtoString* bfKey = JSSymbols::boundFn(ctx);
    if (bfKey && fn->hasAttribute(ctx, bfKey) == PROTO_TRUE) return true;
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
        const proto::ProtoString* isSymK = JSSymbols::isSymbol(ctx);
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
    // §19.2.3.2 step 8 SetFunctionLength: descriptor is
    // {writable:false, enumerable:false, configurable:true} → 0x2.
    // Pre-fix the sidecar was absent so the slot defaulted to
    // fully enumerable / writable (built-ins/Function/prototype/bind/
    // instance-length and instance-name).
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
        const proto::ProtoString* pdls = JSSymbols::pdLength(ctx);
        if (pdls) bound = bound->setAttribute(ctx, pdls, ctx->fromInteger(0x2LL));
    }
    // Set bound.name = "bound " + target.name with same §17 descriptor.
    // §20.2.3.2 step 12 ! Get(Target, "name") propagates abrupts from
    // a throwing accessor (instance-name-error.js).  Probe the
    // __get_name__ sidecar explicitly — getAttribute alone reads the
    // (empty) data slot for accessor properties.
    const proto::ProtoString* nameKey2 = JSSymbols::name(ctx);
    if (nameKey2) {
        std::string targetName;
        const proto::ProtoObject* no = nullptr;
        const proto::ProtoObject* gko = ctx->fromUTF8String("__get_name__");
        const proto::ProtoString* gk  = gko ? gko->asString(ctx) : nullptr;
        if (gk) {
            const proto::ProtoObject* getter = self->getAttribute(ctx, gk, true);
            if (getter && getter != PROTO_NONE) {
                no = callJSFunction(ctx, getter, self, ctx->newList());
                if (hasCallException()) return PROTO_NONE;
            }
        }
        if (!no || no == PROTO_NONE)
            no = self->getAttribute(ctx, nameKey2, false);
        if (no && no != PROTO_NONE) {
            const proto::ProtoString* ns = no->asString(ctx);
            if (ns) targetName = ns->toStdString(ctx);
        }
        std::string boundName = "bound " + targetName;
        const proto::ProtoObject* bnVal = ctx->fromUTF8String(boundName.c_str());
        if (bnVal) bound = bound->setAttribute(ctx, nameKey2, bnVal);
        const proto::ProtoString* pdns = JSSymbols::pdName(ctx);
        if (pdns) bound = bound->setAttribute(ctx, pdns, ctx->fromInteger(0x2LL));
    }
    return bound;
}

// ---------------------------------------------------------------------------
// Function.prototype.toString() — returns generic function source string
// ---------------------------------------------------------------------------

// ---------------------------------------------------------------------------
// Function.prototype[@@hasInstance](V) — ECMA-262 §20.2.3.6 OrdinaryHasInstance
// Default `obj instanceof Constructor` semantics.  Walks V's prototype chain
// and reports whether Constructor.prototype is encountered.
// ---------------------------------------------------------------------------
static const proto::ProtoObject* fnHasInstance(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    // Spec step 1: if IsCallable(this) is false, return false.
    if (!fnIsCallable(ctx, self)) return PROTO_FALSE;
    const proto::ProtoObject* V = (args && args->getSize(ctx) > 0)
        ? args->getAt(ctx, 0) : PROTO_NONE;
    // Spec step 3: if Type(V) is not Object, return false.
    if (!V || V == PROTO_NONE
        || V == getUndefinedSentinel() || V == getNullSentinel())
        return PROTO_FALSE;
    if (V->isInteger(ctx) || V->isDouble(ctx) || V->isFloat(ctx)
        || V == PROTO_TRUE || V == PROTO_FALSE)
        return PROTO_FALSE;
    if (V->isString(ctx)) {
        // primitive string — distinguish from a String wrapper
        const proto::ProtoString* pvK = JSSymbols::primitiveValue(ctx);
        if (!pvK || V->getAttribute(ctx, pvK, false) == PROTO_NONE)
            return PROTO_FALSE;
    }

    // Spec step 2: if this has [[BoundTargetFunction]], delegate to it.
    const proto::ProtoString* boundKey = JSSymbols::boundFn(ctx);
    const proto::ProtoObject* boundTarget = (boundKey)
        ? self->getAttribute(ctx, boundKey, false) : nullptr;
    if (boundTarget && boundTarget != PROTO_NONE) self = boundTarget;

    // Spec step 4: P = Get(this, "prototype").
    const proto::ProtoString* protoKey = JSSymbols::prototype(ctx);
    if (!protoKey) return PROTO_FALSE;
    const proto::ProtoObject* P = self->getAttribute(ctx, protoKey, false);
    if (!P || P == PROTO_NONE
        || P == getUndefinedSentinel() || P == getNullSentinel()) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Function has non-object prototype in instanceof check"));
        return PROTO_NONE;
    }
    if (P->isInteger(ctx) || P->isDouble(ctx) || P->isFloat(ctx)
        || P == PROTO_TRUE || P == PROTO_FALSE || P->isString(ctx)) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Function has non-object prototype in instanceof check"));
        return PROTO_NONE;
    }

    // Spec step 6: walk V's prototype chain looking for P.
    const proto::ProtoObject* cur = V;
    for (int hops = 0; hops < 1024; ++hops) {
        // Follow JS-level [[Prototype]] via the override map first, then
        // the protoCore parent chain.
        const proto::ProtoObject* next = nullptr;
        // Re-use objectGetPrototypeOf semantics inline.
        {
            const proto::ProtoList* singleArg = ctx->newList();
            singleArg = singleArg->appendLast(ctx, cur);
            // Direct call: we know objectGetPrototypeOf is defined nearby,
            // but it has internal linkage.  Use getJSProtoOverride directly.
            next = protojs::getJSProtoOverride(cur);
            if (!next) next = cur->getPrototype(ctx);
        }
        if (!next || next == PROTO_NONE || next == getNullSentinel())
            return PROTO_FALSE;
        if (next == P) return PROTO_TRUE;
        cur = next;
    }
    return PROTO_FALSE;
}

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

// Native [[Call]] / [[Construct]] for the Function constructor.
// ECMA-262 §20.2.1: Function(p1, p2, ..., pn, body) returns a new
// function whose formal parameter list is the comma-joined list of
// ToString-coerced p1..pn and whose body is the ToString-coerced body
// argument.
//
// Implementation: assemble "(function anonymous(<params>\n) {\n<body>\n})"
// and route through JSContextWrapper::evalIsolatedToProto, which
// compiles via QuickJS, runs the resulting bytecode on the protoCore
// interpreter using a *separate* bytecode module (does not disturb the
// caller's rootModule_), and returns the protoCore object directly —
// so the resulting closure keeps its __bytecode_id__ identity and is
// natively invokable.
//
// Pre-fix the Function constructor object carried only the
// __is_constructor__ marker (for the IsConstructor probe) and none of
// the call-dispatch markers (__native_fn__, __bytecode_id__,
// __construct__, __<name>_ctor__). The L_OP_call dispatch fell through
// to the "not callable" TypeError branch. As a result Function(),
// new Function(body), and new Function("a,b", "return a+b") all threw
// "TypeError: is not a function". Around 50-80 test262 cases under
// built-ins/Function/prototype/{apply,call,bind,...} that wrap a
// Function(body) input were blocked by this single missing handler.
static const proto::ProtoObject* functionConstructorCall(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!ctx) return PROTO_NONE;
    JSContextWrapper* wrapper = JSContextWrapper::current();
    if (!wrapper) return PROTO_NONE;

    // Coerce a ProtoObject to a UTF-8 std::string. Strings pass through
    // verbatim; numerics and booleans surface their canonical literal
    // form; everything else (undefined / null / arbitrary object)
    // surfaces an empty string, which produces a syntax-free no-op
    // segment the way the spec's ToString steps eventually do.
    auto toStr = [&](const proto::ProtoObject* o) -> std::string {
        std::string r;
        if (!o || o == PROTO_NONE) return r;
        if (o->isString(ctx)) { o->asString(ctx)->toUTF8String(ctx, r); return r; }
        if (o->isInteger(ctx)) return std::to_string(o->asLong(ctx));
        if (o->isDouble(ctx) || o->isFloat(ctx)) {
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%.15g", o->asDouble(ctx));
            return std::string(buf);
        }
        if (o == PROTO_TRUE) return "true";
        if (o == PROTO_FALSE) return "false";
        return r;
    };

    const size_t argCount = args ? args->getSize(ctx) : 0;
    std::string params;
    std::string body;

    if (argCount == 0) {
        // Function() — no params, no body.
    } else if (argCount == 1) {
        // Function(body)
        body = toStr(args->getAt(ctx, 0));
    } else {
        // Function(p1, p2, ..., pn, body)
        for (size_t i = 0; i < argCount - 1; i++) {
            if (i > 0) params += ",";
            params += toStr(args->getAt(ctx, static_cast<int>(i)));
        }
        body = toStr(args->getAt(ctx, static_cast<int>(argCount - 1)));
    }

    // §20.2.1.1.1 CreateDynamicFunction step 30 (function kind):
    // wrap as a parenthesised function expression so the script-mode
    // evaluator produces the function value as its result. Use
    // "anonymous" as the inferred name per the spec's
    // CreateDynamicFunction synthesis.
    std::string source = "(function anonymous(" + params + "\n) {\n" + body + "\n})";

    const proto::ProtoObject* result =
        wrapper->evalIsolatedToProto(source, "<Function>");
    return (result && result != PROTO_NONE) ? result : PROTO_NONE;
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

        // Wrap with name + length so the spec-mandated §17 descriptor
        // shape (length = argc, name = "<method>") is visible.  fp is
        // mutable, and methodPrototype is set to fp later in this
        // function; the wrapper's parent slot points to whatever
        // methodPrototype was at construction time (most likely null or
        // an interim value), so we explicitly parent on fp itself —
        // this is the same trick the class-side methods use to find
        // .bind / .apply later.
        const proto::ProtoObject* wrapper = fp->newChild(ctx, true);
        if (!wrapper) {
            // Fallback: install raw method without name/length.
            const proto::ProtoObject* rawMethod = ctx->fromMethod(nullptr, fn);
            if (rawMethod) fp = fp->setAttribute(ctx, mk, rawMethod);
            return;
        }
        const proto::ProtoString* nfKey2 = JSSymbols::nativeFn(ctx);
        if (nfKey2) wrapper = wrapper->setAttribute(ctx, nfKey2,
            ctx->fromMethod(nullptr, fn));
        const proto::ProtoString* lenKey2 = JSSymbols::length(ctx);
        if (lenKey2) {
            wrapper = wrapper->setAttribute(ctx, lenKey2, ctx->fromInteger(argc));
            const proto::ProtoString* pdlk = JSSymbols::pdLength(ctx);
            if (pdlk) wrapper = wrapper->setAttribute(ctx, pdlk, ctx->fromInteger(0x2LL));
        }
        const proto::ProtoString* nmKey2 = JSSymbols::name(ctx);
        if (nmKey2) {
            wrapper = wrapper->setAttribute(ctx, nmKey2, ctx->fromUTF8String(methodName));
            const proto::ProtoString* pdnk = JSSymbols::pdName(ctx);
            if (pdnk) wrapper = wrapper->setAttribute(ctx, pdnk, ctx->fromInteger(0x2LL));
        }
        const proto::ProtoString* hnwFp = JSSymbols::hasNonWritableProps(ctx);
        if (hnwFp) wrapper = wrapper->setAttribute(ctx, hnwFp, PROTO_TRUE);
        fp = fp->setAttribute(ctx, mk, wrapper);

        // Method descriptor: {writable:true, enumerable:false, configurable:true} → 0x3
        std::string pdStr = std::string("__pd_") + methodName + "__";
        const proto::ProtoObject* pdko = ctx->fromUTF8String(pdStr.c_str());
        const proto::ProtoString* pdks = pdko ? pdko->asString(ctx) : nullptr;
        if (pdks) fp = fp->setAttribute(ctx, pdks, ctx->fromInteger(0x3LL));
    };

    installFpMethod("call",     fnCall,     1);
    installFpMethod("apply",    fnApply,    2);
    installFpMethod("bind",     fnBind,     1);
    installFpMethod("toString", fnToString, 0);

    // Function.prototype[@@hasInstance] — ECMA-262 §20.2.3.6.  Unlike the
    // other Function.prototype methods this slot is non-writable,
    // non-enumerable AND non-configurable (descriptor 0x0).  Install
    // separately so it carries the right descriptor sidecar.
    {
        const proto::ProtoString* hiK = JSSymbols::symbolHasInstance(ctx);
        if (hiK) {
            const proto::ProtoObject* hiWrapper = fp->newChild(ctx, true);
            if (hiWrapper) {
                const proto::ProtoString* nfk = JSSymbols::nativeFn(ctx);
                if (nfk) hiWrapper = hiWrapper->setAttribute(ctx, nfk,
                    ctx->fromMethod(nullptr, fnHasInstance));
                const proto::ProtoString* lenK = JSSymbols::length(ctx);
                if (lenK) {
                    hiWrapper = hiWrapper->setAttribute(ctx, lenK, ctx->fromInteger(1LL));
                    const proto::ProtoString* pdlk = JSSymbols::pdLength(ctx);
                    if (pdlk) hiWrapper = hiWrapper->setAttribute(ctx, pdlk, ctx->fromInteger(0x2LL));
                }
                const proto::ProtoString* nmK = JSSymbols::name(ctx);
                if (nmK) {
                    hiWrapper = hiWrapper->setAttribute(ctx, nmK,
                        ctx->fromUTF8String("[Symbol.hasInstance]"));
                    const proto::ProtoString* pdnk = JSSymbols::pdName(ctx);
                    if (pdnk) hiWrapper = hiWrapper->setAttribute(ctx, pdnk, ctx->fromInteger(0x2LL));
                }
                const proto::ProtoString* hnwK = JSSymbols::hasNonWritableProps(ctx);
                if (hnwK) hiWrapper = hiWrapper->setAttribute(ctx, hnwK, PROTO_TRUE);
                fp = fp->setAttribute(ctx, hiK, hiWrapper);
                // Slot descriptor 0x0 = writable:false, enumerable:false, configurable:false
                const proto::ProtoObject* pdo = ctx->fromUTF8String("__pd_Symbol.hasInstance__");
                const proto::ProtoString* pdks = pdo ? pdo->asString(ctx) : nullptr;
                if (pdks) fp = fp->setAttribute(ctx, pdks, ctx->fromInteger(0x0LL));
            }
        }
    }

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
            const proto::ProtoString* pdk = JSSymbols::pdLength(ctx);
            if (pdk) fp = fp->setAttribute(ctx, pdk, ctx->fromInteger(0x2LL));
        }
        const proto::ProtoString* nmKey = JSSymbols::name(ctx);
        if (nmKey) {
            fp = fp->setAttribute(ctx, nmKey, ctx->fromUTF8String(""));
            const proto::ProtoString* pdnk = JSSymbols::pdName(ctx);
            if (pdnk) fp = fp->setAttribute(ctx, pdnk, ctx->fromInteger(0x2LL));
        }
        // Stamp the per-target gating flag so resolvePutFieldOOP enforces
        // the writable=false bits on Function.prototype.length / name.
        // Round 12/13 swept this across every other built-in but missed
        // Function.prototype itself — `Function.prototype.length = 99`
        // silently succeeded pre-fix.
        const proto::ProtoString* hnwFp = JSSymbols::hasNonWritableProps(ctx);
        if (hnwFp) fp = fp->setAttribute(ctx, hnwFp, PROTO_TRUE);
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
            // Mutable so verifyConfigurable's JS-level delete can
            // remove configurable own properties.  Mirrors the
            // mutability fix applied to every other ctor.
            const proto::ProtoObject* fnCtor = ctx->newObject(true);
            if (fnCtor) {
                // §10.3 IsConstructor + typeof: stamp markers so
                // typeof Function === "function" AND isConstructor(Function)
                // === true.  Pre-fix the ctor had only name/length/
                // prototype, so typeof reported "object" and the
                // test262 isConstructor harness rejected it as
                // "non-function value" (built-ins/Function/
                // is-a-constructor).
                {
                    const proto::ProtoString* icK = JSSymbols::isConstructor(ctx);
                    if (icK) fnCtor = fnCtor->setAttribute(ctx, icK, PROTO_TRUE);
                }
                const proto::ProtoString* nameKey = JSSymbols::name(ctx);
                const proto::ProtoString* protoKey = JSSymbols::prototype(ctx);
                const proto::ProtoString* lenKey = JSSymbols::length(ctx);

                // Function.name = "Function", {writable:false, enumerable:false, configurable:true} → 0x2
                if (nameKey) {
                    fnCtor = fnCtor->setAttribute(ctx, nameKey, ctx->fromUTF8String("Function"));
                    const proto::ProtoString* pdnk = JSSymbols::pdName(ctx);
                    if (pdnk) fnCtor = fnCtor->setAttribute(ctx, pdnk, ctx->fromInteger(0x2));
                }

                // Function.length = 1, {writable:false, enumerable:false, configurable:true} → 0x2
                if (lenKey) {
                    fnCtor = fnCtor->setAttribute(ctx, lenKey, ctx->fromInteger(1LL));
                    const proto::ProtoString* pdlk = JSSymbols::pdLength(ctx);
                    if (pdlk) fnCtor = fnCtor->setAttribute(ctx, pdlk, ctx->fromInteger(0x2));
                }
                // Hot-path hint — Round 12/13 constructor sweep.
                {
                    const proto::ProtoString* hnw = JSSymbols::hasNonWritableProps(ctx);
                    if (hnw) fnCtor = fnCtor->setAttribute(ctx, hnw, PROTO_TRUE);
                }

                if (protoKey) {
                    fnCtor = fnCtor->setAttribute(ctx, protoKey, fp);
                    // §20.2.3.1 / §17: Function.prototype is
                    // {writable:false, enumerable:false, configurable:
                    // false} → bits 0x0.  Pre-fix the slot defaulted to
                    // fully enumerable/writable/configurable.  test262
                    // built-ins/Object/getOwnPropertyDescriptor/15.2.3.3-
                    // 4-185 verifies the spec-mandated descriptor.
                    const proto::ProtoObject* pdpo = ctx->fromUTF8String("__pd_prototype__");
                    const proto::ProtoString* pdpk = pdpo ? pdpo->asString(ctx) : nullptr;
                    if (pdpk) fnCtor = fnCtor->setAttribute(ctx, pdpk,
                        ctx->fromInteger(0x0LL));
                }

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
                            const proto::ProtoString* pdk = JSSymbols::pdConstructor(ctx);
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
                // ECMA-262 §17 / §20.2.1: every built-in constructor's
                // [[Prototype]] is Function.prototype.  Pre-fix Function
                // itself inherited the bare Object.prototype because
                // ctx->newObject(true) wires no JS prototype override.
                // Other constructors set this via the unimplemented stub
                // installer; Function is installed inline before that
                // loop runs, so wire the override explicitly.  Without
                // it, Object.getPrototypeOf(Function) !== Function.prototype
                // (built-ins/Object/getPrototypeOf/15.2.3.2-2-4).
                protojs::setJSProtoOverride(ctx, fnCtor, fp);

                // Stamp the [[Call]] / [[Construct]] entrypoint so
                // L_OP_call's __native_fn__ unwrap finds something
                // invokable. Without this, Function() / new Function(
                // body) fell through to the dispatch's terminal
                // "TypeError: is not a function" branch. See the
                // header comment on functionConstructorCall for the
                // full rationale.
                {
                    const proto::ProtoString* nfKey = JSSymbols::nativeFn(ctx);
                    if (nfKey) {
                        const proto::ProtoObject* rawMethod =
                            ctx->fromMethod(nullptr, functionConstructorCall);
                        if (rawMethod) {
                            fnCtor = fnCtor->setAttribute(ctx, nfKey, rawMethod);
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
                static const char* kJsonNames[] = { "stringify", "parse", "rawJSON", "isRawJSON", nullptr };
                for (int i = 0; kJsonNames[i]; ++i) {
                    const proto::ProtoObject* mko = ctx->fromUTF8String(kJsonNames[i]);
                    const proto::ProtoString* mk = mko ? mko->asString(ctx) : nullptr;
                    if (!mk) continue;
                    const proto::ProtoObject* w =
                        jsonNs->getAttribute(ctx, mk, false);
                    if (w && w != PROTO_NONE) {
                        w->addParent(ctx, fp);
                        protojs::setJSProtoOverride(ctx, w, fp);
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
        const proto::ProtoString* pdlk = JSSymbols::pdLength(ctx);
        if (pdlk) wrapper = wrapper->setAttribute(ctx, pdlk, ctx->fromInteger(0x2));
    }

    if (nmKey) {
        wrapper = wrapper->setAttribute(ctx, nmKey, ctx->fromUTF8String(name ? name : ""));
        // name: {writable: false, enumerable: false, configurable: true} → bits = 0x2
        const proto::ProtoString* pdnk = JSSymbols::pdName(ctx);
        if (pdnk) wrapper = wrapper->setAttribute(ctx, pdnk, ctx->fromInteger(0x2));
    }

    // The writability enforcement in resolvePutFieldOOP (ProtoInterpreter.cpp:172)
    // gates the entire `__pd_<key>__` probe behind `__has_nonwritable_props__`.
    // wrapNativeFunction stamps non-writable bits for `name` and `length` but
    // pre-fix never set the hot-path hint, so `Object.create.name = "x"`
    // silently succeeded — failing test262 built-ins/Object/create/name.js,
    // built-ins/Object/setPrototypeOf/name.js, ditto length, and the analogous
    // tests for every other Object.* / Array.* / String.* / ... static method
    // registered via this helper.
    const proto::ProtoString* hnw = JSSymbols::hasNonWritableProps(ctx);
    if (hnw) wrapper = wrapper->setAttribute(ctx, hnw, PROTO_TRUE);

    return wrapper ? wrapper : PROTO_NONE;
}

} // namespace protojs
