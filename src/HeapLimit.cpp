#include "HeapLimit.h"

#include <climits>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <sys/types.h>
#include <sys/sysctl.h>
#else
#include <unistd.h>
#endif

namespace protojs {
namespace heaplimit {

unsigned long long physicalMemoryBytes() {
#if defined(_WIN32)
    MEMORYSTATUSEX status;
    status.dwLength = sizeof(status);
    if (::GlobalMemoryStatusEx(&status)) return status.ullTotalPhys;
    return 0;
#elif defined(__APPLE__)
    unsigned long long bytes = 0;
    size_t len = sizeof(bytes);
    if (::sysctlbyname("hw.memsize", &bytes, &len, nullptr, 0) == 0) return bytes;
    return 0;
#else
    const long pages = ::sysconf(_SC_PHYS_PAGES);
    const long pageSize = ::sysconf(_SC_PAGESIZE);
    if (pages > 0 && pageSize > 0)
        return static_cast<unsigned long long>(pages) * static_cast<unsigned long long>(pageSize);
    return 0;
#endif
}

#if defined(__linux__)
namespace {

// A limit file's value; 0 for "max", an absent file or an unreadable one.
unsigned long long readLimit(const std::string& path) {
    std::ifstream in(path);
    std::string text;
    if (!in || !(in >> text) || text == "max") return 0;
    char* end = nullptr;
    const unsigned long long v = std::strtoull(text.c_str(), &end, 10);
    if (end == text.c_str()) return 0;
    // cgroup v1 reports "no limit" as a huge page-rounded value.
    if (v >= (1ULL << 60)) return 0;
    return v;
}

void tighten(unsigned long long& best, unsigned long long v) {
    if (v > 0 && (best == 0 || v < best)) best = v;
}

}  // namespace
#endif

unsigned long long cgroupMemoryLimitBytes() {
#if defined(__linux__)
    unsigned long long best = 0;
    std::ifstream cg("/proc/self/cgroup");
    std::string line;
    while (std::getline(cg, line)) {
        // "hierarchy-id:controllers:path"; v2 is "0::path".
        const size_t a = line.find(':');
        const size_t b = a == std::string::npos ? a : line.find(':', a + 1);
        if (b == std::string::npos) continue;
        const std::string controllers = line.substr(a + 1, b - a - 1);
        std::string path = line.substr(b + 1);
        std::string root;
        std::string file;
        if (controllers.empty()) {
            root = "/sys/fs/cgroup";
            file = "memory.max";
        } else if (controllers.find("memory") != std::string::npos) {
            root = "/sys/fs/cgroup/memory";
            file = "memory.limit_in_bytes";
        } else {
            continue;
        }
        // A limit may be set on any ancestor: take the tightest on the path.
        while (true) {
            tighten(best, readLimit(root + path + (path == "/" ? "" : "/") + file));
            if (path.empty() || path == "/") break;
            const size_t slash = path.find_last_of('/');
            path = slash == 0 || slash == std::string::npos ? "/" : path.substr(0, slash);
        }
    }
    return best;
#else
    return 0;
#endif
}

long long defaultHeapLimitCells(unsigned long long physicalBytes,
                                unsigned long long cgroupLimitBytes) {
    unsigned long long bytes = physicalBytes;
    if (cgroupLimitBytes > 0 && (bytes == 0 || cgroupLimitBytes < bytes))
        bytes = cgroupLimitBytes;
    if (bytes == 0) return kFallbackCells;
    const unsigned long long cells = bytes / 4 * 3 / static_cast<unsigned long long>(kCellBytes);
    if (cells > static_cast<unsigned long long>(INT_MAX)) return INT_MAX;
    return cells > 0 ? static_cast<long long>(cells) : 1;
}

long long processDefaultHeapLimitCells() {
    return defaultHeapLimitCells(physicalMemoryBytes(), cgroupMemoryLimitBytes());
}

}  // namespace heaplimit
}  // namespace protojs
