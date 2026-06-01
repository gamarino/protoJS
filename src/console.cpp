#include "console.h"
#include "ProtoNativeModule.h"
#include "runtime/ProtoInterpreter.h"
#include "JSSymbols.h"
#include "ArrayElementsStorage.h"
#include <chrono>
#include <cmath>
#include <iostream>
#include <mutex>
#include <string>
#include <unordered_map>

namespace protojs {

namespace {

/** Convert a single ProtoObject to its string representation for printing. */
static void printProtoValue(proto::ProtoContext* ctx, const proto::ProtoObject* val,
                             std::ostream& out, int depth = 0) {
    if (!ctx || !val || val == PROTO_NONE || val->isNone(ctx)) {
        out << "undefined";
        return;
    }
    if (val == protojs::getNullSentinel()) {
        out << "null";
        return;
    }
    if (val == protojs::getUndefinedSentinel()) {
        out << "undefined";
        return;
    }
    if (val->isString(ctx)) {
        const proto::ProtoString* s = val->asString(ctx);
        if (s) {
            std::string tmp;
            s->toUTF8String(ctx, tmp);
            // Top-level strings print unquoted (Node.js console.log);
            // nested ones get JSON-style quotes for readability.
            if (depth == 0) out << tmp;
            else out << '"' << tmp << '"';
        }
        return;
    }
    if (val->isBoolean(ctx)) {
        out << (val->asBoolean(ctx) ? "true" : "false");
        return;
    }
    if (val->isInteger(ctx)) {
        out << val->asLong(ctx);
        return;
    }
    if (val->isDouble(ctx)) {
        const double d = val->asDouble(ctx);
        if (std::isnan(d))      out << "NaN";
        else if (std::isinf(d)) out << (d < 0 ? "-Infinity" : "Infinity");
        else {
            // Match JS ToString(Number) — drop trailing zeros, use up to
            // 17 significant digits for round-trip safety.  Integer-valued
            // doubles in safe-int range print as plain integers.
            char buf[64];
            if (d == std::trunc(d) && std::abs(d) < 1e21) {
                long long iv = static_cast<long long>(d);
                if (static_cast<double>(iv) == d) {
                    snprintf(buf, sizeof(buf), "%lld", iv);
                    out << buf;
                    return;
                }
            }
            snprintf(buf, sizeof(buf), "%.17g", d);
            out << buf;
        }
        return;
    }
    // Real arrays: print [v1, v2, ...]
    {
        const proto::ProtoObject* isArrObj = nullptr;
        const proto::ProtoString* isArrKey = JSSymbols::isArray(ctx);
        if (isArrKey) isArrObj = val->getAttribute(ctx, isArrKey, false);
        if (isArrObj == PROTO_TRUE) {
            out << '[';
            const proto::ProtoList* els = getArrayElements(ctx, val);
            if (els) {
                size_t sz = els->getSize(ctx);
                for (size_t i = 0; i < sz; ++i) {
                    if (i > 0) out << ", ";
                    printProtoValue(ctx, els->getAt(ctx, static_cast<int>(i)), out, depth + 1);
                }
            }
            out << ']';
            return;
        }
    }
    // Method / function callable
    if (val->isMethod(ctx)) {
        out << "[Function]";
        return;
    }
    // Plain object: print {k: v, ...}; recurse one level only to avoid blowup.
    if (depth >= 4) {
        out << "[Object]";
        return;
    }
    out << '{';
    const proto::ProtoSparseList* attrs = val->getOwnAttributes(ctx);
    if (attrs) {
        const proto::ProtoSparseListIterator* it = attrs->getIterator(ctx);
        bool first = true;
        while (it && it->hasNext(ctx)) {
            unsigned long hash = it->nextKey(ctx);
            const proto::ProtoObject* v = it->nextValue(ctx);
            std::string key = JSSymbols::getNameFromHash(ctx, hash);
            if (key.empty()) {
                const proto::ProtoString* sym = reinterpret_cast<const proto::ProtoString*>(hash);
                if (hash > 0x1000) sym->toUTF8String(ctx, key);
            }
            // Skip internal "__"-prefixed keys.
            if (!key.empty() && key[0] != '_') {
                if (!first) out << ", ";
                out << key << ": ";
                printProtoValue(ctx, v, out, depth + 1);
                first = false;
            }
            it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
        }
    }
    out << '}';
}

} // anonymous namespace

const proto::ProtoObject* Console::log(proto::ProtoContext* ctx,
                                        const proto::ProtoObject* /*self*/,
                                        const proto::ParentLink* /*parentLink*/,
                                        const proto::ProtoList* args,
                                        const proto::ProtoSparseList* /*kwargs*/) {
    if (!ctx) return PROTO_NONE;
    int argc = args ? static_cast<int>(args->getSize(ctx)) : 0;
    for (int i = 0; i < argc; i++) {
        if (i > 0) std::cout << " ";
        printProtoValue(ctx, args->getAt(ctx, i), std::cout);
    }
    std::cout << "\n";
    std::cout.flush();
    return PROTO_NONE;
}

const proto::ProtoObject* Console::error(proto::ProtoContext* ctx,
                                          const proto::ProtoObject* /*self*/,
                                          const proto::ParentLink* /*parentLink*/,
                                          const proto::ProtoList* args,
                                          const proto::ProtoSparseList* /*kwargs*/) {
    if (!ctx) return PROTO_NONE;
    int argc = args ? static_cast<int>(args->getSize(ctx)) : 0;
    for (int i = 0; i < argc; i++) {
        if (i > 0) std::cerr << " ";
        printProtoValue(ctx, args->getAt(ctx, i), std::cerr);
    }
    std::cerr << "\n";
    return PROTO_NONE;
}

const proto::ProtoObject* Console::warn(proto::ProtoContext* ctx,
                                         const proto::ProtoObject* /*self*/,
                                         const proto::ParentLink* /*parentLink*/,
                                         const proto::ProtoList* args,
                                         const proto::ProtoSparseList* /*kwargs*/) {
    if (!ctx) return PROTO_NONE;
    std::cerr << "Warning: ";
    int argc = args ? static_cast<int>(args->getSize(ctx)) : 0;
    for (int i = 0; i < argc; i++) {
        if (i > 0) std::cerr << " ";
        printProtoValue(ctx, args->getAt(ctx, i), std::cerr);
    }
    std::cerr << "\n";
    return PROTO_NONE;
}

// Per-process backing store for console.time / console.timeEnd / console.timeLog.
// Node.js semantics: time(label) records the current monotonic clock under
// the label; timeEnd(label) prints "<label>: <elapsed>ms" and removes the
// entry; timeLog(label, ...rest) prints elapsed without removing.  Default
// label is "default".  The store is process-wide because timers are
// addressed by string and benchmarks that thread time/timeEnd across
// callbacks expect consistent identity.
namespace {
    struct TimerStore {
        std::mutex mtx;
        std::unordered_map<std::string, std::chrono::steady_clock::time_point> timers;
    };
    TimerStore& timerStore() {
        static TimerStore s;
        return s;
    }
    std::string firstStringArg(proto::ProtoContext* ctx, const proto::ProtoList* args,
                                const std::string& fallback) {
        if (!args || args->getSize(ctx) == 0) return fallback;
        const proto::ProtoObject* a0 = args->getAt(ctx, 0);
        if (a0 && a0->isString(ctx)) {
            std::string s;
            a0->asString(ctx)->toUTF8String(ctx, s);
            return s;
        }
        return fallback;
    }
}

const proto::ProtoObject* Console::time(proto::ProtoContext* ctx,
                                         const proto::ProtoObject* /*self*/,
                                         const proto::ParentLink* /*parentLink*/,
                                         const proto::ProtoList* args,
                                         const proto::ProtoSparseList* /*kwargs*/) {
    if (!ctx) return PROTO_NONE;
    std::string label = firstStringArg(ctx, args, "default");
    auto& store = timerStore();
    std::lock_guard<std::mutex> lock(store.mtx);
    store.timers[label] = std::chrono::steady_clock::now();
    return PROTO_NONE;
}

const proto::ProtoObject* Console::timeEnd(proto::ProtoContext* ctx,
                                            const proto::ProtoObject* /*self*/,
                                            const proto::ParentLink* /*parentLink*/,
                                            const proto::ProtoList* args,
                                            const proto::ProtoSparseList* /*kwargs*/) {
    if (!ctx) return PROTO_NONE;
    std::string label = firstStringArg(ctx, args, "default");
    auto& store = timerStore();
    std::chrono::steady_clock::time_point start;
    bool found = false;
    {
        std::lock_guard<std::mutex> lock(store.mtx);
        auto it = store.timers.find(label);
        if (it != store.timers.end()) {
            start = it->second;
            found = true;
            store.timers.erase(it);
        }
    }
    if (!found) {
        std::cerr << "Warning: No such label '" << label << "' for console.timeEnd()\n";
        return PROTO_NONE;
    }
    auto end = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(end - start).count();
    std::cout << label << ": " << ms << "ms\n";
    std::cout.flush();
    return PROTO_NONE;
}

const proto::ProtoObject* Console::timeLog(proto::ProtoContext* ctx,
                                            const proto::ProtoObject* /*self*/,
                                            const proto::ParentLink* /*parentLink*/,
                                            const proto::ProtoList* args,
                                            const proto::ProtoSparseList* /*kwargs*/) {
    if (!ctx) return PROTO_NONE;
    std::string label = firstStringArg(ctx, args, "default");
    auto& store = timerStore();
    std::chrono::steady_clock::time_point start;
    bool found = false;
    {
        std::lock_guard<std::mutex> lock(store.mtx);
        auto it = store.timers.find(label);
        if (it != store.timers.end()) {
            start = it->second;
            found = true;
        }
    }
    if (!found) {
        std::cerr << "Warning: No such label '" << label << "' for console.timeLog()\n";
        return PROTO_NONE;
    }
    auto end = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(end - start).count();
    std::cout << label << ": " << ms << "ms";
    int argc = args ? static_cast<int>(args->getSize(ctx)) : 0;
    for (int i = 1; i < argc; i++) {
        std::cout << " ";
        printProtoValue(ctx, args->getAt(ctx, i), std::cout);
    }
    std::cout << "\n";
    std::cout.flush();
    return PROTO_NONE;
}

void Console::init(proto::ProtoContext* ctx, const proto::ProtoObject*& globalObj) {
    if (!ctx || !globalObj) return;
    static const NativeEntry entries[] = {
        {"log",     Console::log},
        {"error",   Console::error},
        {"warn",    Console::warn},
        {"info",    Console::log},      // info is a log alias in Node.js
        {"debug",   Console::log},      // debug is a log alias in Node.js
        {"time",    Console::time},
        {"timeEnd", Console::timeEnd},
        {"timeLog", Console::timeLog},
        NATIVE_MODULE_END
    };
    const proto::ProtoObject* consoleObj =
        ProtoNativeModule::buildModule(ctx, entries, 8);
    if (!consoleObj) return;
    globalObj = ProtoNativeModule::registerOnGlobal(ctx, globalObj, "console", consoleObj);
}

// ---------------------------------------------------------------------------
// TimingAPIs: Date.now() and performance.now()
//
// Note on JSON.stringify / JSON.parse: those are installed as a JS-level
// polyfill from the runtime bootstrap (see installJSONPolyfill in main.cpp).
// A native C++ implementation would have to replicate the full JSON spec
// (string escaping, number formatting, key iteration on protoCore objects)
// — too large for the scope of restoring the standard benchmark suite.
// ---------------------------------------------------------------------------

namespace {
    // Captured once, used as the reference point for performance.now().
    const std::chrono::steady_clock::time_point kPerfEpoch =
        std::chrono::steady_clock::now();
}

const proto::ProtoObject* TimingAPIs::dateConstructor(proto::ProtoContext* ctx,
                                                       const proto::ProtoObject* self,
                                                       const proto::ParentLink* /*parentLink*/,
                                                       const proto::ProtoList* /*args*/,
                                                       const proto::ProtoSparseList* /*kwargs*/) {
    if (!ctx) return PROTO_NONE;
    
    // If called with 'new', self is the new instance.
    if (self && self != PROTO_NONE && self != getUndefinedSentinel() && self != getNullSentinel()) {
        const proto::ProtoString* gtKey = ctx->fromUTF8String("getTime") ? ctx->fromUTF8String("getTime")->asString(ctx) : nullptr;
        if (gtKey) {
            self = self->setAttribute(ctx, gtKey, ctx->fromMethod(nullptr, TimingAPIs::dateNow));
        }
        return self;
    }
    
    // Date() as function returns a string.
    return ctx->fromUTF8String("Thu Jan 01 1970 00:00:00 GMT+0000");
}


const proto::ProtoObject* TimingAPIs::dateNow(proto::ProtoContext* ctx,
                                               const proto::ProtoObject* /*self*/,
                                               const proto::ParentLink* /*parentLink*/,
                                               const proto::ProtoList* /*args*/,
                                               const proto::ProtoSparseList* /*kwargs*/) {
    if (!ctx) return PROTO_NONE;
    auto now = std::chrono::system_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count();
    // Returns whole-millisecond integer, matching Node/V8 Date.now() type.
    return ctx->fromLong(static_cast<long long>(ms));
}

const proto::ProtoObject* TimingAPIs::performanceNow(proto::ProtoContext* ctx,
                                                      const proto::ProtoObject* /*self*/,
                                                      const proto::ParentLink* /*parentLink*/,
                                                      const proto::ProtoList* /*args*/,
                                                      const proto::ProtoSparseList* /*kwargs*/) {
    if (!ctx) return PROTO_NONE;
    auto now = std::chrono::steady_clock::now();
    double ms = std::chrono::duration<double, std::milli>(now - kPerfEpoch).count();
    // High-resolution monotonic time in ms since program start, double-precision.
    return ctx->fromDouble(ms);
}

void TimingAPIs::init(proto::ProtoContext* ctx, const proto::ProtoObject*& globalObj) {
    if (!ctx || !globalObj) return;

    // Install a `Date` global as a mutable namespace object carrying the
    // static `.now` method (and `.name` / `.prototype` to mirror what the
    // interpreter's "unimplemented stub constructor" pass would otherwise
    // install).  We must use newObject(true), not fromMethod: method
    // objects created via fromMethod do not retain attributes written via
    // setAttribute, so adding `.now` to one silently produces a Date with
    // no static methods.  ProtoInterpreter's stub installer has an
    // `existing && existing != PROTO_NONE` guard which skips our object
    // since we install before eval starts.  None of the standard
    // benchmarks construct Date instances via `new Date()`, so callable-
    // constructor behaviour is intentionally not provided here.
    const proto::ProtoString* dateKey =
        ctx->fromUTF8String("Date") ? ctx->fromUTF8String("Date")->asString(ctx) : nullptr;
    if (dateKey) {
        const proto::ProtoObject* dateObj = ctx->newObject(true);
        if (dateObj) {
            const proto::ProtoString* nameKey =
                ctx->fromUTF8String("name") ? ctx->fromUTF8String("name")->asString(ctx) : nullptr;
            if (nameKey)
                dateObj = dateObj->setAttribute(ctx, nameKey,
                                                ctx->fromUTF8String("Date"));
            const proto::ProtoString* protoKey =
                ctx->fromUTF8String("prototype") ? ctx->fromUTF8String("prototype")->asString(ctx) : nullptr;
            if (protoKey) {
                const proto::ProtoObject* dateProto = ctx->newObject(true);
                if (dateProto)
                    dateObj = dateObj->setAttribute(ctx, protoKey, dateProto);
            }
            const proto::ProtoString* nowKey =
                ctx->fromUTF8String("now") ? ctx->fromUTF8String("now")->asString(ctx) : nullptr;
            if (nowKey) {
                const proto::ProtoObject* nowFn =
                    ctx->fromMethod(nullptr, TimingAPIs::dateNow);
                if (nowFn)
                    dateObj = dateObj->setAttribute(ctx, nowKey, nowFn);
            }
            // __native_fn__ — make typeof Date === 'function'.
            // The cell is reused — Date.now is the canonical \"thing that
            // looks like a callable\" we have here, so point __native_fn__
            // at it so the wrapper passes the isMethod check.  Calling
            // Date() still returns the current epoch ms (since dateNow
            // ignores args).  new Date() doesn't construct a real Date
            // object yet — that's separate work.
            const proto::ProtoString* nfKey =
                ctx->fromUTF8String("__native_fn__")
                    ? ctx->fromUTF8String("__native_fn__")->asString(ctx) : nullptr;
            if (nfKey) {
                const proto::ProtoObject* m =
                    ctx->fromMethod(nullptr, TimingAPIs::dateNow);
                if (m) dateObj = dateObj->setAttribute(ctx, nfKey, m);
            }
            globalObj = globalObj->setAttribute(ctx, dateKey, dateObj);
        }
    }

    // Install `performance.now()` global.
    static const NativeEntry perfEntries[] = {
        {"now", TimingAPIs::performanceNow},
        NATIVE_MODULE_END
    };
    const proto::ProtoObject* perfObj = ProtoNativeModule::buildModule(ctx, perfEntries, 1);
    if (perfObj) {
        globalObj = ProtoNativeModule::registerOnGlobal(ctx, globalObj, "performance", perfObj);
    }
}

} // namespace protojs
