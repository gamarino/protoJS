// __filename and __dirname are absolute paths, as in Node, even when the
// script is run by a relative path: ctest runs this file as
// `protojs modules/dirname_absolute.js` from tests/integration.  Until
// 2026-10 they kept the path as typed ("modules"), so
// require(__dirname + "/x.js") took "modules/x.js" for a package name and
// loaded the wrong thing or failed (the structure benchmarks did, run from
// the repository root).  Asserting test: exits 1 on any failure.
var path = require("path");
var failures = [];
// Compared with "/" separators: on Windows the main script's paths use "\\"
// (std::filesystem) and the module loader's use "/".
function slash(p) { return String(p).replace(/\\/g, "/"); }
function check(name, got, expected) {
    if (got !== expected) failures.push(name + ": got " + String(got) + ", expected " + String(expected));
}
check("__dirname is absolute", path.isAbsolute(__dirname), true);
check("__filename is absolute", path.isAbsolute(__filename), true);
check("__filename is in __dirname", slash(__filename), slash(__dirname) + "/dirname_absolute.js");
check("__dirname ends with modules", path.basename(__dirname), "modules");
var lib = require(__dirname + "/dirname_absolute_lib.js");
check("require(__dirname + file)", lib.name, "dirname_absolute_lib");
check("the module's own __dirname", slash(lib.dirname), slash(__dirname));
if (failures.length) {
    for (var f of failures) console.log("FAIL " + f);
    process.exit(1);
}
console.log("dirname_absolute: all checks passed");
