#ifndef PROTOJS_MICROTASKQUEUE_H
#define PROTOJS_MICROTASKQUEUE_H

// MicrotaskQueue -- the ECMAScript job queue of one agent.
//
// ECMA-262 runs promise reactions as jobs: HostEnqueuePromiseJob (§9.5.5)
// appends a job, and the host runs the queued jobs, first in, first out, when
// no JavaScript is on the stack.  Node calls that a microtask checkpoint and
// performs one after the main script and after every macrotask (an I/O
// callback, a setImmediate callback, ...), draining the queue completely --
// jobs queued by jobs included -- before the next macrotask runs.  protoJS does
// the same:
//
//   - JSContextWrapper::eval ends with a checkpoint (the main script, a
//     preloaded script, a worker's script, each REPL input);
//   - EventLoop::processCallbacks performs one after every callback;
//   - the checkpoint refuses to run while the interpreter has a frame on the
//     stack, so a nested eval (the Function constructor) never drains it.
//
// One queue per JSContextWrapper (agent): a worker_threads worker has its own.
// Jobs are enqueued on the queue of the wrapper current on the calling thread
// (JSContextWrapper::current()), or, when none is current, on the queue of the
// wrapper the thread constructed.
//
// What a job is
// -------------
// A job is a ProtoList whose element 0 is a JobKind.  The operands are
// ordinary protoCore objects (the reaction, the promise, the thenable), so
// they must be visible to the collector for as long as the job is queued or
// running.  They are: every queued job hangs off ONE mutable holder object
// pinned in the wrapper's root set (docs/GC_BRIDGING.md, Mechanism B), never
// one root-set handle per job.  While a batch runs it stays reachable as the
// holder's "draining" list.  Once the batch has run, the holder drops it, so a
// settled promise is not kept alive by the queue.
//
// Unhandled rejections (HostPromiseRejectionTracker, §27.2.1.9)
// --------------------------------------------------------------
// A promise rejected while it has no handler is recorded on the holder's
// rejection list for the current checkpoint.  Attaching a handler sets the
// promise's [[PromiseIsHandled]]; the list is not searched.  At the end of the
// checkpoint, after the job queue is empty, every recorded promise still not
// handled is reported -- "Uncaught (in promise) <reason>" -- and, as Node does
// by default (--unhandled-rejections=throw), the process ends with status 1.
// The REPL reports without ending the process.  The list is emptied at every
// checkpoint, so it holds only the rejections of the current turn.
//
// Cross-thread entry point
// ------------------------
// enqueueFromAnyThread() is the one method another thread may call.  It is
// meant for a native producer that finishes work on a pool thread and must
// settle a promise -- or run any other continuation -- on the thread that owns
// the promise, as a microtask.  The job is a C++ closure; it must not capture a
// ProtoObject* the collector cannot see (pin such values in the wrapper's root
// set before handing them to the pool thread, and release the pins inside the
// job, which runs on the owner thread with a ProtoContext of the owner's
// space).  The closure goes to a mutex-protected inbox; the method then wakes
// the event loop with a callback, and the checkpoint that follows that
// callback moves the inbox into the job queue in arrival order and runs it.

#include <protoCore.h>

#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <unordered_map>

namespace protojs {

class JSContextWrapper;

class MicrotaskQueue {
public:
    enum JobKind : long long {
        kReactionJob        = 1,  // [kind, reaction, argument, settledState]
        kResolveThenableJob = 2,  // [kind, promise, thenable, then]
        kCallbackJob        = 3,  // [kind, callback]            (queueMicrotask)
        kNativeJob          = 4,  // [kind, id]                  (enqueueFromAnyThread)
    };

    using NativeJob = std::function<void(proto::ProtoContext*)>;

    explicit MicrotaskQueue(JSContextWrapper* owner);
    ~MicrotaskQueue();

    MicrotaskQueue(const MicrotaskQueue&) = delete;
    MicrotaskQueue& operator=(const MicrotaskQueue&) = delete;

    /** The queue the calling thread enqueues on, or nullptr when the thread has
     *  no wrapper at all. */
    static MicrotaskQueue* current();

    /** Owner thread: append a job (a ProtoList, element 0 a JobKind). */
    void enqueue(proto::ProtoContext* ctx, const proto::ProtoList* job);

    /** Owner thread: queueMicrotask(callback). */
    void enqueueCallback(proto::ProtoContext* ctx, const proto::ProtoObject* callback);

    /** Any thread: run `job` on the owner thread as a microtask, after the jobs
     *  already queued.  Wakes the event loop.  See the header comment. */
    void enqueueFromAnyThread(NativeJob job);

    /** HostPromiseRejectionTracker(promise, "reject"): `promise` was rejected
     *  while it had no handler. */
    void noteRejection(proto::ProtoContext* ctx, const proto::ProtoObject* promise);

    /**
     * Perform a microtask checkpoint: run every queued job, including the jobs
     * they queue, then report the rejections of this turn that are still
     * unhandled.  A no-op while the interpreter has a frame on this thread's
     * stack or while a checkpoint is already running.  `parent` is the context
     * the jobs' contexts are created under: the calling thread's current
     * context (the wrapper's root context when nothing else is open).
     */
    void checkpoint(proto::ProtoContext* parent = nullptr);

    /** True when jobs or cross-thread jobs are waiting. */
    bool hasPendingJobs() const;

    /** Perform a checkpoint on every queue that received a job from this
     *  thread since the last one.  Called by the event loop after each
     *  callback. */
    static void checkpointThread();

    /** Whether an unhandled rejection ends the process (true, the default, as
     *  in Node) or is only reported (the REPL). */
    static void setUnhandledRejectionsAreFatal(bool fatal);

private:
    void ensureHolder(proto::ProtoContext* ctx);
    const proto::ProtoList* holderList(proto::ProtoContext* ctx, const proto::ProtoString* key) const;
    void setHolderList(proto::ProtoContext* ctx, const proto::ProtoString* key,
                       const proto::ProtoList* list);
    void scheduleOnThisThread();
    void takeInbox(proto::ProtoContext* ctx);
    void runJob(proto::ProtoContext* ctx, const proto::ProtoObject* job);
    void reportRejections(proto::ProtoContext* ctx);

    JSContextWrapper* owner_;
    const proto::ProtoObject* holder_ = nullptr;   // pinned in owner_'s root set
    std::size_t pendingJobs_ = 0;                  // jobs on the holder's list
    bool pendingRejections_ = false;
    bool draining_ = false;

    std::unordered_map<long long, NativeJob> nativeJobs_;
    long long nextNativeId_ = 1;

    mutable std::mutex inboxMutex_;
    std::deque<NativeJob> inbox_;
    std::atomic<bool> inboxNonEmpty_{false};
};

}  // namespace protojs

#endif  // PROTOJS_MICROTASKQUEUE_H
