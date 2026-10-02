/*
 * SizedThread — a joinable native thread with a chosen stack size.
 *
 * Every JavaScript call is a runBytecode frame on the native stack, so the
 * recursion depth a thread reaches is set by its stack. std::thread cannot be
 * given a stack size, and the platforms' defaults differ by two orders of
 * magnitude: 8 MiB on Linux (RLIMIT_STACK), the executable's /STACK reservation
 * on Windows (64 MiB for protojs.exe), and 512 KiB on macOS whatever the main
 * thread has. A worker_threads Worker running on a std::thread therefore died
 * on macOS at a depth the main thread reaches easily.
 *
 * Threads protoCore creates (ProtoSpace::newThread) take their size from
 * ProtoSpace::setThreadStackBytes. A Worker cannot be one of them: it owns a
 * ProtoSpace of its own and is that space's adopted main thread, so it is not a
 * thread of the main space. protoCore's sized-thread primitive
 * (core/ThreadStack.cpp) is internal to the library, so this header is the one
 * place protoJS creates a native thread with the platform API rather than
 * through protoCore -- the documented exception to the rule that runtimes use
 * protoCore's threads. Exporting that primitive from protoCore would retire it.
 *
 * Same contract as the std::thread it replaces in WorkerThreadsModule:
 * start(), joinable(), join(). If the platform refuses the requested size, the
 * thread is created with the platform's default size and a warning is printed
 * once, as protoCore's newThread does.
 */
#ifndef PROTOJS_PLATFORM_SIZEDTHREAD_H
#define PROTOJS_PLATFORM_SIZEDTHREAD_H

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <memory>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <process.h>
#include <climits>
#else
#include <pthread.h>
#include <climits>
#include <unistd.h>
#endif

namespace protojs::platform {

// The stack size of threads that run JavaScript, set once at start-up
// (src/main.cpp, configureThreadStacks) to the main thread's reservation.
// 0 means "the platform's default".
inline std::atomic<std::size_t>& jsThreadStackBytesSlot() {
    static std::atomic<std::size_t> bytes{0};
    return bytes;
}
inline void setJsThreadStackBytes(std::size_t bytes) { jsThreadStackBytesSlot().store(bytes); }
inline std::size_t jsThreadStackBytes() { return jsThreadStackBytesSlot().load(); }

class SizedThread {
public:
    SizedThread() = default;
    SizedThread(const SizedThread&) = delete;
    SizedThread& operator=(const SizedThread&) = delete;
    ~SizedThread() {
        // As std::thread, destroying a joinable thread is a programming error;
        // join rather than terminate, so a missed join costs a wait, not the process.
        if (joinable()) join();
    }

    // Starts fn on a new thread with `bytes` of stack (0: the platform's
    // default). Returns false if no thread could be created at all.
    bool start(std::size_t bytes, std::function<void()> fn) {
        if (joinable()) return false;
        auto* job = new std::function<void()>(std::move(fn));
        if (create(bytes, job)) return true;
        if (bytes != 0) {
            warnOnce(bytes);
            if (create(0, job)) return true;
        }
        delete job;
        return false;
    }

    bool joinable() const { return started_; }

    void join() {
        if (!started_) return;
#if defined(_WIN32)
        ::WaitForSingleObject(handle_, INFINITE);
        ::CloseHandle(handle_);
        handle_ = nullptr;
#else
        ::pthread_join(thread_, nullptr);
#endif
        started_ = false;
    }

private:
#if defined(_WIN32)
    static unsigned __stdcall trampoline(void* p) {
        std::unique_ptr<std::function<void()>> job(static_cast<std::function<void()>*>(p));
        (*job)();
        return 0;
    }
    bool create(std::size_t bytes, std::function<void()>* job) {
        if (bytes > UINT_MAX) bytes = 0;
        // STACK_SIZE_PARAM_IS_A_RESERVATION: the size is address space
        // reserved, as /STACK is for the main thread, not memory committed.
        const uintptr_t h = ::_beginthreadex(nullptr, static_cast<unsigned>(bytes), trampoline, job,
                                             bytes ? STACK_SIZE_PARAM_IS_A_RESERVATION : 0, nullptr);
        if (h == 0) return false;
        handle_ = reinterpret_cast<HANDLE>(h);
        started_ = true;
        return true;
    }
    HANDLE handle_ = nullptr;
#else
    static void* trampoline(void* p) {
        std::unique_ptr<std::function<void()>> job(static_cast<std::function<void()>*>(p));
        (*job)();
        return nullptr;
    }
    bool create(std::size_t bytes, std::function<void()>* job) {
        pthread_attr_t attr;
        if (::pthread_attr_init(&attr) != 0) return false;
        bool ok = true;
        if (bytes != 0) {
            // A whole number of pages, never below the platform's minimum.
            const long pageSize = ::sysconf(_SC_PAGESIZE);
            const std::size_t page = pageSize > 0 ? static_cast<std::size_t>(pageSize) : 16384;
            std::size_t size = bytes < static_cast<std::size_t>(PTHREAD_STACK_MIN)
                ? static_cast<std::size_t>(PTHREAD_STACK_MIN) : bytes;
            size = (size + page - 1) / page * page;
            ok = ::pthread_attr_setstacksize(&attr, size) == 0;
        }
        if (ok) ok = ::pthread_create(&thread_, &attr, trampoline, job) == 0;
        ::pthread_attr_destroy(&attr);
        if (ok) started_ = true;
        return ok;
    }
    pthread_t thread_{};
#endif

    static void warnOnce(std::size_t bytes) {
        static std::atomic<bool> warned{false};
        if (!warned.exchange(true)) {
            std::fprintf(stderr,
                         "protojs: could not create a thread with a %zu-byte stack; "
                         "using the platform's default size\n", bytes);
        }
    }

    bool started_ = false;
};

} // namespace protojs::platform

#endif // PROTOJS_PLATFORM_SIZEDTHREAD_H
