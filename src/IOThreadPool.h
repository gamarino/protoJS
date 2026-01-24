#ifndef PROTOJS_IOTHREADPOOL_H
#define PROTOJS_IOTHREADPOOL_H

#include "ThreadPoolExecutor.h"
#include <memory>

namespace protojs {

/**
 * @brief I/O thread pool optimized for blocking I/O operations.
 * 
 * Size is automatically set to 3-4x the number of CPU cores (configurable).
 * Used for file system operations, network I/O, etc.
 */
class IOThreadPool {
public:
    /**
     * @brief Get the singleton instance of IOThreadPool.
     */
    static IOThreadPool& getInstance();
    
    /**
     * @brief Initialize the pool with a specific number of threads or factor.
     * @param numThreads Number of threads (0 = auto-calculate)
     * @param factor Multiplier for CPU cores (default: 3.0, used if numThreads == 0)
     */
    static void initialize(size_t numThreads = 0, double factor = 3.0);
    
    /**
     * @brief Get the underlying ThreadPoolExecutor.
     */
    ThreadPoolExecutor& getExecutor() { return *executor; }
    
    /**
     * @brief Get the optimal number of threads based on CPU count and factor.
     * @param factor Multiplier for CPU cores (default: 3.0)
     */
    static size_t getOptimalThreadCount(double factor = 3.0);
    
    /**
     * @brief Shutdown the pool.
     */
    static void shutdown();

    // Constructor made public for make_unique, but should only be called via initialize()
    IOThreadPool(size_t numThreads);
    ~IOThreadPool() = default;
    
private:
    IOThreadPool(const IOThreadPool&) = delete;
    IOThreadPool& operator=(const IOThreadPool&) = delete;
    
    static std::unique_ptr<IOThreadPool> instance;
    static std::mutex instanceMutex;
    static double defaultFactor;
    
    std::unique_ptr<ThreadPoolExecutor> executor;
};

} // namespace protojs

#endif // PROTOJS_IOTHREADPOOL_H
