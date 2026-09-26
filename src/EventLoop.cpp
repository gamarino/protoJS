#include "EventLoop.h"
#include "GcOrphanQueue.h"
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

    // The mutator-side half of the finalizer contract.  Five of protoJS's
    // ProtoExternalPointer finalizers used to join threads and release ProtoRootSet
    // pins on the GC thread, which protoCore forbids (docs/GarbageCollector.md S7:
    // a finalizer must not block, must not publish to a shared structure and must
    // not call protoCore).  They now record the orphan and return; the work happens
    // here, on a thread that owns a protoCore context and may legally block.
    //
    // This is the right place because it is where protoJS already pumps deferred
    // work: the process drain loop calls processCallbacks until nothing is
    // outstanding, so every orphan posted during a run is released before exit.
    GcOrphanQueue::drain();
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
