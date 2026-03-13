#include "MathBuiltin.h"
#include "ProtoJSStringCache.h"
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
    return makeDouble(ctx, x > 0.0 ? 1.0 : (x < 0.0 ? -1.0 : 0.0));
}

static const proto::ProtoObject* mathRound(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    double x = argToDouble(ctx, args, 0);
    // JS Math.round: ties go toward +Infinity (floor(x + 0.5))
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

static const proto::ProtoObject* mathClz32(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    double x = argToDouble(ctx, args, 0, 0.0);
    uint32_t n = static_cast<uint32_t>(static_cast<int32_t>(x));
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
    return ctx->fromDouble(std::pow(base, exp));
}

static const proto::ProtoObject* mathHypot(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    unsigned long argc = args ? static_cast<unsigned long>(args->getSize(ctx)) : 0;
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
    double result = -std::numeric_limits<double>::infinity();
    for (unsigned long i = 0; i < argc; i++) {
        double v = argToDouble(ctx, args, static_cast<unsigned>(i));
        if (std::isnan(v)) return ctx->fromDouble(v);
        if (v > result) result = v;
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
    double result = std::numeric_limits<double>::infinity();
    for (unsigned long i = 0; i < argc; i++) {
        double v = argToDouble(ctx, args, static_cast<unsigned>(i));
        if (std::isnan(v)) return ctx->fromDouble(v);
        if (v < result) result = v;
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

    const proto::ProtoString* keyMath = ProtoJSStringCache::getKey(ctx, "Math");
    if (!keyMath) return;

    const proto::ProtoObject* existing = (*globalRoot)->getAttribute(ctx, keyMath, false);
    if (existing && existing != PROTO_NONE) return;

    const proto::ProtoObject* math = ctx->newObject(false);
    if (!math) return;

    auto reg = [&](const char* name, proto::ProtoMethod fn) {
        const proto::ProtoString* key = ProtoJSStringCache::getKey(ctx, name);
        if (key) math = math->setAttribute(ctx, key, ctx->fromMethod(nullptr, fn));
    };

    auto setConst = [&](const char* name, double val) {
        const proto::ProtoString* key = ProtoJSStringCache::getKey(ctx, name);
        if (key) math = math->setAttribute(ctx, key, ctx->fromDouble(val));
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

    // Methods
    reg("abs",    mathAbs);
    reg("acos",   mathAcos);
    reg("acosh",  mathAcosh);
    reg("asin",   mathAsin);
    reg("asinh",  mathAsinh);
    reg("atan",   mathAtan);
    reg("atanh",  mathAtanh);
    reg("atan2",  mathAtan2);
    reg("cbrt",   mathCbrt);
    reg("ceil",   mathCeil);
    reg("clz32",  mathClz32);
    reg("cos",    mathCos);
    reg("cosh",   mathCosh);
    reg("exp",    mathExp);
    reg("expm1",  mathExpm1);
    reg("floor",  mathFloor);
    reg("fround", mathFround);
    reg("hypot",  mathHypot);
    reg("imul",   mathImul);
    reg("log",    mathLog);
    reg("log1p",  mathLog1p);
    reg("log2",   mathLog2);
    reg("log10",  mathLog10);
    reg("max",    mathMax);
    reg("min",    mathMin);
    reg("pow",    mathPow);
    reg("random", mathRandom);
    reg("round",  mathRound);
    reg("sign",   mathSign);
    reg("sin",    mathSin);
    reg("sinh",   mathSinh);
    reg("sqrt",   mathSqrt);
    reg("tan",    mathTan);
    reg("tanh",   mathTanh);
    reg("trunc",  mathTrunc);

    *globalRoot = (*globalRoot)->setAttribute(ctx, keyMath, math);
}

} // namespace protojs
