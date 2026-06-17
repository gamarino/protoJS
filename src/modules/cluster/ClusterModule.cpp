#include "ClusterModule.h"
#include "../../ProtoNativeModule.h"
#include "../../FunctionPrototype.h"
#include "../../JSSymbols.h"
#include <unistd.h>
#include <signal.h>
#include <sys/wait.h>
#include <sys/socket.h>
#include <atomic>

namespace protojs {

namespace {

// Per-process state.  `is_master_process` flips to false in the
// child after a successful fork().  The worker_id_counter gives each
// worker a unique numeric id for tracking.
std::atomic<bool> is_master_process{true};
std::atomic<int> worker_id_counter{0};

// ---- Worker class methods ----------------------------------------------

bool argString(proto::ProtoContext* ctx, const proto::ProtoList* args,
                int idx, std::string& out) {
    if (!ctx || !args) return false;
    if (idx >= static_cast<int>(args->getSize(ctx))) return false;
    const proto::ProtoObject* a = args->getAt(ctx, idx);
    if (!a || !a->isString(ctx)) return false;
    a->asString(ctx)->toUTF8String(ctx, out);
    return true;
}

const proto::ProtoString* pidKey(proto::ProtoContext* ctx) {
    return proto::ProtoString::createSymbol(ctx, "__pid__");
}
const proto::ProtoString* ipcReadKey(proto::ProtoContext* ctx) {
    return proto::ProtoString::createSymbol(ctx, "__ipc_read__");
}
const proto::ProtoString* ipcWriteKey(proto::ProtoContext* ctx) {
    return proto::ProtoString::createSymbol(ctx, "__ipc_write__");
}

int getIntAttr(proto::ProtoContext* ctx, const proto::ProtoObject* self,
                const proto::ProtoString* k) {
    if (!self || !k) return -1;
    const proto::ProtoObject* v = self->getAttribute(ctx, k, false);
    if (!v || !v->isInteger(ctx)) return -1;
    return static_cast<int>(v->asLong(ctx));
}

const proto::ProtoObject* workerSend(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    std::string msg;
    if (!argString(ctx, args, 0, msg)) return PROTO_FALSE;
    int fd = getIntAttr(ctx, self, ipcWriteKey(ctx));
    if (fd < 0) return PROTO_FALSE;
    msg.push_back('\n');
    ssize_t w;
    {
        proto::ProtoContext::UnmanagedScope u(ctx);
        w = ::write(fd, msg.data(), msg.size());
    }
    return (w == static_cast<ssize_t>(msg.size())) ? PROTO_TRUE : PROTO_FALSE;
}

const proto::ProtoObject* workerDisconnect(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* /*args*/,
    const proto::ProtoSparseList*) {
    int rfd = getIntAttr(ctx, self, ipcReadKey(ctx));
    int wfd = getIntAttr(ctx, self, ipcWriteKey(ctx));
    if (rfd >= 0) ::close(rfd);
    if (wfd >= 0) ::close(wfd);
    if (self) {
        self->setAttribute(ctx, ipcReadKey(ctx),  ctx->fromInteger(-1));
        self->setAttribute(ctx, ipcWriteKey(ctx), ctx->fromInteger(-1));
    }
    return PROTO_NONE;
}

const proto::ProtoObject* workerKill(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    int sig = SIGTERM;
    if (ctx && args && args->getSize(ctx) > 0) {
        const proto::ProtoObject* a = args->getAt(ctx, 0);
        if (a && a->isInteger(ctx)) sig = static_cast<int>(a->asLong(ctx));
    }
    int pid = getIntAttr(ctx, self, pidKey(ctx));
    if (pid > 0) ::kill(pid, sig);
    return PROTO_NONE;
}

// ---- Cluster module methods --------------------------------------------

const proto::ProtoObject* setupMasterImpl(
    proto::ProtoContext* /*ctx*/,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* /*args*/,
    const proto::ProtoSparseList*) {
    return PROTO_NONE;
}

const proto::ProtoObject* clusterFork(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* /*args*/,
    const proto::ProtoSparseList*);

const proto::ProtoObject* isMasterImpl(
    proto::ProtoContext* /*ctx*/,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* /*args*/,
    const proto::ProtoSparseList*) {
    return is_master_process.load() ? PROTO_TRUE : PROTO_FALSE;
}

const proto::ProtoObject* isWorkerImpl(
    proto::ProtoContext* /*ctx*/,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* /*args*/,
    const proto::ProtoSparseList*) {
    return is_master_process.load() ? PROTO_FALSE : PROTO_TRUE;
}

// Build the Worker prototype lazily so each fork doesn't rebuild it.
const proto::ProtoObject* getWorkerProto(proto::ProtoContext* ctx) {
    static const proto::ProtoObject* proto = nullptr;
    if (proto) return proto;
    static const NativeEntry entries[] = {
        {"send",       workerSend},
        {"disconnect", workerDisconnect},
        {"kill",       workerKill},
        NATIVE_MODULE_END
    };
    proto = ProtoNativeModule::buildModule(ctx, entries, 3);
    return proto;
}

const proto::ProtoObject* clusterFork(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* /*args*/,
    const proto::ProtoSparseList*) {
    if (!ctx || !is_master_process.load()) return PROTO_NONE;
    int sv[2];
    if (socketpair(AF_UNIX, SOCK_STREAM, 0, sv) < 0) return PROTO_NONE;

    int workerId = ++worker_id_counter;
    const proto::ProtoObject* proto = getWorkerProto(ctx);
    const proto::ProtoObject* worker = proto
        ? proto->newChild(ctx, /*mutable=*/true)
        : ctx->newObject(/*mutable=*/true);
    worker->setAttribute(ctx,
        ctx->fromUTF8String("id")->asString(ctx),
        ctx->fromInteger(workerId));

    pid_t pid = ::fork();
    if (pid < 0) {
        ::close(sv[0]);
        ::close(sv[1]);
        return PROTO_NONE;
    }
    if (pid == 0) {
        // Child: minimal lifecycle — flag and exit.  A full
        // implementation would re-exec with the worker entry point.
        is_master_process.store(false);
        ::close(sv[1]);
        _exit(0);
    }
    ::close(sv[0]);  // master keeps the write end on sv[1]
    worker->setAttribute(ctx, pidKey(ctx),
        ctx->fromInteger(static_cast<long long>(pid)));
    worker->setAttribute(ctx, ipcReadKey(ctx),  ctx->fromInteger(-1));
    worker->setAttribute(ctx, ipcWriteKey(ctx),
        ctx->fromInteger(sv[1]));
    return worker;
}

}  // namespace

const proto::ProtoObject* ClusterModule::init(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* globalObj) {
    if (!ctx || !globalObj) return globalObj;
    static const NativeEntry entries[] = {
        {"setupMaster", setupMasterImpl},
        {"fork",        clusterFork},
        {"isMaster",    isMasterImpl},
        {"isWorker",    isWorkerImpl},
        NATIVE_MODULE_END
    };
    const proto::ProtoObject* mod =
        ProtoNativeModule::buildModule(ctx, entries, 4);
    if (!mod) return globalObj;
    return ProtoNativeModule::registerOnGlobal(ctx, globalObj, "cluster", mod);
}

} // namespace protojs
