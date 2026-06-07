#include "NumberPrototype.h"
#include "FunctionPrototype.h"
#include "JSSymbols.h"
#include "PrototypeUtils.h"
#include "TypeBridge.h"
#include "headers/protoCore.h"
#include "runtime/ProtoInterpreter.h"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>

namespace protojs {

namespace {

double getNumberValue(proto::ProtoContext* context, const proto::ProtoObject* self) {
    if (!self || self == PROTO_NONE) return 0.0;
    if (self->isInteger(context)) return static_cast<double>(self->asLong(context));
    if (self->isDouble(context) || self->isFloat(context)) return self->asDouble(context);
    // Number wrapper object: extract from __primitive_value__.
    const proto::ProtoString* pvKey = JSSymbols::primitiveValue(context);
    if (pvKey) {
        const proto::ProtoObject* pv = self->getAttribute(context, pvKey, false);
        if (pv && pv != PROTO_NONE) {
            if (pv->isInteger(context)) return static_cast<double>(pv->asLong(context));
            if (pv->isDouble(context) || pv->isFloat(context)) return pv->asDouble(context);
        }
    }
    return 0.0;
}

/** Throws TypeError if this is null, undefined, or not a Number value/wrapper.
 *  Returns false to abort; caller must return PROTO_NONE. */
static bool requireNumberThis(proto::ProtoContext* ctx,
                               const proto::ProtoObject* self) {
    if (!self || self == PROTO_NONE || self->isNone(ctx)) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Number.prototype method called on incompatible receiver"));
        return false;
    }
    const proto::ProtoObject* nullSentinel = getNullSentinel();
    if (nullSentinel && self == nullSentinel) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Number.prototype method called on incompatible receiver"));
        return false;
    }
    // Valid if self is a numeric primitive.
    if (self->isInteger(ctx) || self->isDouble(ctx) || self->isFloat(ctx))
        return true;
    // Valid if self is a Number wrapper object (has __primitive_value__ that is numeric).
    const proto::ProtoString* pvk = JSSymbols::primitiveValue(ctx);
    if (pvk) {
        const proto::ProtoObject* pv = self->getAttribute(ctx, pvk, false);
        if (pv && pv != PROTO_NONE &&
            (pv->isInteger(ctx) || pv->isDouble(ctx) || pv->isFloat(ctx)))
            return true;
    }
    signalNativeException(makeNativeError(ctx, "TypeError",
        "Number.prototype method called on incompatible receiver"));
    return false;
}

const proto::ProtoObject* numberValueOf(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* /*positionalParameters*/,
    const proto::ProtoSparseList* /*keywordParameters*/)
{
    if (!requireNumberThis(context, self)) return PROTO_NONE;
    // Primitive number: return as-is.
    if (self->isInteger(context) || self->isDouble(context) || self->isFloat(context))
        return self;
    // Number wrapper object: extract and return __primitive_value__.
    const proto::ProtoString* pvKey = JSSymbols::primitiveValue(context);
    if (pvKey) {
        const proto::ProtoObject* pv = self->getAttribute(context, pvKey, false);
        if (pv && pv != PROTO_NONE &&
            (pv->isInteger(context) || pv->isDouble(context) || pv->isFloat(context)))
            return pv;
    }
    return context->fromDouble(0.0); // fallback
}

