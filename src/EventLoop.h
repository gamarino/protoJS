#ifndef PROTOJS_EVENTLOOP_H
#define PROTOJS_EVENTLOOP_H

#include <functional>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <chrono>

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
     * @brief Block until a callback is queued or `maxWait` has passed.
     *
     * The process drain loop in main.cpp used to sleep a fixed 10 ms between
     * passes, so every completion handed over by another thread (a Deferred
     * settling, an I/O result) waited up to 10 ms to be noticed. A completion
     * now wakes the loop at once; the time-out bounds the wait for the
     * conditions the loop polls (workers, servers, orphaned resources).
     * The caller must be outside protoCore's running set (UnmanagedScope).
     */
    void waitForCallbacks(std::chrono::milliseconds maxWait);

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
