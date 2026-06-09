#include "BigIntPrototype.h"
#include "JSContext.h"
#include "JSSymbols.h"
#include "ObjectPrototype.h"
#include "FunctionPrototype.h"
#include "PrototypeUtils.h"
#include "runtime/ProtoInterpreter.h"
#include "headers/protoCore.h"

#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace protojs {

namespace {

// Cached BigInt.prototype pointer per-thread.  ensureBigIntConstructor
// publishes it on first call; isBigInt / wrapBigInt use it as the
// parent for fresh wrappers and the source of the __is_bigint__ marker.
//
// Why thread-local: prototypes are stored on `ProtoSpace` which is
// per-runtime, and each JSContextWrapper runs on its own thread.
static thread_local const proto::ProtoObject* t_bigIntPrototype = nullptr;

// Build the integer-coercion result for a JS value, with BigInt
// semantics — strings parse as decimal/hex/etc., numbers must be
// exact integers (no fractional component, no NaN, no Infinity).
// Returns the resulting protoCore Integer (SmallInt or LargeInteger),
// or PROTO_NONE with a pending exception on coercion failure.
//
// §7.1.13 ToBigInt + §7.1.14 StringToBigInt.
static const proto::ProtoObject* coerceToInteger(proto::ProtoContext* ctx,
                                                 const proto::ProtoObject* v) {
    if (!ctx) return PROTO_NONE;
    if (!v || v == PROTO_NONE) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Cannot convert undefined to a BigInt"));
        return PROTO_NONE;
    }
    if (v == getUndefinedSentinel()) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Cannot convert undefined to a BigInt"));
        return PROTO_NONE;
    }
    if (v == getNullSentinel()) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Cannot convert null to a BigInt"));
        return PROTO_NONE;
    }
    if (v->isBoolean(ctx)) {
        return ctx->fromInteger(v->asBoolean(ctx) ? 1LL : 0LL);
    }
    // Already a BigInt wrapper — unwrap it.
    {
        const proto::ProtoString* mk = JSSymbols::isBigInt(ctx);
        if (mk && v->getAttribute(ctx, mk, true) == PROTO_TRUE) {
            return unwrapBigInt(ctx, v);
        }
    }
    // §7.1.13 step 2.c: Number → TypeError unless the number is an
    // exact integer.  Throw RangeError for finite non-integers per
    // NumericToBigInt; the "TypeError on Number" branch only fires
    // for the implicit ToBigInt conversion in arithmetic dispatch.
    // The BigInt() constructor (the most common caller) maps Number
    // through NumberToBigInt, which is the RangeError branch.
    if (v->isInteger(ctx)) {
        // Already an integer — but it was stored as a Number, so the
        // caller (BigInt() ctor) wants a copy with bignum semantics.
        return v;
    }
    if (v->isDouble(ctx) || v->isFloat(ctx)) {
        double d = v->asDouble(ctx);
        if (std::isnan(d) || std::isinf(d) || d != std::trunc(d)) {
            signalNativeException(makeNativeError(ctx, "RangeError",
                "The number cannot be converted to a BigInt because it is not an integer"));
            return PROTO_NONE;
        }
        return ctx->fromInteger(static_cast<long long>(d));
    }
    if (v->isString(ctx)) {
        std::string s;
        v->asString(ctx)->toUTF8String(ctx, s);
        // §7.1.14 StringToBigInt: trim WhiteSpace, optional sign, then
        // DecimalDigits | 0b… | 0o… | 0x….  protoCore::fromString takes
        // a base argument; detect the radix prefix here and strip it.
        // The empty string is +0n per spec.
        size_t lo = 0, hi = s.size();
        while (lo < hi && (s[lo] == ' ' || s[lo] == '\t' || s[lo] == '\n' || s[lo] == '\r'))
            ++lo;
        while (hi > lo && (s[hi-1] == ' ' || s[hi-1] == '\t' || s[hi-1] == '\n' || s[hi-1] == '\r'))
            --hi;
        if (lo == hi) return ctx->fromInteger(0LL);
        bool neg = false;
        if (s[lo] == '+' || s[lo] == '-') {
            neg = (s[lo] == '-');
            ++lo;
        }
        int base = 10;
        if (lo + 1 < hi && s[lo] == '0') {
            char c = s[lo + 1];
            if (c == 'x' || c == 'X') { base = 16; lo += 2; }
            else if (c == 'o' || c == 'O') { base = 8;  lo += 2; }
            else if (c == 'b' || c == 'B') { base = 2;  lo += 2; }
            // §7.1.14: signed non-decimal-radix literals are SyntaxError
            // for the BigInt parser (matches NumericLiteral / ToBigInt).
            if (base != 10 && neg) {
                signalNativeException(makeNativeError(ctx, "SyntaxError",
                    "Cannot convert signed non-decimal literal to a BigInt"));
                return PROTO_NONE;
            }
        }
        if (lo == hi) {
            signalNativeException(makeNativeError(ctx, "SyntaxError",
                "Cannot convert empty digit sequence to a BigInt"));
            return PROTO_NONE;
        }
        std::string body = s.substr(lo, hi - lo);
        const proto::ProtoObject* parsed = ctx->fromString(body.c_str(), base);
        if (!parsed) {
            signalNativeException(makeNativeError(ctx, "SyntaxError",
                "Cannot convert string to a BigInt"));
            return PROTO_NONE;
        }
        if (neg) parsed = parsed->negate(ctx);
        return parsed;
    }
    // Symbols / objects / others reach here and are TypeError per spec.
    signalNativeException(makeNativeError(ctx, "TypeError",
        "Cannot convert to a BigInt"));
    return PROTO_NONE;
}

