#include "MathBuiltin.h"
#include "JSSymbols.h"
#include "ArrayElementsStorage.h"
#include "ObjectPrototype.h"
#include "runtime/ProtoInterpreter.h"
#include "headers/protoCore.h"
#include <cmath>
#include <cstdlib>
#include <ctime>
#include <limits>
#include <vector>

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
    // §21.3.2.28: above the 2^52 boundary every float is an integer
    // and adding 0.5 loses precision (x + 0.5 either equals x or
    // rounds up an extra ULP). The spec says return x in that range.
    // Pre-fix `floor(x + 0.5)` rounded large negative integers up to
    // the next representable value, breaking Math.round(-(2/EPSILON-1))
    // === -(2/EPSILON-1) and other ULP-precision identities.
    if (x >= 4503599627370496.0 || x <= -4503599627370496.0) {
        return ctx->fromDouble(x);
    }
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

// ECMA-262 §21.3.2.20b Math.sumPrecise(items): returns the exact
// IEEE 754 round-to-nearest sum of the elements of items.  Spec
// uses Shewchuk's distillation algorithm to maintain a list of
// non-overlapping partial sums; the final sum is then accumulated
// from those partials.
//
// This implementation iterates only Array-shaped inputs.  Generator
// and custom-Symbol.iterator support is not yet wired (would
// require an iterator-protocol shim from C++ to JS); call
// throws-on-non-number and takes-iterable cover that gap.
static const proto::ProtoObject* mathSumPrecise(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    // Step 1: require one argument; otherwise TypeError.
    if (!args || args->getSize(ctx) < 1) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Math.sumPrecise requires an iterable argument"));
        return PROTO_NONE;
    }
    const proto::ProtoObject* iter = args->getAt(ctx, 0);
    if (!iter || iter == PROTO_NONE) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Math.sumPrecise requires an iterable, got null/undefined"));
        return PROTO_NONE;
    }

    const proto::ProtoList* els = getArrayElements(ctx, iter);
    if (!els) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Math.sumPrecise: argument is not iterable"));
        return PROTO_NONE;
    }

    // State machine per spec step 6.
    enum class State { MinusZero, Finite, MinusInf, PlusInf, NaN_ };
    State state = State::MinusZero;
    std::vector<double> partials;
    partials.reserve(8);

    const unsigned long n = static_cast<unsigned long>(els->getSize(ctx));
    for (unsigned long i = 0; i < n && state != State::NaN_; ++i) {
        const proto::ProtoObject* v = els->getAt(ctx, i);
        // Spec step 6.b: each value MUST be a Number; non-Number → TypeError.
        if (!v || v == PROTO_NONE) {
            signalNativeException(makeNativeError(ctx, "TypeError",
                "Math.sumPrecise: element is not a Number"));
            return PROTO_NONE;
        }
        double x;
        if (proto::isSmallInt(v)) x = static_cast<double>(v->asLong(ctx));
        else if (v->isInteger(ctx)) x = static_cast<double>(v->asLong(ctx));
        else if (v->isDouble(ctx) || v->isFloat(ctx)) x = v->asDouble(ctx);
        else {
            signalNativeException(makeNativeError(ctx, "TypeError",
                "Math.sumPrecise: element is not a Number"));
            return PROTO_NONE;
        }

        if (std::isnan(x)) { state = State::NaN_; break; }
        if (std::isinf(x)) {
            if (x > 0) {
                if (state == State::MinusInf) state = State::NaN_;
                else state = State::PlusInf;
            } else {
                if (state == State::PlusInf) state = State::NaN_;
                else state = State::MinusInf;
            }
            continue;
        }
        if (state == State::PlusInf || state == State::MinusInf) continue;
        // Track -0 vs 0 contribution; +0 promotes minus-zero to finite.
        if (x == 0.0) {
            if (!std::signbit(x) && state == State::MinusZero)
                state = State::Finite;
            continue;
        }
        if (state == State::MinusZero) state = State::Finite;

        // Shewchuk merge: keep partials non-overlapping.
        size_t out = 0;
        for (size_t j = 0; j < partials.size(); ++j) {
            double y = partials[j];
            double hi, lo;
            if (std::fabs(x) < std::fabs(y)) { std::swap(x, y); }
            hi = x + y;
            lo = y - (hi - x);
            if (lo != 0.0) partials[out++] = lo;
            x = hi;
        }
        partials.resize(out);
        partials.push_back(x);
    }

    if (state == State::NaN_)
        return ctx->fromDouble(std::numeric_limits<double>::quiet_NaN());
    if (state == State::PlusInf)
        return ctx->fromDouble(std::numeric_limits<double>::infinity());
    if (state == State::MinusInf)
        return ctx->fromDouble(-std::numeric_limits<double>::infinity());
    if (state == State::MinusZero)
        return ctx->fromDouble(-0.0);

    // Final accumulation: sum partials from largest (last) to smallest.
    double total = 0.0;
    for (auto it = partials.rbegin(); it != partials.rend(); ++it)
        total += *it;
    return ctx->fromDouble(total);
}

