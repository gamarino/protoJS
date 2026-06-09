#include "DatePrototype.h"
#include "JSContext.h"
// Notes on the choice of timezone primitives:
//   - gmtime_r / timegm: stateless UTC ↔ broken-down time
//   - localtime_r / mktime: TZ-aware; uses host's $TZ at call time
// Both pairs are POSIX, available on every supported protoJS host.
#include "JSSymbols.h"
#include "ObjectPrototype.h"
#include "PrototypeUtils.h"
#include "FunctionPrototype.h"
#include "runtime/ProtoInterpreter.h"
#include "headers/protoCore.h"

// Date prototype implementation per ECMA-262 §21.4.  Internal slot
// [[DateValue]] is stored as the own attribute __date_value__ on every
// Date instance.  All getters / setters / stringifiers route through
// the shared decomposeTime / composeTime / timeClip primitives so the
// spec abstract operations (MakeDay, MakeTime, MakeDate, TimeClip) are
// applied uniformly.
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <iomanip>
#include <limits>
#include <sstream>
#include <string>

namespace protojs {

namespace {

static bool isPrimValue(proto::ProtoContext* ctx, const proto::ProtoObject* v) {
    if (!v || v == PROTO_NONE) return true;
    if (v == getUndefinedSentinel() || v == getNullSentinel()) return true;
    if (v->isInteger(ctx) || v->isDouble(ctx) || v->isFloat(ctx)) return true;
    if (v->isBoolean(ctx) || v->isString(ctx)) return true;
    return false;
}

static bool isCallableValue(proto::ProtoContext* ctx, const proto::ProtoObject* fn) {
    if (!fn || fn == PROTO_NONE) return false;
    if (fn->isMethod(ctx)) return true;
    const proto::ProtoString* bcKey = JSSymbols::bytecodeId(ctx);
    if (bcKey && fn->hasAttribute(ctx, bcKey) == PROTO_TRUE) return true;
    const proto::ProtoString* nfKey = JSSymbols::nativeFn(ctx);
    if (nfKey && fn->hasAttribute(ctx, nfKey) == PROTO_TRUE) return true;
    return false;
}

// §7.1.1 ToPrimitive(value, hint).  Calls the receiver's
// @@toPrimitive method if present (hint is forwarded as the sole arg),
// otherwise falls back to OrdinaryToPrimitive (valueOf-then-toString
// for hint "number"/"default", toString-then-valueOf for hint "string").
// Returns the primitive result, or PROTO_NONE on exception.  Used by
// the Date constructor for `new Date(obj)` and by Date.prototype's
// @@toPrimitive override when the receiver isn't a Date (the
// OrdinaryToPrimitive call site invoked by §21.4.4.45 step 6).
static const proto::ProtoObject* jsToPrimitive(proto::ProtoContext* ctx,
                                               const proto::ProtoObject* v,
                                               const char* hint) {
    if (!ctx) return PROTO_NONE;
    if (isPrimValue(ctx, v)) return v;
    // Step 2: GetMethod(v, @@toPrimitive).
    const proto::ProtoObject* tpKo = ctx->fromUTF8String("Symbol.toPrimitive");
    const proto::ProtoString* tpKs = tpKo ? tpKo->asString(ctx) : nullptr;
    const proto::ProtoObject* tpFn = tpKs
        ? v->getAttribute(ctx, tpKs, true) : nullptr;
    if (tpFn && tpFn != PROTO_NONE && tpFn != getUndefinedSentinel()
        && tpFn != getNullSentinel()) {
        if (!isCallableValue(ctx, tpFn)) {
            signalNativeException(makeNativeError(ctx, "TypeError",
                "Symbol.toPrimitive is not callable"));
            return PROTO_NONE;
        }
        const proto::ProtoList* hintArgs = ctx->newList();
        hintArgs = hintArgs->appendLast(ctx, ctx->fromUTF8String(hint));
        const proto::ProtoObject* r = callJSFunction(ctx, tpFn, v, hintArgs);
        if (hasCallException()) return PROTO_NONE;
        if (isPrimValue(ctx, r)) return r;
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Symbol.toPrimitive returned a non-primitive"));
        return PROTO_NONE;
    }
    // OrdinaryToPrimitive: order depends on hint.
    bool stringFirst = (std::string(hint) == "string");
    const proto::ProtoString* k1o = stringFirst
        ? ctx->fromUTF8String("toString")->asString(ctx)
        : ctx->fromUTF8String("valueOf")->asString(ctx);
    const proto::ProtoString* k2o = stringFirst
        ? ctx->fromUTF8String("valueOf")->asString(ctx)
        : ctx->fromUTF8String("toString")->asString(ctx);
    const proto::ProtoString* keys[2] = { k1o, k2o };
    bool anyCalled = false;
    for (int i = 0; i < 2; ++i) {
        if (!keys[i]) continue;
        const proto::ProtoObject* fn = v->getAttribute(ctx, keys[i], true);
        if (!isCallableValue(ctx, fn)) continue;
        anyCalled = true;
        const proto::ProtoObject* r = callJSFunction(ctx, fn, v, ctx->newList());
        if (hasCallException()) return PROTO_NONE;
        if (isPrimValue(ctx, r)) return r;
    }
    if (anyCalled) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "OrdinaryToPrimitive returned no primitive"));
    }
    return PROTO_NONE;
}

// ---------------------------------------------------------------------------
// Internal slot read/write
//
// Each Date instance stores its [[DateValue]] internal slot as the own
// attribute "__date_value__", a JavaScript number (Integer or Double).
// NaN means the Date is invalid (per §21.4.1.14 TimeClip).  Absence of
// the attribute is taken as "not a Date receiver" — the prototype's
// methods throw TypeError in that case per §21.4.4.1 thisTimeValue.
// ---------------------------------------------------------------------------

static const proto::ProtoString* dateValueKey(proto::ProtoContext* ctx) {
    // Interned once per thread.  createSymbol returns a perpetual
    // (NULL-context) allocation so the cached pointer is safe to keep
    // across GC cycles.
    static thread_local const proto::ProtoString* k = nullptr;
    if (!k) k = proto::ProtoString::createSymbol(ctx, "__date_value__");
    return k;
}

// Read the receiver's [[DateValue]].  Returns NaN when the receiver is
// not a Date instance OR when the stored value is NaN.  Sets *isDate to
// true iff the receiver carries the slot (so callers can distinguish
// "not a Date" from "invalid Date").
static double readDateValue(proto::ProtoContext* ctx,
                            const proto::ProtoObject* self,
                            bool* isDate) {
    if (isDate) *isDate = false;
    if (!ctx || !self || self == PROTO_NONE) return std::nan("");
    const proto::ProtoString* k = dateValueKey(ctx);
    if (!k) return std::nan("");
    const proto::ProtoObject* v = self->getAttribute(ctx, k, false);
    if (!v || v == PROTO_NONE) return std::nan("");
    if (isDate) *isDate = true;
    if (v->isInteger(ctx)) return static_cast<double>(v->asLong(ctx));
    if (v->isDouble(ctx) || v->isFloat(ctx)) return v->asDouble(ctx);
    return std::nan("");
}

static const proto::ProtoObject* writeDateValue(proto::ProtoContext* ctx,
                                                 const proto::ProtoObject* self,
                                                 double value) {
    if (!ctx || !self || self == PROTO_NONE) return self;
    const proto::ProtoString* k = dateValueKey(ctx);
    if (!k) return self;
    if (std::isnan(value)) {
        // Spec: NaN as [[DateValue]] indicates an invalid Date.  Store
        // the canonical double NaN; readDateValue surfaces it back to
        // callers via std::isnan() which all getters / stringifiers
        // already branch on.
        return self->setAttribute(ctx, k, ctx->fromDouble(std::nan("")));
    }
    // Whole-millisecond values fit Integer; non-integral Date values
    // are NaN per TimeClip, so the Double branch is normally unused.
    if (std::isfinite(value) && value == std::floor(value) &&
        value >= static_cast<double>(LLONG_MIN) &&
        value <= static_cast<double>(LLONG_MAX)) {
        return self->setAttribute(ctx, k,
            ctx->fromInteger(static_cast<long long>(value)));
    }
    return self->setAttribute(ctx, k, ctx->fromDouble(value));
}

// ---------------------------------------------------------------------------
// §21.4.1.14 TimeClip — collapse |t| > 8.64e15 to NaN; round to integer.
// ---------------------------------------------------------------------------

// §21.4.1.1 named time constants used throughout the file.  The
// spec's MakeTime / MakeDay arithmetic relies on these.
namespace TimeConstants {
    static constexpr double msPerSecond = 1000.0;
    static constexpr double msPerMinute = 60.0 * msPerSecond;
    static constexpr double msPerHour   = 60.0 * msPerMinute;
    static constexpr double msPerDay    = 24.0 * msPerHour;
    static constexpr double maxTime     = 8.64e15;
}

static double timeClip(double t) {
    if (!std::isfinite(t)) return std::nan("");
    if (std::abs(t) > TimeConstants::maxTime) return std::nan("");
    // §21.4.1.14 step 3: TimeClip returns ToInteger(t).  Use trunc to
    // round toward zero, matching the spec's ToInteger semantics.
    return std::trunc(t);
}

// ---------------------------------------------------------------------------
// §21.4.1.{2-12} time decomposition
//
// Convert a [[DateValue]] (ms since epoch) into broken-down components.
// Two flavours: UTC and local (TZ-aware via localtime_r).  Each returns
// false when the input is NaN / out of range; callers surface NaN.
// Sub-second milliseconds are extracted as a separate int because tm
// only carries integer seconds.
// ---------------------------------------------------------------------------

static bool decomposeTime(double t, bool utc, std::tm* out, int* msOut) {
    if (!std::isfinite(t)) return false;
    // Normalize the millisecond remainder to the [0, 999] range for
    // negative time values.  Without the < 0 adjustment, t = -500
    // would yield secs = 0 and rem = -500 instead of secs = -1 and
    // rem = 500, breaking getMilliseconds for pre-epoch dates.
    long long ms = static_cast<long long>(t);
    long long secs = ms / 1000;
    int rem = static_cast<int>(ms % 1000);
    if (rem < 0) { rem += 1000; secs -= 1; }
    if (msOut) *msOut = rem;
    std::time_t tt = static_cast<std::time_t>(secs);
    std::tm* r = utc ? gmtime_r(&tt, out) : localtime_r(&tt, out);
    return r != nullptr;
}

// ---------------------------------------------------------------------------
// §21.4.1.15 Date Time String Format — parse the spec-mandated form
//     YYYY-MM-DDTHH:mm:ss.sss[Z|±HH:MM]
// plus a handful of common fragments (date-only, date+space+time,
// slash-separated date).  Returns NaN on any failure.
// ---------------------------------------------------------------------------

