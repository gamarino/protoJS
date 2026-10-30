#pragma once

// setTimeout, setInterval, clearTimeout and clearInterval, run by the
// process event loop (the drain loop in main.cpp), with Node's semantics:
//
//   - setTimeout(callback, delay, ...args) and setInterval(...) return a
//     Timeout object; the callback runs with that object as `this` and the
//     extra arguments as its arguments.  A callback that is not a function
//     is a TypeError (ERR_INVALID_ARG_TYPE).
//   - The delay is converted with ToNumber; a value that is not between 1
//     and 2^31 - 1 (NaN, 0, negative, too large) becomes 1, and a fraction
//     is truncated.
//   - Timers run in order of their due time; timers due at the same time
//     run in the order they were created.  A timer created by a timer
//     callback never runs in the same pass, even with a delay of 0.
//   - Each callback is a macrotask: the microtask queue is drained after it,
//     and an exception it throws ends the process as an uncaught exception.
//   - An interval is rescheduled `delay` ms after its callback returns.
//   - Timeout.prototype: ref(), unref(), hasRef(), refresh(), close() and
//     [Symbol.toPrimitive]() (the timer's numeric id).  A referenced timer
//     keeps the process alive; an unreferenced one runs only while something
//     else does.  clearTimeout and clearInterval are interchangeable and
//     accept a Timeout or its numeric id; anything else is ignored.
//
// The timers belong to the main script's realm.  Calling them inside a
// Deferred function or from a worker_threads worker throws an Error (those
// threads have no event loop of their own to run them).

#include <protoCore.h>

namespace protojs {

class JSContextWrapper;

class Timers {
public:
    /** Installs the four functions on the protoCore-native global. */
    static const proto::ProtoObject* init(proto::ProtoContext* ctx,
                                          const proto::ProtoObject* globalObj);

    /** The wrapper whose event loop runs the timers (the main script's). */
    static void setOwner(JSContextWrapper* wrapper);

    /** True while a referenced timer is pending: the process stays alive. */
    static bool hasRefedTimers();

    /**
     * Milliseconds until the earliest pending timer is due (0 when one is
     * due now), or -1 when no timer is pending.
     */
    static long long msUntilNext();

    /**
     * Runs every timer due now, earliest first.  Main thread only, from the
     * drain loop, outside any JavaScript frame.
     */
    static void runDue();
};

}  // namespace protojs
