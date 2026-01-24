#ifndef PROTOJS_EVENTLOOP_H
#define PROTOJS_EVENTLOOP_H

#include <functional>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>

namespace protojs {

/**
 * @brief Event loop for processing callbacks from Deferred and I/O operations.
 * 
 * All callbacks are executed on the main thread to ensure thread safety
 * when interacting with JavaScript context.
 */
class EventLoop {
public:
    /**
     * @brief Get the singleton instance of EventLoop.
     */
    static EventLoop& getInstance();
    
    /**
     * @brief Enqueue a callback to be executed on the main thread.
     * @param callback Function to execute
     */
    void enqueueCallback(std::function<void()> callback);
    
    /**
     * @brief Process all pending callbacks.
     * 
     * Should be called from the main thread periodically or in a loop.
     */
    void processCallbacks();
    
    /**
     * @brief Run the event loop until stopped.
     * 
     * Blocks until stop() is called.
     */
    void run();
    
    /**
     * @brief Stop the event loop.
     */
    void stop();
    
    /**
     * @brief Check if there are pending callbacks.
     */
    bool hasPendingCallbacks() const;

private:
    EventLoop() = default;
    ~EventLoop() = default;
    EventLoop(const EventLoop&) = delete;
    EventLoop& operator=(const EventLoop&) = delete;
    
    static EventLoop instance;
    
    std::queue<std::function<void()>> callbackQueue;
    mutable std::mutex queueMutex;
    std::condition_variable condition;
    std::atomic<bool> running{false};
};

} // namespace protojs

#endif // PROTOJS_EVENTLOOP_H