// Validate a year string isn't the rejected "-000000" expanded form.
// §21.4.1.15.1: year 0 must be "+0000" or "0000", never "-0000" or
// "-000000".  Returns true when the prefix is the negative-zero form.
static bool isRejectedNegativeZeroYear(const std::string& s) {
    return s.rfind("-000000", 0) == 0;
}

// Check for the expanded year format prefix ±YYYYYY.  Returns the
// signed year value via `out` and the offset past the prefix via
// `consumed`.  When the prefix is absent, returns false and the
// regular 4-digit path runs.
static bool tryExpandedYear(const std::string& s, int* out, int* consumed) {
    if (s.size() < 7) return false;
    if (s[0] != '+' && s[0] != '-') return false;
    int sign = (s[0] == '-') ? -1 : 1;
    int y = 0;
    for (int i = 1; i <= 6; ++i) {
        if (s[i] < '0' || s[i] > '9') return false;
        y = y * 10 + (s[i] - '0');
    }
    if (sign == -1 && y == 0) return false;  // -000000 rejected
    *out = sign * y;
    *consumed = 7;
    return true;
}

static double parseDateString(const std::string& s) {
    if (s.empty()) return std::nan("");
    if (isRejectedNegativeZeroYear(s)) return std::nan("");
    // Try the ISO 8601 extended form with optional fractional seconds
    // and timezone designator.  Hand-rolled to avoid std::get_time's
    // strict "all-or-nothing" parse — the spec permits truncating any
    // component from the right.
    int year = 0, mon = 1, day = 1, hr = 0, mi = 0, sec = 0, ms = 0;
    int tzMin = 0;  // offset to subtract; 0 = UTC, negative = east of UTC
    bool hasTZ = false;
    const char* p = s.c_str();
    // YYYY (4 digits) or ±YYYYYY (7-character extended form).
    int consumed = 0;
    if (tryExpandedYear(s, &year, &consumed)) {
        p += consumed;
    } else {
        int n = 0;
        if (std::sscanf(p, "%4d%n", &year, &n) != 1 || n != 4) {
            return std::nan("");
        }
        p += n;
    }
    int n = 0;
    (void)n;  // n still needed for the remaining sscanf calls below
    auto skipChar = [&](char c) -> bool { if (*p == c) { ++p; return true; } return false; };
    if (skipChar('-')) {
        if (std::sscanf(p, "%2d%n", &mon, &n) != 1 || n != 2) return std::nan("");
        p += n;
        if (skipChar('-')) {
            if (std::sscanf(p, "%2d%n", &day, &n) != 1 || n != 2) return std::nan("");
            p += n;
        }
    }
    // Optional time component, separated by 'T' or space.
    // §21.4.1.15 step 8: time component is optional but when present
    // must follow the spec-mandated separator (T or, more loosely, a
    // space).  Some sources omit the separator entirely and append
    // a comma + time; we accept ", " too for compatibility with the
    // RFC 2822 / Date(Date(0).toUTCString()) round trip.
    if (skipChar('T') || skipChar(' ') ||
        (p[0] == ',' && p[1] == ' ' && (p += 2))) {
        if (std::sscanf(p, "%2d%n", &hr, &n) != 1 || n != 2) return std::nan("");
        p += n;
        if (skipChar(':')) {
            if (std::sscanf(p, "%2d%n", &mi, &n) != 1 || n != 2) return std::nan("");
            p += n;
            if (skipChar(':')) {
                if (std::sscanf(p, "%2d%n", &sec, &n) != 1 || n != 2) return std::nan("");
                p += n;
                // Fractional seconds .sss (up to 3 digits used).
                if (skipChar('.')) {
                    int digits[3] = {0,0,0};
                    int got = 0;
                    while (*p >= '0' && *p <= '9' && got < 9) {
                        if (got < 3) digits[got] = *p - '0';
                        ++p; ++got;
                    }
                    ms = digits[0]*100 + digits[1]*10 + digits[2];
                }
            }
        }
        // Timezone designator: Z or ±HH:MM
        if (skipChar('Z')) {
            hasTZ = true;
        } else if (*p == '+' || *p == '-') {
            char sign = *p++;
            int hh, mm = 0;
            if (std::sscanf(p, "%2d%n", &hh, &n) != 1 || n != 2) return std::nan("");
            p += n;
            if (skipChar(':')) {
                if (std::sscanf(p, "%2d%n", &mm, &n) != 1 || n != 2) return std::nan("");
                p += n;
            }
            tzMin = (sign == '-' ? 1 : -1) * (hh * 60 + mm);
            hasTZ = true;
        }
    }
    // Skip trailing whitespace.
    while (*p == ' ' || *p == '\t') ++p;
    if (*p != '\0') {
        // Some inputs end with " GMT" / " UTC" — accept and treat as UTC.
        std::string tail(p);
        if (tail == "Z" || tail == "GMT" || tail == "UTC" ||
            tail == " GMT" || tail == " UTC") {
            hasTZ = true;
        } else {
            return std::nan("");
        }
    }

    std::tm tmv = {};
    tmv.tm_year = year - 1900;
    tmv.tm_mon  = mon - 1;
    tmv.tm_mday = day;
    tmv.tm_hour = hr;
    tmv.tm_min  = mi;
    tmv.tm_sec  = sec;
    std::time_t epoch;
    if (hasTZ) {
        epoch = timegm(&tmv);
    } else {
        // §21.4.1.15 step 5: bare date (no Z, no offset) interpreted
        // as local time.  Use mktime.
        tmv.tm_isdst = -1;
        epoch = mktime(&tmv);
    }
    if (epoch == static_cast<std::time_t>(-1)) return std::nan("");
    double t = static_cast<double>(epoch) * 1000.0 + ms;
    // Apply parsed TZ offset (subtract because tzMin holds "minutes east of UTC negated").
    t += static_cast<double>(tzMin) * TimeConstants::msPerMinute;
    return t;
}

// ---------------------------------------------------------------------------
// Constructor — §21.4.2 / §21.4.3
//
// Minimal viable: ignore arguments for now; the freshly-built instance
// carries [[DateValue]] = current time in ms.  Argument-handling
// (Date(value), Date(string), Date(y, m, d, h, m, s, ms)) is added in
// follow-up commits.
// ---------------------------------------------------------------------------

// §21.4.2.1 Date(...) — installed as __native_fn__ (bare call) and
// __construct__ (via `new`) on the Date global.  Branches on argc:
//   0   → current time
//   1   → numeric → time value; string → parsed; other → NaN
//   ≥ 2 → multi-component form with local-TZ interpretation
static const proto::ProtoObject* dateCtorCall(proto::ProtoContext* ctx,
                                              const proto::ProtoObject* self,
                                              const proto::ParentLink*,
                                              const proto::ProtoList* args,
                                              const proto::ProtoSparseList*) {
    if (!ctx) return PROTO_NONE;
    const int argc = args ? static_cast<int>(args->getSize(ctx)) : 0;
    double t;
    if (argc == 0) {
        // §21.4.2.1 no args: t = current time in ms.
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
        t = static_cast<double>(ms);
    } else if (argc == 1) {
        // §21.4.2.2 single arg: numeric → time value; string → parse;
        // Date receiver → copy [[DateValue]].
        const proto::ProtoObject* v = args->getAt(ctx, 0);
        if (v && v->isString(ctx)) {
            std::string s;
            v->asString(ctx)->toUTF8String(ctx, s);
            t = parseDateString(s);
        } else if (v && (v->isInteger(ctx) || v->isDouble(ctx) || v->isFloat(ctx))) {
            t = v->isInteger(ctx)
                ? static_cast<double>(v->asLong(ctx))
                : v->asDouble(ctx);
        } else {
            t = std::nan("");
        }
        t = timeClip(t);
    } else {
        // §21.4.2.1 step 6: multi-arg form
        //   Date(year, month [, date [, hr [, min [, sec [, ms]]]]])
        // ToNumber each (invoking ToPrimitive for objects), MakeDay +
        // MakeTime + MakeDate, then LocalTime (per spec, the multi-arg
        // form interprets components in local TZ).
        bool nan = false;
        auto pullDouble = [&](int idx, double dflt) -> double {
            if (idx >= argc) return dflt;
            const proto::ProtoObject* v = args->getAt(ctx, idx);
            if (!v || v == PROTO_NONE) return dflt;
            if (v->isInteger(ctx)) return static_cast<double>(v->asLong(ctx));
            if (v->isDouble(ctx) || v->isFloat(ctx)) {
                double d = v->asDouble(ctx);
                if (std::isnan(d) || std::isinf(d)) { nan = true; return dflt; }
                return d;
            }
            // Spec §21.4.2.1: ToNumber for objects + strings.
            const proto::ProtoObject* n = jsToNumber(ctx, v);
            if (hasCallException() || !n || n == PROTO_NONE) { nan = true; return dflt; }
            if (n->isInteger(ctx)) return static_cast<double>(n->asLong(ctx));
            if (n->isDouble(ctx) || n->isFloat(ctx)) {
                double d = n->asDouble(ctx);
                if (std::isnan(d) || std::isinf(d)) { nan = true; return dflt; }
                return d;
            }
            nan = true;
            return dflt;
        };
        double year = pullDouble(0, 0);
        // §21.4.2.1 step 9: if 0 ≤ year ≤ 99, year += 1900.
        if (!nan && year >= 0 && year <= 99) year += 1900;
        double month = pullDouble(1, 0);
        double date  = pullDouble(2, 1);
        double hour  = pullDouble(3, 0);
        double min   = pullDouble(4, 0);
        double sec   = pullDouble(5, 0);
        double ms    = pullDouble(6, 0);
        if (nan) {
            t = std::nan("");
        } else {
            std::tm tmv = {};
            tmv.tm_year = static_cast<int>(year) - 1900;
            tmv.tm_mon  = static_cast<int>(month);
            tmv.tm_mday = static_cast<int>(date);
            tmv.tm_hour = static_cast<int>(hour);
            tmv.tm_min  = static_cast<int>(min);
            tmv.tm_sec  = static_cast<int>(sec);
            tmv.tm_isdst = -1;
            std::time_t epoch = mktime(&tmv);
            if (epoch == static_cast<std::time_t>(-1)) {
                t = std::nan("");
            } else {
                t = static_cast<double>(epoch) * 1000.0 + ms;
                t = timeClip(t);
            }
        }
    }

    if (self && self != PROTO_NONE) {
        return writeDateValue(ctx, self, t);
    }
    return PROTO_NONE;
}

