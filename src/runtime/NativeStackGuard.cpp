#include "NativeStackGuard.h"

#include <protoCore.h>

#include <cstddef>
#include <cstdint>

namespace protojs {

namespace {

// When protoCore cannot tell the stack size: the smallest default a thread
// gets on a supported platform (a secondary thread on macOS).
constexpr std::size_t kFallbackStackBytes = 512 * 1024;
// An unlimited RLIMIT_STACK can report an absurd size; the main thread's stack
// cannot usefully exceed this, and a smaller assumed size only moves the
// RangeError earlier.
constexpr std::size_t kMaxStackBytes = std::size_t(1) << 30;
// The free stack kept back: an eighth of the stack, at least 256 KiB (dozens of
// GCC frames, three MSVC frames) and at most 8 MiB.
constexpr std::size_t kMinMarginBytes = 256 * 1024;
constexpr std::size_t kMaxMarginBytes = 8 * 1024 * 1024;

struct ThreadStackLimit {
    std::uintptr_t limit = 0;          // 0: not computed yet on this thread
    std::size_t stackBytes = 0;
    std::size_t marginBytes = 0;
};

thread_local ThreadStackLimit t_stack;

#if defined(_MSC_VER) && !defined(__clang__)
#define PROTOJS_GUARD_NOINLINE __declspec(noinline)
#else
#define PROTOJS_GUARD_NOINLINE __attribute__((noinline))
#endif

PROTOJS_GUARD_NOINLINE std::uintptr_t currentStackAddress() {
    volatile char here = 0;
    return reinterpret_cast<std::uintptr_t>(&here);
}

PROTOJS_GUARD_NOINLINE void computeLimit(std::uintptr_t sp) {
    std::size_t bytes = 0;
#if defined(PROTOCORE_HAS_CURRENT_THREAD_STACK_BYTES)
    bytes = proto::ProtoSpace::currentThreadStackBytes();
#endif
    if (bytes == 0) bytes = kFallbackStackBytes;
    if (bytes > kMaxStackBytes) bytes = kMaxStackBytes;
    std::size_t margin = bytes / 8;
    if (margin < kMinMarginBytes) margin = kMinMarginBytes;
    if (margin > kMaxMarginBytes) margin = kMaxMarginBytes;
    t_stack.stackBytes = bytes;
    t_stack.marginBytes = margin;
    // sp - bytes + margin, without wrapping below zero.
    t_stack.limit = (sp > bytes) ? sp - bytes + margin : margin;
}

}  // namespace

bool nativeStackExhausted() {
    const std::uintptr_t sp = currentStackAddress();
    if (t_stack.limit == 0) computeLimit(sp);
    return sp < t_stack.limit;
}

void nativeStackGuardLimits(unsigned long long& stackBytes, unsigned long long& marginBytes) {
    stackBytes = t_stack.stackBytes;
    marginBytes = t_stack.marginBytes;
}

}  // namespace protojs
