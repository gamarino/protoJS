// An exception crosses module boundaries like any call boundary: a function
// of this module, called from another module, throws to that module's try
// statement, and a function of another module throws to this one's.
//
// The interpreter flattens a module's functions into one table and names a
// function by its index there. A closure created by the main script did not
// record which table it came from, so when another module called it, the
// index was resolved against the CALLER's table and an unrelated function of
// that module ran instead: the "throwing" callback never threw, its exception
// was lost, and a different function's result came back. Which callbacks went
// wrong depended only on how many functions each module defines. Closures now
// always carry their table.
//
// The callbacks are deliberately the first functions in this file, so their
// indices are ones the fixture module also has.
//
// Asserting test: exits 1 on the first failed check.

var failures = [];
function check(name, got, expected) {
    if (got !== expected) failures.push(name + ": got " + String(got) + ", expected " + String(expected));
}

var m = require('./fixtures/call_and_catch.js');

var e1 = m.catchFrom(function () { throw new TypeError('one'); });
check("caught in the other module (TypeError)", e1 instanceof TypeError, true);
check("message preserved", e1 && e1.message, 'one');

var e2 = m.catchFrom(function () { throw 42; });
check("caught in the other module (primitive)", e2, 42);

var e3 = m.catchFrom(() => { throw new Error('arrow'); });
check("caught in the other module (arrow)", e3 && e3.message, 'arrow');

check("return value crosses back", m.callAndReturn(function () { return 'value'; }), 'value');
check("arrow return value crosses back", m.callAndReturn(() => 7), 7);

var path = require('path');
var e4 = m.catchFrom(function () { path.posix.join(1); });
check("native error raised in a callback", e4 && e4.code, 'ERR_INVALID_ARG_TYPE');

// Through the other module without a try there, back to a try here.
var e5 = null;
try {
    m.callThrough(function () { throw new RangeError('through'); });
} catch (e) {
    e5 = e;
}
check("through the other module to here", e5 instanceof RangeError && e5.message, 'through');

// A finally block in the other module runs, and the exception goes on.
var log = [];
var e6 = null;
try {
    m.callWithFinally(function () { throw new Error('fin'); }, log);
} catch (e) {
    e6 = e;
}
check("finally in the other module ran", log.join(), 'finally');
check("exception continued past finally", e6 && e6.message, 'fin');

// Thrown by the other module's own function, caught here.
var e7 = null;
try {
    m.thrower('theirs');
} catch (e) {
    e7 = e;
}
check("the other module's throw caught here", e7 instanceof TypeError && e7.message, 'theirs');

// Repeated: the same callback many times, and fresh closures each time.
var caught = 0;
for (var i = 0; i < 20; i++) {
    if (m.catchFrom(function () { throw i; }) === i) caught++;
}
check("caught every time", caught, 20);

if (failures.length) {
    for (var i = 0; i < failures.length; i++) console.log("FAIL: " + failures[i]);
    process.exit(1);
}
console.log("modules/test_cross_module_exception: all checks passed");
