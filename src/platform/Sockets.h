/*
 * Sockets — BSD sockets on every platform.
 *
 * On Linux and macOS this header includes the usual POSIX socket headers and
 * defines three one-line helpers that are exactly the calls the sources made
 * before (::close, ::read and ::write on a socket descriptor).
 *
 * On Windows the same names come from Winsock 2. A socket there is a SOCKET
 * handle, not a C runtime file descriptor: it is closed with closesocket() and
 * read/written with recv()/send(), which is why the sources go through the
 * helpers below for those three operations. Winsock must be started once per
 * process; every translation unit that includes this header does so (WSAStartup
 * is reference counted).
 */
#ifndef PROTOJS_PLATFORM_SOCKETS_H
#define PROTOJS_PLATFORM_SOCKETS_H

#include <climits>
#include <cstddef>

#if defined(_WIN32)

#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef _WINSOCK_DEPRECATED_NO_WARNINGS
#define _WINSOCK_DEPRECATED_NO_WARNINGS // gethostbyname, inet_addr
#endif
#include <winsock2.h>
#include <ws2tcpip.h>

#include "Posix.h"

#ifndef SHUT_RD
#define SHUT_RD   SD_RECEIVE
#define SHUT_WR   SD_SEND
#define SHUT_RDWR SD_BOTH
#endif

namespace protojs::platform {

struct WinsockStartup {
    WinsockStartup() {
        WSADATA data;
        ::WSAStartup(MAKEWORD(2, 2), &data);
    }
};
// One per translation unit; the first to run starts Winsock.
static const WinsockStartup winsockStartup;

// A socket handle fits in an int on Windows (its high bits are always zero),
// which is what lets the sources keep int descriptors.
inline int closeSocket(int fd) { return ::closesocket(static_cast<SOCKET>(fd)); }

// socket() and accept() that a child process does not inherit. A Winsock
// socket is inheritable by default, so a child started while a server listened
// would hold its port; WSA_FLAG_NO_HANDLE_INHERIT (Windows 7 SP1) creates it
// otherwise, and an accepted socket is made non-inheritable explicitly.
// WSA_FLAG_OVERLAPPED is what socket() itself passes. -1 on failure, as POSIX.
inline int openSocket(int af, int type, int protocol) {
    const SOCKET s = ::WSASocketW(af, type, protocol, nullptr, 0,
                                  WSA_FLAG_OVERLAPPED | WSA_FLAG_NO_HANDLE_INHERIT);
    return s == INVALID_SOCKET ? -1 : static_cast<int>(s);
}
inline int acceptSocket(int fd, sockaddr* addr, socklen_t* len) {
    const SOCKET s = ::accept(static_cast<SOCKET>(fd), addr, len);
    if (s == INVALID_SOCKET) return -1;
    ::SetHandleInformation(reinterpret_cast<HANDLE>(s), HANDLE_FLAG_INHERIT, 0);
    return static_cast<int>(s);
}
// Winsock takes buffer lengths as int, POSIX as size_t. One call moves at
// most INT_MAX bytes on Windows; a longer buffer is a short read or write,
// which the caller must already handle on every platform.
inline int socketLength(std::size_t len) {
    return len > static_cast<std::size_t>(INT_MAX) ? INT_MAX : static_cast<int>(len);
}
inline ssize_t readSocket(int fd, void* buf, std::size_t len) {
    return ::recv(static_cast<SOCKET>(fd), static_cast<char*>(buf), socketLength(len), 0);
}
inline ssize_t writeSocket(int fd, const void* buf, std::size_t len) {
    return ::send(static_cast<SOCKET>(fd), static_cast<const char*>(buf), socketLength(len), 0);
}
inline bool setSocketBlocking(int fd, bool blocking) {
    u_long nonBlocking = blocking ? 0 : 1;
    return ::ioctlsocket(static_cast<SOCKET>(fd), FIONBIO, &nonBlocking) == 0;
}
// > 0 readable (or an error the next call reports), 0 timeout, < 0 failure.
inline int waitReadable(int fd, int timeoutMs) {
    WSAPOLLFD p{};
    p.fd = static_cast<SOCKET>(fd);
    p.events = POLLRDNORM;
    return ::WSAPoll(&p, 1, timeoutMs);
}

} // namespace protojs::platform

#else // POSIX

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>