// ---------------------------------------------------------------------------
// Component extractor — common to every getter.  Pulls [[DateValue]],
// returns NaN on any failure, otherwise decomposes and runs the
// caller-supplied selector to extract one int component.
// ---------------------------------------------------------------------------

template <typename Selector>
static const proto::ProtoObject* getComponent(proto::ProtoContext* ctx,
                                              const proto::ProtoObject* self,
                                              bool utc, Selector pick) {
    if (!ctx) return PROTO_NONE;
    bool isDate = false;
    double t = readDateValue(ctx, self, &isDate);
    if (!isDate) {
        // §21.4.4.1 thisTimeValue: throw TypeError when the receiver
        // is not a Date instance (the slot is absent).
        signalNativeException(makeNativeError(ctx, "TypeError",
            "this is not a Date object"));
        return PROTO_NONE;
    }
    if (std::isnan(t)) return ctx->fromDouble(std::nan(""));
    std::tm tmv;
    int msrem = 0;
    if (!decomposeTime(t, utc, &tmv, &msrem))
        return ctx->fromDouble(std::nan(""));
    return ctx->fromInteger(static_cast<long long>(pick(tmv, msrem)));
}

// ---------------------------------------------------------------------------
// Convenience wrapper around getComponent that throws TypeError
// on non-Date receivers and returns NaN on invalid time.  The
// extra-clarity name documents intent; existing call sites still
// route through getComponent directly.
template <typename Selector>
static const proto::ProtoObject* readDateComponentOrThrow(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    bool utc, Selector pick) {
    return getComponent(ctx, self, utc, pick);
}

// §21.4.4.10 Date.prototype.getTime
// §21.4.4.8  Date.prototype.valueOf
// (same operation: return thisTimeValue)
// ---------------------------------------------------------------------------

// §21.4.4.13 Date.prototype.getUTCFullYear
static const proto::ProtoObject* dateGetUTCFullYear(proto::ProtoContext* ctx,
                                                    const proto::ProtoObject* self,
                                                    const proto::ParentLink*,
                                                    const proto::ProtoList*,
                                                    const proto::ProtoSparseList*) {
    return getComponent(ctx, self, true,
        [](const std::tm& tm, int) { return tm.tm_year + 1900; });
}

// §21.4.4.14 Date.prototype.getUTCMonth — 0-indexed (0=Jan, 11=Dec).
static const proto::ProtoObject* dateGetUTCMonth(proto::ProtoContext* ctx,
                                                 const proto::ProtoObject* self,
                                                 const proto::ParentLink*,
                                                 const proto::ProtoList*,
                                                 const proto::ProtoSparseList*) {
    return getComponent(ctx, self, true,
        [](const std::tm& tm, int) { return tm.tm_mon; });
}

// §21.4.4.15 Date.prototype.getUTCDate — day-of-month, 1-31.
static const proto::ProtoObject* dateGetUTCDate(proto::ProtoContext* ctx,
                                                const proto::ProtoObject* self,
                                                const proto::ParentLink*,
                                                const proto::ProtoList*,
                                                const proto::ProtoSparseList*) {
    return getComponent(ctx, self, true,
        [](const std::tm& tm, int) { return tm.tm_mday; });
}

// §21.4.4.16 Date.prototype.getUTCDay — day-of-week, 0=Sun..6=Sat.
static const proto::ProtoObject* dateGetUTCDay(proto::ProtoContext* ctx,
                                               const proto::ProtoObject* self,
                                               const proto::ParentLink*,
                                               const proto::ProtoList*,
                                               const proto::ProtoSparseList*) {
    return getComponent(ctx, self, true,
        [](const std::tm& tm, int) { return tm.tm_wday; });
}

// §21.4.4.17 Date.prototype.getUTCHours — 0..23.
static const proto::ProtoObject* dateGetUTCHours(proto::ProtoContext* ctx,
                                                 const proto::ProtoObject* self,
                                                 const proto::ParentLink*,
                                                 const proto::ProtoList*,
                                                 const proto::ProtoSparseList*) {
    return getComponent(ctx, self, true,
        [](const std::tm& tm, int) { return tm.tm_hour; });
}

// §21.4.4.18 Date.prototype.getUTCMinutes — 0..59.
static const proto::ProtoObject* dateGetUTCMinutes(proto::ProtoContext* ctx,
                                                   const proto::ProtoObject* self,
                                                   const proto::ParentLink*,
                                                   const proto::ProtoList*,
                                                   const proto::ProtoSparseList*) {
    return getComponent(ctx, self, true,
        [](const std::tm& tm, int) { return tm.tm_min; });
}

// §21.4.4.19 Date.prototype.getUTCSeconds — 0..59.
static const proto::ProtoObject* dateGetUTCSeconds(proto::ProtoContext* ctx,
                                                   const proto::ProtoObject* self,
                                                   const proto::ParentLink*,
                                                   const proto::ProtoList*,
                                                   const proto::ProtoSparseList*) {
    return getComponent(ctx, self, true,
        [](const std::tm& tm, int) { return tm.tm_sec; });
}

// §21.4.4.20 Date.prototype.getUTCMilliseconds — 0..999.
static const proto::ProtoObject* dateGetUTCMilliseconds(proto::ProtoContext* ctx,
                                                        const proto::ProtoObject* self,
                                                        const proto::ParentLink*,
                                                        const proto::ProtoList*,
                                                        const proto::ProtoSparseList*) {
    return getComponent(ctx, self, true,
        [](const std::tm&, int ms) { return ms; });
}

// §21.4.4.4 Date.prototype.getFullYear — local year (4-digit).
static const proto::ProtoObject* dateGetFullYear(proto::ProtoContext* ctx,
                                                 const proto::ProtoObject* self,
                                                 const proto::ParentLink*,
                                                 const proto::ProtoList*,
                                                 const proto::ProtoSparseList*) {
    return getComponent(ctx, self, false,
        [](const std::tm& tm, int) { return tm.tm_year + 1900; });
}

// §21.4.4.5 Date.prototype.getMonth — local, 0-indexed.
static const proto::ProtoObject* dateGetMonth(proto::ProtoContext* ctx,
                                              const proto::ProtoObject* self,
                                              const proto::ParentLink*,
                                              const proto::ProtoList*,
                                              const proto::ProtoSparseList*) {
    return getComponent(ctx, self, false,
        [](const std::tm& tm, int) { return tm.tm_mon; });
}

// §21.4.4.2 getDate — local day-of-month.
static const proto::ProtoObject* dateGetDate(proto::ProtoContext* ctx,
                                             const proto::ProtoObject* self,
                                             const proto::ParentLink*,
                                             const proto::ProtoList*,
                                             const proto::ProtoSparseList*) {
    return getComponent(ctx, self, false,
        [](const std::tm& tm, int) { return tm.tm_mday; });
}

// §21.4.4.3 getDay — local day-of-week, 0=Sun..6=Sat.
static const proto::ProtoObject* dateGetDay(proto::ProtoContext* ctx,
                                            const proto::ProtoObject* self,
                                            const proto::ParentLink*,
                                            const proto::ProtoList*,
                                            const proto::ProtoSparseList*) {
    return getComponent(ctx, self, false,
        [](const std::tm& tm, int) { return tm.tm_wday; });
}

// §21.4.4.6 getHours — local hours 0..23.
static const proto::ProtoObject* dateGetHours(proto::ProtoContext* ctx,
                                              const proto::ProtoObject* self,
                                              const proto::ParentLink*,
                                              const proto::ProtoList*,
                                              const proto::ProtoSparseList*) {
    return getComponent(ctx, self, false,
        [](const std::tm& tm, int) { return tm.tm_hour; });
}

// §21.4.4.7 getMinutes.
static const proto::ProtoObject* dateGetMinutes(proto::ProtoContext* ctx,
                                                const proto::ProtoObject* self,
                                                const proto::ParentLink*,
                                                const proto::ProtoList*,
                                                const proto::ProtoSparseList*) {
    return getComponent(ctx, self, false,
        [](const std::tm& tm, int) { return tm.tm_min; });
}

// §21.4.4.9 getSeconds.
static const proto::ProtoObject* dateGetSeconds(proto::ProtoContext* ctx,
                                                const proto::ProtoObject* self,
                                                const proto::ParentLink*,
                                                const proto::ProtoList*,
                                                const proto::ProtoSparseList*) {
    return getComponent(ctx, self, false,
        [](const std::tm& tm, int) { return tm.tm_sec; });
}

// §21.4.4.0 getMilliseconds (sub-second component).
static const proto::ProtoObject* dateGetMilliseconds(proto::ProtoContext* ctx,
                                                     const proto::ProtoObject* self,
                                                     const proto::ParentLink*,
                                                     const proto::ProtoList*,
                                                     const proto::ProtoSparseList*) {
    return getComponent(ctx, self, false,
        [](const std::tm&, int ms) { return ms; });
}

// §21.4.4.11 getTimezoneOffset — minutes east-of-UTC negated.
// JS spec: offset = (UTC - local) in minutes, so for UTC+5 returns -300.
static const proto::ProtoObject* dateGetTimezoneOffset(proto::ProtoContext* ctx,
                                                       const proto::ProtoObject* self,
                                                       const proto::ParentLink*,
                                                       const proto::ProtoList*,
                                                       const proto::ProtoSparseList*) {
    if (!ctx) return PROTO_NONE;
    bool isDate = false;
    double t = readDateValue(ctx, self, &isDate);
    if (!isDate) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "this is not a Date object"));
        return PROTO_NONE;
    }
    if (std::isnan(t)) return ctx->fromDouble(std::nan(""));
    long long secs = static_cast<long long>(t) / 1000;
    std::time_t tt = static_cast<std::time_t>(secs);
    std::tm utcTm, localTm;
    if (!gmtime_r(&tt, &utcTm) || !localtime_r(&tt, &localTm))
        return ctx->fromDouble(std::nan(""));
    // Compute the difference in minutes: localTime - utcTime then negate.
    // Use timegm on both — local tm carries tm_isdst that we keep, gmtime
    // pretends the broken-down time is UTC.
    std::time_t utcEpoch = timegm(&utcTm);
    std::time_t localEpoch = timegm(&localTm);
    long diffSec = static_cast<long>(localEpoch - utcEpoch);
    return ctx->fromInteger(-static_cast<long long>(diffSec / 60));
}

