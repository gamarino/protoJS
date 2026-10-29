// GC safety: the value of a caught proto::ProtoException is rooted only while
// the exception (or a copy of it) is alive (protoCore docs/EXCEPTIONS.md 3.1).
//
// A boundary that catches the exception, copies `e.value()` into a C++ local
// and uses it after the catch block has ended -- to reject a promise, to
// complete an async generator, to report it -- holds a bare pointer: a C++
// local is not a root.  If a collection runs between the end of the catch and
// the moment the value is stored somewhere the collector sees, the value is
// reclaimed, and the promise is rejected with a freed cell.
//
// Three test cases:
//  - the rule itself, deterministic (last case): a value thrown from a
//    context that unwinding ends, used after its catch across a forced
//    collection.  Holding the caught exception keeps it; copying only the
//    value does not (PROTOJS_TEST_BARE_COPY=1 runs that pattern, and the case
//    then fails in every round).
//  - the boundaries that were fixed (interpreter async entry, await resume,
//    AsyncGeneratorAwaitReturn; eval's uncaught-exception report), driven
//    under collections requested from a second thread every 50 us, the way
//    protoCore's own tests force cycles (gcStarted + gcCV).  The window there
//    is a few allocations wide and a thread parks only when its free-cell
//    batch runs out, so these cases did not fail before the fix: they guard
//    the paths, the deterministic case guards the rule.

#include <catch2/catch_all.hpp>

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <sstream>
#include <string>
#include <thread>

#include "../../src/JSContext.h"
#include "../../src/runtime/ProtoBytecodeModule.h"
#include "../../src/runtime/ProtoInterpreter.h"

using namespace protojs;

namespace {

// Requests collection cycles until stopped.  It never touches the heap, so it
// needs no protoCore thread of its own.
class CycleHammer {
public:
    explicit CycleHammer(proto::ProtoSpace* space)
        : space_(space), thread_([this] { run(); }) {}
    ~CycleHammer() {
        stop_.store(true);
        thread_.join();
    }
    CycleHammer(const CycleHammer&) = delete;
    CycleHammer& operator=(const CycleHammer&) = delete;

private:
    void run() {
        while (!stop_.load()) {
            {
                std::lock_guard<std::recursive_mutex> lock(proto::ProtoSpace::globalMutex);
                space_->gcStarted = true;
                space_->gcCV.notify_all();
            }
            std::this_thread::sleep_for(std::chrono::microseconds(50));
        }
    }

    proto::ProtoSpace* space_;
    std::atomic<bool> stop_{false};
    std::thread thread_;
};

// Requests a cycle and waits at safepoints until one has completed, sweep
// included (protoCore test/GCRootScopeTests.cpp, cycleNow).  Bounded: a missed
// cycle weakens the iteration, it does not hang.  Returns whether one ran.
// The bound is generous for loaded CI runners; a cycle that completes sooner
// returns at once.
bool cycleNow(proto::ProtoContext* ctx) {
    proto::ProtoSpace* space = ctx->space;
    proto::ProtoThread* self = const_cast<proto::ProtoThread*>(ctx->thread);
    const uint64_t before = space->getGCCycleCount();
    {
        std::lock_guard<std::recursive_mutex> lock(proto::ProtoSpace::globalMutex);
        space->gcStarted = true;
        space->gcCV.notify_all();
    }
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (std::chrono::steady_clock::now() < deadline) {
        self->synchToGC();
        if (space->getGCCycleCount() > before && !space->gcStarted.load()) return true;
        std::this_thread::yield();
    }
    return false;
}

long long globalInt(JSContextWrapper& w, const char* name) {
    proto::ProtoContext* ctx = w.getProtoContext();
    const proto::ProtoObject* v =
        w.getGlobalObject(ctx)->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, name));
    return (v && v->isInteger(ctx)) ? v->asLong(ctx) : -1;
}