const proto::ProtoObject* numberToString(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* /*keywordParameters*/)
{
    if (!requireNumberThis(context, self)) return PROTO_NONE;
    // §21.1.3.6 step 3: ToIntegerOrInfinity on radix; undefined defaults
    // to 10. jsToNumber handles strings ("16" → 16) and ToPrimitive for
    // objects, after which we truncate.
    int radix = 10;
    if (positionalParameters && positionalParameters->getSize(context) > 0) {
        const proto::ProtoObject* radixObj = positionalParameters->getAt(context, 0);
        if (radixObj && radixObj != PROTO_NONE
            && radixObj != getUndefinedSentinel()) {
            const proto::ProtoObject* num = jsToNumber(context, radixObj);
            if (num) {
                if (num->isInteger(context)) {
                    radix = static_cast<int>(num->asLong(context));
                } else if (num->isDouble(context) || num->isFloat(context)) {
                    double d = num->asDouble(context);
                    if (std::isnan(d)) radix = 0;
                    else if (std::isinf(d)) radix = d > 0 ? 1000 : -1000;
                    else radix = static_cast<int>(d);
                }
            }
        }
    }
    if (radix < 2 || radix > 36) {
        signalNativeException(makeNativeError(context, "RangeError",
            "Number.prototype.toString() radix must be between 2 and 36"));
        return PROTO_NONE;
    }
    double value = getNumberValue(context, self);
    std::string result;
    // Spec-mandated names for non-finite values (ECMA-262 §7.1.12.1).
    // C's "%g" outputs "nan", "inf", "-inf" which JS test262 rejects.
    if (std::isnan(value)) {
        return context->fromUTF8String("NaN");
    }
    if (std::isinf(value)) {
        return context->fromUTF8String(value < 0 ? "-Infinity" : "Infinity");
    }
    if (radix == 10) {
        char buf[64];
        if (value == static_cast<long long>(value) && value >= -9007199254740992.0 && value <= 9007199254740991.0) {
            snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(value));
            result = buf;
        } else {
            // ECMA-262 §6.1.6.1.13 Number::toString: pick the shortest
            // decimal mantissa that round-trips through the double, then
            // emit:
            //   * scientific form when the decimal exponent is < -6 or
            //     >= 21;
            //   * integer notation (with trailing zeros) when the exponent
            //     fits in [0, 21);
            //   * "0." + leading zeros + mantissa when the exponent is
            //     in [-6, 0).
            // Pre-fix `%.15g` flattened any value whose magnitude was
            // exactly at 15 significant digits, so
            // `(1000000000000000128).toString()` returned "1e+18" instead
            // of the spec-required "1000000000000000100".
            int chosenP = 17;
            for (int p = 1; p <= 17; ++p) {
                snprintf(buf, sizeof(buf), "%.*e", p - 1, value);
                double check = 0.0;
                std::sscanf(buf, "%lf", &check);
                if (check == value) { chosenP = p; break; }
            }
            // buf currently holds "[-]d.dddd...e[+/-]NN" with chosenP-1
            // fraction digits.  Extract sign / mantissa / exponent.
            const char* sp = buf;
            bool neg = false;
            if (*sp == '-') { neg = true; ++sp; }
            std::string mant;
            mant.push_back(*sp++);
            if (*sp == '.') {
                ++sp;
                while (*sp && *sp != 'e') mant.push_back(*sp++);
            }
            int expVal = 0;
            if (*sp == 'e') {
                ++sp;
                int sgn = 1;
                if (*sp == '+') ++sp;
                else if (*sp == '-') { sgn = -1; ++sp; }
                while (*sp >= '0' && *sp <= '9') { expVal = expVal * 10 + (*sp - '0'); ++sp; }
                expVal *= sgn;
            }
            // mant has chosenP significant digits, expVal is the
            // decimal exponent of the first digit.
            std::string m;
            if (expVal < -6 || expVal >= 21) {
                // Scientific notation: 'd.dddd' + 'e' + sign + |exp|.
                m.push_back(mant[0]);
                if (mant.size() > 1) {
                    m.push_back('.');
                    m.append(mant.substr(1));
                }
                m.push_back('e');
                m.push_back(expVal >= 0 ? '+' : '-');
                char eb[16];
                snprintf(eb, sizeof(eb), "%d", std::abs(expVal));
                m.append(eb);
            } else if (expVal >= 0) {
                // Integer-form: place dot after (expVal+1) digits, pad
                // with trailing zeros up to that position.
                int intLen = expVal + 1;
                if (static_cast<int>(mant.size()) >= intLen) {
                    m.append(mant.substr(0, intLen));
                    if (static_cast<int>(mant.size()) > intLen) {
                        m.push_back('.');
                        m.append(mant.substr(intLen));
                    }
                } else {
                    m.append(mant);
                    m.append(intLen - mant.size(), '0');
                }
            } else {
                // "0." + leading zeros + mantissa for expVal in [-6, -1].
                m.append("0.");
                for (int i = 0; i < -expVal - 1; ++i) m.push_back('0');
                m.append(mant);
            }
            if (neg) m.insert(0, "-");
            result = m;
        }
    } else {
        // Spec §21.1.3.6: split value into sign, integer, fractional
        // parts and emit each in the requested radix. Pre-fix the
        // fractional part was discarded entirely, so
        //   (0.5).toString(2) -> "0"  (spec: "0.1")
        //   (0.1).toString(2) -> "0"  (spec: long binary expansion)
        double absVal = std::fabs(value);
        bool neg = std::signbit(value) && value != 0.0;
        double intPart;
        double fracPart = std::modf(absVal, &intPart);
        const char digits[] = "0123456789abcdefghijklmnopqrstuvwxyz";
        std::string intStr;
        unsigned long long u = static_cast<unsigned long long>(intPart);
        if (u == 0) {
            intStr = "0";
        } else {
            std::string rev;
            do {
                rev += digits[u % static_cast<unsigned>(radix)];
                u /= radix;
            } while (u);
            intStr.append(rev.rbegin(), rev.rend());
        }
        result = neg ? "-" : "";
        result += intStr;
        if (fracPart > 0.0) {
            result += '.';
            // Emit up to ~52 fractional digits (matches V8's cap for
            // the long binary expansions of doubles).
            for (int k = 0; k < 52 && fracPart > 0.0; ++k) {
                fracPart *= radix;
                double d;
                fracPart = std::modf(fracPart, &d);
                int idx = static_cast<int>(d);
                if (idx < 0) idx = 0;
                if (idx >= radix) idx = radix - 1;
                result += digits[idx];
            }
        }
    }
    return context->fromUTF8String(result.c_str());
}

const proto::ProtoObject* numberToFixed(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* /*keywordParameters*/)
{
    if (!requireNumberThis(context, self)) return PROTO_NONE;
    // ECMA-262 §22.1.3.27 step 2: f = ToIntegerOrInfinity(fractionDigits).
    // Step 3: if f is +Inf, throw RangeError. Step 5: if f < 0 or f > 100,
    // throw RangeError. ToIntegerOrInfinity of NaN/undefined/non-numeric
    // string -> 0; of numeric string -> the rounded integer.
    double fdDouble = 0.0;
    bool isInfinite = false;
    if (positionalParameters && positionalParameters->getSize(context) > 0) {
        const proto::ProtoObject* fdObj = positionalParameters->getAt(context, 0);
        if (fdObj && fdObj != PROTO_NONE) {
            // §22.1.3.27 step 2 requires ToIntegerOrInfinity, whose
            // first step is ToNumber. Routing every non-primitive arg
            // through jsToNumber lets the Symbol / object / BigInt
            // rejection (built-ins/Number/prototype/toFixed/toFixed-
            // tonumber-throws-typeerror-{symbol,bigint,toprimitive})
            // and the ToPrimitive-via-valueOf integer extraction share
            // one code path.
            const proto::ProtoObject* num = fdObj;
            if (!fdObj->isInteger(context) && !fdObj->isDouble(context)
                && !fdObj->isFloat(context) && fdObj != PROTO_TRUE
                && fdObj != PROTO_FALSE) {
                num = jsToNumber(context, fdObj);
                if (hasCallException()) return PROTO_NONE;
            }
            if (!num) num = fdObj;
            if (num->isInteger(context)) {
                fdDouble = static_cast<double>(num->asLong(context));
            } else if (num->isDouble(context) || num->isFloat(context)) {
                fdDouble = num->asDouble(context);
            } else if (num == PROTO_TRUE) {
                fdDouble = 1.0;
            } else if (num == PROTO_FALSE) {
                fdDouble = 0.0;
            }
            if (std::isnan(fdDouble)) fdDouble = 0.0;
            if (std::isinf(fdDouble)) isInfinite = true;
        }
    }
    // ECMA-262 §22.1.3.27 step 2 applies ToIntegerOrInfinity BEFORE
    // the range gate; pre-fix the gate compared the raw double, so
    //   (0).toFixed(-0.1)  // ToInteger -> 0 → spec-valid
    // raised RangeError instead of returning "0".  ToInteger truncates
    // toward zero (-0.1 → 0, 99.9 → 99); only ±Infinity is preserved.
    if (!isInfinite) {
        fdDouble = (fdDouble < 0 ? -std::floor(-fdDouble) : std::floor(fdDouble));
    }
    if (isInfinite || fdDouble < 0.0 || fdDouble > 100.0) {
        signalNativeException(makeNativeError(context, "RangeError",
            "Number.prototype.toFixed() fractionDigits must be between 0 and 100"));
        return PROTO_NONE;
    }
    int fractionDigits = static_cast<int>(fdDouble);
    double value = getNumberValue(context, self);
    // Step 6: NaN -> "NaN".
    if (std::isnan(value)) return context->fromUTF8String("NaN");
    // Step 7: +/- Infinity -> "Infinity" / "-Infinity".
    if (std::isinf(value))
        return context->fromUTF8String(value < 0 ? "-Infinity" : "Infinity");
    // Step 9: if |x| >= 1e21, return ToString(x).
    if (std::fabs(value) >= 1e21) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%g", value);
        return context->fromUTF8String(buf);
    }
    // Step 5 normalizes -0: since `-0 < 0` is false, the sign must not
    // be emitted. printf("-0.0", "%.2f") yields "-0.00"; spec wants
    // "0.00". Strip the sign by re-assigning the literal +0.
    if (value == 0.0) value = 0.0;
    char buf[256];
    snprintf(buf, sizeof(buf), "%.*f", fractionDigits, value);
    // Step 5 also affects small negative magnitudes that round to 0:
    //   (-0.4).toFixed(0)  -> "-0"  (printf)  -> "0"  (spec)
    // Detect "-0" / "-0.0...0" output and strip the leading minus,
    // matching V8 / SpiderMonkey behaviour.
    if (buf[0] == '-') {
        bool allZero = true;
        for (const char* p = buf + 1; *p; ++p) {
            if (*p != '0' && *p != '.') { allZero = false; break; }
        }
        if (allZero) return context->fromUTF8String(buf + 1);
    }
    return context->fromUTF8String(buf);
}

