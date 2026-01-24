#include "CPUThreadPool.h"
#include <thread>
#include <mutex>
#include <memory>

namespace protojs {

std::unique_ptr<CPUThreadPool> CPUThreadPool::instance = nullptr;
std::mutex CPUThreadPool::instanceMutex;

CPUThreadPool::CPUThreadPool(size_t numThreads) {
    if (numThreads == 0) {
        numThreads = getOptimalThreadCount();
    }
    executor = std::make_unique<ThreadPoolExecutor>(numThreads, "CPUThreadPool");
}

CPUThreadPool& CPUThreadPool::getInstance() {
    std::lock_guard<std::mutex> lock(instanceMutex);
    if (!instance) {
        instance = std::make_unique<CPUThreadPool>(0);
    }
    return *instance;
}

void CPUThreadPool::initialize(size_t numThreads) {
    std::lock_guard<std::mutex> lock(instanceMutex);
    if (instance) {
        instance->executor->shutdown();
    }
    instance = std::make_unique<CPUThreadPool>(numThreads);
}

size_t CPUThreadPool::getOptimalThreadCount() {
    size_t cores = std::thread::hardware_concurrency();
    return cores > 0 ? cores : 1; // Fallback to 1 if detection fails
}

void CPUThreadPool::shutdown() {
    std::lock_guard<std::mutex> lock(instanceMutex);
    if (instance) {
        instance->executor->shutdown();
        instance.reset();
    }
}

} // namespace protojs