// §21.2.1.1 BigInt(value).  Not callable as constructor — `new BigInt`
// throws TypeError immediately.  Plain call coerces via ToBigInt and
// returns the wrapper.
static const proto::ProtoObject* bigIntCtor(proto::ProtoContext* ctx,
                                            const proto::ProtoObject*,
                                            const proto::ParentLink*,
                                            const proto::ProtoList* args,
                                            const proto::ProtoSparseList*) {
    if (!ctx) return PROTO_NONE;
    if (!args || args->getSize(ctx) == 0) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "BigInt() requires an argument"));
        return PROTO_NONE;
    }
    const proto::ProtoObject* v = args->getAt(ctx, 0);
    const proto::ProtoObject* integer = coerceToInteger(ctx, v);
    if (hasCallException() || !integer || integer == PROTO_NONE) return PROTO_NONE;
    return wrapBigInt(ctx, integer);
}

// §21.2.1.1 step 1: throw TypeError when called via `new BigInt(...)`.
static const proto::ProtoObject* bigIntConstruct(proto::ProtoContext* ctx,
                                                 const proto::ProtoObject*,
                                                 const proto::ParentLink*,
                                                 const proto::ProtoList*,
                                                 const proto::ProtoSparseList*) {
    if (!ctx) return PROTO_NONE;
    signalNativeException(makeNativeError(ctx, "TypeError",
        "BigInt is not a constructor"));
    return PROTO_NONE;
}

// §21.2.3.3 BigInt.prototype.toString( [ radix ] ).  Per spec the
// receiver may be either a BigInt primitive or an Object whose
// [[BigIntData]] is a BigInt — we honour both via isBigInt.  Radix
// defaults to 10; valid range 2..36.
static const proto::ProtoObject* bigIntToString(proto::ProtoContext* ctx,
                                                const proto::ProtoObject* self,
                                                const proto::ParentLink*,
                                                const proto::ProtoList* args,
                                                const proto::ProtoSparseList*) {
    if (!ctx) return PROTO_NONE;
    if (!isBigInt(ctx, self)) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "BigInt.prototype.toString requires that 'this' be a BigInt"));
        return PROTO_NONE;
    }
    int base = 10;
    if (args && args->getSize(ctx) >= 1) {
        const proto::ProtoObject* r = args->getAt(ctx, 0);
        if (r && r != getUndefinedSentinel()) {
            if (r->isInteger(ctx)) base = static_cast<int>(r->asLong(ctx));
            else if (r->isDouble(ctx) || r->isFloat(ctx))
                base = static_cast<int>(r->asDouble(ctx));
            if (base < 2 || base > 36) {
                signalNativeException(makeNativeError(ctx, "RangeError",
                    "toString radix must be between 2 and 36"));
                return PROTO_NONE;
            }
        }
    }
    const proto::ProtoObject* inner = unwrapBigInt(ctx, self);
    if (!inner || inner == PROTO_NONE) return PROTO_NONE;
    const proto::ProtoString* s = inner->asIntegerString(ctx, base);
    if (!s) return ctx->fromUTF8String("0");
    return s->asObject(ctx);
}