const proto::ProtoObject* numberToExponential(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* /*keywordParameters*/)
{
    if (!requireNumberThis(context, self)) return PROTO_NONE;
    double value = getNumberValue(context, self);

    // ECMA-262 §21.1.3.2 specifies step ordering:
    //   1. Let x be ? thisNumberValue(this value).
    //   2. Let f be ? ToInteger(fractionDigits).
    //   3. If x is NaN, return "NaN".
    //   4. If x < 0, …
    //   5. If x is +∞ or -∞, return "Infinity" / "-Infinity".
    // ToInteger(fractionDigits) runs before any of the NaN / ±Infinity
    // shortcuts.  Pre-fix the NaN / Inf checks were hoisted above the
    // f conversion so user-visible side effects of
    // `fractionDigits.valueOf()` and `fractionDigits.toString()` were
    // silently swallowed when `this` was NaN or ±Infinity.
    bool fdUndefined = true;
    int fractionDigits = -1;
    if (positionalParameters && positionalParameters->getSize(context) > 0) {
        const proto::ProtoObject* fdObj = positionalParameters->getAt(context, 0);
        // Spec §21.1.3.2: `fractionDigits === undefined` falls into the
        // "no-argument" branch (round-trip shortest mantissa).  The
        // explicit `undefined` value is the language's t_undefinedSentinel
        // rather than PROTO_NONE — pre-fix passing `undefined` explicitly
        // was treated as `0`, producing "1e+2" for 123.456 instead of the
        // shortest round-trip "1.23456e+2".
        if (fdObj && fdObj != PROTO_NONE && fdObj != getUndefinedSentinel()) {
            fdUndefined = false;
            const proto::ProtoObject* numObj = fdObj;
            if (!fdObj->isInteger(context) && !fdObj->isDouble(context)
                && !fdObj->isFloat(context)) {
                numObj = jsToNumber(context, fdObj);
                if (hasCallException()) return PROTO_NONE;
            }
            if (numObj && numObj != PROTO_NONE) {
                if (numObj->isInteger(context)) {
                    fractionDigits = static_cast<int>(numObj->asLong(context));
                } else if (numObj->isDouble(context) || numObj->isFloat(context)) {
                    double d = numObj->asDouble(context);
                    // ToInteger: truncate toward zero (NaN → 0).
                    if (std::isnan(d)) fractionDigits = 0;
                    else if (std::isinf(d)) fractionDigits = (d > 0) ? 101 : 0;
                    else fractionDigits = static_cast<int>(d);
                }
            }
        }
    }
    // Step 3 spec: NaN -> "NaN" AFTER f has been computed.
    if (std::isnan(value)) return context->fromUTF8String("NaN");
    // glibc's %e formats ±Infinity as "inf" — spec-incompliant tokens.
    if (std::isinf(value)) return context->fromUTF8String(value > 0 ? "Infinity" : "-Infinity");
    // Spec §21.1.3.2 step 11: fractionDigits must be in [0, 100].
    if (!fdUndefined && (fractionDigits < 0 || fractionDigits > 100)) {
        signalNativeException(makeNativeError(context, "RangeError",
            "toExponential() argument must be between 0 and 100"));
        return PROTO_NONE;
    }
    // Spec §21.1.3.2 step 5: if x < 0, prepend "-" and negate x.
    // Crucially -0 < 0 is FALSE, so -0 must serialise without the
    // sign. glibc's %e prints "-0e+00" for -0.0; normalise it to +0
    // before formatting to match the spec.
    if (value == 0.0) value = 0.0;
    char buf[256];
    if (fdUndefined) {
        // Spec: choose the smallest number of digits that round-trips
        // through the resulting decimal string back to value.
        for (int p = 0; p <= 20; ++p) {
            snprintf(buf, sizeof(buf), "%.*e", p, value);
            double check = 0.0;
            std::sscanf(buf, "%lf", &check);
            if (check == value) break;
        }
    } else {
        snprintf(buf, sizeof(buf), "%.*e", fractionDigits, value);
    }
    // ECMA-262 emits "1e+0", "3.14e+0", "1e-7": single-digit exponent,
    // no leading zero. glibc's %e emits "1e+00" / "1e-07"; strip a
    // leading zero that immediately follows the exponent sign.
    if (char* e = std::strchr(buf, 'e')) {
        if (e[1] && e[2] == '0' && e[3]) {
            std::memmove(e + 2, e + 3, std::strlen(e + 3) + 1);
        }
    }
    return context->fromUTF8String(buf);
}

