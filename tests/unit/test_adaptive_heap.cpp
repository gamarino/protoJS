// When protoJS runs protoCore's adaptive heap controller.
//
// A sequential program keeps the fixed policy of src/HeapLimit.h: a hard limit
// at 75 % of the memory available, which such a program rarely reaches, so it
// rarely collects -- and protoCore's measurements show the controller costing
// sequential programs 13-36 % of their time.  A program that runs Deferreds is
// the case the controller was made for (shorter waits, a bounded heap with many
// allocating threads), so the space switches to it when its DeferredPool first
// starts.  PROTOCORE_ADAPTIVE_HEAP=1 / =0 force it on / off;
// PROTOCORE_HEAP_LIMIT_CELLS gives the budget, and 0 there (no limit) keeps the
// controller off.
//
// The state is read from ProtoSpace::adaptiveHeapStats(), so nothing here
// depends on timing or on a collection happening.

#include "../../src/ProtoCoreTypes.h"
#include <catch2/catch_all.hpp>

#include <cstdlib>
#include <memory>
#include <optional>
#include <string>

#include <protoCore.h>

#include "../../src/EventLoop.h"
#include "../../src/HeapLimit.h"
#include "../../src/JSContext.h"
#include "../../src/ProtoDeferred.h"

using namespace protojs;
using protojs::heaplimit::AdaptiveHeapMode;

namespace {

void putEnv(const char* name, const char* value) {
#if defined(_WIN32)
    _putenv_s(name, value ? value : "");  // "" removes the variable
#else
    if (value) setenv(name, value, 1);
    else unsetenv(name);
#endif
}

// Sets (or clears, with nullptr) an environment variable for one scope.
class ScopedEnv {
public:
    ScopedEnv(const char* name, const char* value) : name_(name) {
        if (const char* old = std::getenv(name)) old_ = std::string(old);
        putEnv(name, value);
    }
    ~ScopedEnv() { putEnv(name_.c_str(), old_ ? old_->c_str() : nullptr); }
    ScopedEnv(const ScopedEnv&) = delete;
    ScopedEnv& operator=(const ScopedEnv&) = delete;

private:
    std::string name_;
    std::optional<std::string> old_;
};

// A wrapper with the `Deferred` global, which main.cpp installs for a script.
std::unique_ptr<JSContextWrapper> makeWrapper() {
    auto w = std::make_unique<JSContextWrapper>();
    w->updateNativeGlobal(ProtoDeferred::init(w->getProtoContext(), w->getNativeGlobal()));
    return w;
}

bool controllerEnabled(JSContextWrapper& w) {
#if defined(PROTOCORE_HAS_ADAPTIVE_HEAP)
    return w.getProtoSpace()->adaptiveHeapStats().enabled;
#else
    (void)w;
    return false;
#endif
}

void run(JSContextWrapper& w, const char* code) {
    JSValue v = w.eval(code, "adaptive-heap.js");
    JS_FreeValue(w.getJSContext(), v);
    EventLoop& loop = EventLoop::getInstance();
    while (loop.hasPendingCallbacks()) loop.processCallbacks();
}

const char* kSequential =
    "let s = 0; for (let i = 0; i < 1000; i++) s += [i, i + 1].length;"
    "globalThis.sequentialResult = s;";
const char* kDeferred =
    "globalThis.deferredResult = new Deferred(() => 6 * 7);";

}  // namespace

TEST_CASE("PROTOCORE_ADAPTIVE_HEAP selects the mode", "[AdaptiveHeap]") {
    using heaplimit::adaptiveHeapModeFromEnv;
    REQUIRE(adaptiveHeapModeFromEnv(nullptr) == AdaptiveHeapMode::Automatic);
    REQUIRE(adaptiveHeapModeFromEnv("") == AdaptiveHeapMode::Automatic);
    REQUIRE(adaptiveHeapModeFromEnv("1") == AdaptiveHeapMode::On);
    REQUIRE(adaptiveHeapModeFromEnv("0") == AdaptiveHeapMode::Off);
    // protoCore reads only "1" and "0"; anything else leaves the default.
    REQUIRE(adaptiveHeapModeFromEnv("yes") == AdaptiveHeapMode::Automatic);
}

TEST_CASE("The controller is wanted once Deferreds run, unless forced or unlimited",
          "[AdaptiveHeap]") {
    using heaplimit::adaptiveHeapWanted;
    // Automatic: only after the first Deferred.
    REQUIRE_FALSE(adaptiveHeapWanted(AdaptiveHeapMode::Automatic, false, nullptr));
    REQUIRE(adaptiveHeapWanted(AdaptiveHeapMode::Automatic, true, nullptr));
    // Forced.
    REQUIRE(adaptiveHeapWanted(AdaptiveHeapMode::On, false, nullptr));
    REQUIRE_FALSE(adaptiveHeapWanted(AdaptiveHeapMode::Off, true, nullptr));
    // An explicit budget is the controller's budget...
    REQUIRE(adaptiveHeapWanted(AdaptiveHeapMode::Automatic, true, "3000000"));
    REQUIRE(adaptiveHeapWanted(AdaptiveHeapMode::Automatic, true, "1000000,3000000"));
    // ...but "no limit" (a hard part of 0) is not one: the controller stays off.
    REQUIRE_FALSE(adaptiveHeapWanted(AdaptiveHeapMode::Automatic, true, "0"));
    REQUIRE_FALSE(adaptiveHeapWanted(AdaptiveHeapMode::Automatic, true, "5,0"));
    // A value protoCore would ignore does not change the decision.
    REQUIRE(adaptiveHeapWanted(AdaptiveHeapMode::Automatic, true, "lots"));
}

