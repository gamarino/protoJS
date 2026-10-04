#pragma once

// NativeStackGuard -- RangeError instead of a native stack overflow.
//
// Every JavaScript call is a runBytecode frame on the native stack (about
// 3.3 KiB with GCC, about 90 KiB with MSVC), so unbounded recursion used to run
// off the end of the thread's stack and end the process with SIGSEGV (Linux,
// macOS) or a stack-overflow exception (Windows). runBytecode now asks
// nativeStackExhausted() on entry and throws RangeError ("Maximum call stack
// size exceeded", as V8 does) while a margin of the stack is still free: enough
// for the frames that build and propagate the error and for the natives that
// sit between two JavaScript calls.
//
// The limit is computed once per thread, on the thread's first check: the
// current stack address minus the thread's stack size as protoCore reports it
// (ProtoSpace::currentThreadStackBytes -- the /STACK or explicit reservation on
// Windows, the pthread stack size elsewhere), plus the margin. The first check
// happens a few frames below the top of the thread's stack (main -> eval ->
// runBytecode, or a worker's entry -> eval -> runBytecode), and the margin
// absorbs that offset. Stacks grow downwards on every platform protoJS
// supports.

namespace protojs {

// True when the calling thread has less native stack left than the margin.
bool nativeStackExhausted();

// The calling thread's limits, for diagnostics and tests: the stack size the
// guard assumed and the margin it keeps (0, 0 before the thread's first check).
void nativeStackGuardLimits(unsigned long long& stackBytes, unsigned long long& marginBytes);

}  // namespace protojs
