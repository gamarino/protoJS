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
    return bound;
}

// ---------------------------------------------------------------------------
// Function.prototype.toString() — returns generic function source string
// ---------------------------------------------------------------------------

static const proto::ProtoObject* fnToString(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList*,
    const proto::ProtoSparseList*)
{
    return ctx->fromUTF8String("function () { [native code] }");
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

    // Build Function.prototype.
    const proto::ProtoObject* fp = ctx->newObject(false);
    if (!fp) return;

    auto reg = [&](const proto::ProtoString* key, proto::ProtoMethod fn) {
        if (key) fp = fp->setAttribute(ctx, key, ctx->fromMethod(nullptr, fn));
    };

    reg(JSSymbols::call(ctx),     fnCall);
    reg(JSSymbols::apply(ctx),    fnApply);
    reg(JSSymbols::bind(ctx),     fnBind);
    reg(JSSymbols::toString(ctx), fnToString);

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

} // namespace protojs
