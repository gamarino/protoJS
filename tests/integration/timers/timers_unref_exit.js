// A process whose only pending timers are unreferenced ends at once, and an
// uncaught exception thrown by a timer callback ends the process with exit
// code 1 (Node).  Run by ctest twice: without an argument it must exit 0 in
// well under the unref'd timer's hour; with "throw" it must exit non-zero.
var t = setInterval(function () { console.log("FAIL: unref'd interval ran"); process.exit(1); }, 3600000);
t.unref();
if (process.argv[2] === "throw") {
    setTimeout(function () { throw new Error("thrown from a timer"); }, 1);
    setTimeout(function () { console.log("FAIL: ran after the uncaught exception"); }, 50);
}