namespace protojs::platform {

inline int closeSocket(int fd) { return ::close(fd); }

// socket() and accept() whose descriptor a child process does not inherit:
// close-on-exec, so a program child_process starts never holds a server's port.
inline int openSocket(int af, int type, int protocol) {
    const int fd = ::socket(af, type, protocol);
    if (fd >= 0) ::fcntl(fd, F_SETFD, FD_CLOEXEC);
    return fd;
}
inline int acceptSocket(int fd, sockaddr* addr, socklen_t* len) {
    const int c = ::accept(fd, addr, len);
    if (c >= 0) ::fcntl(c, F_SETFD, FD_CLOEXEC);
    return c;
}
// The length type the socket calls take (see the Windows definition).
inline std::size_t socketLength(std::size_t len) { return len; }
inline ssize_t readSocket(int fd, void* buf, std::size_t len) { return ::read(fd, buf, len); }
inline ssize_t writeSocket(int fd, const void* buf, std::size_t len) { return ::write(fd, buf, len); }
inline bool setSocketBlocking(int fd, bool blocking) {
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0) return false;
    return ::fcntl(fd, F_SETFL, blocking ? (flags & ~O_NONBLOCK) : (flags | O_NONBLOCK)) == 0;
}
// > 0 readable (or an error the next call reports), 0 timeout, < 0 failure.
inline int waitReadable(int fd, int timeoutMs) {
    pollfd p{};
    p.fd = fd;
    p.events = POLLIN;
    return ::poll(&p, 1, timeoutMs);
}

} // namespace protojs::platform

#endif

#include <atomic>

namespace protojs::platform {

// The longest an accept loop waits before it looks at its stop flag again when
// nothing wakes it: always on Windows (WSAPoll takes sockets only, so there is
// no wake pipe), and on POSIX only if the wake pipe could not be created.
constexpr int kAcceptPollMs = 50;

// Wakes a thread waiting in acceptUntilStopped.  On POSIX a self-pipe the wait
// polls with the listening socket, so a stop is seen at once; wake() is one
// non-blocking write(), legal in a finalizer (it blocks on nothing and calls no
// protoCore API).  On Windows wake() does nothing and the wait times out.
class AcceptWaker {
public:
    AcceptWaker() noexcept {
#if !defined(_WIN32)
        int fds[2];
        if (::pipe(fds) == 0) {
            for (int f : fds) {
                ::fcntl(f, F_SETFD, FD_CLOEXEC);
                ::fcntl(f, F_SETFL, ::fcntl(f, F_GETFL, 0) | O_NONBLOCK);
            }
            readFd_ = fds[0];
            writeFd_ = fds[1];
        }
#endif
    }
    ~AcceptWaker() {
#if !defined(_WIN32)
        if (readFd_ >= 0) ::close(readFd_);
        if (writeFd_ >= 0) ::close(writeFd_);
#endif
    }
    AcceptWaker(const AcceptWaker&) = delete;
    AcceptWaker& operator=(const AcceptWaker&) = delete;

    void wake() noexcept {
#if !defined(_WIN32)
        if (writeFd_ >= 0) {
            const char b = 1;
            [[maybe_unused]] const auto n = ::write(writeFd_, &b, 1);  // full pipe: already woken
        }
#endif
    }
    // Empties the pipe, so a server listening again after a close does not
    // start out woken.  Only while no loop is waiting.
    void reset() noexcept {
#if !defined(_WIN32)
        char buf[64];
        while (readFd_ >= 0 && ::read(readFd_, buf, sizeof buf) > 0) {}
#endif
    }
    int readFd() const noexcept { return readFd_; }

private:
    int readFd_{-1};
    int writeFd_{-1};
};

// The next connection on the NON-BLOCKING listening socket `fd`, as a blocking
// descriptor, or -1 once `keepGoing` is false.  The loop that calls it is
// stopped by clearing `keepGoing` and calling waker.wake(), and is joined before
// `fd` is closed.
//
// Why not close the descriptor to wake a thread blocked in accept(): on macOS
// shutdown() is a no-op on a listening socket (ENOTCONN) and close() of a
// descriptor another thread is inside accept() on waits for that accept to
// drain, which it may not do -- http-server-collected stalled there, the main
// thread in close() and the loop in accept(), for over a minute (2026-10,
// sampled on the macOS runner).  Closing a descriptor another thread still uses
// is also a reuse race on every platform.  The listening socket is non-blocking
// so that a connection reset between the wait and accept() cannot block the
// loop; an accepted socket inherits that flag on macOS and Windows, so it is
// cleared.
inline int acceptUntilStopped(int fd, const std::atomic<bool>& keepGoing,
                              const AcceptWaker& waker, sockaddr* addr, socklen_t* len) {
    const socklen_t capacity = *len;
    while (keepGoing.load()) {
#if defined(_WIN32)
        (void)waker;
        if (waitReadable(fd, kAcceptPollMs) <= 0) continue;   // timeout
#else
        pollfd p[2] = {{fd, POLLIN, 0}, {waker.readFd(), POLLIN, 0}};
        const int n = ::poll(p, waker.readFd() >= 0 ? 2 : 1,
                             waker.readFd() >= 0 ? -1 : kAcceptPollMs);
        if (n <= 0) continue;                                 // timeout or EINTR
#endif
        if (!keepGoing.load()) break;
        *len = capacity;
        const int c = acceptSocket(fd, addr, len);
        if (c >= 0) {
            setSocketBlocking(c, true);
            return c;
        }
        // EAGAIN / EWOULDBLOCK (the connection went away) or an error: wait again.
    }
    return -1;
}

} // namespace protojs::platform

#endif // PROTOJS_PLATFORM_SOCKETS_H
