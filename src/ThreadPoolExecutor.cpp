#include "ThreadPoolExecutor.h"
#include "ThreadProtoContext.h"
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

    // Everything below blocks, and the callers that reach it are registered
    // protoCore threads: ~JSContextWrapper (main thread, and the main thread again
    // when it releases a worker's wrapper) and CPUThreadPool/IOThreadPool
    // ::initialize, which shut the previous pool down from the constructing thread.
    // A registered thread that blocks without leaving protoCore's running set is
    // still counted in `runningThreads`, so the stop-the-world quorum can never be
    // met, no collection can start, and every thread that needs memory waits for a
    // cycle that cannot begin -- including, often, a pool worker being joined here.
    //
    // The guard covers the CONDITION WAIT as well as the joins. Bracketing only the
    // joins would leave the wait at the top of the region blocking just as long: it
    // waits for the queue to drain and for `activeCount` to reach zero, i.e. for
    // tasks that may themselves be waiting for memory.
    //
    // ThreadUnmanagedScope rather than ProtoContext::UnmanagedScope: this class is
    // deliberately free of protoCore in its header, and the right context is the
    // CALLING thread's, which is not reachable from anything this class holds. It is
    // an UnmanagedScope that finds that context, and a no-op on an unregistered
    // thread -- which is correct, since such a thread has nothing to leave.
    //
    // One guard per blocking region, each next to the call it protects.
    {
        ThreadUnmanagedScope parked;
        std::unique_lock<std::mutex> lock(queueMutex);
        condition.wait(lock, [this] {
            return taskQueue.empty() && activeCount.load() == 0;
        });
    }

    {
        ThreadUnmanagedScope parked;
        for (auto& thread : threads) {
            if (thread.joinable()) {
                thread.join();
            }
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

    // Same reasoning as shutdown(): the joins block on a registered thread. There
    // is no condition wait here because the queue was cleared above, but a worker
    // already inside a task still has to finish it, and that task can be waiting
    // for memory.
    {
        ThreadUnmanagedScope parked;
        for (auto& thread : threads) {
            if (thread.joinable()) {
                thread.join();
            }
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
