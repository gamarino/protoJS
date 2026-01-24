#include "EventLoop.h"
#include <iostream>

namespace protojs {

EventLoop EventLoop::instance;

EventLoop& EventLoop::getInstance() {
    return instance;
}

void EventLoop::enqueueCallback(std::function<void()> callback) {
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        callbackQueue.push(std::move(callback));
    }
    condition.notify_one();
}

void EventLoop::processCallbacks() {
    std::queue<std::function<void()>> callbacksToProcess;
    
    {
        std::lock_guard<std::mutex> lock(queueMutex);
        callbacksToProcess.swap(callbackQueue);
    }
    
    while (!callbacksToProcess.empty()) {
        auto callback = callbacksToProcess.front();
        callbacksToProcess.pop();
        
        try {
            callback();
        } catch (const std::exception& e) {
            // Log error but continue processing other callbacks
            std::cerr << "Exception in event loop callback: " << e.what() << std::endl;
        } catch (...) {
            std::cerr << "Unknown exception in event loop callback" << std::endl;
        }
    }
}

void EventLoop::run() {
    running = true;
    
    while (running) {
        std::unique_lock<std::mutex> lock(queueMutex);
        
        condition.wait(lock, [this] {
            return !callbackQueue.empty() || !running;
        });
        
        if (!running && callbackQueue.empty()) {
            break;
        }
        
        lock.unlock();
        processCallbacks();
    }
}

void EventLoop::stop() {
    running = false;
    condition.notify_all();
}

bool EventLoop::hasPendingCallbacks() const {
    std::lock_guard<std::mutex> lock(queueMutex);
    return !callbackQueue.empty();
}

} // namespace protojs
