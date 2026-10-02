/*
 * Posix — the handful of POSIX calls protoJS uses, on Windows.
 *
 * On Linux and macOS the sources call the system's own functions, exactly as
 * before. On Windows this header supplies the same names with the same
 * contracts on top of the C runtime and the Windows API, so the callers stay as
 * they are. A few helpers (protojs::platform::statPath & co.) exist on every
 * platform: they are the plain POSIX call outside Windows, and take UTF-8 paths
 * to the UTF-16 API there.
 */
#ifndef PROTOJS_PLATFORM_POSIX_H
#define PROTOJS_PLATFORM_POSIX_H

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

#include <direct.h>
#include <io.h>
#include <process.h>
#include <sys/stat.h>
#include <sys/types.h>

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

// MSVC's C runtime spells these with a leading underscore.
#ifndef popen
#define popen _popen
#endif
#ifndef pclose
#define pclose _pclose
#endif

using ssize_t = long long;
using pid_t = int;

namespace protojs::platform {

// UTF-8 (protoJS's strings) <-> UTF-16 (the Windows API).
inline std::wstring widen(const std::string& s) {
    if (s.empty()) return std::wstring();
    const int n = ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n > 0 ? n : 0), L'\0');
    if (n > 0) ::MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

inline std::string narrow(const std::wstring& w) {
    if (w.empty()) return std::string();
    const int n = ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), nullptr, 0, nullptr, nullptr);
    std::string s(static_cast<std::size_t>(n > 0 ? n : 0), '\0');
    if (n > 0) ::WideCharToMultiByte(CP_UTF8, 0, w.data(), static_cast<int>(w.size()), s.data(), n, nullptr, nullptr);
    return s;
}

} // namespace protojs::platform

// <time.h>: the reentrant conversions and the inverse of gmtime.
//
// The C runtime's gmtime_s, localtime_s and _mkgmtime only cover 1970..3000,
// while glibc's cover any 64-bit time; JavaScript dates span +-275760 years.
// UTC conversions are therefore exact arithmetic on the proleptic Gregorian
// calendar (H. Hinnant's days_from_civil / civil_from_days), and local time
// outside the C runtime's range is computed in a year with the same calendar,
// whole 400-year cycles away, then moved back.
namespace protojs::platform::detail {

inline long long floorDiv(long long a, long long b) {
    long long q = a / b;
    if ((a % b != 0) && ((a < 0) != (b < 0))) --q;
    return q;
}

// Days from 1970-01-01 to y-m-d (m in 1..12).
inline long long daysFromCivil(long long y, unsigned m, unsigned d) {
    y -= m <= 2;
    const long long era = floorDiv(y, 400);
    const unsigned yoe = static_cast<unsigned>(y - era * 400);
    const unsigned doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
    const unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    return era * 146097 + static_cast<long long>(doe) - 719468;
}

inline void civilFromDays(long long z, long long* y, unsigned* m, unsigned* d) {
    z += 719468;
    const long long era = floorDiv(z, 146097);
    const unsigned doe = static_cast<unsigned>(z - era * 146097);
    const unsigned yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
    const unsigned doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
    const unsigned mp = (5 * doy + 2) / 153;
    *d = doy - (153 * mp + 2) / 5 + 1;
    *m = mp < 10 ? mp + 3 : mp - 9;
    *y = static_cast<long long>(yoe) + era * 400 + (*m <= 2);
}

constexpr long long kSecondsPer400Years = 146097LL * 86400LL;
constexpr long long kCrtMinTime = 0;                 // 1970-01-01T00:00:00Z
constexpr long long kCrtMaxTime = 32535215999LL;     // 3000-12-31T23:59:59Z

} // namespace protojs::platform::detail

inline std::tm* gmtime_r(const std::time_t* t, std::tm* out) {
    using namespace protojs::platform::detail;
    const long long secs = static_cast<long long>(*t);
    const long long days = floorDiv(secs, 86400);
    const long long rem = secs - days * 86400;
    long long y;
    unsigned m, d;
    civilFromDays(days, &y, &m, &d);
    if (y - 1900 < INT_MIN || y - 1900 > INT_MAX) return nullptr;
    out->tm_year = static_cast<int>(y - 1900);
    out->tm_mon = static_cast<int>(m) - 1;
    out->tm_mday = static_cast<int>(d);
    out->tm_hour = static_cast<int>(rem / 3600);
    out->tm_min = static_cast<int>((rem % 3600) / 60);
    out->tm_sec = static_cast<int>(rem % 60);
    out->tm_wday = static_cast<int>((days + 4) - 7 * floorDiv(days + 4, 7)); // 1970-01-01: Thursday
    out->tm_yday = static_cast<int>(days - daysFromCivil(y, 1, 1));
    out->tm_isdst = 0;
    return out;
}

