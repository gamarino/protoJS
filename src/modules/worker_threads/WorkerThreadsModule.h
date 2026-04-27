#ifndef PROTOJS_WORKERTHREADSMODULE_H
#define PROTOJS_WORKERTHREADSMODULE_H

#include "headers/protoCore.h"
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
};

} // namespace protojs

#endif // PROTOJS_WORKERTHREADSMODULE_H
