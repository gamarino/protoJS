#include "console.h"
#include "ProtoNativeModule.h"
#include "runtime/ProtoInterpreter.h"
#include <chrono>
#include <iostream>
#include <mutex>
#include <string>
#include <unordered_map>

namespace protojs {

namespace {

/** Convert a single ProtoObject to its string representation for printing. */
static void printProtoValue(proto::ProtoContext* ctx, const proto::ProtoObject* val,
                             std::ostream& out) {
    if (!ctx || !val || val == PROTO_NONE || val->isNone(ctx)) {
        out << "undefined";
        return;
    }
    // Check for null sentinel before any other type check.
    if (val == protojs::getNullSentinel()) {
        out << "null";
        return;
    }
    if (val->isString(ctx)) {
        const proto::ProtoString* s = val->asString(ctx);
        if (s) {
            std::string tmp;
            s->toUTF8String(ctx, tmp);
            out << tmp;
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
        out << val->asDouble(ctx);
        return;
    }
    /* Objects, arrays, etc. */
    out << "[object Object]";
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

    // Install or augment a `Date` global with a static `.now` method.
    // ProtoInterpreter's "unimplemented stub constructor" pass would
    // otherwise overwrite Date with an empty stub later; we install
    // first so its `existing && existing != PROTO_NONE` guard skips us.
    const proto::ProtoString* dateKey =
        ctx->fromUTF8String("Date") ? ctx->fromUTF8String("Date")->asString(ctx) : nullptr;
    if (dateKey) {
        const proto::ProtoObject* existingDate =
            globalObj->getAttribute(ctx, dateKey, false);
        const proto::ProtoObject* dateObj =
            (existingDate && existingDate != PROTO_NONE)
                ? existingDate
                : ctx->newObject(true);
        if (dateObj) {
            const proto::ProtoString* nowKey =
                ctx->fromUTF8String("now") ? ctx->fromUTF8String("now")->asString(ctx) : nullptr;
            if (nowKey) {
                const proto::ProtoObject* nowFn =
                    ctx->fromMethod(nullptr, TimingAPIs::dateNow);
                if (nowFn) {
                    dateObj = dateObj->setAttribute(ctx, nowKey, nowFn);
                }
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
