// Fixture for test_cross_module_exception.js: functions that call a function
// they are given, from this module.
'use strict';

// Catches what fn throws and reports it; 'no exception' if nothing was thrown.
function catchFrom(fn) {
    try {
        fn();
    } catch (e) {
        return e;
    }
    return 'no exception';
}

// Calls fn without a try statement, so its exception leaves this module.
function callThrough(fn) {
    return fn();
}

// Calls fn and returns what it returns.
function callAndReturn(fn) {
    return fn();
}

// Runs fn inside try/finally: the finally block runs and the exception goes on.
function callWithFinally(fn, log) {
    try {
        return fn();
    } finally {
        log.push('finally');
    }
}

module.exports = {
    catchFrom: catchFrom,
    callThrough: callThrough,
    callAndReturn: callAndReturn,
    callWithFinally: callWithFinally,
    // Throws from this module, for a try statement in the caller's.
    thrower: function (message) { throw new TypeError(message); },
};