const proto::ProtoObject* numberToPrecision(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* /*keywordParameters*/)
{
    if (!requireNumberThis(context, self)) return PROTO_NONE;
    // Spec §21.1.3.5 step 2: if precision is undefined, behave as
    // ToString(this) — no precision check, no RangeError.
    // `precision === undefined` (either omitted or explicitly
    // passed) skips the rest of §21.1.3.5 and behaves like
    // ToString(x).  Treat the language's t_undefinedSentinel the
    // same as PROTO_NONE here.
    const proto::ProtoObject* rawPrec =
        (positionalParameters && positionalParameters->getSize(context) > 0)
            ? positionalParameters->getAt(context, 0) : nullptr;
    bool precUndefined = !rawPrec
        || rawPrec == PROTO_NONE
        || rawPrec == getUndefinedSentinel();
    if (precUndefined) {
        return numberToString(context, self, nullptr, nullptr, nullptr);
    }
    double value = getNumberValue(context, self);
    // ECMA-262 §21.1.3.5 step ordering: ToInteger(precision) precedes
    // the NaN/±Infinity guards.  Pre-fix the NaN check ran first and
    // observable side effects of `precision.valueOf()` were skipped
    // when `this` was NaN — the test262 nan.js / infinity.js / range.js
    // tests assert valueOf is invoked exactly once even on NaN.
    const proto::ProtoObject* precObj = positionalParameters->getAt(context, 0);
    int precision = 0;
    {
        const proto::ProtoObject* numObj = precObj;
        if (precObj && !precObj->isInteger(context) && !precObj->isDouble(context)
            && !precObj->isFloat(context)) {
            numObj = jsToNumber(context, precObj);
            if (hasCallException()) return PROTO_NONE;
        }
        if (numObj && numObj != PROTO_NONE) {
            if (numObj->isInteger(context)) precision = static_cast<int>(numObj->asLong(context));
            else if (numObj->isDouble(context) || numObj->isFloat(context)) {
                double d = numObj->asDouble(context);
                if (std::isnan(d)) precision = 0;
                else if (std::isinf(d)) precision = (d > 0) ? 101 : 0; // out-of-range
                else precision = static_cast<int>(d);
            }
        }
    }
    // Step 4: NaN -> "NaN" after ToInteger(precision) has run.
    if (std::isnan(value)) return context->fromUTF8String("NaN");
    // Step 7: ±Infinity -> "Infinity" / "-Infinity" BEFORE the
    // precision-range guard at step 8.  Pre-fix the range check
    // ran first, so `Infinity.toPrecision(1000)` threw RangeError
    // even though the spec emits "Infinity" without consulting
    // precision.
    if (std::isinf(value)) return context->fromUTF8String(value > 0 ? "Infinity" : "-Infinity");
    if (precision < 1 || precision > 100) {
        signalNativeException(makeNativeError(context, "RangeError",
            "toPrecision() argument must be between 1 and 100"));
        return PROTO_NONE;
    }
    // -0 must emit without sign (step 5: `x < 0` is false for -0).
    if (value == 0.0) value = 0.0;
    // Spec step 10: when x = 0, the mantissa is p occurrences of "0".
    // glibc's %.*g collapses to "0" regardless of precision, dropping
    // the trailing zeros the spec requires. Special-case zero.
    if (value == 0.0) {
        if (precision == 1) return context->fromUTF8String("0");
        std::string z = "0.";
        z.append(static_cast<size_t>(precision - 1), '0');
        return context->fromUTF8String(z.c_str());
    }
    // Spec §21.1.3.5 step 12 requires EXACTLY `precision` significant
    // digits — %g strips trailing zeros, so (3.14).toPrecision(5)
    // returned '3.14' instead of '3.1400'. Format via %e first to lock
    // in `precision` digits, then decide between exponent and fixed
    // notation based on the spec's [-6, p) window on the decimal
    // exponent.
    char ebuf[256];
    snprintf(ebuf, sizeof(ebuf), "%.*e", precision - 1, value);
    // %e shape: "[-]d.ddd...e[+-]NN". Extract mantissa digits + sign +
    // exponent.
    std::string mant;
    int expVal = 0;
    bool negative = false;
    {
        const char* p = ebuf;
        if (*p == '-') { negative = true; ++p; }
        // Skip the leading integer digit.
        if (*p) { mant.push_back(*p); ++p; }
        if (*p == '.') {
            ++p;
            while (*p && *p != 'e') { mant.push_back(*p); ++p; }
        }
        if (*p == 'e') {
            ++p;
            int sgn = 1;
            if (*p == '+') ++p;
            else if (*p == '-') { sgn = -1; ++p; }
            while (*p >= '0' && *p <= '9') { expVal = expVal * 10 + (*p - '0'); ++p; }
            expVal *= sgn;
        }
    }
    // mant currently has exactly `precision` digits.
    std::string result;
    if (expVal < -6 || expVal >= precision) {
        // Exponent form: m[0] '.' m[1..p-1] 'e' sign exp
        result.push_back(mant[0]);
        if (precision > 1) {
            result.push_back('.');
            result.append(mant.substr(1));
        }
        result.push_back('e');
        result.push_back(expVal >= 0 ? '+' : '-');
        char ebuf2[32];
        snprintf(ebuf2, sizeof(ebuf2), "%d", std::abs(expVal));
        result.append(ebuf2);
    } else if (expVal >= 0) {
        // Fixed form, exponent in [0, p-1]: integer part = mant[0..exp],
        // fractional = mant[exp+1..]
        result.append(mant.substr(0, static_cast<size_t>(expVal + 1)));
        if (static_cast<int>(mant.size()) > expVal + 1) {
            result.push_back('.');
            result.append(mant.substr(static_cast<size_t>(expVal + 1)));
        }
    } else {
        // Fixed form, exponent in [-6, -1]: "0." + (-expVal-1) zeros + mant
        result.append("0.");
        for (int i = 0; i < -expVal - 1; ++i) result.push_back('0');
        result.append(mant);
    }
    if (negative) result.insert(0, "-");
    return context->fromUTF8String(result.c_str());
}

