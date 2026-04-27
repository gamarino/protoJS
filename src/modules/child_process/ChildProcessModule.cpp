#include "ChildProcessModule.h"
#include "../../ProtoNativeModule.h"
#include "../../ArrayElementsStorage.h"
#include "../../ArrayPrototype.h"
#include "../../FunctionPrototype.h"
#include "../../JSSymbols.h"
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>
#include <string>
#include <vector>
#include <cstring>

namespace protojs {

namespace {

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
    static thread_local const proto::ProtoString* k = nullptr;
    if (!k) k = proto::ProtoString::createSymbol(ctx, "__pid__");
    return k;
}

int getIntAttr(proto::ProtoContext* ctx, const proto::ProtoObject* self,
                const proto::ProtoString* k) {
    if (!self || !k) return -1;
    const proto::ProtoObject* v = self->getAttribute(ctx, k, false);
    if (!v || !v->isInteger(ctx)) return -1;
    return static_cast<int>(v->asLong(ctx));
}

const proto::ProtoObject* childKill(
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

const proto::ProtoObject* childSend(
    proto::ProtoContext* /*ctx*/,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* /*args*/,
    const proto::ProtoSparseList*) {
    // Stub matches the original QuickJS-side behaviour.
    return PROTO_FALSE;
}

const proto::ProtoObject* getChildProto(proto::ProtoContext* ctx) {
    static const proto::ProtoObject* proto = nullptr;
    if (proto) return proto;
    static const NativeEntry entries[] = {
        {"kill", childKill},
        {"send", childSend},
        NATIVE_MODULE_END
    };
    proto = ProtoNativeModule::buildModule(ctx, entries, 2);
    return proto;
}

const proto::ProtoObject* makeChildInstance(proto::ProtoContext* ctx,
                                              pid_t pid) {
    const proto::ProtoObject* proto = getChildProto(ctx);
    const proto::ProtoObject* inst = proto
        ? proto->newChild(ctx, /*mutable=*/true)
        : ctx->newObject(/*mutable=*/true);
    inst->setAttribute(ctx, pidKey(ctx),
        ctx->fromInteger(static_cast<long long>(pid)));
    inst->setAttribute(ctx,
        ctx->fromUTF8String("pid")->asString(ctx),
        ctx->fromInteger(static_cast<long long>(pid)));
    return inst;
}

const proto::ProtoObject* spawnImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    std::string command;
    if (!argString(ctx, args, 0, command)) return PROTO_NONE;
    std::vector<std::string> argv;
    argv.push_back(command);
    if (args && args->getSize(ctx) >= 2) {
        const proto::ProtoObject* arr = args->getAt(ctx, 1);
        const proto::ProtoList* els = arr ? getArrayElements(ctx, arr) : nullptr;
        if (els) {
            long long n = static_cast<long long>(els->getSize(ctx));
            for (long long i = 0; i < n; ++i) {
                const proto::ProtoObject* a = els->getAt(ctx, static_cast<int>(i));
                if (a && a->isString(ctx)) {
                    std::string s;
                    a->asString(ctx)->toUTF8String(ctx, s);
                    argv.push_back(s);
                }
            }
        }
    }
    pid_t pid = ::fork();
    if (pid < 0) return PROTO_NONE;
    if (pid == 0) {
        std::vector<char*> cargv;
        for (auto& s : argv) cargv.push_back(const_cast<char*>(s.c_str()));
        cargv.push_back(nullptr);
        execvp(command.c_str(), cargv.data());
        _exit(127);
    }
    return makeChildInstance(ctx, pid);
}

const proto::ProtoObject* execImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    std::string line;
    if (!argString(ctx, args, 0, line)) return PROTO_NONE;
    pid_t pid = ::fork();
    if (pid < 0) return PROTO_NONE;
    if (pid == 0) {
        execl("/bin/sh", "/bin/sh", "-c", line.c_str(), nullptr);
        _exit(127);
    }
    return makeChildInstance(ctx, pid);
}

const proto::ProtoObject* execFileImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* pl,
    const proto::ProtoList* args,
    const proto::ProtoSparseList* kw) {
    return spawnImpl(ctx, self, pl, args, kw);
}

const proto::ProtoObject* forkImpl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* pl,
    const proto::ProtoList* args,
    const proto::ProtoSparseList* kw) {
    return spawnImpl(ctx, self, pl, args, kw);
}

}  // namespace

const proto::ProtoObject* ChildProcessModule::init(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* globalObj) {
    if (!ctx || !globalObj) return globalObj;
    static const NativeEntry entries[] = {
        {"spawn",    spawnImpl},
        {"exec",     execImpl},
        {"execFile", execFileImpl},
        {"fork",     forkImpl},
        NATIVE_MODULE_END
    };
    const proto::ProtoObject* mod =
        ProtoNativeModule::buildModule(ctx, entries, 4);
    if (!mod) return globalObj;
    return ProtoNativeModule::registerOnGlobal(ctx, globalObj, "child_process", mod);
}

} // namespace protojs