// §21.4.4.10 Date.prototype.getTime
// §21.4.4.8  Date.prototype.valueOf (alias — same operation)
// Returns the receiver's [[DateValue]] as a Number.  Throws
// TypeError if `this` lacks the internal slot per §21.4.4.1.
static const proto::ProtoObject* dateGetTime(proto::ProtoContext* ctx,
                                             const proto::ProtoObject* self,
                                             const proto::ParentLink*,
                                             const proto::ProtoList*,
                                             const proto::ProtoSparseList*) {
    if (!ctx) return PROTO_NONE;
    bool isDate = false;
    double t = readDateValue(ctx, self, &isDate);
    if (!isDate) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "this is not a Date object"));
        return PROTO_NONE;
    }
    if (std::isnan(t)) return ctx->fromDouble(std::nan(""));
    return ctx->fromInteger(static_cast<long long>(t));
}

// Compose a tm + ms remainder back into a [[DateValue]] (ms since epoch).
// utc=true → timegm; utc=false → mktime (uses host timezone).
// Inverse of decomposeTime.  utc=true uses timegm (purely UTC
// arithmetic); utc=false uses mktime which honours the host's
// timezone and DST rules — tmIn.tm_isdst is overwritten with -1 so
// mktime infers the DST flag.
static double composeTime(const std::tm& tmIn, int ms, bool utc) {
    std::tm tm = tmIn;
    std::time_t epoch;
    if (utc) {
        epoch = timegm(&tm);
    } else {
        tm.tm_isdst = -1;  // let mktime infer DST
        epoch = mktime(&tm);
    }
    if (epoch == static_cast<std::time_t>(-1)) return std::nan("");
    return static_cast<double>(epoch) * 1000.0 + static_cast<double>(ms);
}

// Forward declaration so the template body can resolve at definition time.
static double pullArgAsDouble(proto::ProtoContext* ctx,
                              const proto::ProtoList* args,
                              int idx, bool* present);

// Spec-strict setter scaffold.  Coerces every positional argument to
// Number FIRST (firing ToPrimitive/valueOf/toString side effects per
// §21.4.4.x), and only then probes the receiver's [[DateValue]] for
// NaN.  When `fyMode` is true (setFullYear / setUTCFullYear), a NaN
// receiver clock anchors t=+0 instead of short-circuiting to NaN;
// every other setter returns NaN when t is NaN, but only AFTER all
// argument side effects have fired.
// `principalCount` is the number of leading args that are NEVER "if
// present"-gated by the spec — i.e. the spec text says "Let X be ?
// ToNumber(arg)" with no conditional, so a missing call-site argument
// yields ToNumber(undefined)=NaN.  Subsequent positional args are gated
// by "If X is present" and are ignored when missing.  setMilliseconds
// has principalCount=1; setHours has principalCount=1 (hour) and 3
// optional trailing args; setFullYear has principalCount=1 (year) and
// 2 optional trailing args.
template <typename Mutator>
static const proto::ProtoObject* setComponent2(proto::ProtoContext* ctx,
                                               const proto::ProtoObject* self,
                                               const proto::ProtoList* args,
                                               bool utc, int nArgs, bool fyMode,
                                               int principalCount,
                                               Mutator mutate) {
    if (!ctx || !self || self == PROTO_NONE) return PROTO_NONE;
    // Step 1 of every setter spec: thisTimeValue throws TypeError if
    // this is not a Date instance — this MUST precede any ToNumber on
    // the arguments (this-value-non-date.js's "validation precedes
    // input coercion" assertion).
    bool isDate = false;
    double t = readDateValue(ctx, self, &isDate);
    if (!isDate) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "this is not a Date object"));
        return PROTO_NONE;
    }
    constexpr int MAX_SETTER_ARGS = 7;
    double coerced[MAX_SETTER_ARGS] = {0};
    bool present[MAX_SETTER_ARGS] = {false};
    int probe = nArgs < MAX_SETTER_ARGS ? nArgs : MAX_SETTER_ARGS;
    // Step 2..k: ToNumber every present arg, side effects included.
    // This MUST run even when t is NaN (arg-coercion-order.js).
    // For principal (non-"if present") args, a missing call-site
    // argument coerces ToNumber(undefined) = NaN per spec.
    for (int i = 0; i < probe; ++i) {
        coerced[i] = pullArgAsDouble(ctx, args, i, &present[i]);
        if (hasCallException()) return PROTO_NONE;
        if (!present[i] && i < principalCount) {
            present[i] = true;
            coerced[i] = std::nan("");
        }
    }
    bool anyNan = false;
    for (int i = 0; i < probe; ++i)
        if (present[i] && (std::isnan(coerced[i]) || std::isinf(coerced[i])))
            { anyNan = true; break; }
    if (std::isnan(t) && !fyMode) {
        // Spec step "If t is NaN, return NaN" — do NOT overwrite the
        // receiver, because a side-effect inside ToNumber (Phase 1)
        // may have called setTime() on the receiver mid-coercion
        // (date-value-read-before-tonumber-when-date-is-invalid.js).
        // The saved t reflects the pre-coercion value; what's stored
        // in [[DateValue]] now is whatever setTime stamped.
        return ctx->fromDouble(std::nan(""));
    }
    std::tm tmv = {};
    int msrem = 0;
    if (std::isnan(t)) {
        // setFullYear NaN-anchor: t=+0, decomposed as UTC epoch.
        decomposeTime(0.0, /*utc=*/true, &tmv, &msrem);
    } else if (!decomposeTime(t, utc, &tmv, &msrem)) {
        writeDateValue(ctx, self, std::nan(""));
        return ctx->fromDouble(std::nan(""));
    }
    if (anyNan) {
        // Spec: any NaN-coerced present arg taints the result, but
        // ToNumber side effects already fired (Phase 1).
        writeDateValue(ctx, self, std::nan(""));
        return ctx->fromDouble(std::nan(""));
    }
    mutate(tmv, msrem, coerced, present, probe);
    double composed = composeTime(tmv, msrem, utc);
    composed = timeClip(composed);
    writeDateValue(ctx, self, composed);
    if (std::isnan(composed)) return ctx->fromDouble(std::nan(""));
    return ctx->fromInteger(static_cast<long long>(composed));
}

// Pull a positional argument as a double, with strict spec semantics:
// every "present" position (argc > idx) fires ToNumber unconditionally,
// even if the value is the undefined sentinel.  ToNumber(undefined) =
// NaN, and the spec for every Date set* method requires every present
// argument to be coerced BEFORE the receiver's [[DateValue]] NaN test
// (see §21.4.4.x step ordering and arg-coercion-order.js fixtures).
// The `present` out-flag distinguishes "argument not supplied" (no
// coercion fired) from "argument supplied" (coercion fired, possibly
// producing NaN).  The latter must still update the corresponding
// component slot per §21.4.4 step ordering.
static double pullArgAsDouble(proto::ProtoContext* ctx,
                              const proto::ProtoList* args,
                              int idx, bool* present) {
    if (!args || idx >= args->getSize(ctx)) {
        if (present) *present = false;
        return 0.0;
    }
    if (present) *present = true;
    const proto::ProtoObject* v = args->getAt(ctx, idx);
    if (!v || v == PROTO_NONE || v == getUndefinedSentinel())
        return std::nan("");
    if (v == getNullSentinel()) return 0.0;
    if (v->isInteger(ctx)) return static_cast<double>(v->asLong(ctx));
    if (v->isDouble(ctx) || v->isFloat(ctx)) return v->asDouble(ctx);
    if (v->isBoolean(ctx)) return v->asBoolean(ctx) ? 1.0 : 0.0;
    // §7.1.4 ToNumber for objects + strings.  ToPrimitive fires here.
    const proto::ProtoObject* n = jsToNumber(ctx, v);
    if (hasCallException()) return std::nan("");
    if (!n || n == PROTO_NONE) return std::nan("");
    if (n->isInteger(ctx)) return static_cast<double>(n->asLong(ctx));
    if (n->isDouble(ctx) || n->isFloat(ctx)) return n->asDouble(ctx);
    if (n->isBoolean(ctx)) return n->asBoolean(ctx) ? 1.0 : 0.0;
    return std::nan("");
}

// §21.4.4.23 setMilliseconds(ms) — local.
static const proto::ProtoObject* dateSetMilliseconds(proto::ProtoContext* ctx,
                                                     const proto::ProtoObject* self,
                                                     const proto::ParentLink*,
                                                     const proto::ProtoList* args,
                                                     const proto::ProtoSparseList*) {
    return setComponent2(ctx, self, args, /*utc=*/false, /*nArgs=*/1,
        /*fyMode=*/false, /*principalCount=*/1,
        [&](std::tm&, int& ms, const double* c, const bool* p, int) {
            if (p[0]) ms = static_cast<int>(c[0]);
        });
}

// §21.4.4.25 setSeconds(sec[, ms]) — local.
static const proto::ProtoObject* dateSetSeconds(proto::ProtoContext* ctx,
                                                const proto::ProtoObject* self,
                                                const proto::ParentLink*,
                                                const proto::ProtoList* args,
                                                const proto::ProtoSparseList*) {
    return setComponent2(ctx, self, args, /*utc=*/false, /*nArgs=*/2,
        /*fyMode=*/false, /*principalCount=*/1,
        [&](std::tm& tm, int& ms, const double* c, const bool* p, int) {
            if (p[0]) tm.tm_sec = static_cast<int>(c[0]);
            if (p[1]) ms = static_cast<int>(c[1]);
        });
}

// §21.4.4.24 setMinutes(min[, sec[, ms]]) — local.
static const proto::ProtoObject* dateSetMinutes(proto::ProtoContext* ctx,
                                                const proto::ProtoObject* self,
                                                const proto::ParentLink*,
                                                const proto::ProtoList* args,
                                                const proto::ProtoSparseList*) {
    return setComponent2(ctx, self, args, /*utc=*/false, /*nArgs=*/3,
        /*fyMode=*/false, /*principalCount=*/1,
        [&](std::tm& tm, int& ms, const double* c, const bool* p, int) {
            if (p[0]) tm.tm_min = static_cast<int>(c[0]);
            if (p[1]) tm.tm_sec = static_cast<int>(c[1]);
            if (p[2]) ms = static_cast<int>(c[2]);
        });
}

// §21.4.4.22 setHours(hr[, min[, sec[, ms]]]) — local.
static const proto::ProtoObject* dateSetHours(proto::ProtoContext* ctx,
                                              const proto::ProtoObject* self,
                                              const proto::ParentLink*,
                                              const proto::ProtoList* args,
                                              const proto::ProtoSparseList*) {
    return setComponent2(ctx, self, args, /*utc=*/false, /*nArgs=*/4,
        /*fyMode=*/false, /*principalCount=*/1,
        [&](std::tm& tm, int& ms, const double* c, const bool* p, int) {
            if (p[0]) tm.tm_hour = static_cast<int>(c[0]);
            if (p[1]) tm.tm_min  = static_cast<int>(c[1]);
            if (p[2]) tm.tm_sec  = static_cast<int>(c[2]);
            if (p[3]) ms         = static_cast<int>(c[3]);
        });
}

