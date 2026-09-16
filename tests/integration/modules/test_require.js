// CommonJS require() of built-in module names.
//
// `require('fs')` used to return `undefined` without throwing: the loader
// looked bare names up on the QuickJS-side global object, while the standard
// modules are registered on the protoCore-native global. A specifier that
// resolved nowhere threw on the QuickJS context, and that exception was
// dropped instead of being raised in the script.
//
// Scope: built-in names, identity and error reporting. Requiring a relative
// JavaScript file is covered separately.
//
// Asserting test: prints the failures and exits 1 when anything is wrong.

var failures = [];

function check(name, condition, detail) {
    if (!condition) {
        failures.push(name + (detail !== undefined ? " — " + detail : ""));
    }
}

function throws(name, fn, expectedName, messageFragment) {
    var threw = null;
    try {
        fn();
    } catch (e) {
        threw = e;
    }
    if (threw === null) {
        failures.push(name + " — did not throw");
        return;
    }
    if (expectedName && threw.name !== expectedName) {
        failures.push(name + " — threw " + threw.name + ": " + threw.message);
        return;
    }
    if (messageFragment && String(threw.message).indexOf(messageFragment) === -1) {
        failures.push(name + " — message was " + JSON.stringify(String(threw.message)));
    }
}

check("require is a function", typeof require === "function",
      "got " + typeof require);
check("require.resolve is a function", typeof require.resolve === "function",
      "got " + typeof require.resolve);
check("require.cache is an object", typeof require.cache === "object",
      "got " + typeof require.cache);

// Every built-in name must return the very object installed as the global,
// so that `require('fs') === fs`.
var builtins = [
    ["fs", fs],
    ["path", path],
    ["url", url],
    ["http", http],
    ["events", events],
    ["stream", stream],
    ["util", util],
    ["crypto", crypto],
    ["net", net],
    ["dns", dns],
    ["dgram", dgram],
    ["cluster", cluster],
    ["child_process", child_process],
    ["worker_threads", worker_threads],
    ["process", process]
];

for (var i = 0; i < builtins.length; i++) {
    var name = builtins[i][0];
    var globalValue = builtins[i][1];
    var required;
    var error = null;
    try {
        required = require(name);
    } catch (e) {
        error = e;
    }
    check("require('" + name + "') does not throw", error === null,
          error && (error.name + ": " + error.message));
    check("require('" + name + "') is the global " + name,
          required === globalValue,
          "got " + (typeof required));
}

// `buffer` is the one built-in that is not a global of the same name: Node
// exposes the Buffer constructor as a property of the module.
var bufferModule = require("buffer");
check("require('buffer') returns an object",
      bufferModule && typeof bufferModule === "object",
      "got " + typeof bufferModule);
check("require('buffer').Buffer is the Buffer global",
      bufferModule && bufferModule.Buffer === Buffer);

// A `node:` prefix names the same built-in.
check("require('node:path') is the path global", require("node:path") === path);
check("require('node:fs') is the fs global", require("node:fs") === fs);

// Requiring the same built-in twice yields the same object.
check("require('path') is stable across calls", require("path") === require("path"));

// Failures must reach the script instead of returning undefined.
throws("require of a missing module throws",
       function () { require("does-not-exist-xyz"); },
       undefined, "Cannot find module");

throws("require() without an argument throws TypeError",
       function () { require(); }, "TypeError");

throws("require(non-string) throws TypeError",
       function () { require(42); }, "TypeError");

// require.resolve reports missing modules too.
throws("require.resolve of a missing module throws",
       function () { require.resolve("does-not-exist-xyz"); });

if (failures.length) {
    console.log("test_require: " + failures.length + " check(s) failed");
    for (var f = 0; f < failures.length; f++) {
        console.log("  FAIL: " + failures[f]);
    }
    process.exit(1);
}
console.log("test_require: all checks passed");
