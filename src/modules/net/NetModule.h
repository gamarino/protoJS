#ifndef PROTOJS_NETMODULE_H
#define PROTOJS_NETMODULE_H

#include "headers/protoCore.h"

namespace protojs {

/**
 * @brief Node-style `net` module — protoCore-native.
 *
 * Replaces the original QuickJS-side implementation: Server / Socket
 * are protoCore prototypes whose per-instance state lives behind
 * ProtoExternalPointer attributes (std::thread, atomic flags, fds).
 * Cross-thread emit goes through EventLoop::enqueueCallback +
 * callJSFunctionFromAsync against the wrapper that owns the instance.
 */
class NetModule {
public:
    static const proto::ProtoObject* init(
        proto::ProtoContext* ctx,
        const proto::ProtoObject* globalObj);

    /** Number of net.Server / net.Socket objects with active background
     *  threads (accept loops, read loops).  main.cpp's drain loop uses
     *  this to keep the process alive while at least one is running. */
    static int getActiveCount();
};

} // namespace protojs

#endif // PROTOJS_NETMODULE_H
