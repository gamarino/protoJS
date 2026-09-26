#include "ThreadProtoContext.h"

namespace protojs {

namespace {
// One slot per OS thread.  Never dereferenced here; only handed back to callers
// that build an UnmanagedScope from it.
thread_local proto::ProtoContext* g_threadProtoContext = nullptr;
}  // namespace

void setThreadProtoContext(proto::ProtoContext* ctx) noexcept {
    // First registration wins: that context's space is the one that adopted this
    // thread.  A second ProtoSpace constructed on the same thread does not adopt
    // it again, so overwriting would point the accounting at the wrong space.
    if (ctx && !g_threadProtoContext) g_threadProtoContext = ctx;
}

void clearThreadProtoContext(proto::ProtoContext* ctx) noexcept {
    if (ctx && g_threadProtoContext == ctx) g_threadProtoContext = nullptr;
}

proto::ProtoContext* threadProtoContext() noexcept {
    return g_threadProtoContext;
}

}  // namespace protojs
