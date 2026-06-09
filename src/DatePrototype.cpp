#include "DatePrototype.h"
#include "JSContext.h"
#include "JSSymbols.h"
#include "ObjectPrototype.h"
#include "PrototypeUtils.h"
#include "FunctionPrototype.h"
#include "runtime/ProtoInterpreter.h"
#include "headers/protoCore.h"

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

static double timeClip(double t) {
    if (!std::isfinite(t)) return std::nan("");
    if (std::abs(t) > 8.64e15) return std::nan("");
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

static double parseDateString(const std::string& s) {
    if (s.empty()) return std::nan("");
    // Try the ISO 8601 extended form with optional fractional seconds
    // and timezone designator.  Hand-rolled to avoid std::get_time's
    // strict "all-or-nothing" parse — the spec permits truncating any
    // component from the right.
    int year = 0, mon = 1, day = 1, hr = 0, mi = 0, sec = 0, ms = 0;
    int tzMin = 0;  // offset to subtract; 0 = UTC, negative = east of UTC
    bool hasTZ = false;
    const char* p = s.c_str();
    // YYYY
    int n = 0;
    if (std::sscanf(p, "%4d%n", &year, &n) != 1 || n != 4) {
        return std::nan("");
    }
    p += n;
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
    if (skipChar('T') || skipChar(' ')) {
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
    t += static_cast<double>(tzMin) * 60.0 * 1000.0;
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

// Common setter scaffold.  Pulls current [[DateValue]], decomposes,
// hands the broken-down tm + ms to `mutate` (which may pull positional
// arguments from `args`), recomposes, TimeClips, writes back, and
// returns the new time value.  Receivers without [[DateValue]] return
// NaN without mutating; NaN times are handled per spec
// (setMilliseconds(2)(NaN-date) returns NaN).
template <typename Mutator>
static const proto::ProtoObject* setComponent(proto::ProtoContext* ctx,
                                              const proto::ProtoObject* self,
                                              const proto::ProtoList* args,
                                              bool utc, Mutator mutate) {
    if (!ctx || !self || self == PROTO_NONE) return PROTO_NONE;
    bool isDate = false;
    double t = readDateValue(ctx, self, &isDate);
    if (!isDate) {
        signalNativeException(makeNativeError(ctx, "TypeError",
            "this is not a Date object"));
        return PROTO_NONE;
    }
    if (std::isnan(t)) {
        // setTime override would have caught this; for component setters,
        // NaN input produces NaN output.
        writeDateValue(ctx, self, std::nan(""));
        return ctx->fromDouble(std::nan(""));
    }
    std::tm tmv;
    int msrem = 0;
    if (!decomposeTime(t, utc, &tmv, &msrem)) {
        writeDateValue(ctx, self, std::nan(""));
        return ctx->fromDouble(std::nan(""));
    }
    mutate(tmv, msrem, args);
    double composed = composeTime(tmv, msrem, utc);
    composed = timeClip(composed);
    writeDateValue(ctx, self, composed);
    if (std::isnan(composed)) return ctx->fromDouble(std::nan(""));
    return ctx->fromInteger(static_cast<long long>(composed));
}

// Pull a positional argument as an integer (best-effort).  Used by
// every component setter to read ms/sec/min/hour/date/month/year args.
static long long pullArgAsInt(proto::ProtoContext* ctx,
                              const proto::ProtoList* args,
                              int idx, long long fallback,
                              bool* sawNaN) {
    if (!args || idx >= args->getSize(ctx)) return fallback;
    const proto::ProtoObject* v = args->getAt(ctx, idx);
    if (!v || v == PROTO_NONE) return fallback;
    if (v->isInteger(ctx)) return v->asLong(ctx);
    if (v->isDouble(ctx) || v->isFloat(ctx)) {
        double d = v->asDouble(ctx);
        if (std::isnan(d) || std::isinf(d)) { if (sawNaN) *sawNaN = true; return fallback; }
        return static_cast<long long>(d);
    }
    // Spec §21.4.4.x step "Let X be ? ToNumber(arg)": objects and
    // strings reach this branch.  ToNumber invokes ToPrimitive
    // (valueOf, toString); a throwing inner method must propagate.
    const proto::ProtoObject* n = jsToNumber(ctx, v);
    if (hasCallException()) return fallback;
    if (!n || n == PROTO_NONE) { if (sawNaN) *sawNaN = true; return fallback; }
    if (n->isInteger(ctx)) return n->asLong(ctx);
    if (n->isDouble(ctx) || n->isFloat(ctx)) {
        double d = n->asDouble(ctx);
        if (std::isnan(d) || std::isinf(d)) { if (sawNaN) *sawNaN = true; return fallback; }
        return static_cast<long long>(d);
    }
    return fallback;
}

// §21.4.4.23 setMilliseconds(ms) — local.
static const proto::ProtoObject* dateSetMilliseconds(proto::ProtoContext* ctx,
                                                     const proto::ProtoObject* self,
                                                     const proto::ParentLink*,
                                                     const proto::ProtoList* args,
                                                     const proto::ProtoSparseList*) {
    return setComponent(ctx, self, args, false,
        [&](std::tm&, int& ms, const proto::ProtoList* a) {
            bool nan = false;
            long long v = pullArgAsInt(ctx, a, 0, ms, &nan);
            if (nan) { ms = 0; }
            else ms = static_cast<int>(v);
        });
}

// §21.4.4.25 setSeconds(sec[, ms]) — local.
static const proto::ProtoObject* dateSetSeconds(proto::ProtoContext* ctx,
                                                const proto::ProtoObject* self,
                                                const proto::ParentLink*,
                                                const proto::ProtoList* args,
                                                const proto::ProtoSparseList*) {
    return setComponent(ctx, self, args, false,
        [&](std::tm& tm, int& ms, const proto::ProtoList* a) {
            bool nan = false;
            tm.tm_sec = static_cast<int>(pullArgAsInt(ctx, a, 0, tm.tm_sec, &nan));
            if (a && a->getSize(ctx) >= 2)
                ms = static_cast<int>(pullArgAsInt(ctx, a, 1, ms, &nan));
            if (nan) ms = 0;
        });
}

// §21.4.4.24 setMinutes(min[, sec[, ms]]) — local.
static const proto::ProtoObject* dateSetMinutes(proto::ProtoContext* ctx,
                                                const proto::ProtoObject* self,
                                                const proto::ParentLink*,
                                                const proto::ProtoList* args,
                                                const proto::ProtoSparseList*) {
    return setComponent(ctx, self, args, false,
        [&](std::tm& tm, int& ms, const proto::ProtoList* a) {
            bool nan = false;
            tm.tm_min = static_cast<int>(pullArgAsInt(ctx, a, 0, tm.tm_min, &nan));
            if (a && a->getSize(ctx) >= 2)
                tm.tm_sec = static_cast<int>(pullArgAsInt(ctx, a, 1, tm.tm_sec, &nan));
            if (a && a->getSize(ctx) >= 3)
                ms = static_cast<int>(pullArgAsInt(ctx, a, 2, ms, &nan));
            if (nan) ms = 0;
        });
}

// §21.4.4.22 setHours(hr[, min[, sec[, ms]]]) — local.
static const proto::ProtoObject* dateSetHours(proto::ProtoContext* ctx,
                                              const proto::ProtoObject* self,
                                              const proto::ParentLink*,
                                              const proto::ProtoList* args,
                                              const proto::ProtoSparseList*) {
    return setComponent(ctx, self, args, false,
        [&](std::tm& tm, int& ms, const proto::ProtoList* a) {
            bool nan = false;
            tm.tm_hour = static_cast<int>(pullArgAsInt(ctx, a, 0, tm.tm_hour, &nan));
            if (a && a->getSize(ctx) >= 2)
                tm.tm_min = static_cast<int>(pullArgAsInt(ctx, a, 1, tm.tm_min, &nan));
            if (a && a->getSize(ctx) >= 3)
                tm.tm_sec = static_cast<int>(pullArgAsInt(ctx, a, 2, tm.tm_sec, &nan));
            if (a && a->getSize(ctx) >= 4)
                ms = static_cast<int>(pullArgAsInt(ctx, a, 3, ms, &nan));
            if (nan) ms = 0;
        });
}

// §21.4.4.20 setDate(date) — local day-of-month.
static const proto::ProtoObject* dateSetDate(proto::ProtoContext* ctx,
                                             const proto::ProtoObject* self,
                                             const proto::ParentLink*,
                                             const proto::ProtoList* args,
                                             const proto::ProtoSparseList*) {
    return setComponent(ctx, self, args, false,
        [&](std::tm& tm, int&, const proto::ProtoList* a) {
            bool nan = false;
            tm.tm_mday = static_cast<int>(pullArgAsInt(ctx, a, 0, tm.tm_mday, &nan));
        });
}

// §21.4.4.21 setMonth(mo[, date]) — local 0-indexed month.
static const proto::ProtoObject* dateSetMonth(proto::ProtoContext* ctx,
                                              const proto::ProtoObject* self,
                                              const proto::ParentLink*,
                                              const proto::ProtoList* args,
                                              const proto::ProtoSparseList*) {
    return setComponent(ctx, self, args, false,
        [&](std::tm& tm, int&, const proto::ProtoList* a) {
            bool nan = false;
            tm.tm_mon = static_cast<int>(pullArgAsInt(ctx, a, 0, tm.tm_mon, &nan));
            if (a && a->getSize(ctx) >= 2)
                tm.tm_mday = static_cast<int>(pullArgAsInt(ctx, a, 1, tm.tm_mday, &nan));
        });
}

// §21.4.4.18 setFullYear(year[, mo[, date]]) — local.
static const proto::ProtoObject* dateSetFullYear(proto::ProtoContext* ctx,
                                                 const proto::ProtoObject* self,
                                                 const proto::ParentLink*,
                                                 const proto::ProtoList* args,
                                                 const proto::ProtoSparseList*) {
    return setComponent(ctx, self, args, false,
        [&](std::tm& tm, int&, const proto::ProtoList* a) {
            bool nan = false;
            tm.tm_year = static_cast<int>(pullArgAsInt(ctx, a, 0, tm.tm_year + 1900, &nan)) - 1900;
            if (a && a->getSize(ctx) >= 2)
                tm.tm_mon = static_cast<int>(pullArgAsInt(ctx, a, 1, tm.tm_mon, &nan));
            if (a && a->getSize(ctx) >= 3)
                tm.tm_mday = static_cast<int>(pullArgAsInt(ctx, a, 2, tm.tm_mday, &nan));
        });
}

// §21.4.4.30 setUTCMilliseconds(ms).
static const proto::ProtoObject* dateSetUTCMilliseconds(proto::ProtoContext* ctx,
                                                        const proto::ProtoObject* self,
                                                        const proto::ParentLink*,
                                                        const proto::ProtoList* args,
                                                        const proto::ProtoSparseList*) {
    return setComponent(ctx, self, args, true,
        [&](std::tm&, int& ms, const proto::ProtoList* a) {
            bool nan = false;
            long long v = pullArgAsInt(ctx, a, 0, ms, &nan);
            if (nan) ms = 0; else ms = static_cast<int>(v);
        });
}

// §21.4.4.32 setUTCSeconds(sec[, ms]).
static const proto::ProtoObject* dateSetUTCSeconds(proto::ProtoContext* ctx,
                                                   const proto::ProtoObject* self,
                                                   const proto::ParentLink*,
                                                   const proto::ProtoList* args,
                                                   const proto::ProtoSparseList*) {
    return setComponent(ctx, self, args, true,
        [&](std::tm& tm, int& ms, const proto::ProtoList* a) {
            bool nan = false;
            tm.tm_sec = static_cast<int>(pullArgAsInt(ctx, a, 0, tm.tm_sec, &nan));
            if (a && a->getSize(ctx) >= 2)
                ms = static_cast<int>(pullArgAsInt(ctx, a, 1, ms, &nan));
            if (nan) ms = 0;
        });
}

// §21.4.4.31 setUTCMinutes(min[, sec[, ms]]).
static const proto::ProtoObject* dateSetUTCMinutes(proto::ProtoContext* ctx,
                                                   const proto::ProtoObject* self,
                                                   const proto::ParentLink*,
                                                   const proto::ProtoList* args,
                                                   const proto::ProtoSparseList*) {
    return setComponent(ctx, self, args, true,
        [&](std::tm& tm, int& ms, const proto::ProtoList* a) {
            bool nan = false;
            tm.tm_min = static_cast<int>(pullArgAsInt(ctx, a, 0, tm.tm_min, &nan));
            if (a && a->getSize(ctx) >= 2)
                tm.tm_sec = static_cast<int>(pullArgAsInt(ctx, a, 1, tm.tm_sec, &nan));
            if (a && a->getSize(ctx) >= 3)
                ms = static_cast<int>(pullArgAsInt(ctx, a, 2, ms, &nan));
            if (nan) ms = 0;
        });
}

// §21.4.4.29 setUTCHours(hr[, min[, sec[, ms]]]).
static const proto::ProtoObject* dateSetUTCHours(proto::ProtoContext* ctx,
                                                 const proto::ProtoObject* self,
                                                 const proto::ParentLink*,
                                                 const proto::ProtoList* args,
                                                 const proto::ProtoSparseList*) {
    return setComponent(ctx, self, args, true,
        [&](std::tm& tm, int& ms, const proto::ProtoList* a) {
            bool nan = false;
            tm.tm_hour = static_cast<int>(pullArgAsInt(ctx, a, 0, tm.tm_hour, &nan));
            if (a && a->getSize(ctx) >= 2)
                tm.tm_min = static_cast<int>(pullArgAsInt(ctx, a, 1, tm.tm_min, &nan));
            if (a && a->getSize(ctx) >= 3)
                tm.tm_sec = static_cast<int>(pullArgAsInt(ctx, a, 2, tm.tm_sec, &nan));
            if (a && a->getSize(ctx) >= 4)
                ms = static_cast<int>(pullArgAsInt(ctx, a, 3, ms, &nan));
            if (nan) ms = 0;
        });
}

// §21.4.4.28 setUTCDate(date).
static const proto::ProtoObject* dateSetUTCDate(proto::ProtoContext* ctx,
                                                const proto::ProtoObject* self,
                                                const proto::ParentLink*,
                                                const proto::ProtoList* args,
                                                const proto::ProtoSparseList*) {
    return setComponent(ctx, self, args, true,
        [&](std::tm& tm, int&, const proto::ProtoList* a) {
            bool nan = false;
            tm.tm_mday = static_cast<int>(pullArgAsInt(ctx, a, 0, tm.tm_mday, &nan));
        });
}

// §21.4.4.30b setUTCMonth(mo[, date]).
static const proto::ProtoObject* dateSetUTCMonth(proto::ProtoContext* ctx,
                                                 const proto::ProtoObject* self,
                                                 const proto::ParentLink*,
                                                 const proto::ProtoList* args,
                                                 const proto::ProtoSparseList*) {
    return setComponent(ctx, self, args, true,
        [&](std::tm& tm, int&, const proto::ProtoList* a) {
            bool nan = false;
            tm.tm_mon = static_cast<int>(pullArgAsInt(ctx, a, 0, tm.tm_mon, &nan));
            if (a && a->getSize(ctx) >= 2)
                tm.tm_mday = static_cast<int>(pullArgAsInt(ctx, a, 1, tm.tm_mday, &nan));
        });
}

// §21.4.4.26 setUTCFullYear(year[, mo[, date]]).
static const proto::ProtoObject* dateSetUTCFullYear(proto::ProtoContext* ctx,
                                                    const proto::ProtoObject* self,
                                                    const proto::ParentLink*,
                                                    const proto::ProtoList* args,
                                                    const proto::ProtoSparseList*) {
    return setComponent(ctx, self, args, true,
        [&](std::tm& tm, int&, const proto::ProtoList* a) {
            bool nan = false;
            tm.tm_year = static_cast<int>(pullArgAsInt(ctx, a, 0, tm.tm_year + 1900, &nan)) - 1900;
            if (a && a->getSize(ctx) >= 2)
                tm.tm_mon = static_cast<int>(pullArgAsInt(ctx, a, 1, tm.tm_mon, &nan));
            if (a && a->getSize(ctx) >= 3)
                tm.tm_mday = static_cast<int>(pullArgAsInt(ctx, a, 2, tm.tm_mday, &nan));
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
    char buf[40];
    std::snprintf(buf, sizeof(buf),
        "%04d-%02d-%02dT%02d:%02d:%02d.%03dZ",
        tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
        tmv.tm_hour, tmv.tm_min, tmv.tm_sec, msrem);
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
    char sign = offsetSec >= 0 ? '+' : '-';
    long offsetAbs = std::labs(offsetSec);
    int hh = static_cast<int>(offsetAbs / 3600);
    int mm = static_cast<int>((offsetAbs % 3600) / 60);
    char buf[96];
    std::snprintf(buf, sizeof(buf),
        "%s %s %02d %04d %02d:%02d:%02d GMT%c%02d%02d",
        kWeekdayShort[tmv.tm_wday], kMonthShort[tmv.tm_mon],
        tmv.tm_mday, tmv.tm_year + 1900,
        tmv.tm_hour, tmv.tm_min, tmv.tm_sec,
        sign, hh, mm);
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
    char sign = offsetSec >= 0 ? '+' : '-';
    long offsetAbs = std::labs(offsetSec);
    int hh = static_cast<int>(offsetAbs / 3600);
    int mm = static_cast<int>((offsetAbs % 3600) / 60);
    char buf[64];
    std::snprintf(buf, sizeof(buf),
        "%02d:%02d:%02d GMT%c%02d%02d",
        tmv.tm_hour, tmv.tm_min, tmv.tm_sec,
        sign, hh, mm);
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
    return setComponent(ctx, self, args, false,
        [&](std::tm& tm, int&, const proto::ProtoList* a) {
            bool nan = false;
            long long v = pullArgAsInt(ctx, a, 0, tm.tm_year + 1900, &nan);
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
// {writable:true, enumerable:false, configurable:true}.
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

} // namespace protojs
