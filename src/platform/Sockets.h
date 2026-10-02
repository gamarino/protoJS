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
inline ssize_t readSocket(int fd, void* buf, std::size_t len) {
    return ::recv(static_cast<SOCKET>(fd), static_cast<char*>(buf), static_cast<int>(len), 0);
}
inline ssize_t writeSocket(int fd, const void* buf, std::size_t len) {
    return ::send(static_cast<SOCKET>(fd), static_cast<const char*>(buf), static_cast<int>(len), 0);
}

} // namespace protojs::platform

#else // POSIX

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <fcntl.h>
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
inline ssize_t readSocket(int fd, void* buf, std::size_t len) { return ::read(fd, buf, len); }
inline ssize_t writeSocket(int fd, const void* buf, std::size_t len) { return ::write(fd, buf, len); }

} // namespace protojs::platform

#endif

#endif // PROTOJS_PLATFORM_SOCKETS_H
