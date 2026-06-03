#include "MathBuiltin.h"
#include "JSSymbols.h"
#include "runtime/ProtoInterpreter.h"
#include "headers/protoCore.h"
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <limits>

namespace protojs {

namespace {

// Helper to convert an argument to double.
static double argToDouble(proto::ProtoContext* ctx, const proto::ProtoList* args, unsigned idx,
                          double defaultVal = std::numeric_limits<double>::quiet_NaN()) {
    if (!args || static_cast<unsigned>(args->getSize(ctx)) <= idx) return defaultVal;
    const proto::ProtoObject* a = args->getAt(ctx, static_cast<int>(idx));
    if (!a || a == PROTO_NONE) return std::numeric_limits<double>::quiet_NaN();
    if (a->isInteger(ctx)) return static_cast<double>(a->asLong(ctx));
    if (a->isDouble(ctx) || a->isFloat(ctx)) return a->asDouble(ctx);
    if (a->isBoolean(ctx)) return a->asBoolean(ctx) ? 1.0 : 0.0;
    // Fall back to spec ToNumber (valueOf/toString chain for objects).
    const proto::ProtoObject* n = jsToNumber(ctx, a);
    if (!n || n == PROTO_NONE) return std::numeric_limits<double>::quiet_NaN();
    if (n->isInteger(ctx)) return static_cast<double>(n->asLong(ctx));
    if (n->isDouble(ctx) || n->isFloat(ctx)) return n->asDouble(ctx);
    return std::numeric_limits<double>::quiet_NaN();
}

static const proto::ProtoObject* makeDouble(proto::ProtoContext* ctx, double d) {
    if (d == std::trunc(d) && std::isfinite(d) &&
        d >= -9.007199254740992e15 && d <= 9.007199254740992e15)
        return ctx->fromInteger(static_cast<long long>(d));
    return ctx->fromDouble(d);
}

// --- Math method implementations ---

#define MATH_ONE_ARG(name, expr) \
static const proto::ProtoObject* math##name( \
    proto::ProtoContext* ctx, const proto::ProtoObject*, \
    const proto::ParentLink*, const proto::ProtoList* args, \
    const proto::ProtoSparseList*) \
{ \
    double x = argToDouble(ctx, args, 0); \
    return ctx->fromDouble(expr); \
}

MATH_ONE_ARG(Abs,   std::abs(x))
MATH_ONE_ARG(Acos,  std::acos(x))
MATH_ONE_ARG(Acosh, std::acosh(x))
MATH_ONE_ARG(Asin,  std::asin(x))
MATH_ONE_ARG(Asinh, std::asinh(x))
MATH_ONE_ARG(Atan,  std::atan(x))
MATH_ONE_ARG(Atanh, std::atanh(x))
MATH_ONE_ARG(Cbrt,  std::cbrt(x))
MATH_ONE_ARG(Ceil,  std::ceil(x))
MATH_ONE_ARG(Cos,   std::cos(x))
MATH_ONE_ARG(Cosh,  std::cosh(x))
MATH_ONE_ARG(Exp,   std::exp(x))
MATH_ONE_ARG(Expm1, std::expm1(x))
MATH_ONE_ARG(Floor, std::floor(x))
MATH_ONE_ARG(Log,   std::log(x))
MATH_ONE_ARG(Log1p, std::log1p(x))
MATH_ONE_ARG(Log2,  std::log2(x))
MATH_ONE_ARG(Log10, std::log10(x))
MATH_ONE_ARG(Sin,   std::sin(x))
MATH_ONE_ARG(Sinh,  std::sinh(x))
MATH_ONE_ARG(Sqrt,  std::sqrt(x))
MATH_ONE_ARG(Tan,   std::tan(x))
MATH_ONE_ARG(Tanh,  std::tanh(x))
MATH_ONE_ARG(Trunc, std::trunc(x))

#undef MATH_ONE_ARG

static const proto::ProtoObject* mathSign(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    double x = argToDouble(ctx, args, 0);
    if (std::isnan(x)) return ctx->fromDouble(x);
    if (x > 0.0) return makeDouble(ctx, 1.0);
    if (x < 0.0) return makeDouble(ctx, -1.0);
    // Spec §20.3.2.29: preserve the sign of zero. signbit catches -0.
    // Bypass makeDouble for -0.0 because it normalises integral doubles
    // through fromInteger, which loses the sign bit.
    if (std::signbit(x)) return ctx->fromDouble(-0.0);
    return makeDouble(ctx, 0.0);
}

static const proto::ProtoObject* mathRound(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    double x = argToDouble(ctx, args, 0);
    // ECMA-262 §21.3.2.28: preserve the sign of -0 and treat the
    // (-0.5, 0) range as rounding to -0. makeDouble normalises 0
    // through fromInteger which loses the sign bit, so cases where
    // the result is -0 must take the fromDouble path explicitly.
    if (std::isnan(x))   return ctx->fromDouble(x);
    if (std::isinf(x))   return ctx->fromDouble(x);
    if (x == 0.0)        return ctx->fromDouble(x);  // preserves -0
    if (x > 0 && x < 0.5)   return ctx->fromDouble(0.0);
    if (x < 0 && x >= -0.5) return ctx->fromDouble(-0.0);
    // Ties go toward +Infinity (floor(x + 0.5)).
    return makeDouble(ctx, std::floor(x + 0.5));
}

static const proto::ProtoObject* mathFround(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    double x = argToDouble(ctx, args, 0);
    return ctx->fromDouble(static_cast<double>(static_cast<float>(x)));
}

// ECMA-262 §7.1.6 ToUint32: NaN, ±0, ±Infinity all become +0;
// otherwise truncate toward zero and apply modulo 2^32.
static uint32_t toUint32(double x) {
    if (std::isnan(x) || std::isinf(x) || x == 0.0) return 0;
    double posInt = std::trunc(x);
    double modVal = std::fmod(posInt, 4294967296.0);
    if (modVal < 0) modVal += 4294967296.0;
    return static_cast<uint32_t>(modVal);
}

static const proto::ProtoObject* mathClz32(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    double x = argToDouble(ctx, args, 0, 0.0);
    uint32_t n = toUint32(x);
    if (n == 0) return ctx->fromInteger(32);
    int cnt = 0;
    while (!(n & 0x80000000u)) { cnt++; n <<= 1; }
    return ctx->fromInteger(cnt);
}

static const proto::ProtoObject* mathImul(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    double a = argToDouble(ctx, args, 0, 0.0);
    double b = argToDouble(ctx, args, 1, 0.0);
    int32_t ia = static_cast<int32_t>(static_cast<uint32_t>(a));
    int32_t ib = static_cast<int32_t>(static_cast<uint32_t>(b));
    return ctx->fromInteger(static_cast<long long>(ia * ib));
}

static const proto::ProtoObject* mathAtan2(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    double y = argToDouble(ctx, args, 0);
    double x = argToDouble(ctx, args, 1);
    return ctx->fromDouble(std::atan2(y, x));
}

static const proto::ProtoObject* mathPow(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    double base = argToDouble(ctx, args, 0);
    double exp  = argToDouble(ctx, args, 1);
    // ECMA-262 §21.3.2.24 step 5: if exponent is NaN, result is NaN —
    // independent of base. glibc's std::pow violates this for the
    // IEEE-754 base==1 special case (pow(1, NaN) == 1). Spec also
    // requires abs(base) == 1 with ±Infinity exponent to yield NaN.
    if (std::isnan(exp)) {
        return ctx->fromDouble(std::numeric_limits<double>::quiet_NaN());
    }
    if (std::isinf(exp) && std::abs(base) == 1.0) {
        return ctx->fromDouble(std::numeric_limits<double>::quiet_NaN());
    }
    return ctx->fromDouble(std::pow(base, exp));
}

static const proto::ProtoObject* mathHypot(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    unsigned long argc = args ? static_cast<unsigned long>(args->getSize(ctx)) : 0;
    // ECMA-262 §21.3.2.18: if ANY coerced argument is ±Infinity,
    // return +Infinity — even when other arguments are NaN. The
    // previous summation approach yielded NaN whenever an Infinity
    // and a NaN both appeared because Infinity² + NaN² = NaN.
    bool sawNaN = false;
    bool sawInf = false;
    for (unsigned long i = 0; i < argc; i++) {
        double v = argToDouble(ctx, args, static_cast<unsigned>(i), 0.0);
        if (std::isnan(v)) sawNaN = true;
        else if (std::isinf(v)) sawInf = true;
    }
    if (sawInf) return ctx->fromDouble(std::numeric_limits<double>::infinity());
    if (sawNaN) return ctx->fromDouble(std::numeric_limits<double>::quiet_NaN());
    double sum = 0.0;
    for (unsigned long i = 0; i < argc; i++) {
        double v = argToDouble(ctx, args, static_cast<unsigned>(i), 0.0);
        sum += v * v;
    }
    return ctx->fromDouble(std::sqrt(sum));
}

static const proto::ProtoObject* mathMax(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    unsigned long argc = args ? static_cast<unsigned long>(args->getSize(ctx)) : 0;
    if (argc == 0) return ctx->fromDouble(-std::numeric_limits<double>::infinity());
    // Spec §20.3.2.24 step 1-2: coerce every argument first via ToNumber,
    // even when an early NaN would short-circuit the result. Mathematical
    // pass happens in a separate sweep.
    bool anyNaN = false;
    double result = -std::numeric_limits<double>::infinity();
    bool sawPositiveZero = false;
    for (unsigned long i = 0; i < argc; i++) {
        double v = argToDouble(ctx, args, static_cast<unsigned>(i));
        if (std::isnan(v)) { anyNaN = true; continue; }
        if (v == 0.0 && !std::signbit(v)) sawPositiveZero = true;
        if (v > result) result = v;
    }
    if (anyNaN) return ctx->fromDouble(std::numeric_limits<double>::quiet_NaN());
    if (result == 0.0) {
        return ctx->fromDouble(sawPositiveZero ? 0.0 : -0.0);
    }
    return makeDouble(ctx, result);
}

static const proto::ProtoObject* mathMin(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    unsigned long argc = args ? static_cast<unsigned long>(args->getSize(ctx)) : 0;
    if (argc == 0) return ctx->fromDouble(std::numeric_limits<double>::infinity());
    bool anyNaN = false;
    double result = std::numeric_limits<double>::infinity();
    bool sawNegativeZero = false;
    for (unsigned long i = 0; i < argc; i++) {
        double v = argToDouble(ctx, args, static_cast<unsigned>(i));
        if (std::isnan(v)) { anyNaN = true; continue; }
        if (v == 0.0 && std::signbit(v)) sawNegativeZero = true;
        if (v < result) result = v;
    }
    if (anyNaN) return ctx->fromDouble(std::numeric_limits<double>::quiet_NaN());
    if (result == 0.0) {
        return ctx->fromDouble(sawNegativeZero ? -0.0 : 0.0);
    }
    return makeDouble(ctx, result);
}

static const proto::ProtoObject* mathRandom(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList*,
    const proto::ProtoSparseList*)
{
    // Simple pseudo-random [0, 1)
    return ctx->fromDouble(static_cast<double>(std::rand()) / (static_cast<double>(RAND_MAX) + 1.0));
}

} // anonymous namespace

