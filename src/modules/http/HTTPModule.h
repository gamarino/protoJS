#ifndef PROTOJS_HTTPMODULE_H
#define PROTOJS_HTTPMODULE_H

#include "headers/protoCore.h"

namespace protojs {

/**
 * @brief Node-style `http` module — protoCore-native.
 *
 * Replaces the original QuickJS-side implementation: every per-instance
 * piece of state (socket fds, port, listening flag, parsed request
 * fields, response status / body / headers, etc.) lives as ProtoObject
 * attributes on the instance itself.  No JS_NewClassID / JS_SetOpaque
 * is used.  The accept loop runs on a std::thread whose handle is
 * carried by an ExternalPointer attached to the server instance, so the
 * thread is joined automatically when the server is GC'd.
 *
 * Callbacks (the server's request listener) are pinned in the
 * wrapper's ProtoRootSet across the IO-thread / event-loop hop, then
 * dispatched back into the interpreter via callJSFunctionFromAsync.
 */
class HTTPModule {
public:
    static const proto::ProtoObject* init(
        proto::ProtoContext* ctx,
        const proto::ProtoObject* globalObj);

    // Number of HTTP servers currently bound and listening.  The main
    // event-loop drainer in main.cpp uses this to keep the process
    // alive while at least one server is active.
    static int getActiveServerCount();
};

} // namespace protojs

#endif // PROTOJS_HTTPMODULE_H
