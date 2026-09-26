#pragma once

#include <protoCore.h>

namespace protojs {

// Per-OS-thread record of the protoCore context that thread is registered with.
//
// WHY THIS EXISTS. A registered protoCore thread that blocks without leaving the
// running set is still counted in `runningThreads`, so the stop-the-world quorum
// (`parkedThreads >= runningThreads`) can never be met, no collection cycle can
// start, and every thread that then needs memory waits for a cycle that cannot
// begin. That is a deadlock, not a slow shutdown. `ProtoContext::UnmanagedScope`
// is the fix, and it needs a `ProtoContext*`.
//
// Some of protoJS's blocking code has no context in hand and cannot be given one
// without dragging protoCore into headers that are deliberately free of it --
// ThreadPoolExecutor is the case that forced this. Two tempting substitutes are
// both wrong:
//
//   * `this->pContext` of the object being destroyed. A worker's JSContextWrapper
//     is destroyed by the MAIN thread, and its context belongs to the worker's own
//     space with the worker thread adopted as that space's main thread. Parking
//     that context from the main thread increments `parkedThreads` for a thread
//     that is actively running managed code -- the inverse failure, where the
//     collector reaches quorum and scans while a mutator mutates.
//   * `JSContextWrapper::current()`. It is only published during `eval()` and
//     under `CurrentScope`, so it is null on exactly the teardown paths that
//     block.
//
// protoCore has no thread-local of its own (there is not one `thread_local` in
// its sources), so the record has to live here.
//
// WHAT REGISTERS A THREAD, verified rather than assumed: protoJS never calls any
// `registerThread`. A thread enters `runningThreads` either by constructing a
// `ProtoSpace` -- whose constructor adopts the constructing thread as that space's
// main thread -- or through `ProtoSpace::newThread`. A bare `std::thread` (the
// CPU and I/O pools, the accept and read loops, the GC thread) is NOT registered,
// and protoCore's Thread.cpp says so explicitly. So the slot is set by
// `JSContextWrapper`'s constructor, which is the moment the constructing thread is
// adopted, and each `worker_threads` worker fills its own slot with its own space.

/**
 * @brief Record @p ctx as the calling thread's protoCore root context.
 *
 * Only the FIRST context registered on a thread is kept: that is the one whose
 * space adopted this thread, and therefore the only one whose parked/running
 * accounting describes this thread.
 */
void setThreadProtoContext(proto::ProtoContext* ctx) noexcept;

/**
 * @brief Forget @p ctx if it is the calling thread's recorded context.
 *
 * A no-op when the slot holds something else, which is what makes it safe to call
 * from a destructor running on a thread other than the one that registered.
 */
void clearThreadProtoContext(proto::ProtoContext* ctx) noexcept;

/** @brief The calling thread's recorded context, or nullptr if unregistered. */
proto::ProtoContext* threadProtoContext() noexcept;

/**
 * @brief A `ProtoContext::UnmanagedScope` that finds its own context.
 *
 * Use it where no `ProtoContext*` is in hand; where one is, pass it explicitly and
 * this behaves exactly like `ProtoContext::UnmanagedScope` on it.
 *
 * On a thread protoCore does not know it is a no-op, which is the correct behaviour
 * there: an unregistered thread is not in `runningThreads` and has nothing to leave.
 *
 * Bracket the WHOLE blocking region, not just the syscall: a
 * `condition_variable::wait` immediately before a `join` blocks just as effectively
 * as the join. And keep the guard next to the call it protects -- protoCore's static
 * conformance rule `blocking_join_unbracketed` looks for a guard within eight lines
 * of the join, which is a good rule for a human reader too.
 *
 * Calls nest; protoCore refcounts the unmanaged depth, so a single thread
 * contributes one slot to the quorum however many regions it has open.
 */
class ThreadUnmanagedScope {
public:
    explicit ThreadUnmanagedScope(proto::ProtoContext* explicitCtx = nullptr) noexcept
        : ctx_(explicitCtx ? explicitCtx : threadProtoContext()) {
        if (ctx_) ctx_->goUnmanaged();
    }
    ~ThreadUnmanagedScope() {
        if (ctx_) ctx_->returnFromUnmanaged();
    }
    ThreadUnmanagedScope(const ThreadUnmanagedScope&) = delete;
    ThreadUnmanagedScope& operator=(const ThreadUnmanagedScope&) = delete;

    /** @brief True when this scope actually left protoCore's running set. */
    bool active() const noexcept { return ctx_ != nullptr; }

private:
    proto::ProtoContext* ctx_;
};

}  // namespace protojs
