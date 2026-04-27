#include "ProcessModule.h"
#include "../ProtoNativeModule.h"
#include "../ArrayElementsStorage.h"
#include "../ArrayPrototype.h"
#include <cstdlib>
#include <unistd.h>
#include <sys/utsname.h>
#include <limits.h>
#include <string>

// `environ` is the POSIX env-variable table; it lives in the global
// (libc) namespace, so the extern must be declared OUTSIDE protojs's
// anonymous namespace or the linker resolves it as a private symbol.
extern char** environ;

namespace protojs {

namespace {

// Cached platform / arch strings — they don't change at runtime, and
// resolving them once at process start avoids repeating `uname()` on
// every getter call.
std::string& cachedPlatform() {
    static std::string s = []() -> std::string {
        struct utsname uts;
        if (uname(&uts) != 0) return "unknown";
        std::string sysname(uts.sysname);
        if (sysname == "Linux") return "linux";
        if (sysname == "Darwin") return "darwin";
        if (sysname.find("WIN") != std::string::npos ||
            sysname == "Windows") return "win32";
        return sysname;
    }();
    return s;
}

std::string& cachedArch() {
    static std::string s = []() -> std::string {
        struct utsname uts;
        if (uname(&uts) != 0) return "unknown";
        std::string machine(uts.machine);
        if (machine == "x86_64" || machine == "amd64") return "x64";
        if (machine == "i386" || machine == "i686")    return "ia32";
        if (machine.find("arm") != std::string::npos)   return "arm";
        return machine;
    }();
    return s;
}

const proto::ProtoObject* processCwd(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* /*args*/,
    const proto::ProtoSparseList*) {
    if (!ctx) return PROTO_NONE;
    char buf[PATH_MAX];
    if (getcwd(buf, sizeof(buf)) != nullptr) {
        return ctx->fromUTF8String(buf);
    }
    return ctx->fromUTF8String("");
}

const proto::ProtoObject* processPlatform(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* /*args*/,
    const proto::ProtoSparseList*) {
    if (!ctx) return PROTO_NONE;
    return ctx->fromUTF8String(cachedPlatform().c_str());
}

const proto::ProtoObject* processArch(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* /*args*/,
    const proto::ProtoSparseList*) {
    if (!ctx) return PROTO_NONE;
    return ctx->fromUTF8String(cachedArch().c_str());
}

const proto::ProtoObject* processExit(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    int exitCode = 0;
    if (ctx && args && args->getSize(ctx) > 0) {
        const proto::ProtoObject* a = args->getAt(ctx, 0);
        if (a && a->isInteger(ctx)) {
            exitCode = static_cast<int>(a->asLong(ctx));
        }
    }
    std::exit(exitCode);
    return PROTO_NONE;  // unreachable
}

// Build a ProtoCore-native Array object whose `__elements__` is a
// ProtoList of ProtoStrings — matches the storage convention used by
// the rest of protoJS so JS code can iterate `process.argv` with the
// usual `for / for-of / .length` idioms.
const proto::ProtoObject* buildArgvArray(proto::ProtoContext* ctx,
                                          int argc, char** argv) {
    if (!ctx) return PROTO_NONE;
    const proto::ProtoObject* arr = createNewArray(ctx, nullptr);
    if (!arr) return PROTO_NONE;
    const proto::ProtoList* elements = ctx->newList();
    for (int i = 0; i < argc && argv && argv[i]; ++i) {
        elements = elements->appendLast(ctx, ctx->fromUTF8String(argv[i]));
    }
    setArrayElements(ctx, arr, elements);
    return arr;
}

// Build the `env` object as a plain ProtoObject with each KEY=VAL pair
// from `environ` as a string attribute.  Lazy enumeration would be
// possible but the cost of eager construction is small: typical shells
// expose < 100 vars, all short.
const proto::ProtoObject* buildEnvObject(proto::ProtoContext* ctx) {
    if (!ctx) return PROTO_NONE;
    const proto::ProtoObject* env = ctx->newObject(/*mutable=*/true);
    if (!env) return PROTO_NONE;
    for (char** e = environ; e && *e; ++e) {
        std::string entry(*e);
        size_t eq = entry.find('=');
        if (eq == std::string::npos) continue;
        std::string key = entry.substr(0, eq);
        std::string val = entry.substr(eq + 1);
        const proto::ProtoString* k =
            ctx->fromUTF8String(key.c_str())->asString(ctx);
        if (k) env->setAttribute(ctx, k, ctx->fromUTF8String(val.c_str()));
    }
    return env;
}

}  // namespace

const proto::ProtoObject* ProcessModule::init(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* globalObj,
    int argc, char** argv) {
    if (!ctx || !globalObj) return globalObj;

    // Methods first.
    static const NativeEntry entries[] = {
        {"cwd",      processCwd},
        {"platform", processPlatform},
        {"arch",     processArch},
        {"exit",     processExit},
        NATIVE_MODULE_END
    };
    const proto::ProtoObject* processObj =
        ProtoNativeModule::buildModule(ctx, entries, 4);
    if (!processObj) return globalObj;

    // Data attributes.
    const proto::ProtoString* argvKey =
        ctx->fromUTF8String("argv")->asString(ctx);
    if (argvKey) {
        processObj = processObj->setAttribute(
            ctx, argvKey, buildArgvArray(ctx, argc, argv));
    }
    const proto::ProtoString* envKey =
        ctx->fromUTF8String("env")->asString(ctx);
    if (envKey) {
        processObj = processObj->setAttribute(
            ctx, envKey, buildEnvObject(ctx));
    }

    return ProtoNativeModule::registerOnGlobal(
        ctx, globalObj, "process", processObj);
}

} // namespace protojs
