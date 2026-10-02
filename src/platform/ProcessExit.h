/*
 * ProcessExit -- ending the process from anywhere, cleanly.
 *
 * process.exit(code) is called from JavaScript, i.e. from inside the
 * interpreter, possibly deep in an event callback, while the runtime's thread
 * pools and protoCore's collector are running and the JavaScript context is
 * still alive on main()'s stack. exit() is wrong there: it runs the C++ static
 * destructors (the thread pools' among them) against a runtime that is still
 * in use, and the process dies with a signal instead of the requested code.
 *
 * exitNow(code) does what a clean end needs and nothing that depends on the
 * runtime's state: it flushes the C++ and C standard streams, runs the exit
 * hooks registered with addExitHook (process-level state to restore, such as
 * the Windows console code pages), and ends the process with _Exit. Pending
 * asynchronous work is abandoned, as with Node's process.exit.
 *
 * A hook also runs on the ordinary ways out (a return from main, exit()),
 * because the first addExitHook registers the hook runner with std::atexit.
 * Each hook runs at most once.
 */
#ifndef PROTOJS_PLATFORM_PROCESS_EXIT_H
#define PROTOJS_PLATFORM_PROCESS_EXIT_H

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <iostream>

namespace protojs::platform {

using ExitHook = void (*)();

namespace detail {

constexpr std::size_t kMaxExitHooks = 8;

struct ExitHooks {
    ExitHook hooks[kMaxExitHooks] = {};
    std::atomic<std::size_t> count{0};
    std::atomic<bool> ran{false};
};

inline ExitHooks& exitHooks() {
    // Never destroyed: the hooks must stay callable from an atexit handler
    // that runs after static destructors may have started.
    static ExitHooks* hooks = new ExitHooks();
    return *hooks;
}

inline void runExitHooks() {
    ExitHooks& h = exitHooks();
    if (h.ran.exchange(true)) return;
    const std::size_t n = h.count.load(std::memory_order_acquire);
    for (std::size_t i = n; i-- > 0;) {
        if (h.hooks[i]) h.hooks[i]();
    }
}

} // namespace detail

// Register a hook to run when the process ends, by any path. Call during
// start-up, from the main thread. Returns false when the table is full.
inline bool addExitHook(ExitHook hook) {
    detail::ExitHooks& h = detail::exitHooks();
    const std::size_t i = h.count.load(std::memory_order_relaxed);
    if (!hook || i >= detail::kMaxExitHooks) return false;
    h.hooks[i] = hook;
    h.count.store(i + 1, std::memory_order_release);
    if (i == 0) std::atexit(detail::runExitHooks);
    return true;
}

// End the process now with `code`: flush the standard streams, run the exit
// hooks, _Exit. Callable from any thread, at any depth.
[[noreturn]] inline void exitNow(int code) {
    std::cout.flush();
    std::cerr.flush();
    std::clog.flush();
    std::fflush(nullptr);
    detail::runExitHooks();
    std::_Exit(code);
}

} // namespace protojs::platform

#endif // PROTOJS_PLATFORM_PROCESS_EXIT_H