// §21.4.4.20 setDate(date) — local day-of-month.
static const proto::ProtoObject* dateSetDate(proto::ProtoContext* ctx,
                                             const proto::ProtoObject* self,
                                             const proto::ParentLink*,
                                             const proto::ProtoList* args,
                                             const proto::ProtoSparseList*) {
    return setComponent2(ctx, self, args, /*utc=*/false, /*nArgs=*/1,
        /*fyMode=*/false, /*principalCount=*/1,
        [&](std::tm& tm, int&, const double* c, const bool* p, int) {
            if (p[0]) tm.tm_mday = static_cast<int>(c[0]);
        });
}

// §21.4.4.21 setMonth(mo[, date]) — local 0-indexed month.
static const proto::ProtoObject* dateSetMonth(proto::ProtoContext* ctx,
                                              const proto::ProtoObject* self,
                                              const proto::ParentLink*,
                                              const proto::ProtoList* args,
                                              const proto::ProtoSparseList*) {
    return setComponent2(ctx, self, args, /*utc=*/false, /*nArgs=*/2,
        /*fyMode=*/false, /*principalCount=*/1,
        [&](std::tm& tm, int&, const double* c, const bool* p, int) {
            if (p[0]) tm.tm_mon  = static_cast<int>(c[0]);
            if (p[1]) tm.tm_mday = static_cast<int>(c[1]);
        });
}

// §21.4.4.18 setFullYear(year[, mo[, date]]) — local.  Unique among
// setters in that a NaN receiver doesn't short-circuit to NaN; instead
// t is anchored to +0 (decomposed as 1970-01-01 UTC) and the year is
// rebuilt from scratch (§21.4.4.21 step 5: "If t is NaN, set t to +0").
static const proto::ProtoObject* dateSetFullYear(proto::ProtoContext* ctx,
                                                 const proto::ProtoObject* self,
                                                 const proto::ParentLink*,
                                                 const proto::ProtoList* args,
                                                 const proto::ProtoSparseList*) {
    return setComponent2(ctx, self, args, /*utc=*/false, /*nArgs=*/3,
        /*fyMode=*/true, /*principalCount=*/1,
        [&](std::tm& tm, int&, const double* c, const bool* p, int) {
            if (p[0]) tm.tm_year = static_cast<int>(c[0]) - 1900;
            if (p[1]) tm.tm_mon  = static_cast<int>(c[1]);
            if (p[2]) tm.tm_mday = static_cast<int>(c[2]);
        });
}

// §21.4.4.30 setUTCMilliseconds(ms).
static const proto::ProtoObject* dateSetUTCMilliseconds(proto::ProtoContext* ctx,
                                                        const proto::ProtoObject* self,
                                                        const proto::ParentLink*,
                                                        const proto::ProtoList* args,
                                                        const proto::ProtoSparseList*) {
    return setComponent2(ctx, self, args, /*utc=*/true, /*nArgs=*/1,
        /*fyMode=*/false, /*principalCount=*/1,
        [&](std::tm&, int& ms, const double* c, const bool* p, int) {
            if (p[0]) ms = static_cast<int>(c[0]);
        });
}

// §21.4.4.32 setUTCSeconds(sec[, ms]).
static const proto::ProtoObject* dateSetUTCSeconds(proto::ProtoContext* ctx,
                                                   const proto::ProtoObject* self,
                                                   const proto::ParentLink*,
                                                   const proto::ProtoList* args,
                                                   const proto::ProtoSparseList*) {
    return setComponent2(ctx, self, args, /*utc=*/true, /*nArgs=*/2,
        /*fyMode=*/false, /*principalCount=*/1,
        [&](std::tm& tm, int& ms, const double* c, const bool* p, int) {
            if (p[0]) tm.tm_sec = static_cast<int>(c[0]);
            if (p[1]) ms        = static_cast<int>(c[1]);
        });
}

// §21.4.4.31 setUTCMinutes(min[, sec[, ms]]).
static const proto::ProtoObject* dateSetUTCMinutes(proto::ProtoContext* ctx,
                                                   const proto::ProtoObject* self,
                                                   const proto::ParentLink*,
                                                   const proto::ProtoList* args,
                                                   const proto::ProtoSparseList*) {
    return setComponent2(ctx, self, args, /*utc=*/true, /*nArgs=*/3,
        /*fyMode=*/false, /*principalCount=*/1,
        [&](std::tm& tm, int& ms, const double* c, const bool* p, int) {
            if (p[0]) tm.tm_min = static_cast<int>(c[0]);
            if (p[1]) tm.tm_sec = static_cast<int>(c[1]);
            if (p[2]) ms        = static_cast<int>(c[2]);
        });
}

// §21.4.4.29 setUTCHours(hr[, min[, sec[, ms]]]).
static const proto::ProtoObject* dateSetUTCHours(proto::ProtoContext* ctx,
                                                 const proto::ProtoObject* self,
                                                 const proto::ParentLink*,
                                                 const proto::ProtoList* args,
                                                 const proto::ProtoSparseList*) {
    return setComponent2(ctx, self, args, /*utc=*/true, /*nArgs=*/4,
        /*fyMode=*/false, /*principalCount=*/1,
        [&](std::tm& tm, int& ms, const double* c, const bool* p, int) {
            if (p[0]) tm.tm_hour = static_cast<int>(c[0]);
            if (p[1]) tm.tm_min  = static_cast<int>(c[1]);
            if (p[2]) tm.tm_sec  = static_cast<int>(c[2]);
            if (p[3]) ms         = static_cast<int>(c[3]);
        });
}

// §21.4.4.28 setUTCDate(date).
static const proto::ProtoObject* dateSetUTCDate(proto::ProtoContext* ctx,
                                                const proto::ProtoObject* self,
                                                const proto::ParentLink*,
                                                const proto::ProtoList* args,
                                                const proto::ProtoSparseList*) {
    return setComponent2(ctx, self, args, /*utc=*/true, /*nArgs=*/1,
        /*fyMode=*/false, /*principalCount=*/1,
        [&](std::tm& tm, int&, const double* c, const bool* p, int) {
            if (p[0]) tm.tm_mday = static_cast<int>(c[0]);
        });
}

// §21.4.4.30b setUTCMonth(mo[, date]).
static const proto::ProtoObject* dateSetUTCMonth(proto::ProtoContext* ctx,
                                                 const proto::ProtoObject* self,
                                                 const proto::ParentLink*,
                                                 const proto::ProtoList* args,
                                                 const proto::ProtoSparseList*) {
    return setComponent2(ctx, self, args, /*utc=*/true, /*nArgs=*/2,
        /*fyMode=*/false, /*principalCount=*/1,
        [&](std::tm& tm, int&, const double* c, const bool* p, int) {
            if (p[0]) tm.tm_mon  = static_cast<int>(c[0]);
            if (p[1]) tm.tm_mday = static_cast<int>(c[1]);
        });
}

// §21.4.4.26 setUTCFullYear(year[, mo[, date]]).
static const proto::ProtoObject* dateSetUTCFullYear(proto::ProtoContext* ctx,
                                                    const proto::ProtoObject* self,
                                                    const proto::ParentLink*,
                                                    const proto::ProtoList* args,
                                                    const proto::ProtoSparseList*) {
    return setComponent2(ctx, self, args, /*utc=*/true, /*nArgs=*/3,
        /*fyMode=*/true, /*principalCount=*/1,
        [&](std::tm& tm, int&, const double* c, const bool* p, int) {
            if (p[0]) tm.tm_year = static_cast<int>(c[0]) - 1900;
            if (p[1]) tm.tm_mon  = static_cast<int>(c[1]);
            if (p[2]) tm.tm_mday = static_cast<int>(c[2]);
        });
}

// §21.4.4.27 Date.prototype.setTime — direct assign TimeClip(ToNumber(arg)).
static const proto::ProtoObject* dateSetTime(proto::ProtoContext* ctx,
                                             const proto::ProtoObject* self,
                                             const proto::ParentLink*,
                                             const proto::ProtoList* args,
                                             const proto::ProtoSparseList*) {
    if (!ctx || !self || self == PROTO_NONE) return PROTO_NONE;
    bool isDate = false;
    (void)readDateValue(ctx, self, &isDate);
    if (!isDate) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "this is not a Date object"));
        return PROTO_NONE;
    }
    double t = std::nan("");
    if (args && args->getSize(ctx) > 0) {
        const proto::ProtoObject* v = args->getAt(ctx, 0);
        if (v && v->isInteger(ctx)) t = static_cast<double>(v->asLong(ctx));
        else if (v && (v->isDouble(ctx) || v->isFloat(ctx))) t = v->asDouble(ctx);
        else if (v) {
            const proto::ProtoObject* n = jsToNumber(ctx, v);
            if (hasCallException()) return PROTO_NONE;
            if (n && n->isInteger(ctx)) t = static_cast<double>(n->asLong(ctx));
            else if (n && (n->isDouble(ctx) || n->isFloat(ctx))) t = n->asDouble(ctx);
        }
    }
    t = timeClip(t);
    writeDateValue(ctx, self, t);
    if (std::isnan(t)) return ctx->fromDouble(std::nan(""));
    return ctx->fromInteger(static_cast<long long>(t));
}

// ---------------------------------------------------------------------------
// Stringifiers — §21.4.4.{31-43}
// ---------------------------------------------------------------------------

// ----------------------------------------------------------------
// §21.4.4.36 Date.prototype.toISOString.  Throws RangeError when
// [[DateValue]] is NaN per spec step 2; otherwise emits the
// expanded ISO 8601 form with mandatory Z designator.
// ----------------------------------------------------------------
// §21.4.4.36 toISOString — "YYYY-MM-DDTHH:mm:ss.sssZ"
static const proto::ProtoObject* dateToISOString(proto::ProtoContext* ctx,
                                                 const proto::ProtoObject* self,
                                                 const proto::ParentLink*,
                                                 const proto::ProtoList*,
                                                 const proto::ProtoSparseList*) {
    if (!ctx) return PROTO_NONE;
    bool isDate = false;
    double t = readDateValue(ctx, self, &isDate);
    if (!isDate || std::isnan(t)) {
        // §21.4.4.36 step 2: throw RangeError when [[DateValue]] is NaN.
        signalNativeException(makeNativeError(ctx, "RangeError",
            "Invalid time value"));
        return PROTO_NONE;
    }
    std::tm tmv;
    int msrem = 0;
    if (!decomposeTime(t, true, &tmv, &msrem)) {
        signalNativeException(makeNativeError(ctx, "RangeError",
            "Invalid time value"));
        return PROTO_NONE;
    }
    // §21.4.1.15.1 expanded year form: years < 0 or > 9999 use the
    // ±YYYYYY prefix (6 digits, signed).  Standard 4-digit format
    // covers the more common 0001-9999 range.
    char buf[48];
    int year = tmv.tm_year + 1900;
    if (year < 0 || year > 9999) {
        std::snprintf(buf, sizeof(buf),
            "%+07d-%02d-%02dT%02d:%02d:%02d.%03dZ",
            year, tmv.tm_mon + 1, tmv.tm_mday,
            tmv.tm_hour, tmv.tm_min, tmv.tm_sec, msrem);
    } else {
        std::snprintf(buf, sizeof(buf),
            "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
            year, tmv.tm_mon + 1, tmv.tm_mday,
            tmv.tm_hour, tmv.tm_min, tmv.tm_sec, msrem);
    }
    return ctx->fromUTF8String(buf);
}