// §21.2.3.4 BigInt.prototype.valueOf.  Returns the receiver if it's a
// BigInt; otherwise TypeError.  Pre-spec: protoJS doesn't have a
// primitive vs boxed-BigInt distinction (every BigInt is the wrapper),
// so we just return self unchanged.
static const proto::ProtoObject* bigIntValueOf(proto::ProtoContext* ctx,
                                               const proto::ProtoObject* self,
                                               const proto::ParentLink*,
                                               const proto::ProtoList*,
                                               const proto::ProtoSparseList*) {
    if (!ctx) return PROTO_NONE;
    if (!isBigInt(ctx, self)) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "BigInt.prototype.valueOf requires that 'this' be a BigInt"));
        return PROTO_NONE;
    }
    return self;
}

// §21.2.2.1 BigInt.asIntN( bits, bigint ).  Truncates to a signed
// `bits`-wide two's-complement representation and returns the BigInt.
// bits is ToIndex'd (non-negative integer); 0 → always 0n.
static const proto::ProtoObject* bigIntAsIntN(proto::ProtoContext* ctx,
                                              const proto::ProtoObject*,
                                              const proto::ParentLink*,
                                              const proto::ProtoList* args,
                                              const proto::ProtoSparseList*) {
    if (!ctx) return PROTO_NONE;
    if (!args || args->getSize(ctx) < 2) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "BigInt.asIntN requires bits and bigint"));
        return PROTO_NONE;
    }
    const proto::ProtoObject* bArg = args->getAt(ctx, 0);
    long long bits = 0;
    if (bArg && bArg->isInteger(ctx)) bits = bArg->asLong(ctx);
    else if (bArg && (bArg->isDouble(ctx) || bArg->isFloat(ctx)))
        bits = static_cast<long long>(bArg->asDouble(ctx));
    if (bits < 0) {
        signalNativeException(makeNativeError(ctx, "RangeError",
            "BigInt.asIntN: bits must be non-negative"));
        return PROTO_NONE;
    }
    const proto::ProtoObject* inner = coerceToInteger(ctx, args->getAt(ctx, 1));
    if (hasCallException() || !inner || inner == PROTO_NONE) return PROTO_NONE;
    if (bits == 0) return wrapBigInt(ctx, ctx->fromInteger(0LL));
    // Compute v mod 2^bits, then if result >= 2^(bits-1) subtract 2^bits.
    // Implemented via protoCore: mask = (1 << bits) - 1; half = 1 << (bits-1).
    const proto::ProtoObject* one = ctx->fromInteger(1LL);
    const proto::ProtoObject* mask = one->shiftLeft(ctx, static_cast<int>(bits))
                                       ->subtract(ctx, one);
    const proto::ProtoObject* low = inner->bitwiseAnd(ctx, mask);
    const proto::ProtoObject* half = one->shiftLeft(ctx, static_cast<int>(bits - 1));
    if (low->compare(ctx, half) >= 0) {
        const proto::ProtoObject* full = one->shiftLeft(ctx, static_cast<int>(bits));
        low = low->subtract(ctx, full);
    }
    return wrapBigInt(ctx, low);
}

// §21.2.2.2 BigInt.asUintN( bits, bigint ).  Truncates to an unsigned
// `bits`-wide representation.
static const proto::ProtoObject* bigIntAsUintN(proto::ProtoContext* ctx,
                                               const proto::ProtoObject*,
                                               const proto::ParentLink*,
                                               const proto::ProtoList* args,
                                               const proto::ProtoSparseList*) {
    if (!ctx) return PROTO_NONE;
    if (!args || args->getSize(ctx) < 2) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "BigInt.asUintN requires bits and bigint"));
        return PROTO_NONE;
    }
    const proto::ProtoObject* bArg = args->getAt(ctx, 0);
    long long bits = 0;
    if (bArg && bArg->isInteger(ctx)) bits = bArg->asLong(ctx);
    else if (bArg && (bArg->isDouble(ctx) || bArg->isFloat(ctx)))
        bits = static_cast<long long>(bArg->asDouble(ctx));
    if (bits < 0) {
        signalNativeException(makeNativeError(ctx, "RangeError",
            "BigInt.asUintN: bits must be non-negative"));
        return PROTO_NONE;
    }
    const proto::ProtoObject* inner = coerceToInteger(ctx, args->getAt(ctx, 1));
    if (hasCallException() || !inner || inner == PROTO_NONE) return PROTO_NONE;
    if (bits == 0) return wrapBigInt(ctx, ctx->fromInteger(0LL));
    const proto::ProtoObject* one = ctx->fromInteger(1LL);
    const proto::ProtoObject* mask = one->shiftLeft(ctx, static_cast<int>(bits))
                                       ->subtract(ctx, one);
    return wrapBigInt(ctx, inner->bitwiseAnd(ctx, mask));
}

} // anonymous namespace

