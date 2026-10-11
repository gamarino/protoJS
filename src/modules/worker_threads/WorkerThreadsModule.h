#ifndef PROTOJS_WORKERTHREADSMODULE_H
#define PROTOJS_WORKERTHREADSMODULE_H

#include "protoCore.h"
#include <string>

namespace protojs {

class JSContextWrapper;

/**
 * @brief Node-style `worker_threads` module — protoCore-native.
 *
 * Replaces the original QuickJS-side implementation: the Worker class
 * is built from a prototype + wrapNativeFunction constructor, and per-
 * instance state (the std::thread, the worker's owned JSContextWrapper,
 * the running/terminated flags) lives behind a ProtoExternalPointer
 * attribute on the Worker instance.  No JS_NewClassID / JS_SetOpaque is
 * used.
 *
 * Cross-context message passing keeps the original "JSON serialize +
 * EventLoop hop" contract: ProtoObjects from the worker space cannot
 * reach the main space directly, so payloads are JSON-encoded by a
 * minimal C++ serializer (`serializeJSON`) and decoded on the other
 * side by `parseJSON`.
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
     * stop it if it still runs, join its thread, drop its pin and destroy
     * its JSContextWrapper (and with it the worker's ProtoSpace and that
     * space's collector thread).  Called by ~JSContextWrapper before the
     * owner's space goes away.  A Worker pins itself, so its finalizer runs
     * only at the owner space's teardown, where it can merely post an orphan
     * that no event loop drains any more: without this call a worker left
     * to finish on its own kept its space -- and a running collector thread
     * -- into the process's static destructors, which then crashed
     * (cli/blocking-joins-and-finalizers, worker-to-completion).
     */
    static void releaseWorkersOf(JSContextWrapper* owner);
};

} // namespace protojs

#endif // PROTOJS_WORKERTHREADSMODULE_H
