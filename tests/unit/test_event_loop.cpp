#include <catch2/catch.hpp>
#include "../../src/EventLoop.h"
#include <thread>
#include <chrono>
#include <atomic>

using namespace protojs;

TEST_CASE("EventLoop: Singleton", "[EventLoop]") {
    auto& loop1 = EventLoop::getInstance();
    auto& loop2 = EventLoop::getInstance();
    REQUIRE(&loop1 == &loop2);
}

TEST_CASE("EventLoop: Enqueue and process", "[EventLoop]") {
    EventLoop& loop = EventLoop::getInstance();
    
    SECTION("Enqueue callback") {
        std::atomic<int> counter{0};
        
        loop.enqueueCallback([&counter]() {
            counter = 42;
        });
        
        loop.processCallbacks();
        
        REQUIRE(counter.load() == 42);
    }
    
    SECTION("Multiple callbacks") {
        std::atomic<int> counter{0};
        
        for (int i = 0; i < 10; ++i) {
            loop.enqueueCallback([&counter, i]() {
                counter += i;
            });
        }
        
        loop.processCallbacks();
        
        REQUIRE(counter.load() == 45); // 0+1+2+...+9 = 45
    }
    
    SECTION("Has pending callbacks") {
        REQUIRE_FALSE(loop.hasPendingCallbacks());
        
        loop.enqueueCallback([]() {});
        REQUIRE(loop.hasPendingCallbacks());
        
        loop.processCallbacks();
        REQUIRE_FALSE(loop.hasPendingCallbacks());
    }
}

TEST_CASE("EventLoop: Exception handling", "[EventLoop]") {
    EventLoop& loop = EventLoop::getInstance();
    
    SECTION("Exception in callback doesn't stop processing") {
        std::atomic<int> counter{0};
        
        loop.enqueueCallback([]() {
            throw std::runtime_error("Test exception");
        });
        
        loop.enqueueCallback([&counter]() {
            counter = 1;
        });
        
        loop.processCallbacks();
        
        REQUIRE(counter.load() == 1);
    }
}
