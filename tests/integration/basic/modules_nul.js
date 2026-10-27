// Strings that the built-in modules make from native data keep a NUL byte:
// they were built from C strings and stopped at the first one (src/modules
// now uses the length-aware helpers of src/runtime/JSStringBytes.h).
// Expected values are Node.js 22's. Asserting test: exits 1 on any failure.

var fs = require("fs"), path = require("path");
var failures = [];
function check(name, got, expected) {
    if (got !== expected) failures.push(name + ": got " + JSON.stringify(got) + ", expected " + JSON.stringify(expected));
}
var file = path.join(__dirname, "modules_nul_" + process.pid + ".tmp");
fs.writeFileSync(file, "a\0b\0c");
try {
    var s = fs.readFileSync(file, "utf8");
    check("readFileSync length", s.length, 5);
    check("readFileSync content", s === "a\0b\0c", true);
} finally {
    fs.unlinkSync(file);
}
var b = Buffer.from([0x61, 0x00, 0x62]);
check("Buffer toString length", b.toString().length, 3);
check("Buffer toString content", b.toString() === "a\0b", true);
check("Buffer latin1", Buffer.from([0x00, 0xe9]).toString("latin1").length, 2);

if (failures.length) {
    for (var f of failures) console.log("FAIL " + f);
    process.exit(1);
}
console.log("modules_nul: all checks passed");
