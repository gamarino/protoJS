// Resolution order: a native addon (.node / .so) wins over a sibling .js file.
//
// fixture.js and fixture.so both sit in this directory, so require('./fixture')
// must load the native addon and `require.resolve` must report its path.
//
// Asserting test: prints the failures and exits 1 when anything is wrong.

var failures = [];

function check(name, condition, detail) {
    if (!condition) {
        failures.push(name + (detail !== undefined ? " — " + detail : ""));
    }
}

var resolved = require.resolve('./fixture');
check("require.resolve returns a string", typeof resolved === "string",
      "got " + typeof resolved);

var ext = typeof resolved === "string"
    ? resolved.slice(resolved.lastIndexOf('.'))
    : "";
var nativeExtensions = ['.node', '.so', '.dll', '.dylib'];
var isNative = nativeExtensions.indexOf(ext) >= 0;

check("the native addon is preferred over the sibling .js", isNative,
      "resolved to " + resolved);

var m = require('./fixture');
check("require loaded the native addon", m && m.type === "native",
      "type = " + (m && m.type));

if (failures.length) {
    console.log("test_resolution: " + failures.length + " check(s) failed");
    for (var f = 0; f < failures.length; f++) {
        console.log("  FAIL: " + failures[f]);
    }
    process.exit(1);
}
console.log("test_resolution: all checks passed");
