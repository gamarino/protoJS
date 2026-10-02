#pragma once

// DeferredPool -- the threads a `Deferred` runs its function on.
//
// `new Promise(executor)` runs the executor on the calling thread, and its
// reactions on that thread's event loop. `new Deferred(fn)` runs `fn` on a
// thread of this pool instead: the developer chooses sequential or parallel
// execution by the constructor, with no new syntax. The Deferred itself is a
// promise of the OWNER thread (src/ProtoDeferred.h): it settles there, and its
// reactions run there as microtasks.
//
// The pool threads are protoCore threads (ProtoSpace::newThread) of the
// owner's space, not separate runtimes. A function and everything it reaches
// are shared with the owner thread without copying: immutable values directly,
// mutable objects through protoCore's atomic references to immutable
// snapshots. docs/DEFERRED_USAGE.md states what that means for programs.
//
// One pool per JSContextWrapper (per space), started on the first `new
// Deferred` with as many threads as the CPU pool (`--cpu-threads`, default
// the hardware thread count), and joined by ~JSContextWrapper.
//
// A pool thread, for its whole life:
//   - runs with the owner wrapper current (root set, prototypes, globals);
//   - has a JSContextWrapper::ThreadView: its own copy of the global root
//     slot, its own QuickJS context (JSON.parse, regular expressions) and its
//     own protoCore root context;
//   - has its own job queue (MicrotaskQueue::ThreadOverride), drained when a
//     task's function returns;
//   - waits for work outside protoCore's running set (UnmanagedScope), so an
//     idle pool never holds up a collection.
// Before each task it adopts the owner thread's identity objects
// (runtime/ThreadIdentity.h).
//
// A task's function, its Deferred and its result travel as root-set handles
// (docs/GC_BRIDGING.md, Mechanism B). The result is handed back with
// MicrotaskQueue::enqueueFromAnyThread, which settles the Deferred on the owner
// thread as a microtask.

#include "runtime/ThreadIdentity.h"

#include <protoCore.h>

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <vector>

namespace protojs {

class JSContextWrapper;
class MicrotaskQueue;
struct ProtoBytecodeModule;

class DeferredPool {
public:
    struct Task {
        proto::ProtoRootSet::Handle fn = proto::ProtoRootSet::kNullHandle;
        proto::ProtoRootSet::Handle deferred = proto::ProtoRootSet::kNullHandle;
        // The global (module scope) the function resolves free names
        // against; kept alive by the owner wrapper's pin on it.
        const proto::ProtoObject* global = nullptr;
        // The module the function's bytecode IDs index (resolveNestedFunction).
        const ProtoBytecodeModule* module = nullptr;
        ThreadIdentity identity;
    };

    DeferredPool(JSContextWrapper* owner, std::size_t threads);
    ~DeferredPool();
    DeferredPool(const DeferredPool&) = delete;
    DeferredPool& operator=(const DeferredPool&) = delete;

    /**
     * Owner thread: queue a task, starting the threads on first use. Returns
     * false when no thread could be started; the task is then not queued and
     * the caller still owns its handles.
     */
    bool submit(proto::ProtoContext* ctx, const Task& task);

    /** Owner thread: stop accepting work and join every thread. Tasks still
     *  queued are dropped (their handles die with the root set). */
    void shutdown();

    std::size_t threadCount() const { return started_; }

    /** Completed tasks since start (diagnostics and tests). */
    std::size_t completedTasks() const;

private:
    static const proto::ProtoObject* threadMain(proto::ProtoContext* ctx,
                                                const proto::ProtoObject* self,
                                                const proto::ParentLink* parentLink,
                                                const proto::ProtoList* args,
                                                const proto::ProtoSparseList* kwargs);
    bool start(proto::ProtoContext* ctx);
    void run(proto::ProtoContext* root);
    void runTask(proto::ProtoContext* root, const Task& task,
                 const proto::ProtoObject*& globalSlot, MicrotaskQueue& jobs);

    JSContextWrapper* owner_;
    std::size_t wanted_;
    std::size_t started_ = 0;
    bool startAttempted_ = false;

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<Task> queue_;
    bool stopping_ = false;
    std::size_t completed_ = 0;

    std::vector<const proto::ProtoThread*> threads_;
    std::vector<proto::ProtoRootSet::Handle> threadPins_;
};

}  // namespace protojs
