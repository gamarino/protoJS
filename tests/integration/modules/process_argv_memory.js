// process.argv holds the script's own arguments, and process.memoryUsage()
// / resourceUsage() report memory with Node's field names. Run as
//   protojs process_argv_memory.js alpha "two words" --flag
// (registered so in tests/CMakeLists.txt; also passes under Node.js).
let failures = 0;
function check(name, cond) { if (!cond) { failures++; console.log("FAILED: " + name); } }

const argv = process.argv;
check("argv[1] is the script", typeof argv[1] === "string" && argv[1].endsWith("process_argv_memory.js"));
check("script arguments", JSON.stringify(argv.slice(2)) === JSON.stringify(["alpha", "two words", "--flag"]));

const before = process.memoryUsage();
const keep = [];
for (let i = 0; i < 50000; i++) keep.push({ i: i, s: "v" + i });
const after = process.memoryUsage();
for (const k of ["rss", "heapTotal", "heapUsed", "external", "arrayBuffers"])
    check("memoryUsage()." + k + " is a number", typeof after[k] === "number" && after[k] >= 0);
check("rss is positive", after.rss > 0);
check("heapUsed <= heapTotal", after.heapUsed <= after.heapTotal);
check("heapUsed grows with live objects", after.heapUsed > before.heapUsed);
const ru = process.resourceUsage();
check("resourceUsage().maxRSS in KiB", typeof ru.maxRSS === "number" && ru.maxRSS * 1024 >= after.rss * 0.5);
check("objects kept", keep.length === 50000 && keep[49999].s === "v49999");

if (failures === 0) console.log("process_argv_memory: all checks passed");
else { console.log("process_argv_memory: " + failures + " checks FAILED"); process.exit(1); }