TEST_CASE("The hard part of PROTOCORE_HEAP_LIMIT_CELLS is the budget", "[AdaptiveHeap]") {
    using heaplimit::hardCellsFromEnv;
    REQUIRE(hardCellsFromEnv(nullptr) == -1);
    REQUIRE(hardCellsFromEnv("3000000") == 3000000);
    REQUIRE(hardCellsFromEnv("1000000,3000000") == 3000000);
    REQUIRE(hardCellsFromEnv("0") == 0);
    REQUIRE(hardCellsFromEnv("12x") == -1);
    REQUIRE(hardCellsFromEnv("") == -1);
}

#if defined(PROTOCORE_HAS_ADAPTIVE_HEAP)

TEST_CASE("A sequential program keeps the fixed policy; the first Deferred enables the controller",
          "[AdaptiveHeap]") {
    ScopedEnv mode("PROTOCORE_ADAPTIVE_HEAP", nullptr);
    ScopedEnv limit("PROTOCORE_HEAP_LIMIT_CELLS", nullptr);
    auto wrapper = makeWrapper();
    JSContextWrapper::CurrentScope scope(wrapper.get());

    REQUIRE_FALSE(controllerEnabled(*wrapper));
    run(*wrapper, kSequential);
    REQUIRE_FALSE(controllerEnabled(*wrapper));

    run(*wrapper, kDeferred);
    REQUIRE(controllerEnabled(*wrapper));
    // The budget is protoCore's default: 75 % of the memory available.
    const proto::AdaptiveHeapStats stats = wrapper->getProtoSpace()->adaptiveHeapStats();
    REQUIRE(stats.hardCells > 0);
}

TEST_CASE("PROTOCORE_ADAPTIVE_HEAP=0 keeps the fixed policy with Deferreds", "[AdaptiveHeap]") {
    ScopedEnv mode("PROTOCORE_ADAPTIVE_HEAP", "0");
    ScopedEnv limit("PROTOCORE_HEAP_LIMIT_CELLS", nullptr);
    auto wrapper = makeWrapper();
    JSContextWrapper::CurrentScope scope(wrapper.get());
    run(*wrapper, kDeferred);
    REQUIRE_FALSE(controllerEnabled(*wrapper));
}

TEST_CASE("PROTOCORE_ADAPTIVE_HEAP=1 enables the controller from the start", "[AdaptiveHeap]") {
    ScopedEnv mode("PROTOCORE_ADAPTIVE_HEAP", "1");
    ScopedEnv limit("PROTOCORE_HEAP_LIMIT_CELLS", nullptr);
    auto wrapper = makeWrapper();
    JSContextWrapper::CurrentScope scope(wrapper.get());
    REQUIRE(controllerEnabled(*wrapper));
    run(*wrapper, kSequential);
    REQUIRE(controllerEnabled(*wrapper));
}

TEST_CASE("PROTOCORE_HEAP_LIMIT_CELLS is the controller's budget", "[AdaptiveHeap]") {
    ScopedEnv mode("PROTOCORE_ADAPTIVE_HEAP", nullptr);
    ScopedEnv limit("PROTOCORE_HEAP_LIMIT_CELLS", "3000000");
    auto wrapper = makeWrapper();
    JSContextWrapper::CurrentScope scope(wrapper.get());
    REQUIRE_FALSE(controllerEnabled(*wrapper));
    run(*wrapper, kDeferred);
    REQUIRE(controllerEnabled(*wrapper));
    REQUIRE(wrapper->getProtoSpace()->adaptiveHeapStats().hardCells == 3000000);
}

TEST_CASE("PROTOCORE_HEAP_LIMIT_CELLS=0 (no limit) keeps the controller off", "[AdaptiveHeap]") {
    ScopedEnv mode("PROTOCORE_ADAPTIVE_HEAP", nullptr);
    ScopedEnv limit("PROTOCORE_HEAP_LIMIT_CELLS", "0");
    auto wrapper = makeWrapper();
    JSContextWrapper::CurrentScope scope(wrapper.get());
    run(*wrapper, kDeferred);
    REQUIRE_FALSE(controllerEnabled(*wrapper));
}

#endif  // PROTOCORE_HAS_ADAPTIVE_HEAP
