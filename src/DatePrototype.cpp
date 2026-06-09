#include "DatePrototype.h"
#include "JSContext.h"
#include "JSSymbols.h"
#include "ObjectPrototype.h"
#include "PrototypeUtils.h"
#include "FunctionPrototype.h"
#include "headers/protoCore.h"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <limits>
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
    // Truncate toward zero.
    return (t >= 0 ? std::floor(t) : std::ceil(t));
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
        // §21.4.2.2 single arg: ToPrimitive then ToNumber if numeric.
        const proto::ProtoObject* v = args->getAt(ctx, 0);
        if (v && (v->isInteger(ctx) || v->isDouble(ctx) || v->isFloat(ctx))) {
            t = v->isInteger(ctx)
                ? static_cast<double>(v->asLong(ctx))
                : v->asDouble(ctx);
        } else {
            // Non-numeric single arg falls through to NaN for now;
            // Date(string) parsing lands in a follow-up commit.
            t = std::nan("");
        }
        t = timeClip(t);
    } else {
        // Multi-arg form (year, month, [date, hour, min, sec, ms])
        // lands in a follow-up commit.  For now: NaN.
        t = std::nan("");
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
    if (!isDate || std::isnan(t)) return ctx->fromDouble(std::nan(""));
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
    if (!isDate || std::isnan(t)) return ctx->fromDouble(std::nan(""));
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
        // §21.4.4.1 — throw TypeError.  For now we surface the spec
        // value NaN; the throwing path is a follow-up commit.
        return ctx->fromDouble(std::nan(""));
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
    if (!isDate) return ctx->fromDouble(std::nan(""));
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

// §21.4.4.27 Date.prototype.setTime — direct assign TimeClip(ToNumber(arg)).
static const proto::ProtoObject* dateSetTime(proto::ProtoContext* ctx,
                                             const proto::ProtoObject* self,
                                             const proto::ParentLink*,
                                             const proto::ProtoList* args,
                                             const proto::ProtoSparseList*) {
    if (!ctx || !self || self == PROTO_NONE) return PROTO_NONE;
    double t = std::nan("");
    if (args && args->getSize(ctx) > 0) {
        const proto::ProtoObject* v = args->getAt(ctx, 0);
        if (v && v->isInteger(ctx)) t = static_cast<double>(v->asLong(ctx));
        else if (v && (v->isDouble(ctx) || v->isFloat(ctx))) t = v->asDouble(ctx);
    }
    t = timeClip(t);
    writeDateValue(ctx, self, t);
    if (std::isnan(t)) return ctx->fromDouble(std::nan(""));
    return ctx->fromInteger(static_cast<long long>(t));
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

        if (protoKey) dateObj = dateObj->setAttribute(ctx, protoKey, proto);
    }

    *globalRoot = (*globalRoot)->setAttribute(ctx, keyDate, dateObj);
}

} // namespace protojs
