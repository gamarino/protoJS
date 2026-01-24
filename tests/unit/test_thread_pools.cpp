#include <catch2/catch_all.hpp>
#include "../../src/ThreadPoolExecutor.h"
#include "../../src/CPUThreadPool.h"
#include "../../src/IOThreadPool.h"
#include <thread>
#include <chrono>

using namespace protojs;

TEST_CASE("ThreadPoolExecutor: Legacy tests", "[ThreadPoolExecutor]") {
    ThreadPoolExecutor pool(4, "TestPool");
    
    auto future1 = pool.submit([]() { return 42; });
    REQUIRE(future1.get() == 42);
    
    std::vector<std::future<int>> futures;
    for (int i = 0; i < 10; ++i) {
        futures.push_back(pool.submit([i]() { return i * 2; }));
    }
    
    for (size_t i = 0; i < futures.size(); ++i) {
        REQUIRE(futures[i].get() == static_cast<int>(i * 2));
    }
    
    REQUIRE(pool.getThreadCount() == 4);
    
    pool.shutdown();
}

TEST_CASE("CPUThreadPool: Legacy tests", "[CPUThreadPool]") {
    CPUThreadPool::initialize(4);
    auto& pool = CPUThreadPool::getInstance();
    
    auto future = pool.getExecutor().submit([]() {
        return std::thread::hardware_concurrency();
    });
    
    auto result = future.get();
    REQUIRE(result > 0);
    REQUIRE(pool.getExecutor().getThreadCount() == 4);
    
    CPUThreadPool::shutdown();
}

TEST_CASE("IOThreadPool: Legacy tests", "[IOThreadPool]") {
    IOThreadPool::initialize(0, 3.0);
    auto& pool = IOThreadPool::getInstance();
    
    size_t expectedThreads = IOThreadPool::getOptimalThreadCount(3.0);
    REQUIRE(pool.getExecutor().getThreadCount() == expectedThreads);
    
    auto future = pool.getExecutor().submit([]() {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return std::string("I/O operation completed");
    });
    
    auto result = future.get();
    REQUIRE(result == "I/O operation completed");
    
    IOThreadPool::shutdown();
}