// ---------------------------------------------------------------------------
// Number static methods
// ---------------------------------------------------------------------------

const proto::ProtoObject* numberIsNaN(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!args || args->getSize(ctx) == 0) return PROTO_FALSE;
    const proto::ProtoObject* v = args->getAt(ctx, 0);
    if (!v || v == PROTO_NONE) return PROTO_FALSE;
    if (!v->isDouble(ctx) && !v->isFloat(ctx)) return PROTO_FALSE;
    return std::isnan(v->asDouble(ctx)) ? PROTO_TRUE : PROTO_FALSE;
}

const proto::ProtoObject* numberIsFinite(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!args || args->getSize(ctx) == 0) return PROTO_FALSE;
    const proto::ProtoObject* v = args->getAt(ctx, 0);
    if (!v || v == PROTO_NONE) return PROTO_FALSE;
    if (v->isInteger(ctx)) return PROTO_TRUE;
    if (v->isDouble(ctx) || v->isFloat(ctx))
        return std::isfinite(v->asDouble(ctx)) ? PROTO_TRUE : PROTO_FALSE;
    return PROTO_FALSE;
}

const proto::ProtoObject* numberIsInteger(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!args || args->getSize(ctx) == 0) return PROTO_FALSE;
    const proto::ProtoObject* v = args->getAt(ctx, 0);
    if (!v || v == PROTO_NONE) return PROTO_FALSE;
    if (v->isInteger(ctx)) return PROTO_TRUE;
    if (v->isDouble(ctx) || v->isFloat(ctx)) {
        double d = v->asDouble(ctx);
        if (!std::isfinite(d)) return PROTO_FALSE;
        return (d == std::trunc(d)) ? PROTO_TRUE : PROTO_FALSE;
    }
    return PROTO_FALSE;
}

const proto::ProtoObject* numberIsSafeInteger(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!args || args->getSize(ctx) == 0) return PROTO_FALSE;
    const proto::ProtoObject* v = args->getAt(ctx, 0);
    if (!v || v == PROTO_NONE) return PROTO_FALSE;
    double d = 0.0;
    if (v->isInteger(ctx)) d = static_cast<double>(v->asLong(ctx));
    else if (v->isDouble(ctx) || v->isFloat(ctx)) d = v->asDouble(ctx);
    else return PROTO_FALSE;
    if (!std::isfinite(d) || d != std::trunc(d)) return PROTO_FALSE;
    return (std::abs(d) <= 9007199254740991.0) ? PROTO_TRUE : PROTO_FALSE;
}

const proto::ProtoObject* numberParseInt(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!args || args->getSize(ctx) == 0) return ctx->fromDouble(std::numeric_limits<double>::quiet_NaN());
    const proto::ProtoObject* strObj = args->getAt(ctx, 0);
    std::string s;
    if (strObj && strObj != PROTO_NONE) {
        if (strObj->isString(ctx)) {
            const proto::ProtoString* ps = strObj->asString(ctx);
            if (ps) ps->toUTF8String(ctx, s);
        } else if (strObj->isInteger(ctx)) {
            s = std::to_string(strObj->asLong(ctx));
        } else if (strObj->isDouble(ctx)) {
            char buf[64]; snprintf(buf, sizeof(buf), "%.15g", strObj->asDouble(ctx)); s = buf;
        }
    }
    // Trim leading whitespace
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r' || s[i] == '\f' || s[i] == '\v')) i++;
    s = s.substr(i);

    int radix = 10;
    if (args->getSize(ctx) > 1) {
        const proto::ProtoObject* radixObj = args->getAt(ctx, 1);
        // ECMA-262 §19.2.5 step 4: R = ? ToInt32(radix).  ToInt32
        // begins with ToNumber, which unwraps Number wrappers via
        // valueOf and coerces strings / booleans.  Pre-fix the
        // radix path matched only primitive integers and doubles,
        // so `parseInt("11", new Number(2))` ignored the wrapper
        // and defaulted to radix 10.
        if (radixObj && radixObj != PROTO_NONE
            && radixObj != getUndefinedSentinel()) {
            const proto::ProtoObject* num = radixObj;
            if (!radixObj->isInteger(ctx) && !radixObj->isDouble(ctx)
                && !radixObj->isFloat(ctx)) {
                num = jsToNumber(ctx, radixObj);
                if (hasCallException()) return PROTO_NONE;
            }
            if (num && num != PROTO_NONE) {
                if (num->isInteger(ctx)) radix = static_cast<int>(num->asLong(ctx));
                else if (num->isDouble(ctx) || num->isFloat(ctx)) {
                    double d = num->asDouble(ctx);
                    // ToInt32: NaN / ±0 / ±Infinity → 0.
                    radix = (std::isnan(d) || std::isinf(d)) ? 0 : static_cast<int>(d);
                }
            }
        }
    }

    // Handle 0x prefix for hex
    if (s.size() >= 2 && s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        if (radix == 10 || radix == 16) { radix = 16; s = s.substr(2); }
    } else if (s.size() >= 2 && s[0] == '0' && (s[1] == 'b' || s[1] == 'B') && radix == 2) {
        s = s.substr(2);
    } else if (s.size() >= 2 && s[0] == '0' && (s[1] == 'o' || s[1] == 'O') && radix == 8) {
        s = s.substr(2);
    }

    if (radix < 2 || radix > 36) return ctx->fromDouble(std::numeric_limits<double>::quiet_NaN());
    if (s.empty()) return ctx->fromDouble(std::numeric_limits<double>::quiet_NaN());

    char* end = nullptr;
    long long result = std::strtoll(s.c_str(), &end, radix);
    if (end == s.c_str()) return ctx->fromDouble(std::numeric_limits<double>::quiet_NaN());
    return ctx->fromInteger(result);
}