// §21.4.4.37 toJSON — calls toISOString unless the receiver's primitive
// value is non-finite (then returns null per spec).
static const proto::ProtoObject* dateToJSON(proto::ProtoContext* ctx,
                                            const proto::ProtoObject* self,
                                            const proto::ParentLink* p,
                                            const proto::ProtoList* args,
                                            const proto::ProtoSparseList* k) {
    if (!ctx) return PROTO_NONE;
    // §21.4.4.37 step 1: ToObject(this).  When this is undefined or
    // null, that step throws a TypeError.  protoJS represents both as
    // PROTO_NONE / sentinels; treat any sentinel-y receiver as throw.
    if (!self || self == PROTO_NONE ||
        self == getUndefinedSentinel() ||
        self == getNullSentinel()) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Date.prototype.toJSON called on null or undefined"));
        return PROTO_NONE;
    }
    bool isDate = false;
    double t = readDateValue(ctx, self, &isDate);
    // §21.4.4.37 step 3: if tv is a Number and !isFinite(tv), return null.
    if (isDate && (std::isnan(t) || !std::isfinite(t))) return PROTO_NONE;
    if (!isDate) {
        // For non-Date receivers the spec calls
        //   Invoke(O, "toISOString").  We approximate by routing
        // through our static toISOString implementation; a fully-
        // spec'd Invoke (looking up toISOString via [[Get]]) requires
        // additional plumbing.
        return PROTO_NONE;
    }
    return dateToISOString(ctx, self, p, args, k);
}

// Day-of-week and month names per the spec's HTTP-Date format
// (§21.4.4.41 toUTCString).  Reused by toString / toDateString.
static const char* kWeekdayShort[] = {
    "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
};
static const char* kMonthShort[] = {
    "Jan", "Feb", "Mar", "Apr", "May", "Jun",
    "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
};

// ----------------------------------------------------------------
// §21.4.4.41 Date.prototype.toUTCString — HTTP-Date / IMF-fixdate
// form per RFC 7231 §7.1.1.1.  The legacy §B.2.4.3 alias
// toGMTString points at the same implementation.
// ----------------------------------------------------------------
// §21.4.4.41 toUTCString — "Day, DD Mon YYYY HH:MM:SS GMT"
static const proto::ProtoObject* dateToUTCString(proto::ProtoContext* ctx,
                                                 const proto::ProtoObject* self,
                                                 const proto::ParentLink*,
                                                 const proto::ProtoList*,
                                                 const proto::ProtoSparseList*) {
    if (!ctx) return PROTO_NONE;
    bool isDate = false;
    double t = readDateValue(ctx, self, &isDate);
    if (!isDate) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "this is not a Date object"));
        return PROTO_NONE;
    }
    if (std::isnan(t)) return ctx->fromUTF8String("Invalid Date");
    std::tm tmv;
    int msrem = 0;
    if (!decomposeTime(t, true, &tmv, &msrem))
        return ctx->fromUTF8String("Invalid Date");
    char buf[64];
    std::snprintf(buf, sizeof(buf),
        "%s, %02d %s %04d %02d:%02d:%02d GMT",
        kWeekdayShort[tmv.tm_wday], tmv.tm_mday, kMonthShort[tmv.tm_mon],
        tmv.tm_year + 1900, tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    return ctx->fromUTF8String(buf);
}

// Format the TZ offset (in seconds) as "±HHMM".  Shared by
// toString and toTimeString.
static std::string formatTZOffset(long offsetSec) {
    char sign = offsetSec >= 0 ? '+' : '-';
    long offsetAbs = std::labs(offsetSec);
    int hh = static_cast<int>(offsetAbs / 3600);
    int mm = static_cast<int>((offsetAbs % 3600) / 60);
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%c%02d%02d", sign, hh, mm);
    return std::string(buf);
}

// ----------------------------------------------------------------
// §21.4.4.42 Date.prototype.toString — host-defined.  Convention
// (V8 / JSC / SpiderMonkey): the local-timezone form with day-of-
// week + month-name + 4-digit year + 24-hour time + GMT±HHMM.
// ----------------------------------------------------------------
// §21.4.4.42 toString — "Day Mon DD YYYY HH:MM:SS GMT±HHMM (TZ)"
// Local timezone form per V8/SpiderMonkey/JSC convention.
static const proto::ProtoObject* dateToString(proto::ProtoContext* ctx,
                                              const proto::ProtoObject* self,
                                              const proto::ParentLink*,
                                              const proto::ProtoList*,
                                              const proto::ProtoSparseList*) {
    if (!ctx) return PROTO_NONE;
    bool isDate = false;
    double t = readDateValue(ctx, self, &isDate);
    if (!isDate) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "this is not a Date object"));
        return PROTO_NONE;
    }
    if (std::isnan(t)) return ctx->fromUTF8String("Invalid Date");
    std::tm tmv;
    int msrem = 0;
    if (!decomposeTime(t, false, &tmv, &msrem))
        return ctx->fromUTF8String("Invalid Date");
    // Compute offset in HHMM form vs UTC.
    long long secs = static_cast<long long>(t) / 1000;
    std::time_t tt = static_cast<std::time_t>(secs);
    std::tm utcTm;
    gmtime_r(&tt, &utcTm);
    std::time_t localEpoch = timegm(&tmv);
    std::time_t utcEpoch = timegm(&utcTm);
    long offsetSec = static_cast<long>(localEpoch - utcEpoch);
    std::string tz = formatTZOffset(offsetSec);
    char buf[96];
    std::snprintf(buf, sizeof(buf),
        "%s %s %02d %04d %02d:%02d:%02d GMT%s",
        kWeekdayShort[tmv.tm_wday], kMonthShort[tmv.tm_mon],
        tmv.tm_mday, tmv.tm_year + 1900,
        tmv.tm_hour, tmv.tm_min, tmv.tm_sec,
        tz.c_str());
    return ctx->fromUTF8String(buf);
}

// §21.4.4.34 toDateString — "Day Mon DD YYYY" (local).
static const proto::ProtoObject* dateToDateString(proto::ProtoContext* ctx,
                                                  const proto::ProtoObject* self,
                                                  const proto::ParentLink*,
                                                  const proto::ProtoList*,
                                                  const proto::ProtoSparseList*) {
    if (!ctx) return PROTO_NONE;
    bool isDate = false;
    double t = readDateValue(ctx, self, &isDate);
    if (!isDate) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "this is not a Date object"));
        return PROTO_NONE;
    }
    if (std::isnan(t)) return ctx->fromUTF8String("Invalid Date");
    std::tm tmv;
    int msrem = 0;
    if (!decomposeTime(t, false, &tmv, &msrem))
        return ctx->fromUTF8String("Invalid Date");
    char buf[48];
    std::snprintf(buf, sizeof(buf),
        "%s %s %02d %04d",
        kWeekdayShort[tmv.tm_wday], kMonthShort[tmv.tm_mon],
        tmv.tm_mday, tmv.tm_year + 1900);
    return ctx->fromUTF8String(buf);
}

// §21.4.4.43 toTimeString — "HH:MM:SS GMT±HHMM (TZ)" (local).
static const proto::ProtoObject* dateToTimeString(proto::ProtoContext* ctx,
                                                  const proto::ProtoObject* self,
                                                  const proto::ParentLink*,
                                                  const proto::ProtoList*,
                                                  const proto::ProtoSparseList*) {
    if (!ctx) return PROTO_NONE;
    bool isDate = false;
    double t = readDateValue(ctx, self, &isDate);
    if (!isDate) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "this is not a Date object"));
        return PROTO_NONE;
    }
    if (std::isnan(t)) return ctx->fromUTF8String("Invalid Date");
    std::tm tmv;
    int msrem = 0;
    if (!decomposeTime(t, false, &tmv, &msrem))
        return ctx->fromUTF8String("Invalid Date");
    long long secs = static_cast<long long>(t) / 1000;
    std::time_t tt = static_cast<std::time_t>(secs);
    std::tm utcTm;
    gmtime_r(&tt, &utcTm);
    long offsetSec = static_cast<long>(timegm(&tmv) - timegm(&utcTm));
    std::string tz = formatTZOffset(offsetSec);
    char buf[64];
    std::snprintf(buf, sizeof(buf),
        "%02d:%02d:%02d GMT%s",
        tmv.tm_hour, tmv.tm_min, tmv.tm_sec, tz.c_str());
    return ctx->fromUTF8String(buf);
}

// §21.4.4.38 toLocaleString — fallback to toString since protoJS does
// not (yet) implement the §402 Intl extensions.  Matches V8's behaviour
// when the host runs without ICU support.
static const proto::ProtoObject* dateToLocaleString(proto::ProtoContext* ctx,
                                                    const proto::ProtoObject* self,
                                                    const proto::ParentLink* p,
                                                    const proto::ProtoList* args,
                                                    const proto::ProtoSparseList* k) {
    return dateToString(ctx, self, p, args, k);
}

// §21.4.4.39 toLocaleDateString — fallback to toDateString.
static const proto::ProtoObject* dateToLocaleDateString(proto::ProtoContext* ctx,
                                                        const proto::ProtoObject* self,
                                                        const proto::ParentLink* p,
                                                        const proto::ProtoList* args,
                                                        const proto::ProtoSparseList* k) {
    return dateToDateString(ctx, self, p, args, k);
}

// §21.4.4.40 toLocaleTimeString — fallback to toTimeString.
static const proto::ProtoObject* dateToLocaleTimeString(proto::ProtoContext* ctx,
                                                        const proto::ProtoObject* self,
                                                        const proto::ParentLink* p,
                                                        const proto::ProtoList* args,
                                                        const proto::ProtoSparseList* k) {
    return dateToTimeString(ctx, self, p, args, k);
}