inline std::time_t timegm(std::tm* tm) {
    using namespace protojs::platform::detail;
    // Normalise the month into the year, then let the day count absorb the
    // rest, as timegm does for out-of-range fields.
    long long y = static_cast<long long>(tm->tm_year) + 1900 + floorDiv(tm->tm_mon, 12);
    const long long mon = tm->tm_mon - floorDiv(tm->tm_mon, 12) * 12;
    const long long days = daysFromCivil(y, static_cast<unsigned>(mon + 1), 1) + (tm->tm_mday - 1);
    const long long secs = days * 86400 + static_cast<long long>(tm->tm_hour) * 3600 +
                           static_cast<long long>(tm->tm_min) * 60 + tm->tm_sec;
    std::time_t result = static_cast<std::time_t>(secs);
    gmtime_r(&result, tm);
    return result;
}

inline std::tm* localtime_r(const std::time_t* t, std::tm* out) {
    using namespace protojs::platform::detail;
    long long secs = static_cast<long long>(*t);
    long long cycles = 0;
    if (secs < kCrtMinTime) cycles = floorDiv(secs - kCrtMinTime, kSecondsPer400Years);
    else if (secs > kCrtMaxTime) cycles = floorDiv(secs - kCrtMinTime, kSecondsPer400Years);
    secs -= cycles * kSecondsPer400Years;
    const std::time_t shifted = static_cast<std::time_t>(secs);
    if (::localtime_s(out, &shifted) != 0) return nullptr;
    out->tm_year += static_cast<int>(cycles * 400);
    return out;
}

// GNU strerror_r: returns the message (here always in buf).
inline const char* strerror_r(int errnum, char* buf, std::size_t len) {
    if (::strerror_s(buf, len, errnum) != 0) return nullptr;
    return buf;
}