const proto::ProtoObject* numberParseFloat(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!args || args->getSize(ctx) == 0) return ctx->fromDouble(std::numeric_limits<double>::quiet_NaN());
    const proto::ProtoObject* strObj = args->getAt(ctx, 0);
    if (!strObj || strObj == PROTO_NONE) return ctx->fromDouble(std::numeric_limits<double>::quiet_NaN());
    if (strObj->isInteger(ctx)) return strObj;
    if (strObj->isDouble(ctx) || strObj->isFloat(ctx)) return strObj;
    std::string s;
    if (strObj->isString(ctx)) {
        const proto::ProtoString* ps = strObj->asString(ctx);
        if (ps) ps->toUTF8String(ctx, s);
    } else { return ctx->fromDouble(std::numeric_limits<double>::quiet_NaN()); }
    // Trim leading whitespace
    size_t i = 0;
    while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r' || s[i] == '\f' || s[i] == '\v')) i++;
    s = s.substr(i);
    if (s == "Infinity" || s == "+Infinity") return ctx->fromDouble(std::numeric_limits<double>::infinity());
    if (s == "-Infinity") return ctx->fromDouble(-std::numeric_limits<double>::infinity());
    char* end = nullptr;
    double result = std::strtod(s.c_str(), &end);
    if (end == s.c_str()) return ctx->fromDouble(std::numeric_limits<double>::quiet_NaN());
    return ctx->fromDouble(result);
}

} // namespace

void BuildNumberPrototype(proto::ProtoSpace* space, proto::ProtoContext* ctx,
                         const proto::ProtoObject* objectProto) {
    if (!space || !ctx || !objectProto) return;

    // Must be mutable so JS-level assignments (Number.prototype.x = y) modify
    // the object in-place, keeping space->smallIntegerPrototype consistent and
    // ensuring attribute lookups on primitive numbers find newly added properties.
    const proto::ProtoObject* numberProto = objectProto->newChild(ctx, true);

    // ECMA-262 §21.1.4: "The Number prototype object is itself an
    // ordinary object … it has a [[NumberData]] internal slot whose
    // value is +0."  Install the slot now so methods that consult
    // thisNumberValue (toString / toFixed / toExponential /
    // toPrecision) treat `Number.prototype` as the numeric value 0
    // rather than throwing TypeError.
    {
        const proto::ProtoString* pvKey = JSSymbols::primitiveValue(ctx);
        if (pvKey) numberProto = numberProto->setAttribute(ctx, pvKey, ctx->fromInteger(0LL));
    }
    numberProto = installNonEnumerableMethod(ctx, numberProto, "valueOf",       numberValueOf,       0);
    numberProto = installNonEnumerableMethod(ctx, numberProto, "toString",      numberToString,      1);
    numberProto = installNonEnumerableMethod(ctx, numberProto, "toFixed",       numberToFixed,       1);
    numberProto = installNonEnumerableMethod(ctx, numberProto, "toExponential", numberToExponential, 1);
    numberProto = installNonEnumerableMethod(ctx, numberProto, "toPrecision",   numberToPrecision,   1);

    space->smallIntegerPrototype = const_cast<proto::ProtoObject*>(numberProto);
    space->largeIntegerPrototype = const_cast<proto::ProtoObject*>(numberProto);
    space->doublePrototype       = const_cast<proto::ProtoObject*>(numberProto);
}

// ---------------------------------------------------------------------------
// Number constructor helper — invoked by OP_call_constructor to initialise
// the newly-created wrapper object.
// ---------------------------------------------------------------------------
static const proto::ProtoObject* numberConstruct(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*)
{
    if (!ctx || !self || self == PROTO_NONE) return self;
    // Spec §21.1.1.1: when called as a constructor, the [[NumberData]]
    // slot is set to ToNumber(value). Delegate to jsToNumber so the
    // ToPrimitive(valueOf/toString) chain runs for objects — pre-fix
    // the constructor only handled primitives explicitly and silently
    // kept val=0 for any object argument.
    double val = 0.0;
    if (args && args->getSize(ctx) > 0) {
        const proto::ProtoObject* a = args->getAt(ctx, 0);
        const proto::ProtoObject* coerced = jsToNumber(ctx, a);
        // §21.1.1.1 step 1: NewTarget-only fast path: if the coercion
        // raised an abrupt completion (e.g.
        // \`{valueOf: null, toString: null}\` after the jsToNumber
        // ES2024 narrowing), propagate it instead of silently writing
        // a zero into [[NumberData]].
        if (hasCallException()) return PROTO_NONE;
        if (coerced) {
            if (coerced->isInteger(ctx)) val = static_cast<double>(coerced->asLong(ctx));
            else if (coerced->isDouble(ctx) || coerced->isFloat(ctx)) val = coerced->asDouble(ctx);
            else val = std::numeric_limits<double>::quiet_NaN();
        }
    }
    const proto::ProtoString* pvKey = JSSymbols::primitiveValue(ctx);
    if (pvKey)
        self = self->setAttribute(ctx, pvKey, ctx->fromDouble(val));
    return self;
}

