#include "IOThreadPool.h"
#include <thread>
#include <mutex>
#include <memory>
#include <cmath>

namespace protojs {

std::unique_ptr<IOThreadPool> IOThreadPool::instance = nullptr;
std::mutex IOThreadPool::instanceMutex;
double IOThreadPool::defaultFactor = 3.0;

IOThreadPool::IOThreadPool(size_t numThreads) {
    if (numThreads == 0) {
        numThreads = getOptimalThreadCount(defaultFactor);
    }
    executor = std::make_unique<ThreadPoolExecutor>(numThreads, "IOThreadPool");
}

IOThreadPool& IOThreadPool::getInstance() {
    std::lock_guard<std::mutex> lock(instanceMutex);
    if (!instance) {
        instance = std::make_unique<IOThreadPool>(0);
    }
    return *instance;
}

void IOThreadPool::initialize(size_t numThreads, double factor) {
    std::lock_guard<std::mutex> lock(instanceMutex);
    if (instance) {
        instance->executor->shutdown();
    }
    defaultFactor = factor;
    instance = std::make_unique<IOThreadPool>(numThreads);
}

size_t IOThreadPool::getOptimalThreadCount(double factor) {
    size_t cores = std::thread::hardware_concurrency();
    if (cores == 0) cores = 1; // Fallback
    return static_cast<size_t>(std::ceil(cores * factor));
}

void IOThreadPool::shutdown() {
    std::lock_guard<std::mutex> lock(instanceMutex);
    if (instance) {
        instance->executor->shutdown();
        instance.reset();
    }
}

} // namespace protojs
