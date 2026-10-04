// Does a blocking call on a REGISTERED protoCore thread actually leave the running
// set?
//
// A registered thread that blocks without leaving protoCore's running set is still
// counted in `runningThreads`, so the stop-the-world quorum
// (`parkedThreads >= runningThreads`) can never be met, no collection cycle can
// start, and every thread that then needs memory waits for a cycle that cannot
// begin. That is a deadlock, not a slow shutdown.
//
// WHY THIS IS A UNIT TEST AND NOT A SCRIPT. The deadlock needs a second thread
// wanting memory in the SAME space at the moment the first one blocks, and that
// coincidence is not something a JS program can be relied on to produce: measured
// against a build with every guard removed, five of the seven end-to-end cases in
// tests/cli/blocking-joins-and-finalizers.sh still completed in about two seconds,
// because nothing else happened to want memory just then. A test that passes on the
// broken build proves nothing, so the property is asserted directly instead:
// `ProtoSpace::parkedThreads` is observable, and it says whether the blocked thread
// left the quorum or not. No timing margin, no race, no reliance on a collection
// happening to be due: the pool task waits until it SEES the caller parked (or
// gives up after a bound that only a failing run reaches).
//
// WHAT WOULD MAKE EACH CASE FAIL. Deleting the `ThreadUnmanagedScope` from
// `ThreadPoolExecutor::shutdown` (src/ThreadPoolExecutor.cpp) makes the first case
// observe parkedThreads == 0 and fail. Deleting it from `shutdownNow` fails the
// second. Both were run that way before the guards were added.

#include "../../src/ProtoCoreTypes.h"
#include <catch2/catch_all.hpp>

#include <atomic>
#include <chrono>
#include <thread>

#include <protoCore.h>

#include "../../src/CPUThreadPool.h"
#include "../../src/ThreadPoolExecutor.h"
#include "../../src/ThreadProtoContext.h"

using namespace protojs;

namespace {

// A space constructed here adopts THIS thread as its main thread and counts it in
// `runningThreads`, which is what makes the thread "registered". protoJS never calls
// any registerThread; constructing a ProtoSpace (as JSContextWrapper does) and
// ProtoSpace::newThread are the only two things that put a thread in the running
// set, and protoCore's Thread.cpp is explicit that a bare std::thread does not.
struct RegisteredThread {
    proto::ProtoSpace space;
    proto::ProtoContext* context{nullptr};
    proto::ProtoContext* previousSlot{nullptr};

    RegisteredThread() : space() {
        context = space.rootContext;
        previousSlot = threadProtoContext();
        // The test binary may already hold a slot from another case; publish ours
        // for the duration and put the old one back afterwards.
        clearThreadProtoContext(previousSlot);
        setThreadProtoContext(context);
    }

    ~RegisteredThread() {
        clearThreadProtoContext(context);
        setThreadProtoContext(previousSlot);
    }

