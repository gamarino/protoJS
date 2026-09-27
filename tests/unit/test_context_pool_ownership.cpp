// A worker's JSContextWrapper must not take the process-wide thread pools away
// from the main thread.
//
// THE BUG.  `CPUThreadPool` and `IOThreadPool` are singletons.
// `JSContextWrapper`'s constructor called `initialize()` on both -- which shuts the
// existing pool down and replaces it -- and its destructor called `shutdown()` on
// both, which destroys them for everybody.  A `worker_threads` worker builds its
// own `JSContextWrapper` (`WorkerThreadsModule.cpp`), so constructing one replaced
// the main thread's pool and destroying one took the replacement away again.
// Recorded in docs/CONFORMANCE.md, "Known, reported, not fixed", item 1.
//
// WHY IT NEEDS A TEST AND NOT JUST A READING.  The symptom is silent.
// `getInstance()` lazily builds a fresh, default-sized pool the moment anybody
// asks for one, so after a worker's wrapper is released the main thread still gets
// *a* pool -- just not the one it configured, and not the one holding its queued
// work.  Nothing throws and nothing logs.  The only observable is the pool's
// identity and its configured size, which is what this test reads.
//
// WHY THE SIZES ARE DERIVED AND NOT LITERAL.  `kMainCpu` is
// `getOptimalThreadCount() + 1`, so it is by construction a size no default
// sizing can produce on any host.  A literal 3 would have made this test pass
// vacuously on a three-core machine.
//
// WHAT MAKES IT FAIL.  Delete either half of the guard in src/JSContext.cpp: drop
// the `poolOwners_.fetch_add(...) == 0` condition around the two `initialize()`
// calls and the FIRST block below goes red (`13 == 12` on a 12-core host); drop
// the `poolOwners_.fetch_sub(...) == 1` condition around the two `shutdown()`
// calls and the SECOND block goes red.  Both were run that way.

#include <catch2/catch_all.hpp>

#include <cstddef>
#include <memory>
#include <thread>

#include "../../src/CPUThreadPool.h"
#include "../../src/IOThreadPool.h"
#include "../../src/JSContext.h"
#include "../../src/ThreadPoolExecutor.h"

using namespace protojs;

TEST_CASE("A worker's wrapper leaves the main thread's pools alone",
          "[JSContextWrapper][threadpool]") {
    // Sizes no default can produce, so a replacement pool is recognisable.
    const std::size_t kMainCpu = CPUThreadPool::getOptimalThreadCount() + 1;
    const std::size_t kMainIo = IOThreadPool::getOptimalThreadCount(3.0) + 1;

    auto mainWrapper = std::make_unique<JSContextWrapper>(kMainCpu, kMainIo);

    ThreadPoolExecutor* const cpu0 = &CPUThreadPool::getInstance().getExecutor();
    ThreadPoolExecutor* const io0 = &IOThreadPool::getInstance().getExecutor();
    REQUIRE(cpu0->getThreadCount() == kMainCpu);
    REQUIRE(io0->getThreadCount() == kMainIo);

    // A worker's wrapper is built on the worker's own thread -- its `ProtoSpace`
    // constructor adopts the constructing thread as that space's main thread, and
    // `setThreadProtoContext` fills that thread's slot, not this one's.  Doing it
    // on a helper thread keeps this test's own protoCore registration intact and
    // is also what production does.
    {
        std::unique_ptr<JSContextWrapper> workerWrapper;
        std::thread builder([&workerWrapper] {
            workerWrapper = std::make_unique<JSContextWrapper>();
        });
        builder.join();
        REQUIRE(workerWrapper != nullptr);

        // --- the constructor half ---------------------------------------------
        CHECK(&CPUThreadPool::getInstance().getExecutor() == cpu0);
        CHECK(&IOThreadPool::getInstance().getExecutor() == io0);
        CHECK(CPUThreadPool::getInstance().getExecutor().getThreadCount() == kMainCpu);
        CHECK(IOThreadPool::getInstance().getExecutor().getThreadCount() == kMainIo);
        CHECK_FALSE(cpu0->isShutdown());
        CHECK_FALSE(io0->isShutdown());

        // `freeWorkerState` does exactly this, from the GC thread.
        workerWrapper.reset();
    }

    // --- the destructor half ------------------------------------------------
    CHECK(&CPUThreadPool::getInstance().getExecutor() == cpu0);
    CHECK(&IOThreadPool::getInstance().getExecutor() == io0);
    CHECK(CPUThreadPool::getInstance().getExecutor().getThreadCount() == kMainCpu);
    CHECK(IOThreadPool::getInstance().getExecutor().getThreadCount() == kMainIo);
    CHECK_FALSE(cpu0->isShutdown());
    CHECK_FALSE(io0->isShutdown());

    // And it still runs work: a pool that had been shut down would refuse a
    // submission, and one that had been replaced would run it on other threads.
    CHECK(cpu0->submit([] { return 7; }).get() == 7);
    CHECK(io0->submit([] { return 11; }).get() == 11);

    // --- the last wrapper DOES release them ---------------------------------
    // Ownership is collective, not absent: the counter must reach zero, or the
    // fix would be a leak of two pools per process.
    mainWrapper.reset();
    CHECK(CPUThreadPool::getInstance().getExecutor().getThreadCount() != kMainCpu);
    CHECK(IOThreadPool::getInstance().getExecutor().getThreadCount() != kMainIo);

    // Leave nothing behind for the next case: the two assertions above rebuilt
    // default-sized pools by asking for them.
    CPUThreadPool::shutdown();
    IOThreadPool::shutdown();
}
