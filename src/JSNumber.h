#ifndef PROTOJS_JSNUMBER_H
#define PROTOJS_JSNUMBER_H

/**
 * Canonical representation of JavaScript numbers on protoCore.
 *
 * A JavaScript Number is an IEEE-754 double, but protoCore has two cheaper
 * and more expensive ways to hold one: a tagged SmallInteger (no Cell, and
 * the interpreter's arithmetic fast paths) and a boxed double (one 64-byte
 * Cell).  JavaScript semantics never depend on which one holds a value
 * (typeof, ===, Object.is, arithmetic and formatting treat 3 and 3.0 alike);
 * performance and memory do.  protoJS therefore keeps one canonical form:
 *
 *   an integral value with |v| <= 2^53 - 1 (Number.MAX_SAFE_INTEGER) that is
 *   not -0 is a SmallInteger; every other value (fractions, -0, NaN,
 *   ±Infinity, integral values beyond the safe range) is a double.
 *
 * Every native producer of a number (built-ins and interpreter arithmetic)
 * builds its result with makeNumber(), never with ProtoContext::fromDouble()
 * directly, so `Math.floor(x)`, `Number("42")` or `6 / 2` take the integer
 * fast paths of the code that consumes them.
 *
 * The safe range, rather than protoCore's full SmallInteger range
 * [-(2^53), 2^53 - 1], keeps the canonical form symmetric, and every double
 * in it is exactly an integer, so the conversion never rounds.
 */

#include "protoCore.h"
#include <cmath>

namespace protojs {

constexpr double kJSMaxSafeInteger = 9007199254740991.0;  // 2^53 - 1

/** The canonical protoCore value for the JavaScript number `d`. */
inline const proto::ProtoObject* makeNumber(proto::ProtoContext* ctx, double d) {
    if (d >= -kJSMaxSafeInteger && d <= kJSMaxSafeInteger) {  // false for NaN
        const long long i = static_cast<long long>(d);
        if (static_cast<double>(i) == d && (i != 0 || !std::signbit(d)))
            return proto::makeSmallInt(i);
    }
    return ctx->fromDouble(d);
}

/**
 * The value of a protoCore number (SmallInteger, double or LargeInteger) as
 * a double.  Returns false, leaving `out` unchanged, for a non-number.
 */
inline bool numberToDouble(proto::ProtoContext* ctx, const proto::ProtoObject* v,
                           double& out) {
    if (proto::isSmallInt(v)) {
        out = static_cast<double>(proto::asSmallInt(v));
        return true;
    }
    if (!v || v == PROTO_NONE) return false;
    if (v->isDouble(ctx)) { out = v->asDouble(ctx); return true; }
    if (v->isInteger(ctx)) { out = v->asDouble(ctx); return true; }
    return false;
}

/**
 * `v` in canonical form: a double holding a safe integer (and not -0)
 * becomes a SmallInteger; a LargeInteger, which is not a JavaScript number
 * representation, becomes the nearest double.  Anything else is returned
 * unchanged.
 */
inline const proto::ProtoObject* canonicalNumber(proto::ProtoContext* ctx,
                                                 const proto::ProtoObject* v) {
    if (proto::isSmallInt(v) || !v || v == PROTO_NONE) return v;
    if (v->isDouble(ctx)) {
        const double d = v->asDouble(ctx);
        const proto::ProtoObject* c = makeNumber(ctx, d);
        return proto::isSmallInt(c) ? c : v;
    }
    if (v->isInteger(ctx)) return makeNumber(ctx, v->asDouble(ctx));
    return v;
}

/**
 * Number::add for two values already converted by ToNumber, in canonical
 * form.  Falls back to protoCore's add (canonicalised) for operands that
 * are not plain numbers.
 */
inline const proto::ProtoObject* numberAdd(proto::ProtoContext* ctx,
                                           const proto::ProtoObject* a,
                                           const proto::ProtoObject* b) {
    double da, db;
    if (numberToDouble(ctx, a, da) && numberToDouble(ctx, b, db))
        return makeNumber(ctx, da + db);
    return canonicalNumber(ctx, a->add(ctx, b));
}

}  // namespace protojs

#endif  // PROTOJS_JSNUMBER_H