void ensureMathObject(proto::ProtoContext* ctx,
                      const proto::ProtoObject** globalRoot) {
    if (!ctx || !globalRoot || !*globalRoot) return;

    const proto::ProtoString* keyMath = JSSymbols::Math(ctx);
    if (!keyMath) return;

    const proto::ProtoObject* existing = (*globalRoot)->getAttribute(ctx, keyMath, false);
    if (existing && existing != PROTO_NONE) return;

    // Mutable so user-level assignments like `Math.x = y` and
    // `delete Math.sqrt` actually persist. An immutable Math snapshot
    // would split on every setAttribute, leaving globalRoot.Math
    // pointing at the original — `delete Math.sqrt` returns true but
    // Math.sqrt would still be reachable through the old snapshot.
    const proto::ProtoObject* math = ctx->newObject(true);
    if (!math) return;

    // Each Math method is exposed through a function-object wrapper so
    // it carries the spec-mandated `name` and `length` properties (with
    // descriptor 0x2 = configurable, non-writable, non-enumerable). The
    // raw ProtoMethod cell can't hold arbitrary attributes, so a thin
    // wrapper object inheriting Function.prototype is needed instead.
    auto reg = [&](const char* name, proto::ProtoMethod fn, long long length) {
        const proto::ProtoString* key = ctx->fromUTF8String(name)->asString(ctx);
        if (!key) return;
        const proto::ProtoObject* wrapper = ctx->space->methodPrototype
            ? ctx->space->methodPrototype->newChild(ctx, true)
            : ctx->newObject(true);
        const proto::ProtoString* nfk = JSSymbols::nativeFn(ctx);
        if (nfk) wrapper = wrapper->setAttribute(ctx, nfk, ctx->fromMethod(nullptr, fn));
        const proto::ProtoString* lenk = JSSymbols::length(ctx);
        if (lenk) {
            wrapper = wrapper->setAttribute(ctx, lenk, ctx->fromInteger(length));
            const proto::ProtoObject* pdlo = ctx->fromUTF8String("__pd_length__");
            const proto::ProtoString* pdlk = pdlo ? pdlo->asString(ctx) : nullptr;
            if (pdlk) wrapper = wrapper->setAttribute(ctx, pdlk, ctx->fromInteger(0x2LL));
        }
        const proto::ProtoString* nmk = JSSymbols::name(ctx);
        if (nmk) {
            wrapper = wrapper->setAttribute(ctx, nmk, ctx->fromUTF8String(name));
            const proto::ProtoObject* pdno = ctx->fromUTF8String("__pd_name__");
            const proto::ProtoString* pdnk = pdno ? pdno->asString(ctx) : nullptr;
            if (pdnk) wrapper = wrapper->setAttribute(ctx, pdnk, ctx->fromInteger(0x2LL));
        }
        math = math->setAttribute(ctx, key, wrapper);
        // Math methods themselves carry the standard built-in descriptor
        // {writable:true, enumerable:false, configurable:true} → 0x3.
        std::string pd = std::string("__pd_") + name + "__";
        const proto::ProtoObject* pko = ctx->fromUTF8String(pd.c_str());
        const proto::ProtoString* pdk = pko ? pko->asString(ctx) : nullptr;
        if (pdk) math = math->setAttribute(ctx, pdk, ctx->fromInteger(0x3LL));
    };

    auto setConst = [&](const char* name, double val) {
        const proto::ProtoString* key = ctx->fromUTF8String(name)->asString(ctx);
        if (!key) return;
        math = math->setAttribute(ctx, key, ctx->fromDouble(val));
        // Math constants are spec'd { writable:false, enumerable:false,
        // configurable:false } — descriptor bits = 0x0.  Without setting
        // __pd_<name>__ they default to writable/enumerable/configurable,
        // so `delete Math.PI` returned true and Math.PI = 99 silently
        // overwrote the value.
        std::string pd = std::string("__pd_") + name + "__";
        const proto::ProtoObject* pko = ctx->fromUTF8String(pd.c_str());
        const proto::ProtoString* pdk = pko ? pko->asString(ctx) : nullptr;
        if (pdk) math = math->setAttribute(ctx, pdk, ctx->fromInteger(0LL));
    };

    // Constants
    setConst("PI",      M_PI);
    setConst("E",       M_E);
    setConst("LN2",     M_LN2);
    setConst("LN10",    M_LN10);
    setConst("LOG2E",   M_LOG2E);
    setConst("LOG10E",  M_LOG10E);
    setConst("SQRT1_2", M_SQRT1_2);
    setConst("SQRT2",   M_SQRT2);

    // Methods. Spec lengths per §21.3.2: most unary methods are 1;
    // atan2/imul/pow are 2; hypot/max/min are 2 (variadic but the
    // spec's "length" is the formal-parameter count); random is 0.
    reg("abs",    mathAbs,    1);
    reg("acos",   mathAcos,   1);
    reg("acosh",  mathAcosh,  1);
    reg("asin",   mathAsin,   1);
    reg("asinh",  mathAsinh,  1);
    reg("atan",   mathAtan,   1);
    reg("atanh",  mathAtanh,  1);
    reg("atan2",  mathAtan2,  2);
    reg("cbrt",   mathCbrt,   1);
    reg("ceil",   mathCeil,   1);
    reg("clz32",  mathClz32,  1);
    reg("cos",    mathCos,    1);
    reg("cosh",   mathCosh,   1);
    reg("exp",    mathExp,    1);
    reg("expm1",  mathExpm1,  1);
    reg("floor",  mathFloor,  1);
    reg("fround", mathFround, 1);
    reg("hypot",  mathHypot,  2);
    reg("imul",   mathImul,   2);
    reg("log",    mathLog,    1);
    reg("log1p",  mathLog1p,  1);
    reg("log2",   mathLog2,   1);
    reg("log10",  mathLog10,  1);
    reg("max",    mathMax,    2);
    reg("min",    mathMin,    2);
    reg("pow",    mathPow,    2);
    reg("random", mathRandom, 0);
    reg("round",  mathRound,  1);
    reg("sign",   mathSign,   1);
    reg("sin",    mathSin,    1);
    reg("sinh",   mathSinh,   1);
    reg("sqrt",   mathSqrt,   1);
    reg("tan",    mathTan,    1);
    reg("tanh",   mathTanh,   1);
    reg("trunc",  mathTrunc,  1);

    // Set Symbol.toStringTag so Object.prototype.toString.call(Math)
    // === "[object Math]". Install under both the internal sidecar
    // and the user-visible key (see Set / Map / Promise fixes).
    {
        const proto::ProtoString* tagKey = JSSymbols::toStringTag(ctx);
        if (tagKey)
            math = math->setAttribute(ctx, tagKey, ctx->fromUTF8String("Math"));
        const proto::ProtoString* userKey = JSSymbols::symbolToStringTag(ctx);
        if (userKey) {
            math = math->setAttribute(ctx, userKey, ctx->fromUTF8String("Math"));
            const proto::ProtoObject* pdko = ctx->fromUTF8String("__pd_Symbol.toStringTag__");
            const proto::ProtoString* pdks = pdko ? pdko->asString(ctx) : nullptr;
            if (pdks) math = math->setAttribute(ctx, pdks, ctx->fromInteger(0x2LL));
        }
    }

    *globalRoot = (*globalRoot)->setAttribute(ctx, keyMath, math);
}

} // namespace protojs
