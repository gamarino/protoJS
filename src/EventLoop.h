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

    /**
     * @brief Install the end-of-turn hook, run after every callback.
     *
     * The host uses it to apply what Node does at the end of each turn: an
     * exception a callback left uncaught, or a promise rejection nobody
     * handled, ends the process (see endOfTurnChecks in ProtoDeferred.h).
     * Main thread only, like processCallbacks.
     */
    void setEndOfTurnHook(std::function<void()> hook);

    /**
     * @brief Keep the process alive for an asynchronous operation in flight.
     *
     * A native module that hands work to a pool thread and will later enqueue
     * the JS callback calls beginOperation() before handing it off and
     * endOperation() once the callback has run.  Until then the queue can be
     * empty while a callback is still owed, and the drain loop in main.cpp
     * (which also checks hasPendingOperations) must not end the process: in
     * Node, a pending request keeps the event loop alive.
     */
    void beginOperation() { pendingOperations.fetch_add(1); }
    void endOperation() { pendingOperations.fetch_sub(1); }
    bool hasPendingOperations() const { return pendingOperations.load() > 0; }

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
    std::function<void()> endOfTurnHook;
    std::atomic<int> pendingOperations{0};
};

} // namespace protojs

#endif // PROTOJS_EVENTLOOP_H