// Every round rejects through the three interpreter boundaries that used to
// keep the thrown value in a C++ local past the catch:
//  - an async function whose body throws before any await (the promise is
//    rejected by startAsyncActivation),
//  - an async function whose body throws after an await (resumeContinuation),
//  - AsyncGenerator.prototype.return on a generator that has not started,
//    when PromiseResolve of the argument throws (AsyncGeneratorAwaitReturn).
const char* kRounds = R"JS(
globalThis.__done = 0;
globalThis.__bad = 0;
globalThis.__firstBad = '';
function check(e, want) {
    __done++;
    var got = (e && typeof e === 'object') ? e.message : e;
    if (!(e instanceof Error) || got !== want) {
        if (!__bad) __firstBad = want + ' -> ' + String(got);
        __bad++;
    }
}
function churn(r) {
    var a = [];
    for (var i = 0; i < 8; i++) a.push({ s: 'g' + i + '-' + r });
    return a.length;
}
async function direct(r) { churn(r); throw new Error('direct-' + r); }
async function afterAwait(r) { await null; churn(r); throw new Error('await-' + r); }
async function* gen() { yield 1; }
function badReturnValue(r) {
    var p = Promise.resolve(0);
    Object.defineProperty(p, 'constructor', {
        get: function () { churn(r); throw new Error('return-' + r); }
    });
    return p;
}
for (let r = 0; r < ROUNDS; r++) {
    direct(r).catch(function (e) { check(e, 'direct-' + r); });
    afterAwait(r).catch(function (e) { check(e, 'await-' + r); });
    gen().return(badReturnValue(r)).catch(function (e) { check(e, 'return-' + r); });
}
)JS";

}  // namespace

TEST_CASE("A thrown value survives collections after the catch that took it",
          "[exceptions][gc]") {
    constexpr int kRoundsPerEval = 200;
    // The hammer only requests cycles; one runs when the interpreter reaches
    // a safepoint, so how many run during a fixed number of evals depends on
    // how fast the rounds are and how the runner schedules threads (a Windows
    // CI run saw 9 in 10 evals). Run at least kMinEvals evals, then keep going
    // until kMinCycles cycles have run, bounded by kMaxEvals and kMaxTime.
    constexpr int kMinEvals = 10;
    constexpr int kMaxEvals = 200;
    constexpr uint64_t kMinCycles = 10;
    constexpr auto kMaxTime = std::chrono::seconds(120);

    auto wrapper = std::make_unique<JSContextWrapper>();
    JSContextWrapper::CurrentScope scope(wrapper.get());
    JS_FreeValue(wrapper->getJSContext(), wrapper->eval("0", "realm.js"));
    proto::ProtoSpace* space = wrapper->getProtoContext()->space;

    std::string source(kRounds);
    source.replace(source.find("ROUNDS"), 6, std::to_string(kRoundsPerEval));

    long long done = 0;
    long long bad = 0;
    int evals = 0;
    const uint64_t cyclesBefore = space->getGCCycleCount();
    {
        CycleHammer hammer(space);
        const auto deadline = std::chrono::steady_clock::now() + kMaxTime;
        while (evals < kMinEvals
               || (space->getGCCycleCount() - cyclesBefore < kMinCycles && evals < kMaxEvals
                   && std::chrono::steady_clock::now() < deadline)) {
            ++evals;
            // eval runs the microtask checkpoint after the script: every
            // rejection handler has run when it returns.
            JS_FreeValue(wrapper->getJSContext(), wrapper->eval(source, "rounds.js"));
            done += globalInt(*wrapper, "__done");
            const long long b = globalInt(*wrapper, "__bad");
            if (b > 0 && bad == 0) {
                proto::ProtoContext* ctx = wrapper->getProtoContext();
                const proto::ProtoObject* first = wrapper->getGlobalObject(ctx)->getAttribute(
                    ctx, proto::ProtoString::createSymbol(ctx, "__firstBad"));
                if (first && first->isString(ctx))
                    UNSCOPED_INFO("first wrong rejection: " << first->asString(ctx)->toStdString(ctx));
            }
            bad += (b > 0) ? b : 0;
        }
    }
    const uint64_t cycles = space->getGCCycleCount() - cyclesBefore;

    INFO("collection cycles during the rounds: " << cycles << ", evals: " << evals);
    CHECK(cycles >= kMinCycles);  // premise: the collector did run during the rounds
    CHECK(done == 3LL * kRoundsPerEval * evals);
    CHECK(bad == 0);
}

