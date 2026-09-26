#pragma once

#include <cstddef>

#include <protoCore.h>

namespace protojs {

// Deferred release for resources a finalizer is forbidden to release itself.
//
// THE CONTRACT THIS EXISTS TO OBEY. protoCore's `Cell::finalize` runs on the GC
// thread during sweep, concurrently with the mutators, and
// protoCore/docs/GarbageCollector.md section 7 says a finalizer "only completes an
// action on an internal or external structure", and "never allocates cells, never
// publishes to a shared structure with compare-and-swap, never loops over protoCore
// data and never dereferences other ProtoObject*". It adds: "A finalizer must also
// not block: it runs on the single GC thread inside the sweep, so a wait there
// stalls collection for the whole space." The `ProtoExternalPointer` callbacks
// passed to `ProtoContext::fromExternalPointer` are finalizers and follow the same
// contract.
//
// protoJS had five finalizers that broke it, each releasing a resource whose
// release needs exactly the forbidden things: join an OS thread (blocks, and is
// not even proof the thread stopped, since sweep runs with the world going),
// release a `ProtoRootSet` pin (takes the very mutex the collector takes during
// root collection -- blocks AND publishes), and in one case destroy an entire
// second `ProtoSpace`.
//
// WHY A QUEUE AND NOT JUST "MOVE IT TO close()". Section 7's prescription is to
// release "at the point where the owner itself gives it up -- for a thread, its own
// exit path and its join", and that is done: `serverClose`, `socketDestroy` and
// `workerTerminate` now do the blocking work under an `UnmanagedScope`, on the
// mutator thread, and clear the pin there. But a script that never calls close()
// still drops the object, and something has to release it. Section 7's escape
// hatch for exactly that is the Phase 5b pattern: "the finalizer records a number,
// and a post-sweep phase with the collector's own context does the work". This is
// that shape, with protoJS's event loop standing in for the post-sweep phase,
// because the work here needs a mutator context rather than the collector's.
//
// WHAT THE FINALIZER SIDE COSTS. `post()` is an intrusive push onto a lock-free
// stack: one CAS loop over a pointer the orphan already carries. It allocates
// nothing -- no `new`, so no malloc that could block -- takes no lock, touches no
// protoCore API and never waits. That is the entire budget a finalizer has.

class GcOrphanQueue {
public:
    /**
     * @brief Base for a resource whose release cannot happen on the GC thread.
     *
     * Derive the state struct from this, have the finalizer do only the
     * non-blocking part (flip a flag, close a file descriptor -- external-resource
     * work section 7 explicitly permits) and then `post(this)`.
     */
    struct Orphan {
        /// Intrusive link, so `post()` allocates nothing.  Owned by the queue.
        Orphan* gcOrphanNext{nullptr};

        virtual ~Orphan() = default;

        /**
         * @brief Do the release, on a mutator thread, and `delete this`.
         *
         * Runs with a live `ProtoContext` for the calling thread available through
         * `threadProtoContext()`, so it may block (inside a `BlockingScope`),
         * call protoCore, and free protoCore-owned resources.
         */
        virtual void releaseOnMutator() = 0;
    };

    /**
     * @brief Finalizer side.  Never blocks, never allocates, calls no protoCore API.
     *
     * Safe from the GC thread.  @p orphan must not already be queued.
     */
    static void post(Orphan* orphan) noexcept;

    /**
     * @brief Mutator side.  Releases everything queued and returns how many.
     *
     * Call from a thread that owns a protoCore context -- protoJS drains from
     * `EventLoop::processCallbacks`, which is pumped by the process drain loop.
     * Reentrant calls are safe: the queue is emptied in one exchange before any
     * orphan is released, so an orphan posted during a release is picked up by the
     * next drain rather than recursed into.
     */
    static std::size_t drain() noexcept;

    /** @brief Orphans waiting to be released.  For tests and for the drain loop. */
    static std::size_t pending() noexcept;

    /** @brief Total ever posted.  A fixture needs this to prove the path ran. */
    static std::size_t postedTotal() noexcept;

    /** @brief Total ever released. */
    static std::size_t releasedTotal() noexcept;
};

}  // namespace protojs