void ensureNumberConstructor(proto::ProtoContext* ctx,
                             const proto::ProtoObject** globalRoot) {
    if (!ctx || !globalRoot || !*globalRoot) return;
    const proto::ProtoString* keyNumber = JSSymbols::Number(ctx);
    if (!keyNumber) return;

    const proto::ProtoObject* existing = (*globalRoot)->getAttribute(ctx, keyNumber, false);
    if (existing && existing != PROTO_NONE) return;

    // BuildNumberPrototype ran before ensureFunctionPrototype, so the
    // method wrappers there were created with `parent = nullptr` —
    // they don't inherit .call/.apply/.bind from Function.prototype.
    // Reinstall the five Number.prototype methods now that
    // methodPrototype is available.
    if (ctx->space && ctx->space->smallIntegerPrototype && ctx->space->methodPrototype) {
        const proto::ProtoObject* np = ctx->space->smallIntegerPrototype;
        np = installNonEnumerableMethod(ctx, np, "valueOf",       numberValueOf,       0);
        np = installNonEnumerableMethod(ctx, np, "toString",      numberToString,      1);
        // §21.1.3.4: Number.prototype.toLocaleString exists as an own
        // property on Number.prototype, distinct from Object.prototype.
        // .toLocaleString.  No-Intl fallback semantics permitted: when
        // Intl is unavailable behave like toString().  Pre-fix the
        // method was absent so verifyProperty(Number.prototype,
        // 'toLocaleString', ...) failed with 'obj should have an own
        // property toLocaleString' (Number/prototype/toLocaleString/
        // prop-desc).
        np = installNonEnumerableMethod(ctx, np, "toLocaleString", numberToString,      0);
        np = installNonEnumerableMethod(ctx, np, "toFixed",       numberToFixed,       1);
        np = installNonEnumerableMethod(ctx, np, "toExponential", numberToExponential, 1);
        np = installNonEnumerableMethod(ctx, np, "toPrecision",   numberToPrecision,   1);
        ctx->space->smallIntegerPrototype = const_cast<proto::ProtoObject*>(np);
        ctx->space->largeIntegerPrototype = const_cast<proto::ProtoObject*>(np);
        ctx->space->doublePrototype       = const_cast<proto::ProtoObject*>(np);
    }

    const proto::ProtoObject* ctorParent =
        (ctx->space && ctx->space->methodPrototype) ? ctx->space->methodPrototype : nullptr;
    const proto::ProtoObject* ctor = ctorParent
        ? ctorParent->newChild(ctx, true)
        : ctx->newObject(true);
    if (!ctor) return;

    // §17: every built-in static method is
    // {writable:true, enumerable:false, configurable:true} → bits 0x3.
    // Pre-fix the slot defaulted to fully enumerable, leaking isNaN /
    // isInteger / isFinite / isSafeInteger / parseInt / parseFloat in
    // for-in over Number (built-ins/Number/<Name>/prop-desc.js).
    auto reg = [&](const char* name, proto::ProtoMethod fn, long long length = 1) {
        const proto::ProtoString* key = ctx->fromUTF8String(name)->asString(ctx);
        if (key) {
            const proto::ProtoObject* wrapped = wrapNativeFunction(ctx, fn, name, length, globalRoot);
            if (wrapped && wrapped != PROTO_NONE) {
                ctor = ctor->setAttribute(ctx, key, wrapped);
                std::string pdStr = std::string("__pd_") + name + "__";
                const proto::ProtoObject* pdo = ctx->fromUTF8String(pdStr.c_str());
                const proto::ProtoString* pdk = pdo ? pdo->asString(ctx) : nullptr;
                if (pdk) ctor = ctor->setAttribute(ctx, pdk, ctx->fromInteger(0x3LL));
            }
        }
    };

    // Static methods
    reg("isNaN",         numberIsNaN,         1);
    reg("isFinite",      numberIsFinite,       1);
    reg("isInteger",     numberIsInteger,      1);
    reg("isSafeInteger", numberIsSafeInteger,  1);
    // §21.1.2.12 / §21.1.2.13 — Number.parseInt === parseInt and
    // Number.parseFloat === parseFloat. Final binding happens via
    // patchNumberParseFns() after the global fns are installed (we
    // run before them, so the global lookup here would be PROTO_NONE).
    // Install local copies for now; they're swapped to the canonical
    // global references at the end of bootstrap.
    reg("parseInt",      numberParseInt,       2);
    reg("parseFloat",    numberParseFloat,     1);

    // Constants
    auto setConst = [&](const char* name, double val) {
        const proto::ProtoObject* keyObj = ctx->fromUTF8String(name);
        const proto::ProtoString* key = keyObj ? keyObj->asString(ctx) : nullptr;
        if (!key) return;
        ctor = ctor->setAttribute(ctx, key, ctx->fromDouble(val));
        // Constants: {writable: false, enumerable: false, configurable: false} → bits = 0x0
        std::string pdKeyStr = "__pd_";
        pdKeyStr += name;
        pdKeyStr += "__";
        const proto::ProtoObject* pdko = ctx->fromUTF8String(pdKeyStr.c_str());
        const proto::ProtoString* pdk = pdko ? pdko->asString(ctx) : nullptr;
        if (pdk) ctor = ctor->setAttribute(ctx, pdk, ctx->fromInteger(0x0));
    };
    setConst("EPSILON",            std::numeric_limits<double>::epsilon());
    setConst("MAX_SAFE_INTEGER",   9007199254740991.0);
    setConst("MIN_SAFE_INTEGER",  -9007199254740991.0);
    setConst("MAX_VALUE",          std::numeric_limits<double>::max());
    setConst("MIN_VALUE",          std::numeric_limits<double>::denorm_min());
    setConst("POSITIVE_INFINITY",  std::numeric_limits<double>::infinity());
    setConst("NEGATIVE_INFINITY", -std::numeric_limits<double>::infinity());
    setConst("NaN",                std::numeric_limits<double>::quiet_NaN());

    const proto::ProtoString* nameKey = JSSymbols::name(ctx);
    if (nameKey) {
        ctor = ctor->setAttribute(ctx, nameKey, ctx->fromUTF8String("Number"));
        // §17 built-in ctor name descriptor 0x2.
        const proto::ProtoString* pdns = JSSymbols::pdName(ctx);
        if (pdns) ctor = ctor->setAttribute(ctx, pdns, ctx->fromInteger(0x2LL));
    }
    // Number.length === 1 per §21.1.1.
    const proto::ProtoString* lenKey = JSSymbols::length(ctx);
    if (lenKey) {
        ctor = ctor->setAttribute(ctx, lenKey, ctx->fromInteger(1LL));
        const proto::ProtoString* pdlk = JSSymbols::pdLength(ctx);
        if (pdlk) ctor = ctor->setAttribute(ctx, pdlk, ctx->fromInteger(0x2LL));
    }
    // Hot-path hint mirroring Boolean / RegExp ctors earlier this
    // round.  Without __has_nonwritable_props__ the writable=false
    // bits on name + length are ignored by resolvePutFieldOOP and
    // `Number.name = "X"` silently succeeded despite the descriptor.
    {
        const proto::ProtoString* hnw = JSSymbols::hasNonWritableProps(ctx);
        if (hnw) ctor = ctor->setAttribute(ctx, hnw, PROTO_TRUE);
    }

    // Number.prototype — point to the number prototype already on space.
    // §21.1.2.1: the `prototype` property of the Number constructor
    // is non-writable, non-enumerable, non-configurable.  Pre-fix
    // it was fully enumerable so `for (k in Number)` listed
    // "prototype".
    const proto::ProtoString* protoKey2 = JSSymbols::prototype(ctx);
    const proto::ProtoObject* numProto = ctx->space ? ctx->space->smallIntegerPrototype : nullptr;
    if (protoKey2 && numProto && numProto != PROTO_NONE) {
        ctor = ctor->setAttribute(ctx, protoKey2, numProto);
        const proto::ProtoObject* pdpo = ctx->fromUTF8String("__pd_prototype__");
        const proto::ProtoString* pdpk = pdpo ? pdpo->asString(ctx) : nullptr;
        if (pdpk) ctor = ctor->setAttribute(ctx, pdpk, ctx->fromInteger(0x0LL));
    }

    // Explicitly mark as a constructor for OP_call_constructor.
    const proto::ProtoString* isCtorKey = ctx->fromUTF8String("__is_constructor__")->asString(ctx);
    if (isCtorKey) ctor = ctor->setAttribute(ctx, isCtorKey, PROTO_TRUE);

    // __number_ctor__ marker for typeof/instanceof checks.
    const proto::ProtoString* numCtorKey = ctx->fromUTF8String("__number_ctor__")->asString(ctx);
    if (numCtorKey) ctor = ctor->setAttribute(ctx, numCtorKey, PROTO_TRUE);

    // __construct__ native — invoked by OP_call_constructor for native constructors.
    // Must be stored as a raw method (isMethod() == true), not a wrapped function object.
    const proto::ProtoString* ctorMethodKey = ctx->fromUTF8String("__construct__")->asString(ctx);
    if (ctorMethodKey) {
        proto::ProtoObject* mCtor2 = const_cast<proto::ProtoObject*>(ctor);
        const proto::ProtoObject* ctorMethodObj = ctx->fromMethod(mCtor2, numberConstruct);
        if (ctorMethodObj && ctorMethodObj != PROTO_NONE)
            ctor = ctor->setAttribute(ctx, ctorMethodKey, ctorMethodObj);
    }

    // Number.prototype.constructor === Number per §21.1.4.1.
    if (numProto && numProto != PROTO_NONE) {
        const proto::ProtoString* ctorWordKey = JSSymbols::constructor(ctx);
        if (ctorWordKey) {
            const proto::ProtoObject* updatedProto =
                numProto->setAttribute(ctx, ctorWordKey, ctor);
            // Non-enumerable per §21.1.4.1 — bits 0x3.
            if (updatedProto && updatedProto != PROTO_NONE) {
                const proto::ProtoString* pdk = JSSymbols::pdConstructor(ctx);
                if (pdk) updatedProto = updatedProto->setAttribute(ctx, pdk,
                    ctx->fromInteger(0x3LL));
            }
            if (ctx->space && updatedProto && updatedProto != PROTO_NONE) {
                ctx->space->smallIntegerPrototype = const_cast<proto::ProtoObject*>(updatedProto);
            }
        }
    }

    *globalRoot = (*globalRoot)->setAttribute(ctx, keyNumber, ctor);
    // §17 globalThis.Number descriptor 0x3.
    {
        const proto::ProtoObject* pdo = ctx->fromUTF8String("__pd_Number__");
        const proto::ProtoString* pdk = pdo ? pdo->asString(ctx) : nullptr;
        if (pdk) *globalRoot = (*globalRoot)->setAttribute(ctx, pdk,
            ctx->fromInteger(0x3LL));
    }
}

