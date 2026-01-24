#include "ThreadPoolExecutor.h"
#include <iostream>
#include <stdexcept>

namespace protojs {

ThreadPoolExecutor::ThreadPoolExecutor(size_t numThreads, const std::string& name)
    : poolName(name)
    , shutdownFlag(false)
    , shutdownNowFlag(false)
    , activeCount(0)
{
    if (numThreads == 0) {
        throw std::invalid_argument("ThreadPoolExecutor: numThreads must be > 0");
    }
    
    threads.reserve(numThreads);
    for (size_t i = 0; i < numThreads; ++i) {
        threads.emplace_back(&ThreadPoolExecutor::workerThread, this);
    }
}

ThreadPoolExecutor::~ThreadPoolExecutor() {
    shutdown();
}

void ThreadPoolExecutor::shutdown() {
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        shutdownFlag = true;
    }
    condition.notify_all();
    
    {
        std::unique_lock<std::mutex> lock(queueMutex);
        condition.wait(lock, [this] {
            return taskQueue.empty() && activeCount.load() == 0;
        });
    }
    
    for (auto& thread : threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}

void ThreadPoolExecutor::shutdownNow() {
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        shutdownNowFlag = true;
        shutdownFlag = true;
        
        // Clear the queue
        while (!taskQueue.empty()) {
            taskQueue.pop();
        }
    }
    
    condition.notify_all();
    
    for (auto& thread : threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}

size_t ThreadPoolExecutor::getActiveCount() const {
    return activeCount.load();
}

size_t ThreadPoolExecutor::getQueueSize() const {
    std::lock_guard<std::mutex> lock(queueMutex);
    return taskQueue.size();
}

void ThreadPoolExecutor::workerThread() {
    while (true) {
        std::function<void()> task;
        
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            
            condition.wait(lock, [this] {
                return !taskQueue.empty() || shutdownFlag;
            });
            
            if (shutdownFlag && (taskQueue.empty() || shutdownNowFlag)) {
                break;
            }
            
            if (!taskQueue.empty()) {
                task = std::move(taskQueue.front());
                taskQueue.pop();
                activeCount++;
            }
        }
        
        if (task) {
            try {
                task();
            } catch (const std::exception& e) {
                std::cerr << "Exception in thread pool '" << poolName 
                         << "': " << e.what() << std::endl;
            } catch (...) {
                std::cerr << "Unknown exception in thread pool '" << poolName << "'" << std::endl;
            }
            
            activeCount--;
        }
        
        condition.notify_all();
    }
}

} // namespace protojs