namespace protojs::platform {

// The canonical path of an existing file or directory: absolute, every symbolic
// link and junction resolved, and each component spelled as the file system
// stores it (the case of a case-insensitive volume included), with '/' as the
// separator. This is a module's identity, for CommonJS and ES modules alike, so
// two spellings of one file are one module. GetFinalPathNameByHandleW names the
// file the handle opened; its "\\?\" prefix is dropped, and "\\?\UNC\"
// becomes the "//server/share" it stands for. false (errno set) when the file
// does not exist or cannot be opened.
inline bool canonicalPath(const std::string& path, std::string& out) {
    if (path.empty()) { errno = ENOENT; return false; }
    // FILE_FLAG_BACKUP_SEMANTICS opens directories too; no access rights are
    // requested, so a file locked for reading can still be named.
    HANDLE h = ::CreateFileW(widen(path).c_str(), 0,
                             FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                             OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        const DWORD err = ::GetLastError();
        errno = (err == ERROR_ACCESS_DENIED) ? EACCES : ENOENT;
        return false;
    }
    std::wstring buf(MAX_PATH, L'\0');
    DWORD n = ::GetFinalPathNameByHandleW(h, buf.data(), static_cast<DWORD>(buf.size()),
                                          FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (n >= buf.size()) {
        buf.assign(static_cast<std::size_t>(n) + 1, L'\0');
        n = ::GetFinalPathNameByHandleW(h, buf.data(), static_cast<DWORD>(buf.size()),
                                        FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    }
    ::CloseHandle(h);
    if (n == 0 || n >= buf.size()) { errno = ENOENT; return false; }
    buf.resize(n);
    if (buf.compare(0, 8, L"\\\\?\\UNC\\") == 0) {
        buf = L"\\\\" + buf.substr(8);
    } else if (buf.compare(0, 4, L"\\\\?\\") == 0) {
        buf = buf.substr(4);
    }
    out = narrow(buf);
    for (char& c : out) if (c == '\\') c = '/';
    return true;
}

} // namespace protojs::platform

// realpath: the canonical path (protojs::platform::canonicalPath), with POSIX's
// contract -- nullptr and errno when the file does not exist.
inline char* realpath(const char* path, char* resolved) {
    std::string out;
    if (!path || !protojs::platform::canonicalPath(path, out)) return nullptr;
    if (out.size() + 1 > PATH_MAX) { errno = ENAMETOOLONG; return nullptr; }
    if (!resolved) resolved = static_cast<char*>(std::malloc(PATH_MAX));
    if (!resolved) { errno = ENOMEM; return nullptr; }
    std::memcpy(resolved, out.c_str(), out.size() + 1);
    return resolved;
}

#ifndef S_ISREG
#define S_ISREG(m) (((m) & _S_IFMT) == _S_IFREG)
#endif
#ifndef S_ISDIR
#define S_ISDIR(m) (((m) & _S_IFMT) == _S_IFDIR)
#endif

namespace protojs::platform {

// stat with 64-bit sizes and times, on a UTF-8 path.
using StatBuf = struct ::_stat64;
inline int statPath(const char* path, StatBuf* st) {
    return ::_wstat64(widen(path).c_str(), st);
}
inline int unlinkPath(const char* path) { return ::_wunlink(widen(path).c_str()); }
inline int rmdirPath(const char* path) { return ::_wrmdir(widen(path).c_str()); }
inline int mkdirPath(const char* path) { return ::_wmkdir(widen(path).c_str()); }
// fopen on a UTF-8 path; errno is set on failure.
inline std::FILE* fopenPath(const char* path, const char* mode) {
    return ::_wfopen(widen(path).c_str(), widen(mode).c_str());
}

// mktime over the whole range of a JavaScript date: outside the C runtime's
// 1970..3000 the year is moved by whole 400-year cycles (same calendar), as in
// localtime_r above.
inline std::time_t mktime(std::tm* tm) {
    using namespace detail;
    // Normalise every field first (a day of the month may be -99999999), with
    // the exact calendar arithmetic of timegm, then shift the normalised year.
    std::tm norm = *tm;
    ::timegm(&norm);
    norm.tm_isdst = tm->tm_isdst;
    const long long cycles = floorDiv(static_cast<long long>(norm.tm_year) + 1900 - 1971, 400);
    norm.tm_year -= static_cast<int>(cycles * 400);
    const std::time_t r = ::mktime(&norm);
    if (r == static_cast<std::time_t>(-1)) return r;
    norm.tm_year += static_cast<int>(cycles * 400);
    *tm = norm;
    return static_cast<std::time_t>(static_cast<long long>(r) + cycles * kSecondsPer400Years);
}

// Directory separators in a path: Windows accepts both.
inline constexpr const char* kPathSeparators = "/\\";

// "C:/x", "C:\x", "\\server\share" and "/x" (the current drive's root).
inline bool isAbsolutePath(const std::string& p) {
    if (!p.empty() && (p[0] == '/' || p[0] == '\\')) return true;
    return p.size() >= 3 && ((p[0] >= 'A' && p[0] <= 'Z') || (p[0] >= 'a' && p[0] <= 'z')) &&
           p[1] == ':' && (p[2] == '/' || p[2] == '\\');
}

} // namespace protojs::platform

#else // POSIX

#include <sys/stat.h>
#include <unistd.h>

#include <ctime>
#include <string>

#include <cerrno>
#include <climits>
#include <cstdio>
#include <cstdlib>

namespace protojs::platform {

// The canonical path of an existing file: realpath(3), which resolves every
// symbolic link. A module's identity, for CommonJS and ES modules alike.
// false (errno set) when the file does not exist.
inline bool canonicalPath(const std::string& path, std::string& out) {
    if (path.empty()) { errno = ENOENT; return false; }
    char* r = ::realpath(path.c_str(), nullptr);
    if (!r) return false;
    out.assign(r);
    std::free(r);
    return true;
}

using StatBuf = struct ::stat;
inline int statPath(const char* path, StatBuf* st) { return ::stat(path, st); }
inline int unlinkPath(const char* path) { return ::unlink(path); }
inline int rmdirPath(const char* path) { return ::rmdir(path); }
inline int mkdirPath(const char* path) { return ::mkdir(path, 0777); }
inline std::FILE* fopenPath(const char* path, const char* mode) {
    return std::fopen(path, mode);
}
inline std::time_t mktime(std::tm* tm) { return ::mktime(tm); }

inline constexpr const char* kPathSeparators = "/";

inline bool isAbsolutePath(const std::string& p) { return !p.empty() && p[0] == '/'; }

} // namespace protojs::platform

#endif // _WIN32

#endif // PROTOJS_PLATFORM_POSIX_H
