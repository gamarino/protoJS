#ifndef PROTOJS_CPUTHREADPOOL_H
#define PROTOJS_CPUTHREADPOOL_H

#include "ThreadPoolExecutor.h"
#include <memory>

namespace protojs {

/**
 * @brief CPU thread pool optimized for CPU-intensive tasks.
 * 
 * Size is automatically set to the number of CPU cores.
 * Used for executing Deferred tasks and computational work.
 */
class CPUThreadPool {
public:
    /**
     * @brief Get the singleton instance of CPUThreadPool.
     */
    static CPUThreadPool& getInstance();
    
    /**
     * @brief Initialize the pool with a specific number of threads.
     * @param numThreads Number of threads (default: number of CPU cores)
     */
    static void initialize(size_t numThreads = 0);
    
    /**
     * @brief Get the underlying ThreadPoolExecutor.
     */
    ThreadPoolExecutor& getExecutor() { return *executor; }
    
    /**
     * @brief Get the optimal number of threads (number of CPU cores).
     */
    static size_t getOptimalThreadCount();
    
    /**
     * @brief Shutdown the pool.
     */
    static void shutdown();

    // Constructor made public for make_unique, but should only be called via initialize()
    CPUThreadPool(size_t numThreads);
    ~CPUThreadPool() = default;
    
private:
    CPUThreadPool(const CPUThreadPool&) = delete;
    CPUThreadPool& operator=(const CPUThreadPool&) = delete;
    
    static std::unique_ptr<CPUThreadPool> instance;
    static std::mutex instanceMutex;
    
    std::unique_ptr<ThreadPoolExecutor> executor;
};

} // namespace protojs

#endif // PROTOJS_CPUTHREADPOOL_H
