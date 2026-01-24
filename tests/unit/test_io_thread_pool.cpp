#include <catch2/catch.hpp>
#include "../../src/IOThreadPool.h"
#include <thread>
#include <chrono>

using namespace protojs;

TEST_CASE("IOThreadPool: Singleton and initialization", "[IOThreadPool]") {
    SECTION("Get instance") {
        auto& pool1 = IOThreadPool::getInstance();
        auto& pool2 = IOThreadPool::getInstance();
        REQUIRE(&pool1 == &pool2);
    }
    
    SECTION("Optimal thread count with factor") {
        size_t optimal = IOThreadPool::getOptimalThreadCount(3.0);
        size_t cpuCount = std::thread::hardware_concurrency();
        if (cpuCount > 0) {
            REQUIRE(optimal >= cpuCount * 3);
        }
    }
    
    SECTION("Custom thread count") {
        IOThreadPool::initialize(12);
        auto& pool = IOThreadPool::getInstance();
        REQUIRE(pool.getExecutor().getThreadCount() == 12);
        
        IOThreadPool::shutdown();
    }
    
    IOThreadPool::shutdown();
}

TEST_CASE("IOThreadPool: I/O simulation", "[IOThreadPool]") {
    IOThreadPool::initialize(0, 3.0);
    auto& pool = IOThreadPool::getInstance();
    
    SECTION("Simulate I/O operation") {
        auto future = pool.getExecutor().submit([]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            return std::string("I/O completed");
        });
        
        auto result = future.get();
        REQUIRE(result == "I/O completed");
    }
    
    IOThreadPool::shutdown();
}
