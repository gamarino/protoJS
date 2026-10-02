// Updating a local that a closure has captured.
//
// When a closure captures a local, OP_fclosure promotes the local's slot to a
// shared cell, and every opcode that reads or writes the slot must go through
// the cell. inc_loc, dec_loc and add_loc (QuickJS's fused forms of `i++`,
// `i--`, `i = i + 1` and `s += x` on a local) and the specialiser's fused
// proto_acc_loc8_loc8 (`a += b` on two locals) read the slot raw: they treated
// the cell itself as the number, so `i++` produced NaN or threw "Cannot convert
// object to primitive value", and the closure kept the old value. A `for` loop
// whose body creates a closure over its `var` counter ran once and stopped.
//
// Asserting test: prints the failures and exits 1 when anything is wrong.

var failures = [];

function check(name, condition, detail) {
    if (!condition) {
        failures.push(name + (detail !== undefined ? " -- " + detail : ""));
    }
}

function incAfterCapture() {
    var i = 0;
    var f = function () { return i; };
    i++;
    return [i, f()];
}
var r = incAfterCapture();
check("i++ on a captured local", r[0] === 1 && r[1] === 1, "got " + r);

function decAfterCapture() {
    var i = 5;
    var f = function () { return i; };
    i--;
    --i;
    return [i, f()];
}
r = decAfterCapture();
check("i-- on a captured local", r[0] === 3 && r[1] === 3, "got " + r);

function addOneAfterCapture() {
    var i = 0;
    var f = function () { return i; };
    i = i + 1;
    return [i, f()];
}
r = addOneAfterCapture();
check("i = i + 1 on a captured local", r[0] === 1 && r[1] === 1, "got " + r);

function addAssignAfterCapture() {
    var s = 10;
    var x = 5;
    var f = function () { return s; };
    s += x;
    s += 'a';
    return [s, f()];
}
r = addAssignAfterCapture();
check("s += x on a captured local", r[0] === "15a" && r[1] === "15a", "got " + r);

function addLocalsAfterCapture() {
    var acc = 0;
    var f = function () { return acc; };
    for (var k = 0; k < 4; k++) { acc += k; }
    return [acc, f()];
}
r = addLocalsAfterCapture();
check("acc += k (both locals) with acc captured", r[0] === 6 && r[1] === 6, "got " + r);

function addLocalsSourceCaptured() {
    var acc = 0;
    var k = 0;
    var g = function () { return k; };
    for (k = 0; k < 4; k++) { acc += k; }
    return [acc, g()];
}
r = addLocalsSourceCaptured();
check("acc += k (both locals) with k captured", r[0] === 6 && r[1] === 4, "got " + r);

// A closure created in every iteration over the loop's own `var` counter.
function closureLoop(n) {
    var sum = 0;
    for (var i = 0; i < n; i++) {
        var f = function (x) { return x + i; };
        sum += f(1);
    }
    return sum;
}
var cl = closureLoop(100);
check("closure over the var loop counter", cl === 5050, "sum = " + cl);

// The same with `let`: each iteration has its own binding.
function closureLoopLet(n) {
    var fs = [];
    for (let i = 0; i < n; i++) {
        fs.push(function () { return i; });
    }
    var out = [];
    for (var j = 0; j < fs.length; j++) out.push(fs[j]());
    return out.join(",");
}
var cll = closureLoopLet(4);
check("closures over a let loop counter", cll === "0,1,2,3", "got " + cll);

// A closure that updates the captured counter itself.
function closureIncrements() {
    var n = 0;
    var inc = function () { n++; };
    inc(); inc();
    n++;
    inc();
    return n;
}
var ci = closureIncrements();
check("closure and frame update the same binding", ci === 4, "n = " + ci);

if (failures.length) {
    console.log("captured_local_update: " + failures.length + " check(s) failed");
    for (var i = 0; i < failures.length; i++) {
        console.log("  FAIL: " + failures[i]);
    }
    process.exit(1);
}
console.log("captured_local_update: all checks passed");
