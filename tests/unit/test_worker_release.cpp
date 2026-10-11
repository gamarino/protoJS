// A worker_threads Worker left to finish on its own is released when the
// wrapper that created it is destroyed.
//
// A Worker pins itself in its owner's root set, so the finalizer of its
// ExternalPointer runs only when the owner's space is torn down -- and there it
// can only post an orphan that no event loop drains any more.  Before
// WorkerThreadsModule::releaseWorkersOf, the worker's JSContextWrapper (a whole
// ProtoSpace with its own collector thread) therefore outlived the main
// wrapper, and the process's static destructors ran beside that collector
// thread: an intermittent SIGSEGV at exit in protoCore's collector loop
// (cli/blocking-joins-and-finalizers, worker-to-completion; 3-6 of 25 runs
// locally, and the master Linux CI run 38108654993).
//
// The property is checked directly: the number of live wrappers returns to
// what it was once the creating wrapper is gone.  Before the fix it stayed one
// higher (the worker's).

#include "../../src/ProtoCoreTypes.h"
#include <catch2/catch_all.hpp>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <memory>
#include <string>
#include <thread>

#include <protoCore.h>

#include "../../src/EventLoop.h"
#include "../../src/JSContext.h"
#include "../../src/modules/worker_threads/WorkerThreadsModule.h"

using namespace protojs;

TEST_CASE("A finished worker is released with the wrapper that created it", "[Worker]") {
    const std::string script = "protojs_worker_release_child.js";
    {
        std::ofstream out(script);
        out << "var acc = 0; for (var i = 0; i < 1000; i++) acc += i;\n";
    }
    const size_t before = JSContextWrapper::liveWrapperCount();
    {
        auto wrapper = std::make_unique<JSContextWrapper>();
        JSContextWrapper::CurrentScope scope(wrapper.get());
        wrapper->updateNativeGlobal(
            WorkerThreadsModule::init(wrapper->getProtoContext(), wrapper->getNativeGlobal()));
        JSValue v = wrapper->eval(
            "globalThis.w = new worker_threads.Worker('" + script + "');", "worker-release.js");
        JS_FreeValue(wrapper->getJSContext(), v);
        // As main.cpp's drain loop: wait until the worker has finished.
        const auto limit = std::chrono::steady_clock::now() + std::chrono::seconds(60);
        EventLoop& loop = EventLoop::getInstance();
        while ((WorkerThreadsModule::getActiveWorkerCount() > 0 || loop.hasPendingCallbacks())
               && std::chrono::steady_clock::now() < limit) {
            loop.processCallbacks();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
        REQUIRE(WorkerThreadsModule::getActiveWorkerCount() == 0);
        // The worker's wrapper is still alive here: nothing has released it.
        REQUIRE(JSContextWrapper::liveWrapperCount() == before + 2);
    }
    // The creating wrapper is gone, and the worker's with it.
    REQUIRE(JSContextWrapper::liveWrapperCount() == before);
    std::remove(script.c_str());
}
