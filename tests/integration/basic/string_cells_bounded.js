// A 100 KB string used the common ways fits a small fixed heap.
//
// tests/CMakeLists.txt runs this file with the adaptive heap controller off
// and PROTOCORE_HEAP_LIMIT_CELLS=6000000. Smallest fixed heap per path on
// Linux (bisection, three runs per probe; `protojs this-file.js <path>` runs
// one path): spread and arrayFrom 2.66 M cells (the 100,000-element list's
// own build), destructure 0.33 M, every other path below 1,000. Before the
// fixes, at the same limit:
//
//   * arrayFrom and arrayFromMapped ran out of memory: Array.from wrote an
//     index attribute and a __pd_<i>__ descriptor per character (12.35 M
//     cells for 100 KB);
//   * replaceGlobal and matchGlobal ran out of memory: every match stored a
//     new copy of the whole subject as its `input` (about 5,600 cells per
//     match, 11 M for the 2,000 matches here);
//   * the other paths fitted; their cost was time: a String Iterator step and
//     s[i] converted the whole string to UTF-8 (O(n^2) loops, about 12 s each
//     for 100 KB), and spread built a {value, done} object per character
//     (5.63 M cells, just under the limit).
//
// Asserting test: prints the failures and exits 1 when anything is wrong.

var failures = [];
function check(name, got, expected) {
    if (got !== expected)
        failures.push(name + ": got " + JSON.stringify(got) + ", expected " + JSON.stringify(expected));
}

var N = 100000;
// toUpperCase().toLowerCase() makes a flat rope of distinct leaves; repeat()
// alone shares one subtree and would understate the string's size.
var s = "word ".repeat(N / 5).toUpperCase().toLowerCase();
check("length", s.length, N);

// Each path runs in its own function, so what it built is garbage once it
// returns and the heap limit bounds one path at a time.
function iterate() {
    var count = 0;
    for (var ch of s) count++;
    check("for-of", count, N);
}
function spread() {
    var a = [...s];
    check("spread length", a.length, N);
    check("spread element", a[N - 2], "d");
}
function arrayFrom() {
    var a = Array.from(s);
    check("Array.from length", a.length, N);
    check("Array.from element", a[1], "o");
}
function arrayFromMapped() {
    var a = Array.from(s, function (c, i) { return i === 0 ? c.toUpperCase() : c; });
    check("Array.from with mapFn", a[0] + a[1], "Wo");
}
function destructure() {
    var [first, second, ...rest] = s;
    check("destructuring", first + second + rest.length, "wo" + (N - 2));
}
function index() {
    var ws = 0;
    for (var i = 0; i < N; i++) if (s[i] === "w") ws++;
    check("s[i]", ws, N / 5);
}
// Global regular expressions. A match every 10 words (2,000 matches) keeps
// the run short: each match still converts the whole subject to UTF-16, a
// time cost this test does not cover.
function replaceGlobal() {
    check("replace /g", s.replace(/(word ){10}/g, "W"), "W".repeat(2000));
}
function matchGlobal() {
    check("match /g", s.match(/(word ){10}/g).length, 2000);
}
function matchAll() {
    var all = 0, inputOk = true;
    for (var m of s.matchAll(/(word ){10}/g)) {
        all++;
        if (m.input !== s) inputOk = false;
    }
    check("matchAll", all, 2000);
    check("matchAll input", inputOk, true);
}
function execLoop() {
    var re = /(word ){10}/g, execs = 0;
    while (re.exec(s) !== null) execs++;
    check("exec loop", execs, 2000);
}

var paths = [iterate, spread, arrayFrom, arrayFromMapped, destructure, index,
             replaceGlobal, matchGlobal, matchAll, execLoop];
var only = process.argv.length > 2 ? process.argv[2] : null;  // one path, for measuring
for (var p = 0; p < paths.length; p++)
    if (!only || paths[p].name === only) paths[p]();

if (failures.length) {
    console.log("FAILED:\n  " + failures.join("\n  "));
    process.exit(1);
}
console.log("string_cells_bounded: all checks passed");