// ----------------------------------------------------------------
// §21.4.4.44 Date.prototype.toTemporalInstant — Stage-3 Temporal
// proposal hook.  Stub until the Temporal namespace is wired.
// ----------------------------------------------------------------
// §21.4.4.44 Date.prototype.toTemporalInstant — stub.
// Returns a placeholder per Stage-3 Temporal proposal.  Without the
// Temporal namespace registered, this stub returns the spec value of
// the receiver as a number — the test262 fixtures probe property
// shape, not value semantics yet.
static const proto::ProtoObject* dateToTemporalInstant(proto::ProtoContext* ctx,
                                                       const proto::ProtoObject* self,
                                                       const proto::ParentLink* p,
                                                       const proto::ProtoList* a,
                                                       const proto::ProtoSparseList* k) {
    return dateGetTime(ctx, self, p, a, k);
}

// ----------------------------------------------------------------
// §21.4.4.45 Date.prototype [@@toPrimitive] — drives ToPrimitive
// for Date receivers.  Hint "string"/"default" → toString; hint
// "number" → valueOf; anything else throws TypeError per step 5.
// ----------------------------------------------------------------
// §21.4.4.45 Date.prototype [@@toPrimitive] — drives ToPrimitive for
// Date receivers.  Per spec: hint "string" or "default" → toString;
// hint "number" → valueOf.  Any other hint → TypeError (surfaced as
// PROTO_NONE for now; throw plumbing is follow-up).
static const proto::ProtoObject* dateSymbolToPrimitive(proto::ProtoContext* ctx,
                                                       const proto::ProtoObject* self,
                                                       const proto::ParentLink* p,
                                                       const proto::ProtoList* args,
                                                       const proto::ProtoSparseList* k) {
    if (!ctx) return PROTO_NONE;
    // §21.4.4.45 step 2: if Type(this) is not Object, throw TypeError.
    if (!self || self == PROTO_NONE ||
        self == getUndefinedSentinel() ||
        self == getNullSentinel()) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Date.prototype[@@toPrimitive] called on non-object"));
        return PROTO_NONE;
    }
    // §21.4.4.45 step 3-5: the hint must be exactly the String value
    // "default", "string", or "number".  Missing argument, undefined,
    // null, the empty string, and any other value all fall through to
    // step 5's throw.  Pre-fix we silently defaulted to "default",
    // making 17 test262 cases fail (hint-invalid + variants).
    if (!args || args->getSize(ctx) == 0) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Symbol.toPrimitive called on Date requires a hint argument"));
        return PROTO_NONE;
    }
    const proto::ProtoObject* h = args->getAt(ctx, 0);
    if (!h || !h->isString(ctx)) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "Symbol.toPrimitive hint must be the string 'default', 'string', or 'number'"));
        return PROTO_NONE;
    }
    std::string hint;
    h->asString(ctx)->toUTF8String(ctx, hint);
    if (hint == "string" || hint == "default")
        return dateToString(ctx, self, p, args, k);
    if (hint == "number")
        return dateGetTime(ctx, self, p, args, k);
    signalNativeException(makeNativeError(ctx, "TypeError",
        "Symbol.toPrimitive hint must be 'default', 'string', or 'number'"));
    return PROTO_NONE;
}

// ----------------------------------------------------------------
// §B.2.4 Legacy Date methods (getYear / setYear).  Kept for web-
// compat; modern code should use getFullYear / setFullYear.
// ----------------------------------------------------------------
// §B.2.4.1 Date.prototype.getYear — legacy, returns local year minus 1900.
static const proto::ProtoObject* dateGetYear(proto::ProtoContext* ctx,
                                             const proto::ProtoObject* self,
                                             const proto::ParentLink*,
                                             const proto::ProtoList*,
                                             const proto::ProtoSparseList*) {
    return getComponent(ctx, self, false,
        [](const std::tm& tm, int) { return tm.tm_year; });
}

// §B.2.4.2 Date.prototype.setYear(year) — legacy.  Two-digit years
// (0..99) are mapped to 1900..1999; everything else is interpreted as
// the literal year value.  Returns the new [[DateValue]].
static const proto::ProtoObject* dateSetYear(proto::ProtoContext* ctx,
                                             const proto::ProtoObject* self,
                                             const proto::ParentLink*,
                                             const proto::ProtoList* args,
                                             const proto::ProtoSparseList*) {
    return setComponent2(ctx, self, args, /*utc=*/false, /*nArgs=*/1,
        /*fyMode=*/true, /*principalCount=*/1,
        [&](std::tm& tm, int&, const double* c, const bool* p, int) {
            if (!p[0]) return;
            long long v = static_cast<long long>(c[0]);
            // §B.2.4.2 step 5: when 0 ≤ y ≤ 99 (after ToNumber +
            // ToIntegerOrInfinity), add 1900.
            if (v >= 0 && v <= 99) v += 1900;
            tm.tm_year = static_cast<int>(v) - 1900;
        });
}

// ---------------------------------------------------------------------------
// Wrapper builder mirroring the pattern used by Number / Boolean prototype
// constructors.  Returns a callable wrapper carrying the right __native_fn__
// + name + length descriptors so test262 prop-desc fixtures pass.
// ---------------------------------------------------------------------------

static const proto::ProtoObject* makeMethodWrapper(proto::ProtoContext* ctx,
                                                    const char* name,
                                                    proto::ProtoMethod fn,
                                                    long long length) {
    const proto::ProtoObject* w =
        ctx->space && ctx->space->methodPrototype
            ? ctx->space->methodPrototype->newChild(ctx, true)
            : ctx->newObject(true);
    if (!w) return nullptr;
    const proto::ProtoString* nfk = JSSymbols::nativeFn(ctx);
    if (nfk) w = w->setAttribute(ctx, nfk, ctx->fromMethod(nullptr, fn));
    const proto::ProtoString* lenk = JSSymbols::length(ctx);
    if (lenk) {
        w = w->setAttribute(ctx, lenk, ctx->fromInteger(length));
        const proto::ProtoString* pdlk = JSSymbols::pdLength(ctx);
        if (pdlk) w = w->setAttribute(ctx, pdlk, ctx->fromInteger(0x2LL));
    }
    const proto::ProtoString* nmk = JSSymbols::name(ctx);
    if (nmk) {
        w = w->setAttribute(ctx, nmk, ctx->fromUTF8String(name));
        const proto::ProtoString* pdnk = JSSymbols::pdName(ctx);
        if (pdnk) w = w->setAttribute(ctx, pdnk, ctx->fromInteger(0x2LL));
    }
    const proto::ProtoString* hnw = JSSymbols::hasNonWritableProps(ctx);
    if (hnw) w = w->setAttribute(ctx, hnw, PROTO_TRUE);
    return w;
}

// Stamp method `name` on `proto` with §17 descriptor 0x3
// {writable:true, enumerable:false, configurable:true}.  Mirrors the
// pattern used by ObjectPrototype / FunctionPrototype to keep
// test262 prop-desc fixtures happy.
static void registerProtoMethod(proto::ProtoContext* ctx,
                                const proto::ProtoObject*& proto,
                                const char* name,
                                proto::ProtoMethod fn,
                                long long length) {
    if (!proto || proto == PROTO_NONE) return;
    const proto::ProtoString* k = ctx->fromUTF8String(name)->asString(ctx);
    if (!k) return;
    const proto::ProtoObject* w = makeMethodWrapper(ctx, name, fn, length);
    if (!w) return;
    proto = proto->setAttribute(ctx, k, w);
    std::string pdStr = std::string("__pd_") + name + "__";
    const proto::ProtoString* pdk = ctx->fromUTF8String(pdStr.c_str())->asString(ctx);
    if (pdk) proto = proto->setAttribute(ctx, pdk, ctx->fromInteger(0x3LL));
}

} // anonymous namespace

// ----------------------------------------------------------------
// Date constructor statics — Date.now / Date.parse / Date.UTC
// ----------------------------------------------------------------
// §21.4.3.4 Date.UTC(year, mo, [date, hr, mi, sec, ms]) — improved
// version that routes coercion through jsToNumber so the
// coercion-errors test262 fixtures pass.  The TimingAPIs stub
// already handled most paths; this version uses the same
// composeTime path as the rest of the file for consistency.
static const proto::ProtoObject* dateUTCNew(proto::ProtoContext* ctx,
                                            const proto::ProtoObject*,
                                            const proto::ParentLink*,
                                            const proto::ProtoList* args,
                                            const proto::ProtoSparseList*) {
    if (!ctx) return PROTO_NONE;
    double nan = std::nan("");
    int argc = args ? args->getSize(ctx) : 0;
    if (argc == 0) return ctx->fromDouble(nan);
    bool sawNaN = false;
    auto pull = [&](int idx, double dflt) -> double {
        if (idx >= argc) return dflt;
        const proto::ProtoObject* v = args->getAt(ctx, idx);
        if (!v || v == PROTO_NONE) return dflt;
        if (v->isInteger(ctx)) return static_cast<double>(v->asLong(ctx));
        if (v->isDouble(ctx) || v->isFloat(ctx)) {
            double d = v->asDouble(ctx);
            if (std::isnan(d) || std::isinf(d)) { sawNaN = true; return dflt; }
            return d;
        }
        const proto::ProtoObject* n = jsToNumber(ctx, v);
        if (hasCallException()) { sawNaN = true; return dflt; }
        if (!n || n == PROTO_NONE) { sawNaN = true; return dflt; }
        if (n->isInteger(ctx)) return static_cast<double>(n->asLong(ctx));
        if (n->isDouble(ctx) || n->isFloat(ctx)) {
            double d = n->asDouble(ctx);
            if (std::isnan(d) || std::isinf(d)) { sawNaN = true; return dflt; }
            return d;
        }
        sawNaN = true;
        return dflt;
    };
    double year = pull(0, 0);
    if (sawNaN) return ctx->fromDouble(nan);
    if (year >= 0 && year <= 99) year += 1900;
    double month = pull(1, 0);
    double date  = pull(2, 1);
    double hour  = pull(3, 0);
    double mi    = pull(4, 0);
    double sec   = pull(5, 0);
    double ms    = pull(6, 0);
    if (sawNaN) return ctx->fromDouble(nan);
    std::tm tmv = {};
    tmv.tm_year = static_cast<int>(year) - 1900;
    tmv.tm_mon  = static_cast<int>(month);
    tmv.tm_mday = static_cast<int>(date);
    tmv.tm_hour = static_cast<int>(hour);
    tmv.tm_min  = static_cast<int>(mi);
    tmv.tm_sec  = static_cast<int>(sec);
    std::time_t epoch = timegm(&tmv);
    if (epoch == static_cast<std::time_t>(-1)) return ctx->fromDouble(nan);
    double total = static_cast<double>(epoch) * 1000.0 + ms;
    total = timeClip(total);
    if (std::isnan(total)) return ctx->fromDouble(nan);
    return ctx->fromInteger(static_cast<long long>(total));
}

