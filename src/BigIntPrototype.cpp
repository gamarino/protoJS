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
    // Object (non-BigInt-wrapper) → call ToPrimitive(value, hint=number)
    // and recurse.  §7.1.13 step 1 / §7.1.1 ToPrimitive.  We probe
    // @@toPrimitive first (with hint "number"), then OrdinaryToPrimitive
    // (valueOf, toString).
    if (!v->isString(ctx)) {
        // Try @@toPrimitive("number").
        const proto::ProtoObject* tpKo = ctx->fromUTF8String("Symbol.toPrimitive");
        const proto::ProtoString* tpK = tpKo ? tpKo->asString(ctx) : nullptr;
        const proto::ProtoObject* tpFn = tpK ? v->getAttribute(ctx, tpK, true) : nullptr;
        if (tpFn && tpFn != PROTO_NONE && tpFn != getUndefinedSentinel()
            && tpFn != getNullSentinel()) {
            const proto::ProtoList* hintArgs = ctx->newList();
            hintArgs = hintArgs->appendLast(ctx, ctx->fromUTF8String("number"));
            const proto::ProtoObject* prim = callJSFunction(ctx, tpFn, v, hintArgs);
            if (hasCallException()) return PROTO_NONE;
            return coerceToInteger(ctx, prim);
        }
        // OrdinaryToPrimitive("number"): valueOf then toString.
        const proto::ProtoString* voK = JSSymbols::valueOf(ctx);
        if (voK) {
            const proto::ProtoObject* voFn = v->getAttribute(ctx, voK, true);
            if (voFn && voFn != PROTO_NONE && voFn != getUndefinedSentinel()) {
                const proto::ProtoObject* r = callJSFunction(ctx, voFn, v, ctx->newList());
                if (hasCallException()) return PROTO_NONE;
                if (r && r != PROTO_NONE
                    && (r->isInteger(ctx) || r->isDouble(ctx) || r->isFloat(ctx)
                        || r->isBoolean(ctx) || r->isString(ctx)
                        || (JSSymbols::isBigInt(ctx) && r->getAttribute(ctx, JSSymbols::isBigInt(ctx), true) == PROTO_TRUE))) {
                    return coerceToInteger(ctx, r);
                }
            }
        }
        const proto::ProtoString* tsK = JSSymbols::toString(ctx);
        if (tsK) {
            const proto::ProtoObject* tsFn = v->getAttribute(ctx, tsK, true);
            if (tsFn && tsFn != PROTO_NONE && tsFn != getUndefinedSentinel()) {
                const proto::ProtoObject* r = callJSFunction(ctx, tsFn, v, ctx->newList());
                if (hasCallException()) return PROTO_NONE;
                if (r && r != PROTO_NONE
                    && (r->isInteger(ctx) || r->isDouble(ctx) || r->isFloat(ctx)
                        || r->isBoolean(ctx) || r->isString(ctx)
                        || (JSSymbols::isBigInt(ctx) && r->getAttribute(ctx, JSSymbols::isBigInt(ctx), true) == PROTO_TRUE))) {
                    return coerceToInteger(ctx, r);
                }
            }
        }
        // Both methods absent / returned non-primitive → spec §7.1.1.1 step 6.
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Cannot convert object to a primitive BigInt"));
        return PROTO_NONE;
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
        // §7.1.14 StringToBigInt: the trimmed body must contain ONLY
        // digits valid for the radix.  protoCore's fromString throws
        // std::invalid_argument on malformed input rather than returning
        // nullptr, so pre-validate to avoid the crash and surface the
        // spec-mandated SyntaxError instead.
        auto isValidDigit = [&](char c) -> bool {
            if (base == 16)  return (c >= '0' && c <= '9')
                                 || (c >= 'a' && c <= 'f')
                                 || (c >= 'A' && c <= 'F');
            if (base == 10)  return c >= '0' && c <= '9';
            if (base == 8)   return c >= '0' && c <= '7';
            if (base == 2)   return c == '0' || c == '1';
            return false;
        };
        for (char c : body) {
            if (!isValidDigit(c)) {
                signalNativeException(makeNativeError(ctx, "SyntaxError",
                    "Cannot convert string to a BigInt"));
                return PROTO_NONE;
            }
        }
        const proto::ProtoObject* parsed = nullptr;
        try {
            parsed = ctx->fromString(body.c_str(), base);
        } catch (...) {
            signalNativeException(makeNativeError(ctx, "SyntaxError",
                "Cannot convert string to a BigInt"));
            return PROTO_NONE;
        }
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
    // §21.2.3.3 BigInt.prototype.toString step 2..5:
    //   if radix is undefined, radixNumber = 10;
    //   else radixNumber = ToIntegerOrInfinity(radix);
    //   if radixNumber < 2 or > 36 → RangeError.
    // ToIntegerOrInfinity routes through ToNumber, so null → 0,
    // boolean → 0/1, string → parse Number, etc. — all of which
    // produce out-of-range radixes that must throw.
    int base = 10;
    if (args && args->getSize(ctx) >= 1) {
        const proto::ProtoObject* r = args->getAt(ctx, 0);
        if (r && r != getUndefinedSentinel()) {
            const proto::ProtoObject* n = jsToNumber(ctx, r);
            if (hasCallException()) return PROTO_NONE;
            double d = 0.0;
            if (n && n->isInteger(ctx)) d = static_cast<double>(n->asLong(ctx));
            else if (n && (n->isDouble(ctx) || n->isFloat(ctx))) d = n->asDouble(ctx);
            if (std::isnan(d)) d = 0.0;
            d = std::trunc(d);
            base = static_cast<int>(d);
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

// §7.1.22 ToIndex(value): coerce to a non-negative integer in
// [0, 2^53-1].  Used by BigInt.asIntN / asUintN's bits parameter.
// undefined → 0; otherwise ToIntegerOrInfinity; then range-check.
static long long toIndexLL(proto::ProtoContext* ctx,
                           const proto::ProtoObject* v) {
    if (!v || v == PROTO_NONE || v == getUndefinedSentinel())
        return 0;
    const proto::ProtoObject* n = jsToNumber(ctx, v);
    if (hasCallException()) return -1;
    if (!n || n == PROTO_NONE) return 0;
    double d = 0.0;
    if (n->isInteger(ctx)) d = static_cast<double>(n->asLong(ctx));
    else if (n->isDouble(ctx) || n->isFloat(ctx)) d = n->asDouble(ctx);
    if (std::isnan(d) || d <= 0.0) return 0;
    if (std::isinf(d)) {
        signalNativeException(makeNativeError(ctx, "RangeError",
            "Index out of range"));
        return -1;
    }
    d = std::trunc(d);
    if (d > 9007199254740991.0) {
        signalNativeException(makeNativeError(ctx, "RangeError",
            "Index out of range"));
        return -1;
    }
    return static_cast<long long>(d);
}

// §21.2.2.1 BigInt.asIntN( bits, bigint ).  Truncates to a signed
// `bits`-wide two's-complement representation and returns the BigInt.
// Spec order: ToIndex(bits) FIRST, ToBigInt(bigint) SECOND
// (asIntN/order-of-steps.js verifies the valueOf call order).
static const proto::ProtoObject* bigIntAsIntN(proto::ProtoContext* ctx,
                                              const proto::ProtoObject*,
                                              const proto::ParentLink*,
                                              const proto::ProtoList* args,
                                              const proto::ProtoSparseList*) {
    if (!ctx) return PROTO_NONE;
    int argc = args ? args->getSize(ctx) : 0;
    const proto::ProtoObject* bArg = argc > 0 ? args->getAt(ctx, 0)
                                              : getUndefinedSentinel();
    long long bits = toIndexLL(ctx, bArg);
    if (hasCallException() || bits < 0) return PROTO_NONE;
    const proto::ProtoObject* vArg = argc > 1 ? args->getAt(ctx, 1)
                                              : getUndefinedSentinel();
    const proto::ProtoObject* inner = coerceToInteger(ctx, vArg);
    if (hasCallException() || !inner || inner == PROTO_NONE) return PROTO_NONE;
    if (bits == 0) return wrapBigInt(ctx, ctx->fromInteger(0LL));
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

// §21.2.2.2 BigInt.asUintN( bits, bigint ).  Same spec ordering as asIntN.
static const proto::ProtoObject* bigIntAsUintN(proto::ProtoContext* ctx,
                                               const proto::ProtoObject*,
                                               const proto::ParentLink*,
                                               const proto::ProtoList* args,
                                               const proto::ProtoSparseList*) {
    if (!ctx) return PROTO_NONE;
    int argc = args ? args->getSize(ctx) : 0;
    const proto::ProtoObject* bArg = argc > 0 ? args->getAt(ctx, 0)
                                              : getUndefinedSentinel();
    long long bits = toIndexLL(ctx, bArg);
    if (hasCallException() || bits < 0) return PROTO_NONE;
    const proto::ProtoObject* vArg = argc > 1 ? args->getAt(ctx, 1)
                                              : getUndefinedSentinel();
    const proto::ProtoObject* inner = coerceToInteger(ctx, vArg);
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
    bool needsOwnMarker = false;
    if (!proto && ctx->space) {
        // ensureBigIntConstructor hasn't run yet (typical case: a BigInt
        // came in via TypeBridge::fromJS during cpool init, before the
        // first runBytecode entry).  Fall back to objectPrototype and
        // stamp the marker on the wrapper itself so typeof still
        // reports "bigint" before the proper prototype is published.
        proto = ctx->space->objectPrototype;
        needsOwnMarker = true;
    }
    const proto::ProtoObject* w = proto
        ? proto->newChild(ctx, true)
        : ctx->newObject(true);
    if (!w) return PROTO_NONE;
    const proto::ProtoString* vk = JSSymbols::bigIntValue(ctx);
    if (vk) w = w->setAttribute(ctx, vk, integer);
    if (needsOwnMarker) {
        const proto::ProtoString* mk = JSSymbols::isBigInt(ctx);
        if (mk) w = w->setAttribute(ctx, mk, PROTO_TRUE);
    }
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
    if (lk) {
        w = w->setAttribute(ctx, lk, ctx->fromInteger(length));
        const proto::ProtoString* pdlk = JSSymbols::pdLength(ctx);
        if (pdlk) w = w->setAttribute(ctx, pdlk, ctx->fromInteger(0x2LL));
    }
    const proto::ProtoString* nk = JSSymbols::name(ctx);
    if (nk) {
        w = w->setAttribute(ctx, nk, ctx->fromUTF8String(name));
        const proto::ProtoString* pdnk = JSSymbols::pdName(ctx);
        if (pdnk) w = w->setAttribute(ctx, pdnk, ctx->fromInteger(0x2LL));
    }
    const proto::ProtoString* methodKey =
        ctx->fromUTF8String(name)
            ? ctx->fromUTF8String(name)->asString(ctx)
            : nullptr;
    if (methodKey) {
        const_cast<proto::ProtoObject*&>(proto) =
            const_cast<proto::ProtoObject*>(proto->setAttribute(ctx, methodKey, w));
        // §17: own methods on builtin prototype default to
        // {writable:true, enumerable:false, configurable:true} = 0x3.
        std::string pdStr = std::string("__pd_") + name + "__";
        const proto::ProtoObject* pdko = ctx->fromUTF8String(pdStr.c_str());
        const proto::ProtoString* pdk = pdko ? pdko->asString(ctx) : nullptr;
        if (pdk) {
            const_cast<proto::ProtoObject*&>(proto) =
                const_cast<proto::ProtoObject*>(proto->setAttribute(ctx, pdk, ctx->fromInteger(0x3LL)));
        }
    }
    return proto;
}

// Per-thread flag: have we installed toString/valueOf yet?  These need
// methodPrototype (= Function.prototype) so the resulting wrapper has
// .call / .apply / .bind reachable via the chain.  When wrapper init
// runs buildBigIntPrototype, methodPrototype may not be wired yet, so
// we publish just the type marker first and lazily install the
// methods when ensureBigIntConstructor runs (post-init).
static thread_local bool t_bigIntMethodsInstalled = false;

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
    // §21.2.3.1 toString, §21.2.3.4 valueOf — only install if
    // methodPrototype is ready; ensureBigIntConstructor will retry.
    if (ctx->space && ctx->space->methodPrototype) {
        proto = installMethod(ctx, proto, "toString", bigIntToString, 0);
        proto = installMethod(ctx, proto, "valueOf",  bigIntValueOf,  0);
        t_bigIntMethodsInstalled = true;
    }
    // §21.2.3.5 Symbol.toStringTag = "BigInt" with descriptor
    // {W:false, E:false, C:true} = 0x2.
    {
        const proto::ProtoObject* tsk = ctx->fromUTF8String("Symbol.toStringTag");
        if (tsk) {
            const proto::ProtoString* ts = tsk->asString(ctx);
            if (ts) {
                proto = proto->setAttribute(ctx, ts, ctx->fromUTF8String("BigInt"));
                const proto::ProtoObject* pdko = ctx->fromUTF8String("__pd_Symbol.toStringTag__");
                const proto::ProtoString* pdk = pdko ? pdko->asString(ctx) : nullptr;
                if (pdk) proto = proto->setAttribute(ctx, pdk, ctx->fromInteger(0x2LL));
            }
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
    // Note: an existing "BigInt" entry on the global root may be the
    // unimplemented-ctor stub installed by ProtoInterpreter::init.  We
    // always overwrite — never adopt the stub's prototype, because that
    // prototype lacks our toString / valueOf / __is_bigint__ marker.
    (void)(*globalRoot)->getAttribute(ctx, keyBigInt, false);  // unused
    // Build the prototype if buildBigIntPrototype hasn't run yet.
    if (!t_bigIntPrototype) {
        const proto::ProtoObject* objProto =
            ctx->space ? ctx->space->objectPrototype : nullptr;
        if (objProto) buildBigIntPrototype(ctx->space, ctx, objProto);
    }
    if (!t_bigIntPrototype) return;
    // (Re)install toString / valueOf against the now-fully-populated
    // methodPrototype.  buildBigIntPrototype's early-init attempt may
    // have built wrappers parented to a partially-populated
    // methodPrototype (no .call/.apply/.bind yet); refresh now so the
    // wrapper chain reaches the full Function.prototype.
    if (ctx->space && ctx->space->methodPrototype) {
        t_bigIntPrototype = installMethod(ctx, t_bigIntPrototype, "toString",
                                          bigIntToString, 0);
        t_bigIntPrototype = installMethod(ctx, t_bigIntPrototype, "valueOf",
                                          bigIntValueOf, 0);
        t_bigIntMethodsInstalled = true;
    }
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
    // §17: builtin function objects have length / name with descriptor
    // {writable:false, enumerable:false, configurable:true} (0x2);
    // prototype is non-writable / non-enumerable / non-configurable (0x0).
    const proto::ProtoString* lk = JSSymbols::length(ctx);
    if (lk) {
        ctor = ctor->setAttribute(ctx, lk, ctx->fromInteger(1LL));
        const proto::ProtoString* pdlk = JSSymbols::pdLength(ctx);
        if (pdlk) ctor = ctor->setAttribute(ctx, pdlk, ctx->fromInteger(0x2LL));
    }
    const proto::ProtoString* nk = JSSymbols::name(ctx);
    if (nk) {
        ctor = ctor->setAttribute(ctx, nk, ctx->fromUTF8String("BigInt"));
        const proto::ProtoString* pdnk = JSSymbols::pdName(ctx);
        if (pdnk) ctor = ctor->setAttribute(ctx, pdnk, ctx->fromInteger(0x2LL));
    }
    const proto::ProtoString* pk = JSSymbols::prototype(ctx);
    if (pk) ctor = ctor->setAttribute(ctx, pk, t_bigIntPrototype);
    // Back-ref on the prototype, with the spec-mandated descriptor
    // {W:true, E:false, C:true} = 0x3 (per
    // built-ins/BigInt/prototype/constructor.js).
    {
        const proto::ProtoString* ck = JSSymbols::constructor(ctx);
        if (ck && t_bigIntPrototype) {
            const_cast<proto::ProtoObject*&>(t_bigIntPrototype) =
                const_cast<proto::ProtoObject*>(
                    t_bigIntPrototype->setAttribute(ctx, ck, ctor));
            const proto::ProtoString* pdck = JSSymbols::pdConstructor(ctx);
            if (pdck) {
                const_cast<proto::ProtoObject*&>(t_bigIntPrototype) =
                    const_cast<proto::ProtoObject*>(
                        t_bigIntPrototype->setAttribute(ctx, pdck, ctx->fromInteger(0x3LL)));
            }
        }
    }
    // BigInt.asIntN / asUintN statics.
    ctor = installMethod(ctx, ctor, "asIntN",  bigIntAsIntN,  2);
    ctor = installMethod(ctx, ctor, "asUintN", bigIntAsUintN, 2);
    *globalRoot = (*globalRoot)->setAttribute(ctx, keyBigInt, ctor);
    // §17: global builtin constructors are {W:true, E:false, C:true} = 0x3.
    {
        const proto::ProtoObject* pdko = ctx->fromUTF8String("__pd_BigInt__");
        const proto::ProtoString* pdk = pdko ? pdko->asString(ctx) : nullptr;
        if (pdk) *globalRoot = (*globalRoot)->setAttribute(ctx, pdk, ctx->fromInteger(0x3LL));
    }
}

} // namespace protojs
