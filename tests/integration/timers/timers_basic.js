// setTimeout, setInterval, clearTimeout and clearInterval (Node semantics).
// Until 2026-10 protoJS had no timer functions at all.
//
// Asserting test: exits 1 on any failure (the final report runs from a
// timer, so a timer that never fires leaves the test to its ctest timeout).
// Every expected value is Node's.

var failures = [];
function check(name, got, expected) {
    if (got !== expected) failures.push(name + ": got " + String(got) + ", expected " + String(expected));
}

check("typeof setTimeout", typeof setTimeout, "function");
check("typeof setInterval", typeof setInterval, "function");
check("typeof clearTimeout", typeof clearTimeout, "function");
check("typeof clearInterval", typeof clearInterval, "function");

var t0 = Date.now();
var order = [];

// Ordering: by due time, then by creation order.
setTimeout(function () { order.push("c30"); }, 30);
setTimeout(function () { order.push("a10"); }, 10);
setTimeout(function () { order.push("b10"); }, 10);
setTimeout(function () { order.push("z0"); }, 0);
setTimeout(function () { order.push("neg"); }, -5);       // becomes 1
setTimeout(function () { order.push("nan"); }, "abc");    // becomes 1

// Arguments and `this`.
var handle = setTimeout(function (a, b) {
    check("arguments", a + "," + b, "1,two");
    check("this is the Timeout", this, handle);
}, 5, 1, "two");
check("handle has ref()", typeof handle.ref, "function");
check("hasRef() by default", handle.hasRef(), true);
check("unref() returns the Timeout", handle.unref(), handle);
check("hasRef() after unref", handle.hasRef(), false);
check("ref() returns the Timeout", handle.ref(), handle);
check("hasRef() after ref", handle.hasRef(), true);
check("Timeout constructor name", handle.constructor.name, "Timeout");
check("numeric id", typeof +handle, "number");
check("Timeout is an object", handle instanceof Object, true);

// The delay is waited.
var delayed = -1;
setTimeout(function () { delayed = Date.now() - t0; }, 50);

// clearTimeout before it runs, by handle and by numeric id.
var cleared1 = setTimeout(function () { check("cleared by handle ran", true, false); }, 5);
clearTimeout(cleared1);
var cleared2 = setTimeout(function () { check("cleared by id ran", true, false); }, 5);
clearTimeout(+cleared2);
clearTimeout(undefined);
clearTimeout(null);
clearTimeout({});
// close() is clearTimeout.
setTimeout(function () { check("closed ran", true, false); }, 5).close();

// setInterval runs until cleared; clearInterval inside the callback works.
var ticks = 0;
var iv = setInterval(function (step) {
    ticks += step;
    if (ticks === 3) clearInterval(iv);
}, 5, 1);

// clearTimeout and clearInterval are interchangeable.
var ticks2 = 0;
var iv2 = setInterval(function () { if (++ticks2 === 2) clearTimeout(iv2); }, 5);

// A timer set inside a timer callback runs in a later pass, after the
// microtasks of the callback that set it.
var nested = [];
setTimeout(function () {
    setTimeout(function () { nested.push("inner"); }, 0);
    Promise.resolve().then(function () { nested.push("microtask"); });
    nested.push("outer");
}, 15);

// refresh() restarts the countdown.
var refreshedAt = -1;
var r = setTimeout(function () { refreshedAt = Date.now() - t0; }, 40);
setTimeout(function () { r.refresh(); }, 30);   // due again at about 70 ms

// A non-function callback is a TypeError with code ERR_INVALID_ARG_TYPE.
try {
    setTimeout("not a function", 1);
    check("string callback throws", false, true);
} catch (e) {
    check("TypeError", e instanceof TypeError, true);
    check("error code", e.code, "ERR_INVALID_ARG_TYPE");
}

// An unreferenced timer does not keep the process alive: if it did, the
// test would end by its ctest timeout instead of process.exit below.
setTimeout(function () { check("unref'd 1 h timer ran", true, false); }, 3600000).unref();

setTimeout(function () {
    check("order", order.join(), "z0,neg,nan,a10,b10,c30");
    check("delay honoured", delayed >= 49, true);
    check("interval ticks", ticks, 3);
    check("interval cleared with clearTimeout", ticks2, 2);
    check("nested order", nested.join(), "outer,microtask,inner");
    check("refresh delayed the timer", refreshedAt >= 69, true);
    if (failures.length) {
        for (var f of failures) console.log("FAIL " + f);
        process.exit(1);
    }
    console.log("timers_basic: all checks passed (50 ms timer after " + delayed +
                " ms, refreshed timer after " + refreshedAt + " ms)");
}, 200);