// ---------------------------------------------------------------------
// Public API
// ---------------------------------------------------------------------

bool isBigInt(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    if (!ctx || !v || v == PROTO_NONE) return false;
    if (v == getNullSentinel() || v == getUndefinedSentinel()) return false;
    // Primitives can't be BigInt — they don't have attributes.
    if (v->isInteger(ctx) || v->isDouble(ctx) || v->isFloat(ctx)
        || v->isBoolean(ctx) || v->isString(ctx)) return false;
    const proto::ProtoString* mk = JSSymbols::isBigInt(ctx);
    if (!mk) return false;
    return v->getAttribute(ctx, mk, true) == PROTO_TRUE;
}

const proto::ProtoObject* unwrapBigInt(proto::ProtoContext* ctx,
                                       const proto::ProtoObject* v) {
    if (!isBigInt(ctx, v)) return PROTO_NONE;
    const proto::ProtoString* vk = JSSymbols::bigIntValue(ctx);
    if (!vk) return PROTO_NONE;
    return v->getAttribute(ctx, vk, false);
}

const proto::ProtoObject* wrapBigInt(proto::ProtoContext* ctx,
                                     const proto::ProtoObject* integer) {
    if (!ctx || !integer || integer == PROTO_NONE) return PROTO_NONE;
    const proto::ProtoObject* proto = t_bigIntPrototype;
    if (!proto && ctx->space) {
        // Fallback: object prototype.  Misses the typeof marker, but
        // arithmetic dispatch can still find __bigint_value__.
        proto = ctx->space->objectPrototype;
    }
    const proto::ProtoObject* w = proto
        ? proto->newChild(ctx, true)
        : ctx->newObject(true);
    if (!w) return PROTO_NONE;
    const proto::ProtoString* vk = JSSymbols::bigIntValue(ctx);
    if (vk) w = w->setAttribute(ctx, vk, integer);
    return w;
}

// ---------------------------------------------------------------------
// Build BigInt.prototype + install Constructor
// ---------------------------------------------------------------------

static const proto::ProtoObject* installMethod(proto::ProtoContext* ctx,
                                               const proto::ProtoObject* proto,
                                               const char* name,
                                               proto::ProtoMethod fn,
                                               long long length) {
    const proto::ProtoObject* w =
        ctx->space && ctx->space->methodPrototype
            ? ctx->space->methodPrototype->newChild(ctx, true)
            : ctx->newObject(true);
    if (!w) return proto;
    const proto::ProtoString* nfk = JSSymbols::nativeFn(ctx);
    if (nfk) w = w->setAttribute(ctx, nfk, ctx->fromMethod(nullptr, fn));
    const proto::ProtoString* lk = JSSymbols::length(ctx);
    if (lk) w = w->setAttribute(ctx, lk, ctx->fromInteger(length));
    const proto::ProtoString* nk = JSSymbols::name(ctx);
    if (nk) w = w->setAttribute(ctx, nk, ctx->fromUTF8String(name));
    const proto::ProtoString* methodKey =
        ctx->fromUTF8String(name)
            ? ctx->fromUTF8String(name)->asString(ctx)
            : nullptr;
    if (methodKey) {
        const_cast<proto::ProtoObject*&>(proto) =
            const_cast<proto::ProtoObject*>(proto->setAttribute(ctx, methodKey, w));
    }
    return proto;
}