    proto::proto_ulong running() const {
        return space.runningThreads.load(std::memory_order_acquire);
    }
    proto::proto_ulong parked() const {
        return space.parkedThreads.load(std::memory_order_acquire);
    }
};

// Wait, polling, until `condition` holds; false if it still does not after
// `limit`. The limit only bounds how long a FAILING run takes -- a passing run
// returns as soon as the condition holds, however slow the machine -- so it is
// not a timing margin. These cases used to sleep for fixed times instead (30 ms
// for the worker to pick the task up, 150 ms for the caller to be inside
// shutdown), and on a loaded macOS runner the caller was not there yet
// (cross-platform run 37206587525).
template <typename Condition>
bool waitUntil(Condition condition, std::chrono::seconds limit = std::chrono::seconds(30)) {
    const auto deadline = std::chrono::steady_clock::now() + limit;
    while (!condition()) {
        if (std::chrono::steady_clock::now() > deadline) return false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return true;
}

}  // namespace

TEST_CASE("ThreadUnmanagedScope: a registered thread leaves the running set", "[gc][blocking]") {
    RegisteredThread self;

    // Premise. If constructing a space did not register this thread there would be
    // nothing to leave, and every assertion below would pass vacuously on a broken
    // build. protoCore's ProtoSpace constructor starts runningThreads at 1 for
    // exactly this thread.
    REQUIRE(self.running() >= 1);
    REQUIRE(self.parked() == 0);
    REQUIRE(threadProtoContext() == self.context);

    {
        ThreadUnmanagedScope parked;
        REQUIRE(parked.active());
        // The whole property: while blocked, this thread is out of the quorum, so
        // `parkedThreads >= runningThreads` can be satisfied and a cycle can start.
        REQUIRE(self.parked() >= self.running());
    }

    REQUIRE(self.parked() == 0);
}

TEST_CASE("ThreadUnmanagedScope: nesting is refcounted, not idempotent", "[gc][blocking]") {
    RegisteredThread self;
    REQUIRE(self.parked() == 0);
    {
        ThreadUnmanagedScope outer;
        REQUIRE(self.parked() == 1);
        {
            ThreadUnmanagedScope inner;
            // A single thread contributes one slot to the quorum however many
            // unmanaged regions it has open -- ThreadPoolExecutor::shutdown nests
            // (CPUThreadPool::shutdown calls it, then ~ThreadPoolExecutor calls it
            // again), so an idempotent implementation would un-park the thread on
            // the inner scope's exit and leave it counted as running for the rest
            // of the outer one.
            REQUIRE(self.parked() == 1);
        }
        REQUIRE(self.parked() == 1);
    }
    REQUIRE(self.parked() == 0);
}

TEST_CASE("ThreadUnmanagedScope: no-op on an unregistered thread", "[gc][blocking]") {
    // A pool worker, an accept loop and the GC thread are not in `runningThreads`,
    // so they have nothing to leave. Parking a context they do not own would be
    // worse than doing nothing: it would tell the collector that a thread actively
    // running managed code is parked.
    std::atomic<bool> wasActive{true};
    std::thread t([&] {
        ThreadUnmanagedScope parked;
        wasActive.store(parked.active());
    });
    t.join();
    REQUIRE(wasActive.load() == false);
}

TEST_CASE("ThreadPoolExecutor::shutdown parks the calling thread", "[gc][blocking]") {
    RegisteredThread self;
    REQUIRE(self.running() >= 1);
    REQUIRE(self.parked() == 0);

    ThreadPoolExecutor pool(2, "BlockingRegionTestPool");

    // Observed from INSIDE the pool, while the calling thread is stuck in
    // shutdown()'s condition wait -- which is the blocking region that matters, and
    // the one that would be missed by a guard wrapped around the join loop alone.
    std::atomic<proto::proto_ulong> parkedDuringShutdown{0};
    std::atomic<proto::proto_ulong> runningDuringShutdown{0};
    std::atomic<bool> sampled{false};

    std::atomic<bool> started{false};
    pool.submit([&]() {
        started.store(true);
        // shutdown() cannot return until this task does, so the caller is either
        // on its way into the wait or inside it. Sample once the caller is
        // parked; if it never parks (no guard), the wait gives up and the sample
        // shows the caller still running.
        waitUntil([&] { return self.running() >= 1 && self.parked() >= self.running(); });
        parkedDuringShutdown.store(self.parked());
        runningDuringShutdown.store(self.running());
        sampled.store(true);
        return 0;
    });
    // The task is running, not queued, when shutdown begins.
    REQUIRE(waitUntil([&] { return started.load(); }));

    pool.shutdown();

    REQUIRE(sampled.load());
    // Premise: the sample was taken while a thread was registered at all.
    REQUIRE(runningDuringShutdown.load() >= 1);
    // The property. 0 here means the main thread sat in the condition wait still
    // counted as running, so no stop-the-world could have begun.
    REQUIRE(parkedDuringShutdown.load() >= runningDuringShutdown.load());

    // And the guard was balanced on the way out.
    REQUIRE(self.parked() == 0);
}

TEST_CASE("ThreadPoolExecutor::shutdownNow parks the calling thread", "[gc][blocking]") {
    RegisteredThread self;
    REQUIRE(self.parked() == 0);

    ThreadPoolExecutor pool(2, "BlockingRegionTestPoolNow");

    std::atomic<proto::proto_ulong> parkedDuringShutdown{0};
    std::atomic<proto::proto_ulong> runningDuringShutdown{0};
    std::atomic<bool> sampled{false};

    std::atomic<bool> started{false};
    pool.submit([&]() {
        started.store(true);
        // shutdownNow clears the QUEUE but cannot abandon a task already running,
        // so the join loop still has to wait for this one. Sample once the caller
        // is parked in it (see the shutdown case).
        waitUntil([&] { return self.running() >= 1 && self.parked() >= self.running(); });
        parkedDuringShutdown.store(self.parked());
        runningDuringShutdown.store(self.running());
        sampled.store(true);
        return 0;
    });
    // The worker has picked the task up, so it is running rather than queued when
    // shutdownNow clears the queue.
    REQUIRE(waitUntil([&] { return started.load(); }));

    pool.shutdownNow();

    REQUIRE(sampled.load());
    REQUIRE(runningDuringShutdown.load() >= 1);
    REQUIRE(parkedDuringShutdown.load() >= runningDuringShutdown.load());
    REQUIRE(self.parked() == 0);
}
