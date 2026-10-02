// The functions of native modules are functions like any other: their
// [[Prototype]] is Function.prototype, so call, apply and bind work on them.
//
// ProtoNativeModule::addMethod parents each wrapper on the space's method
// prototype. The native modules (path, fs, util, ...) are built before the
// first script runs, and until Function.prototype was created together with
// the global object that prototype was still Object.prototype: path.join.call
// was undefined and `path.join.apply(null, args)` threw "is not a function".
//
// Asserting test: exits 1 on the first failed check.

var failures = [];
function check(name, got, expected) {
    if (got !== expected) failures.push(name + ": got " + String(got) + ", expected " + String(expected));
}
function attempt(name, fn, expected) {
    try {
        check(name, fn(), expected);
    } catch (e) {
        failures.push(name + ": threw " + e);
    }
}

var path = require('path');
var fs = require('fs');
var util = require('util');

[['path.join', path.join], ['path.posix.join', path.posix.join],
 ['fs.readFileSync', fs.readFileSync], ['util.format', util.format],
 ['console.log', console.log], ['JSON.stringify', JSON.stringify]].forEach(function (pair) {
    var name = pair[0], fn = pair[1];
    check(name + ' typeof', typeof fn, 'function');
    check(name + ' [[Prototype]]', Object.getPrototypeOf(fn), Function.prototype);
    check(name + ' instanceof Function', fn instanceof Function, true);
    check(name + '.call', typeof fn.call, 'function');
    check(name + '.apply', typeof fn.apply, 'function');
    check(name + '.bind', typeof fn.bind, 'function');
});

attempt('path.join.call', function () { return path.posix.join.call(null, 'a', 'b'); }, 'a/b');
attempt('path.join.apply', function () { return path.posix.join.apply(null, ['a', 'b', '..', 'c']); }, 'a/c');
attempt('path.join.bind', function () { return path.posix.join.bind(null, 'x')('y', 'z'); }, 'x/y/z');
attempt('fs.readFileSync.bind(fs)', function () {
    var read = fs.readFileSync.bind(fs);
    return read(__filename, 'utf8').indexOf('native_function_prototype') >= 0;
}, true);
attempt('path.basename.apply', function () { return path.posix.basename.apply(path.posix, ['/a/b.js', '.js']); }, 'b');

if (failures.length) {
    for (var i = 0; i < failures.length; i++) console.log("FAIL: " + failures[i]);
    process.exit(1);
}
console.log("basic/native_function_prototype: all checks passed");
