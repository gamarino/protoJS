#include "GcOrphanQueue.h"

#include <atomic>

namespace protojs {

namespace {

// A Treiber stack.  Push is a CAS loop; drain is a single exchange.  Order is not
// meaningful -- these are independent resources -- so a stack is chosen over a
// queue purely because push needs no second pointer and therefore no lock.
std::atomic<GcOrphanQueue::Orphan*> g_head{nullptr};

std::atomic<std::size_t> g_pending{0};
std::atomic<std::size_t> g_posted{0};
std::atomic<std::size_t> g_released{0};

// Guards against a drain re-entering itself.  `releaseOnMutator` can run a whole
// JSContextWrapper destructor, which pumps other code; if that code reached
// `drain()` again it would release orphans while an earlier release was still in
// flight.  A thread-local flag turns the inner call into a no-op, and the outer
// drain picks up anything posted meanwhile on its caller's next pass.
thread_local bool g_draining = false;

}  // namespace

void GcOrphanQueue::post(Orphan* orphan) noexcept {
    if (!orphan) return;
    // No allocation, no lock, no protoCore call, no wait: the whole budget a
    // finalizer running on the GC thread during sweep has.
    Orphan* head = g_head.load(std::memory_order_relaxed);
    do {
        orphan->gcOrphanNext = head;
    } while (!g_head.compare_exchange_weak(head, orphan,
                                           std::memory_order_release,
                                           std::memory_order_relaxed));
    g_pending.fetch_add(1, std::memory_order_relaxed);
    g_posted.fetch_add(1, std::memory_order_relaxed);
}

std::size_t GcOrphanQueue::drain() noexcept {
    if (g_draining) return 0;
    g_draining = true;

    // Detach the whole stack first, so an orphan posted during a release lands on
    // a fresh stack rather than in the list being walked.
    Orphan* list = g_head.exchange(nullptr, std::memory_order_acquire);
    std::size_t n = 0;
    while (list) {
        Orphan* next = list->gcOrphanNext;
        list->gcOrphanNext = nullptr;
        g_pending.fetch_sub(1, std::memory_order_relaxed);
        // Releases and deletes itself.  May block; it is on a mutator thread and
        // uses ThreadUnmanagedScope to leave protoCore's running set while it does.
        list->releaseOnMutator();
        g_released.fetch_add(1, std::memory_order_relaxed);
        ++n;
        list = next;
    }

    g_draining = false;
    return n;
}

std::size_t GcOrphanQueue::pending() noexcept {
    return g_pending.load(std::memory_order_relaxed);
}

std::size_t GcOrphanQueue::postedTotal() noexcept {
    return g_posted.load(std::memory_order_relaxed);
}

std::size_t GcOrphanQueue::releasedTotal() noexcept {
    return g_released.load(std::memory_order_relaxed);
}

}  // namespace protojs