// ECMA-262 §21.3.2.20a Math.f16round(x): round x to IEEE 754 binary16
// (half-precision) and return the result as a Number.  Uses the
// compiler-provided _Float16 type where available; on platforms
// without it, falls back to binary32 rounding (Math.fround behaviour).
static const proto::ProtoObject* mathF16round(
    proto::ProtoContext* ctx, const proto::ProtoObject*,
    const proto::ParentLink*, const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    double x = argToDouble(ctx, args, 0);
    if (std::isnan(x))
        return ctx->fromDouble(std::numeric_limits<double>::quiet_NaN());
#if defined(__FLT16_MAX__)
    _Float16 h = static_cast<_Float16>(static_cast<float>(x));
    return ctx->fromDouble(static_cast<double>(static_cast<float>(h)));
#else
    return ctx->fromDouble(static_cast<double>(static_cast<float>(x)));
#endif
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
    // ECMA-262 §21.3.2.18 step 2: ToNumber every argument in order,
    // propagating any abrupt completion. Pre-fix the impl called
    // argToDouble twice (once for Inf/NaN scan, once for summation)
    // and didn't propagate exceptions from valueOf — so a throwing
    // valueOf on argument N was followed by a second pass that
    // touched argument N+1's valueOf too (counter test).
    std::vector<double> coerced;
    coerced.reserve(argc);
    for (unsigned long i = 0; i < argc; i++) {
        double v = argToDouble(ctx, args, static_cast<unsigned>(i), 0.0);
        if (hasCallException()) return PROTO_NONE;
        coerced.push_back(v);
    }
    // Step 3: scan for ±Infinity / NaN. Inf wins over NaN per spec —
    // any +/-Infinity yields +Infinity.
    bool sawNaN = false, sawInf = false;
    for (double v : coerced) {
        if (std::isnan(v)) sawNaN = true;
        else if (std::isinf(v)) sawInf = true;
    }
    if (sawInf) return ctx->fromDouble(std::numeric_limits<double>::infinity());
    if (sawNaN) return ctx->fromDouble(std::numeric_limits<double>::quiet_NaN());
    double sum = 0.0;
    for (double v : coerced) sum += v * v;
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
    //
    // Parent the Math namespace on Object.prototype per §21.3 ("The
    // Math object [...] has a [[Prototype]] internal slot whose value
    // is %Object.prototype%."). Without this Math has no prototype
    // chain to Object, so the spec-required inheritance of every
    // Object.prototype method (hasOwnProperty / toString / isPrototypeOf
    // / propertyIsEnumerable / valueOf / __defineGetter__ / …) is
    // missing — and ToPropertyDescriptor(Math) in Object.defineProperty
    // never sees the prototype-chain `value` / `writable` / etc. slots
    // that the spec walks via [[Get]] (8.10.5).
    const proto::ProtoObject* objectProto = ctx->space
        ? ctx->space->objectPrototype : nullptr;
    const proto::ProtoObject* math = objectProto
        ? objectProto->newChild(ctx, true) : ctx->newObject(true);
    if (!math) return;
    // Register the override so Object.getPrototypeOf(Math) and the
    // attribute walk paths that consult t_jsProtoMap see %Object.prototype%
    // as the [[Prototype]] (newChild only updates protoCore parents).
    if (objectProto) setJSProtoOverride(ctx, math, objectProto);

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
            const proto::ProtoString* pdlk = JSSymbols::pdLength(ctx);
            if (pdlk) wrapper = wrapper->setAttribute(ctx, pdlk, ctx->fromInteger(0x2LL));
        }
        const proto::ProtoString* nmk = JSSymbols::name(ctx);
        if (nmk) {
            wrapper = wrapper->setAttribute(ctx, nmk, ctx->fromUTF8String(name));
            const proto::ProtoString* pdnk = JSSymbols::pdName(ctx);
            if (pdnk) wrapper = wrapper->setAttribute(ctx, pdnk, ctx->fromInteger(0x2LL));
        }
        // Hot-path hint mirroring the Round 12 sweep — every Math method
        // wrapper's name + length are writable=false per §17, but the
        // enforcement only runs when __has_nonwritable_props__ is set on
        // the wrapper itself.
        const proto::ProtoString* hnwM = JSSymbols::hasNonWritableProps(ctx);
        if (hnwM) wrapper = wrapper->setAttribute(ctx, hnwM, PROTO_TRUE);
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
        // Hot-path hint mirroring the constructor sweep in Round 12 —
        // resolvePutFieldOOP only consults __pd_<key>__ when the per-
        // target __has_nonwritable_props__ flag is set on Math.  Without
        // it `Math.PI = 99` silently succeeded despite bits 0x0.
        const proto::ProtoString* hnw = JSSymbols::hasNonWritableProps(ctx);
        if (hnw) math = math->setAttribute(ctx, hnw, PROTO_TRUE);
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
    reg("f16round", mathF16round, 1);
    reg("sumPrecise", mathSumPrecise, 1);
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
    // Spec §17 descriptor on the global slot: {writable:true,
    // enumerable:false, configurable:true} → 0x3. Pre-fix no sidecar
    // so the default 0x7 (full enumerable) leaked Math into for-in
    // and Object.keys(globalThis).
    {
        const proto::ProtoObject* pdo = ctx->fromUTF8String("__pd_Math__");
        const proto::ProtoString* pdks = pdo ? pdo->asString(ctx) : nullptr;
        if (pdks) *globalRoot = (*globalRoot)->setAttribute(ctx, pdks, ctx->fromInteger(0x3LL));
    }
}

} // namespace protojs
