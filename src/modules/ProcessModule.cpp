#include "ProcessModule.h"
#include "../ProtoNativeModule.h"
#include "../ArrayElementsStorage.h"
#include "../ArrayPrototype.h"
#include "../platform/ProcessExit.h"
#include <cstdlib>
#include "protoCore.h"
#if defined(_WIN32)
#include "../platform/Posix.h"
#include <windows.h>
#include <psapi.h>
#else
#include <unistd.h>
#include <sys/utsname.h>
#include <sys/resource.h>
#include <cstdio>
#endif
#if defined(__APPLE__)
#include <mach/mach.h>
#endif
#include <limits.h>
#include <string>
#include <vector>
#include <initializer_list>
#include <utility>
#include <cwchar>

#if !defined(_WIN32)
// `environ` is the POSIX env-variable table; it lives in the global
// (libc) namespace, so the extern must be declared OUTSIDE protojs's
// anonymous namespace or the linker resolves it as a private symbol.
extern char** environ;
#endif

namespace protojs {

namespace {

// Cached platform / arch strings — they don't change at runtime, and
// resolving them once at process start avoids repeating `uname()` on
// every getter call.
std::string& cachedPlatform() {
    static std::string s = []() -> std::string {
#if defined(_WIN32)
        return "win32";
#else
        struct utsname uts;
        if (uname(&uts) != 0) return "unknown";
        std::string sysname(uts.sysname);
        if (sysname == "Linux") return "linux";
        if (sysname == "Darwin") return "darwin";
        if (sysname.find("WIN") != std::string::npos ||
            sysname == "Windows") return "win32";
        return sysname;
#endif
    }();
    return s;
}

std::string& cachedArch() {
    static std::string s = []() -> std::string {
#if defined(_WIN32)
        SYSTEM_INFO si;
        ::GetNativeSystemInfo(&si);
        switch (si.wProcessorArchitecture) {
            case PROCESSOR_ARCHITECTURE_AMD64: return "x64";
            case PROCESSOR_ARCHITECTURE_ARM64: return "arm64";
            case PROCESSOR_ARCHITECTURE_INTEL: return "ia32";
            case PROCESSOR_ARCHITECTURE_ARM:   return "arm";
            default:                           return "unknown";
        }
#else
        struct utsname uts;
        if (uname(&uts) != 0) return "unknown";
        std::string machine(uts.machine);
        if (machine == "x86_64" || machine == "amd64") return "x64";
        if (machine == "i386" || machine == "i686")    return "ia32";
        if (machine.find("arm") != std::string::npos)   return "arm";
        return machine;
#endif
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
#if defined(_WIN32)
    if (wchar_t* wd = ::_wgetcwd(nullptr, 0)) {
        const std::string cwd = platform::narrow(wd);
        std::free(wd);
        return ctx->fromUTF8String(cwd.c_str());
    }
#else
    char buf[PATH_MAX];
    if (getcwd(buf, sizeof(buf)) != nullptr) {
        return ctx->fromUTF8String(buf);
    }
#endif
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
    // Not std::exit: this runs inside the interpreter, with the thread pools
    // and the collector running, and exit()'s static destructors crashed
    // against them. See platform/ProcessExit.h.
    platform::exitNow(exitCode);
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
#if defined(_WIN32)
    // The environment block in UTF-16, converted to protoJS's UTF-8. Entries
    // whose name starts with '=' are the per-drive current directories.
    std::vector<std::string> entries;
    if (wchar_t* block = ::GetEnvironmentStringsW()) {
        for (const wchar_t* p = block; *p; p += std::wcslen(p) + 1)
            entries.push_back(platform::narrow(p));
        ::FreeEnvironmentStringsW(block);
    }
    for (const std::string& entry : entries) {
        size_t eq = entry.find('=');
        if (eq == std::string::npos || eq == 0) continue;
#else
    for (char** e = environ; e && *e; ++e) {
        std::string entry(*e);
        size_t eq = entry.find('=');
        if (eq == std::string::npos) continue;
#endif
        std::string key = entry.substr(0, eq);
        std::string val = entry.substr(eq + 1);
        const proto::ProtoString* k =
            ctx->fromUTF8String(key.c_str())->asString(ctx);
        if (k) env->setAttribute(ctx, k, ctx->fromUTF8String(val.c_str()));
    }
    return env;
}

// ---- Memory ------------------------------------------------------------

// Resident set size of the process now, in bytes (0 when unknown).
long long currentRssBytes() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc;
    if (::GetProcessMemoryInfo(::GetCurrentProcess(), &pmc, sizeof(pmc)))
        return static_cast<long long>(pmc.WorkingSetSize);
    return 0;
#elif defined(__APPLE__)
    mach_task_basic_info info;
    mach_msg_type_number_t count = MACH_TASK_BASIC_INFO_COUNT;
    if (task_info(mach_task_self(), MACH_TASK_BASIC_INFO,
                  reinterpret_cast<task_info_t>(&info), &count) == KERN_SUCCESS)
        return static_cast<long long>(info.resident_size);
    return 0;
#else
    long long pages = 0, resident = 0;
    if (FILE* f = std::fopen("/proc/self/statm", "r")) {
        if (std::fscanf(f, "%lld %lld", &pages, &resident) != 2) resident = 0;
        std::fclose(f);
    }
    return resident * static_cast<long long>(::sysconf(_SC_PAGESIZE));
#endif
}

// Peak resident set size of the process, in bytes (0 when unknown).
long long peakRssBytes() {
#if defined(_WIN32)
    PROCESS_MEMORY_COUNTERS pmc;
    if (::GetProcessMemoryInfo(::GetCurrentProcess(), &pmc, sizeof(pmc)))
        return static_cast<long long>(pmc.PeakWorkingSetSize);
    return 0;
#else
    struct rusage usage;
    if (::getrusage(RUSAGE_SELF, &usage) != 0) return 0;
#if defined(__APPLE__)
    return static_cast<long long>(usage.ru_maxrss);          // bytes on macOS
#else
    return static_cast<long long>(usage.ru_maxrss) * 1024;   // KiB on Linux
#endif
#endif
}

const proto::ProtoObject* newDataObject(
        proto::ProtoContext* ctx,
        std::initializer_list<std::pair<const char*, long long>> fields) {
    const proto::ProtoObject* obj = ctx->newObject(/*mutable=*/true);
    for (const auto& f : fields) {
        const proto::ProtoString* k = proto::ProtoString::createSymbol(ctx, f.first);
        if (k) obj = obj->setAttribute(ctx, k, ctx->fromInteger(f.second));
    }
    return obj;
}

// process.memoryUsage(): Node's field names.  heapTotal is the protoCore heap
// (cells obtained from the OS, 64 bytes each); heapUsed excludes the cells on
// the space's free list.  external and arrayBuffers are not tracked (0).
const proto::ProtoObject* processMemoryUsage(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* /*args*/,
    const proto::ProtoSparseList*) {
    if (!ctx) return PROTO_NONE;
    long long heapCells = 0, freeCells = 0;
    if (ctx->space) {
        heapCells = ctx->space->heapSize;
        freeCells = ctx->space->freeCellsCount;
    }
    const long long used = heapCells > freeCells ? heapCells - freeCells : 0;
    return newDataObject(ctx, {
        {"rss", currentRssBytes()},
        {"heapTotal", heapCells * 64},
        {"heapUsed", used * 64},
        {"external", 0},
        {"arrayBuffers", 0},
    });
}

// process.resourceUsage(): only maxRSS (KiB, as in Node.js) is reported.
const proto::ProtoObject* processResourceUsage(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* /*args*/,
    const proto::ProtoSparseList*) {
    if (!ctx) return PROTO_NONE;
    return newDataObject(ctx, {{"maxRSS", peakRssBytes() / 1024}});
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
        {"memoryUsage",   processMemoryUsage},
        {"resourceUsage", processResourceUsage},
        NATIVE_MODULE_END
    };
    const proto::ProtoObject* processObj =
        ProtoNativeModule::buildModule(ctx, entries, 6);
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