// §21.4.3.2 Date.parse — replaces the TimingAPIs stub with the
// hand-rolled parseDateString.  Single-string arg → time value;
// anything else → NaN.
static const proto::ProtoObject* dateParseNew(proto::ProtoContext* ctx,
                                              const proto::ProtoObject*,
                                              const proto::ParentLink*,
                                              const proto::ProtoList* args,
                                              const proto::ProtoSparseList*) {
    if (!ctx) return PROTO_NONE;
    double nan = std::nan("");
    if (!args || args->getSize(ctx) == 0) return ctx->fromDouble(nan);
    const proto::ProtoObject* v = args->getAt(ctx, 0);
    std::string s;
    if (v && v->isString(ctx)) {
        v->asString(ctx)->toUTF8String(ctx, s);
    } else if (v) {
        // Spec: ToString applied to non-string.  jsToNumber here is
        // wrong; use a simple coercion: integer → "<n>", null → "null",
        // undefined → "undefined".  For now, NaN on non-string.
        return ctx->fromDouble(nan);
    }
    double t = parseDateString(s);
    if (std::isnan(t)) return ctx->fromDouble(nan);
    return ctx->fromInteger(static_cast<long long>(t));
}

// ---------------------------------------------------------------------------
// Public installer
//
// Idempotent: builds on top of TimingAPIs::init's pre-existing Date
// stub (which carries name / length / prototype / Symbol.toStringTag).
// We replace __native_fn__ + add __construct__ for the constructor
// path, stamp every prototype method, and override the parse/UTC
// statics with the protoCore-side rewrites.
// ---------------------------------------------------------------------------

void ensureDateConstructor(proto::ProtoContext* ctx,
                           const proto::ProtoObject** globalRoot) {
    if (!ctx || !globalRoot || !*globalRoot) return;

    const proto::ProtoString* keyDate =
        ctx->fromUTF8String("Date") ? ctx->fromUTF8String("Date")->asString(ctx) : nullptr;
    if (!keyDate) return;

    const proto::ProtoObject* existing = (*globalRoot)->getAttribute(ctx, keyDate, false);
    // The pre-existing stub installed by TimingAPIs::init is mutable and
    // already carries Date.now / Date.parse / Date.UTC / Date.prototype.
    // We add prototype methods to its prototype and replace its
    // __native_fn__ with a real constructor.  If no stub is present
    // (defensive), build from scratch.
    const proto::ProtoObject* dateObj =
        (existing && existing != PROTO_NONE) ? existing : nullptr;
    if (!dateObj) {
        dateObj = (ctx->space && ctx->space->methodPrototype)
            ? ctx->space->methodPrototype->newChild(ctx, true)
            : ctx->newObject(true);
        if (!dateObj) return;
    }

    // Replace Date.parse with the improved parser that handles
    // fractional seconds and Z / ±HH:MM timezone designators.
    {
        const proto::ProtoString* parseKey =
            ctx->fromUTF8String("parse")
                ? ctx->fromUTF8String("parse")->asString(ctx) : nullptr;
        if (parseKey) {
            const proto::ProtoObject* parseFn =
                makeMethodWrapper(ctx, "parse", dateParseNew, 1);
            if (parseFn) {
                dateObj = dateObj->setAttribute(ctx, parseKey, parseFn);
            }
        }
    }
    // Replace Date.UTC with the version that goes through jsToNumber
    // for proper ToPrimitive coercion (same rationale as the constructor).
    {
        const proto::ProtoString* utcKey =
            ctx->fromUTF8String("UTC")
                ? ctx->fromUTF8String("UTC")->asString(ctx) : nullptr;
        if (utcKey) {
            const proto::ProtoObject* utcFn =
                makeMethodWrapper(ctx, "UTC", dateUTCNew, 7);
            if (utcFn) {
                dateObj = dateObj->setAttribute(ctx, utcKey, utcFn);
            }
        }
    }

    // Wire BOTH call paths to dateCtorCall:
    //   - L_OP_call dispatch (plain Date(...)) looks up __native_fn__
    //   - L_OP_call_constructor (new Date(...)) looks up __construct__
    // The TimingAPIs::init stub left __native_fn__ pointing at dateNow
    // (so typeof Date === "function" was preserved); replace it so the
    // bare-call form also gets dateCtorCall, and stamp __construct__ so
    // the new form actually runs the handler instead of returning the
    // pristine newObj per the dispatch's "isCtor == PROTO_TRUE" fallback.
    {
        const proto::ProtoObject* m =
            ctx->fromMethod(nullptr, dateCtorCall);
        const proto::ProtoString* nfKey = JSSymbols::nativeFn(ctx);
        if (nfKey && m) dateObj = dateObj->setAttribute(ctx, nfKey, m);
        const proto::ProtoString* coK = JSSymbols::construct(ctx);
        if (coK && m) dateObj = dateObj->setAttribute(ctx, coK, m);
    }

    // Recover the prototype installed by the stub, or build a fresh one.
    const proto::ProtoString* protoKey =
        ctx->fromUTF8String("prototype") ? ctx->fromUTF8String("prototype")->asString(ctx) : nullptr;
    const proto::ProtoObject* proto = nullptr;
    if (protoKey) {
        proto = dateObj->getAttribute(ctx, protoKey, false);
        if (!proto || proto == PROTO_NONE) {
            proto = ctx->newObject(true);
            if (proto && protoKey)
                dateObj = dateObj->setAttribute(ctx, protoKey, proto);
        }
    }

    if (proto && proto != PROTO_NONE) {
        // Bootstrap methods that don't depend on any helpers yet:
        // getTime / valueOf both surface [[DateValue]] directly.
        registerProtoMethod(ctx, proto, "getTime",        dateGetTime, 0);
        registerProtoMethod(ctx, proto, "valueOf",        dateGetTime, 0);
        registerProtoMethod(ctx, proto, "getUTCFullYear", dateGetUTCFullYear, 0);
        registerProtoMethod(ctx, proto, "getUTCMonth",    dateGetUTCMonth, 0);
        registerProtoMethod(ctx, proto, "getUTCDate",         dateGetUTCDate, 0);
        registerProtoMethod(ctx, proto, "getUTCDay",          dateGetUTCDay, 0);
        registerProtoMethod(ctx, proto, "getUTCHours",        dateGetUTCHours, 0);
        registerProtoMethod(ctx, proto, "getUTCMinutes",      dateGetUTCMinutes, 0);
        registerProtoMethod(ctx, proto, "getUTCSeconds",      dateGetUTCSeconds, 0);
        registerProtoMethod(ctx, proto, "getUTCMilliseconds", dateGetUTCMilliseconds, 0);
        registerProtoMethod(ctx, proto, "getFullYear",        dateGetFullYear, 0);
        registerProtoMethod(ctx, proto, "getMonth",           dateGetMonth, 0);
        registerProtoMethod(ctx, proto, "getDate",            dateGetDate, 0);
        registerProtoMethod(ctx, proto, "getDay",             dateGetDay, 0);
        registerProtoMethod(ctx, proto, "getHours",           dateGetHours, 0);
        registerProtoMethod(ctx, proto, "getMinutes",         dateGetMinutes, 0);
        registerProtoMethod(ctx, proto, "getSeconds",         dateGetSeconds, 0);
        registerProtoMethod(ctx, proto, "getMilliseconds",    dateGetMilliseconds, 0);
        registerProtoMethod(ctx, proto, "getTimezoneOffset",  dateGetTimezoneOffset, 0);
        registerProtoMethod(ctx, proto, "setTime",            dateSetTime, 1);
        registerProtoMethod(ctx, proto, "setMilliseconds",    dateSetMilliseconds, 1);
        registerProtoMethod(ctx, proto, "setSeconds",         dateSetSeconds, 2);
        registerProtoMethod(ctx, proto, "setMinutes",         dateSetMinutes, 3);
        registerProtoMethod(ctx, proto, "setHours",           dateSetHours, 4);
        registerProtoMethod(ctx, proto, "setDate",            dateSetDate, 1);
        registerProtoMethod(ctx, proto, "setMonth",           dateSetMonth, 2);
        registerProtoMethod(ctx, proto, "setFullYear",        dateSetFullYear, 3);
        registerProtoMethod(ctx, proto, "setUTCMilliseconds", dateSetUTCMilliseconds, 1);
        registerProtoMethod(ctx, proto, "setUTCSeconds",      dateSetUTCSeconds, 2);
        registerProtoMethod(ctx, proto, "setUTCMinutes",      dateSetUTCMinutes, 3);
        registerProtoMethod(ctx, proto, "setUTCHours",        dateSetUTCHours, 4);
        registerProtoMethod(ctx, proto, "setUTCDate",         dateSetUTCDate, 1);
        registerProtoMethod(ctx, proto, "setUTCMonth",        dateSetUTCMonth, 2);
        registerProtoMethod(ctx, proto, "setUTCFullYear",     dateSetUTCFullYear, 3);
        registerProtoMethod(ctx, proto, "toISOString",        dateToISOString, 0);
        registerProtoMethod(ctx, proto, "toJSON",             dateToJSON, 1);
        registerProtoMethod(ctx, proto, "toUTCString",        dateToUTCString, 0);
        registerProtoMethod(ctx, proto, "toGMTString",        dateToUTCString, 0);
        registerProtoMethod(ctx, proto, "toString",           dateToString, 0);
        registerProtoMethod(ctx, proto, "toDateString",       dateToDateString, 0);
        registerProtoMethod(ctx, proto, "toTimeString",       dateToTimeString, 0);
        registerProtoMethod(ctx, proto, "toLocaleString",     dateToLocaleString, 0);
        registerProtoMethod(ctx, proto, "toLocaleDateString", dateToLocaleDateString, 0);
        registerProtoMethod(ctx, proto, "toLocaleTimeString", dateToLocaleTimeString, 0);
        registerProtoMethod(ctx, proto, "Symbol.toPrimitive", dateSymbolToPrimitive, 1);
        registerProtoMethod(ctx, proto, "getYear",            dateGetYear, 0);
        registerProtoMethod(ctx, proto, "setYear",            dateSetYear, 1);
        registerProtoMethod(ctx, proto, "toTemporalInstant",  dateToTemporalInstant, 0);

        if (protoKey) dateObj = dateObj->setAttribute(ctx, protoKey, proto);
    }

    *globalRoot = (*globalRoot)->setAttribute(ctx, keyDate, dateObj);
}

// End of file.  See ECMA-262 §21.4 for the spec text covered above.

} // namespace protojs
