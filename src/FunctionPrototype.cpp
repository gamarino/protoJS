#include "FunctionPrototype.h"
#include "JSSymbols.h"
#include "runtime/ProtoInterpreter.h"
#include "headers/protoCore.h"
#include <string>

namespace protojs {

namespace {

// ---------------------------------------------------------------------------
// Function.prototype.call(thisArg, arg0, arg1, ...)
// Calls `this` (the function) with the given this-binding and arguments.
// ---------------------------------------------------------------------------

static const proto::ProtoObject* fnCall(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!self || self == PROTO_NONE) return PROTO_NONE;
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
    if (!self || self == PROTO_NONE) return PROTO_NONE;
    int argc = args ? args->getSize(ctx) : 0;
    const proto::ProtoObject* thisArg = (argc > 0) ? args->getAt(ctx, 0) : PROTO_NONE;
    if (!thisArg) thisArg = PROTO_NONE;
    const proto::ProtoObject* argsArray = (argc > 1) ? args->getAt(ctx, 1) : nullptr;

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
            const proto::ProtoString* ik =
                JSSymbols::indexKey(ctx, static_cast<uint32_t>(i));
            const proto::ProtoObject* av =
                ik ? argsArray->getAttribute(ctx, ik, false) : PROTO_NONE;
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
    if (!self || self == PROTO_NONE) return PROTO_NONE;
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

    // Build the bound function sentinel object.
    const proto::ProtoObject* bound = ctx->newObject(true);
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
    const proto::ProtoObject* objProto =
        ctx->space ? ctx->space->objectPrototype : nullptr;
    const proto::ProtoObject* fp = (objProto && objProto != PROTO_NONE)
        ? objProto->newChild(ctx, false)
        : ctx->newObject(false);
    if (!fp) return;

    auto reg = [&](const proto::ProtoString* key, proto::ProtoMethod fn) {
        if (key) fp = fp->setAttribute(ctx, key, ctx->fromMethod(nullptr, fn));
    };

    reg(JSSymbols::call(ctx),     fnCall);
    reg(JSSymbols::apply(ctx),    fnApply);
    reg(JSSymbols::bind(ctx),     fnBind);
    reg(JSSymbols::toString(ctx), fnToString);

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
                if (nameKey) fnCtor = fnCtor->setAttribute(ctx, nameKey,
                    ctx->fromUTF8String("Function"));
                if (protoKey) fnCtor = fnCtor->setAttribute(ctx, protoKey, fp);
                *globalRoot = (*globalRoot)->setAttribute(ctx, keyFunction, fnCtor);
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
    if (lenKey) wrapper = wrapper->setAttribute(ctx, lenKey, ctx->fromInteger(length));
    if (nmKey)  wrapper = wrapper->setAttribute(ctx, nmKey,  ctx->fromUTF8String(name ? name : ""));

    return wrapper ? wrapper : PROTO_NONE;
}

} // namespace protojs
