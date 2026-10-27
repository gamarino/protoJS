// A 100 KB string used the common ways fits a small fixed heap.
//
// tests/CMakeLists.txt runs this file with the adaptive heap controller off
// and a fixed PROTOCORE_HEAP_LIMIT_CELLS (the value and the measurements
// behind it are next to the test there). Before the fixes it covers:
//
//   * every match of a global replace / match / matchAll / exec loop stored a
//     new copy of the whole subject as the match's `input` (about 5,600 cells
//     per match for 100 KB): 5.6 M cells for the 1,000 matches below, about
//     114 M for a replace with a match per word;
//   * Array.from(string) wrote an index attribute and a __pd_<i>__
//     descriptor per character (about 12 M cells for 100 KB), and spreading
//     or destructuring a string built a {value, done} object per character;
//   * each String Iterator step, and each s[i], converted the whole string
//     to UTF-8, so a loop over a string was O(n^2) (seconds for 100 KB).
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

// Iteration.
var count = 0;
for (var ch of s) count++;
check("for-of", count, N);
var spread = [...s];
check("spread length", spread.length, N);
check("spread element", spread[N - 2], "d");
var from = Array.from(s);
check("Array.from length", from.length, N);
check("Array.from element", from[1], "o");
var mapped = Array.from(s, function (c, i) { return i === 0 ? c.toUpperCase() : c; });
check("Array.from with mapFn", mapped[0] + mapped[1], "Wo");
var [first, second, ...rest] = s;
check("destructuring", first + second + rest.length, "wo" + (N - 2));
spread = from = mapped = rest = null;

// Indexed reads.
var ws = 0;
for (var i = 0; i < N; i++) if (s[i] === "w") ws++;
check("s[i]", ws, N / 5);

// Global regular expressions. A match every 20 words (1,000 matches) keeps
// the run short: each match still converts the whole subject to UTF-16, a
// time cost this test does not cover.
var replaced = s.replace(/(word ){20}/g, "W");
check("replace /g", replaced, "W".repeat(1000));
replaced = null;
var matches = s.match(/(word ){20}/g);
check("match /g", matches.length, 1000);
matches = null;
var all = 0, inputOk = true;
for (var m of s.matchAll(/(word ){20}/g)) {
    all++;
    if (m.input !== s) inputOk = false;
}
check("matchAll", all, 1000);
check("matchAll input", inputOk, true);
var re = /(word ){20}/g, execs = 0, r;
while ((r = re.exec(s)) !== null) execs++;
check("exec loop", execs, 1000);

if (failures.length) {
    console.log("FAILED:\n  " + failures.join("\n  "));
    process.exit(1);
}
console.log("string_cells_bounded: all checks passed");