TEST_CASE("An uncaught exception of eval is reported with its own message under collections",
          "[exceptions][gc]") {
    constexpr int kEvals = 200;

    auto wrapper = std::make_unique<JSContextWrapper>();
    JSContextWrapper::CurrentScope scope(wrapper.get());
    JS_FreeValue(wrapper->getJSContext(), wrapper->eval("0", "realm.js"));
    proto::ProtoSpace* space = wrapper->getProtoContext()->space;

    int wrong = 0;
    std::string firstWrong;
    {
        CycleHammer hammer(space);
        for (int i = 0; i < kEvals; ++i) {
            const std::string want = "top-" + std::to_string(i);
            std::ostringstream captured;
            std::streambuf* saved = std::cerr.rdbuf(captured.rdbuf());
            JS_FreeValue(wrapper->getJSContext(),
                         wrapper->eval("var a = []; for (var i = 0; i < 8; i++) a.push({ i: i });"
                                       "throw new Error('" + want + "');",
                                       "uncaught.js"));
            std::cerr.rdbuf(saved);
            if (captured.str().find("Error: " + want + "\n") == std::string::npos) {
                if (!wrong) firstWrong = captured.str();
                ++wrong;
            }
        }
    }
    INFO("first wrong report: " << firstWrong);
    CHECK(wrong == 0);
}

// The rule itself, deterministically: a value thrown as a ProtoException from
// a context that unwinding then ends is kept, after the catch, only by the
// caught exception.  Each round holds the exception past its catch, runs a
// whole collection cycle and enough fresh allocation to reuse whatever the
// sweep freed, and reads the value back.  Copying only `value()` out of the
// catch (the pattern this replaces) fails every round: all 40 values read back
// wrong.
TEST_CASE("A value used after its catch is kept by holding the caught exception",
          "[exceptions][gc]") {
    constexpr int kRounds = 40;

    auto wrapper = std::make_unique<JSContextWrapper>();
    JSContextWrapper::CurrentScope scope(wrapper.get());
    JS_FreeValue(wrapper->getJSContext(), wrapper->eval("0", "realm.js"));
    proto::ProtoContext* ctx = wrapper->getProtoContext();
    const proto::ProtoString* tagKey = proto::ProtoString::createSymbol(ctx, "tag");
    const bool bareCopy = std::getenv("PROTOJS_TEST_BARE_COPY") != nullptr;

    int cycles = 0;
    int wrong = 0;
    for (int r = 0; r < kRounds; ++r) {
        const std::string want = "thrown-value-" + std::to_string(r) + "-long-enough-to-be-a-cell";
        std::optional<proto::ProtoException> held;
        const proto::ProtoObject* value = nullptr;
        try {
            proto::ProtoContext callee(ctx->space, ctx, nullptr, nullptr, nullptr, nullptr);
            throw proto::ProtoException(
                &callee, callee.newObject(true)->setAttribute(
                             &callee, tagKey, callee.fromUTF8String(want.c_str())));
        } catch (const proto::ProtoException& thrown) {
            if (!bareCopy) held.emplace(thrown);
            value = thrown.value();
        }
        REQUIRE(value);
        if (cycleNow(ctx)) ++cycles;
        {
            // Garbage that recycles the cells the sweep freed.
            proto::ProtoContext garbage(ctx->space, ctx, nullptr, nullptr, nullptr, nullptr);
            for (int i = 0; i < 4096; ++i)
                (void) garbage.newList()->appendLast(&garbage, garbage.fromInteger(-i));
        }
        const proto::ProtoObject* tag = value->getAttribute(ctx, tagKey);
        if (!tag || !tag->isString(ctx) || tag->asString(ctx)->toStdString(ctx) != want) ++wrong;
    }
    INFO("forced collection cycles: " << cycles);
    CHECK(cycles == kRounds);  // premise: every round ran a whole cycle
    CHECK(wrong == 0);
}