// Re-bind Number.parseInt to globalThis.parseInt and Number.parseFloat
// to globalThis.parseFloat after both have been installed, so the
// spec-mandated identity `Number.parseInt === parseInt` (§21.1.2.12)
// holds. Called from runBytecode after ensureGlobalFn registers the
// global functions.
void patchNumberParseFns(proto::ProtoContext* ctx,
                         const proto::ProtoObject** globalRoot) {
    if (!ctx || !globalRoot || !*globalRoot) return;
    const proto::ProtoString* keyNumber = JSSymbols::Number(ctx);
    if (!keyNumber) return;
    const proto::ProtoObject* num = (*globalRoot)->getAttribute(ctx, keyNumber, false);
    if (!num || num == PROTO_NONE) return;

    auto getGlobal = [&](const char* name) -> const proto::ProtoObject* {
        const proto::ProtoObject* o = ctx->fromUTF8String(name);
        const proto::ProtoString* k = o ? o->asString(ctx) : nullptr;
        if (!k) return nullptr;
        const proto::ProtoObject* v = (*globalRoot)->getAttribute(ctx, k, false);
        return (v && v != PROTO_NONE) ? v : nullptr;
    };
    const proto::ProtoObject* gParseInt = getGlobal("parseInt");
    const proto::ProtoObject* gParseFloat = getGlobal("parseFloat");

    auto patch = [&](const char* name, const proto::ProtoObject* fn) {
        if (!fn) return;
        const proto::ProtoObject* ko = ctx->fromUTF8String(name);
        const proto::ProtoString* k = ko ? ko->asString(ctx) : nullptr;
        if (k) num = num->setAttribute(ctx, k, fn);
    };
    patch("parseInt", gParseInt);
    patch("parseFloat", gParseFloat);

    *globalRoot = (*globalRoot)->setAttribute(ctx, keyNumber, num);
}

} // namespace protojs