void buildBigIntPrototype(proto::ProtoSpace* /*space*/,
                          proto::ProtoContext* ctx,
                          const proto::ProtoObject* objectProto) {
    if (!ctx || !objectProto) return;
    if (t_bigIntPrototype) return;  // idempotent
    const proto::ProtoObject* proto = objectProto->newChild(ctx, true);
    if (!proto) return;
    // §13.5.3: typeof marker.  Putting it on the PROTOTYPE means every
    // wrapper inherits it without paying per-instance attribute storage,
    // and isBigInt's chain probe (getAttribute(..., true)) finds it on
    // the first walk step from the wrapper.
    const proto::ProtoString* mk = JSSymbols::isBigInt(ctx);
    if (mk) proto = proto->setAttribute(ctx, mk, PROTO_TRUE);
    // §21.2.3.1 toString, §21.2.3.4 valueOf — toString on the proto so
    // String(BigInt) and `${BigInt}` template coercion both fire it.
    proto = installMethod(ctx, proto, "toString", bigIntToString, 0);
    proto = installMethod(ctx, proto, "valueOf",  bigIntValueOf,  0);
    // §21.2.3.5 Symbol.toStringTag = "BigInt".
    {
        const proto::ProtoObject* tsk = ctx->fromUTF8String("Symbol.toStringTag");
        if (tsk) {
            const proto::ProtoString* ts = tsk->asString(ctx);
            if (ts) proto = proto->setAttribute(ctx, ts, ctx->fromUTF8String("BigInt"));
        }
    }
    t_bigIntPrototype = proto;
}

void ensureBigIntConstructor(proto::ProtoContext* ctx,
                             const proto::ProtoObject** globalRoot) {
    if (!ctx || !globalRoot || !*globalRoot) return;
    const proto::ProtoString* keyBigInt =
        ctx->fromUTF8String("BigInt")
            ? ctx->fromUTF8String("BigInt")->asString(ctx)
            : nullptr;
    if (!keyBigInt) return;
    const proto::ProtoObject* existing =
        (*globalRoot)->getAttribute(ctx, keyBigInt, false);
    if (existing && existing != PROTO_NONE) {
        // Already registered (e.g. previous wrapper-init pass).
        // Ensure the prototype is still cached for wrap/isBigInt.
        const proto::ProtoString* pk = JSSymbols::prototype(ctx);
        if (pk) {
            const proto::ProtoObject* p =
                existing->getAttribute(ctx, pk, false);
            if (p && p != PROTO_NONE) t_bigIntPrototype = p;
        }
        return;
    }
    // Build the prototype if buildBigIntPrototype hasn't run yet.
    if (!t_bigIntPrototype) {
        const proto::ProtoObject* objProto =
            ctx->space ? ctx->space->objectPrototype : nullptr;
        if (objProto) buildBigIntPrototype(ctx->space, ctx, objProto);
    }
    if (!t_bigIntPrototype) return;
    // BigInt constructor object: a function with __native_fn__ for
    // plain call and __construct__ for `new` (which throws TypeError).
    const proto::ProtoObject* fnProto =
        ctx->space && ctx->space->methodPrototype
            ? ctx->space->methodPrototype
            : ctx->space ? ctx->space->objectPrototype : nullptr;
    const proto::ProtoObject* ctor = fnProto
        ? fnProto->newChild(ctx, true)
        : ctx->newObject(true);
    if (!ctor) return;
    const proto::ProtoString* nfk = JSSymbols::nativeFn(ctx);
    if (nfk) ctor = ctor->setAttribute(ctx, nfk,
        ctx->fromMethod(nullptr, bigIntCtor));
    const proto::ProtoString* cok = JSSymbols::construct(ctx);
    if (cok) ctor = ctor->setAttribute(ctx, cok,
        ctx->fromMethod(nullptr, bigIntConstruct));
    const proto::ProtoString* lk = JSSymbols::length(ctx);
    if (lk) ctor = ctor->setAttribute(ctx, lk, ctx->fromInteger(1LL));
    const proto::ProtoString* nk = JSSymbols::name(ctx);
    if (nk) ctor = ctor->setAttribute(ctx, nk, ctx->fromUTF8String("BigInt"));
    const proto::ProtoString* pk = JSSymbols::prototype(ctx);
    if (pk) ctor = ctor->setAttribute(ctx, pk, t_bigIntPrototype);
    // Back-ref on the prototype.
    {
        const proto::ProtoString* ck = JSSymbols::constructor(ctx);
        if (ck && t_bigIntPrototype) {
            const_cast<proto::ProtoObject*&>(t_bigIntPrototype) =
                const_cast<proto::ProtoObject*>(
                    t_bigIntPrototype->setAttribute(ctx, ck, ctor));
        }
    }
    // BigInt.asIntN / asUintN statics.
    ctor = installMethod(ctx, ctor, "asIntN",  bigIntAsIntN,  2);
    ctor = installMethod(ctx, ctor, "asUintN", bigIntAsUintN, 2);
    *globalRoot = (*globalRoot)->setAttribute(ctx, keyBigInt, ctor);
}

} // namespace protojs
