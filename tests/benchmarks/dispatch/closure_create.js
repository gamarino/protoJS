// Closure-creation benchmark: every iteration creates two closures and calls
// each once.
//
// Measures OP_fclosure -- the cost of building a function instance, including
// the function-table attribute every closure records (CHANGELOG, "Every closure
// records its function table"; docs/PERFORMANCE_NOTES.md). It reports and
// checks the value it computed: a wrong result exits 1.
function makeAdder(k) {
    return function (x) { return x + k; };
}

// Kept small: in protoJS a closure currently costs tens of microseconds and
// about 20 KB that the collector does not reclaim during the run
// (docs/PERFORMANCE_NOTES.md), so 1,000,000 iterations exhaust the memory of
// a 64 GB machine.
var N = 20000;
function run() {
    var acc = 0;
    for (var i = 0; i < N; i++) {
        var add = makeAdder(i & 7);          // a function expression
        var twice = (y) => y + y;            // an arrow function
        acc = (acc + add(1) + twice(1)) | 0;
    }
    return acc;
}

var r = run();
// Each block of 8 iterations adds (1+...+8) from add(1) and 8 * 2 from twice(1).
var expected = (N / 8) * (36 + 16);
if (r !== expected) {
    console.log("closure_create: FAIL, acc = " + r + " expected " + expected);
    process.exit(1);
}
console.log("closure_create: acc = " + r + " (verified, " + (2 * N) +
            " closures created and called)");
