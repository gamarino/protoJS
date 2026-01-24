#ifndef PROTOJS_THREADPOOLEXECUTOR_H
#define PROTOJS_THREADPOOLEXECUTOR_H

#include <thread>
#include <vector>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <atomic>
#include <string>

namespace protojs {

/**
 * @brief Generic thread pool executor, similar to Java's ExecutorService.
 * 
 * Manages a pool of worker threads that execute tasks from a queue.
 * Supports graceful shutdown and provides metrics.
 */
class ThreadPoolExecutor {
public:
    /**
     * @brief Constructs a thread pool executor.
     * @param numThreads Number of worker threads in the pool
     * @param name Name of the pool (for debugging/logging)
     */
    ThreadPoolExecutor(size_t numThreads, const std::string& name = "ThreadPool");
    
    /**
     * @brief Destructor. Performs graceful shutdown.
     */
    ~ThreadPoolExecutor();
    
    /**
     * @brief Submits a task to the pool for execution.
     * @param task Callable object (function, lambda, etc.)
     * @return std::future containing the result of the task
     */
    template<typename F>
    auto submit(F&& task) -> std::future<decltype(task())> {
        using ReturnType = decltype(task());
        
        auto packagedTask = std::make_shared<std::packaged_task<ReturnType()>>(
            std::forward<F>(task)
        );
        
        std::future<ReturnType> result = packagedTask->get_future();
        
        {
            std::unique_lock<std::mutex> lock(queueMutex);
            
            if (shutdownFlag) {
                throw std::runtime_error("ThreadPoolExecutor is shutdown");
            }
            
            taskQueue.emplace([packagedTask]() {
                (*packagedTask)();
            });
        }
        
        condition.notify_one();
        return result;
    }
    
    /**
     * @brief Initiates graceful shutdown.
     * 
     * No new tasks will be accepted, but already queued tasks will complete.
     */
    void shutdown();
    
    /**
     * @brief Initiates immediate shutdown.
     * 
     * No new tasks accepted, queued tasks are abandoned.
     */
    void shutdownNow();
    
    /**
     * @brief Returns the number of currently active (executing) threads.
     */
    size_t getActiveCount() const;
    
    /**
     * @brief Returns the number of tasks waiting in the queue.
     */
    size_t getQueueSize() const;
    
    /**
     * @brief Returns the total number of threads in the pool.
     */
    size_t getThreadCount() const { return threads.size(); }
    
    /**
     * @brief Returns the name of this thread pool.
     */
    const std::string& getName() const { return poolName; }
    
    /**
     * @brief Checks if the pool is shutdown.
     */
    bool isShutdown() const { return shutdownFlag; }

private:
    void workerThread();
    
    std::string poolName;
    std::vector<std::thread> threads;
    std::queue<std::function<void()>> taskQueue;
    mutable std::mutex queueMutex;
    std::condition_variable condition;
    std::atomic<bool> shutdownFlag;
    std::atomic<bool> shutdownNowFlag;
    std::atomic<size_t> activeCount;
};

} // namespace protojs

#endif // PROTOJS_THREADPOOLEXECUTOR_H
