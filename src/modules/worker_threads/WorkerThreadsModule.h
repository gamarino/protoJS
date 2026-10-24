#ifndef PROTOJS_WORKERTHREADSMODULE_H
#define PROTOJS_WORKERTHREADSMODULE_H

#include "protoCore.h"
#include <string>

namespace protojs {

class JSContextWrapper;

/**
 * @brief Node-style `worker_threads` module — protoCore-native.
 *
 * A Worker runs on a protoCore thread of the main thread's ProtoSpace (one
 * space per process), with a JSContextWrapper that borrows that space: its
 * own global object (a child of the main global object), module cache, job
 * queue and root set, and the realm's intrinsics shared.  Per-instance state
 * (the thread, the worker's wrapper, the running/terminated flags) lives
 * behind a ProtoExternalPointer attribute on the Worker instance.
 *
 * A message is the structured clone of the posted value, made by the sender
 * at the call inside the one space: immutable values are shared, objects and
 * arrays copied.  No serialization (docs/API_REFERENCE.md, "worker_threads").
 */
class WorkerThreadsModule {
public:
    static const proto::ProtoObject* init(
        proto::ProtoContext* ctx,
        const proto::ProtoObject* globalObj);

    /** Number of worker threads currently running.  main.cpp's drain
     *  loop checks this so the process keeps the event loop alive
     *  while at least one worker is in flight. */
    static int getActiveWorkerCount();

    /**
     * Release every Worker that `owner` created and that is still held:
     * stop it if it still runs, join its thread, drop its pins and destroy
     * its JSContextWrapper.  Called by ~JSContextWrapper before the
     * owner's space goes away.  A Worker pins itself, so its finalizer runs
     * only at the owner space's teardown, where it can merely post an orphan
     * that no event loop drains any more: without this call a worker left
     * to finish on its own kept its thread into the process's static
     * destructors (cli/blocking-joins-and-finalizers, worker-to-completion).
     */
    static void releaseWorkersOf(JSContextWrapper* owner);
};

} // namespace protojs

#endif // PROTOJS_WORKERTHREADSMODULE_H
