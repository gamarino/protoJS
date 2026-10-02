// MicrotaskQueue: the cross-thread entry point and the checkpoint order.
//
// enqueueFromAnyThread is the one method another thread may call
// (src/MicrotaskQueue.h): a pool thread that finished native work hands the
// owner thread a continuation to run as a microtask.  These cases check that
// such jobs run on the owner thread, in arrival order, at the checkpoint that
// follows the event-loop callback that wakes the loop, with a context of the
// owner's space -- and that a job a native producer queues and a promise
// reaction JavaScript queued run first in, first out.

#include <catch2/catch_all.hpp>

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "../../src/EventLoop.h"
#include "../../src/JSContext.h"
#include "../../src/MicrotaskQueue.h"
#include "../../src/PromisePrototype.h"

using namespace protojs;

TEST_CASE("Jobs enqueued from another thread run on the owner thread, in order",
          "[MicrotaskQueue]") {
    auto wrapper = std::make_unique<JSContextWrapper>();
    MicrotaskQueue& queue = wrapper->microtasks();
    const std::thread::id owner = std::this_thread::get_id();

    std::vector<int> order;
    std::atomic<int> wrongThread{0};
    std::atomic<int> wrongSpace{0};
    proto::ProtoSpace* space = wrapper->getProtoSpace();

    std::thread producer([&]() {
        for (int i = 0; i < 5; ++i) {
            queue.enqueueFromAnyThread([&, i](proto::ProtoContext* ctx) {
                if (std::this_thread::get_id() != owner) wrongThread++;
                if (!ctx || ctx->space != space) wrongSpace++;
                order.push_back(i);
            });
        }
    });
    producer.join();

    // Nothing runs on the producer thread, and nothing before the loop turns.
    REQUIRE(order.empty());
    REQUIRE(queue.hasPendingJobs());

    EventLoop& loop = EventLoop::getInstance();
    while (loop.hasPendingCallbacks()) loop.processCallbacks();

    REQUIRE(order == std::vector<int>{0, 1, 2, 3, 4});
    REQUIRE(wrongThread.load() == 0);
    REQUIRE(wrongSpace.load() == 0);
    REQUIRE_FALSE(queue.hasPendingJobs());
}

TEST_CASE("A native job and promise reactions share one first-in, first-out queue",
          "[MicrotaskQueue]") {
    auto wrapper = std::make_unique<JSContextWrapper>();
    JSContextWrapper::CurrentScope scope(wrapper.get());

    // The script ends with a checkpoint, so its own reactions have run by the
    // time eval returns; queue fresh ones from a native job instead.
    JSValue v = wrapper->eval(
        "globalThis.order = [];"
        "globalThis.later = function (tag) { Promise.resolve().then(function () { order.push(tag); }); };",
        "microtask-order.js");
    JS_FreeValue(wrapper->getJSContext(), v);

    std::vector<std::string> nativeOrder;
    MicrotaskQueue& queue = wrapper->microtasks();
    std::thread producer([&]() {
        queue.enqueueFromAnyThread([&](proto::ProtoContext*) { nativeOrder.push_back("first"); });
        queue.enqueueFromAnyThread([&](proto::ProtoContext*) { nativeOrder.push_back("second"); });
    });
    producer.join();
    EventLoop& loop = EventLoop::getInstance();
    while (loop.hasPendingCallbacks()) loop.processCallbacks();
    REQUIRE(nativeOrder == std::vector<std::string>{"first", "second"});

    v = wrapper->eval("later('a'); later('b'); order.join(',')", "microtask-order-2.js");
    const char* s = JS_ToCString(wrapper->getJSContext(), v);
    // The reactions queued by the script ran in the checkpoint after it, but
    // the script's completion value was computed before that checkpoint.
    REQUIRE(std::string(s ? s : "") == "");
    if (s) JS_FreeCString(wrapper->getJSContext(), s);
    JS_FreeValue(wrapper->getJSContext(), v);

    v = wrapper->eval("order.join(',')", "microtask-order-3.js");
    s = JS_ToCString(wrapper->getJSContext(), v);
    REQUIRE(std::string(s ? s : "") == "a,b");
    if (s) JS_FreeCString(wrapper->getJSContext(), s);
    JS_FreeValue(wrapper->getJSContext(), v);
}
