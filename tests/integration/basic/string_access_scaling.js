// Reading a string one position at a time costs O(log n) per read, so a loop
// over an n-character string is O(n log n), not O(n^2):
//
//   * charCodeAt / codePointAt at any index (they converted the whole string
//     to UTF-16 on every call beyond index 0);
//   * Array.prototype methods called on a string (map, join, indexOf, ...:
//     every element read converted the whole string to UTF-8);
//   * a global RegExp walk (replace, match, matchAll, exec loops: every exec
//     converted the whole subject to UTF-16, and so did every empty match of
//     a matchAll walk).
//
// The check is a ratio, robust on a loaded CI machine and with a coarse
// clock (Windows ticks every 15.6 ms): each operation runs over a string of n
// and of 4n characters, repeated until at least 60 ms have passed, and the
// time per run is the best of three such samples. Linear work grows about 4x
// (5x with the log factor); quadratic work grows 16x. A ratio above 10 fails.
//
// The samples of n and 4n alternate, after one run over 4n: both sizes are
// timed with the heap at the same size and the collector in the same phase.
// Timing every n sample first let a heap growth or a collection that only the
// 4n samples met push a linear operation past the bound (16.6 on macOS, for
// Array.prototype.slice.call). A ratio above the bound is measured once more
// and fails only if it is reproduced; a quadratic path always is.
//
// Measured ratios (2026-10-29): 3.4-5.3 on Linux, 2.3-6.6 on the macOS runner
// (the latter under the default heap policy); a quadratic path gives about 16. The test runs under the adaptive heap
// controller (tests/CMakeLists.txt): under the default heap policy the heap
// size, and with it when and how long collections run, depends on the
// machine's memory.
// Asserting test: exits 1 on any failure.

var failures = [];
var N = 12000;

function sample(f, s) {
    var runs = 0, t0 = Date.now(), t;
    do { f(s); runs++; t = Date.now() - t0; } while (t < 60);
    return t / runs;
}

function measure(f, small, large) {
    f(large);
    var ts = Infinity, tl = Infinity;
    for (var r = 0; r < 3; r++) {
        ts = Math.min(ts, sample(f, small));
        tl = Math.min(tl, sample(f, large));
    }
    return { ts: ts, tl: tl, ratio: tl / ts };
}

function scales(name, f) {
    var small = "abcdefghij".repeat(N / 10);
    var large = "abcdefghij".repeat(4 * N / 10);
    var expectedSmall = f(small), expectedLarge = f(large);
    if (expectedLarge === expectedSmall && typeof expectedSmall === "number" && expectedSmall !== 0)
        failures.push(name + ": same result for n and 4n (" + expectedSmall + ")");
    var t0 = Date.now();
    var m = measure(f, small, large);
    var again = m.ratio > 10;
    if (again) m = measure(f, small, large);
    // Self-report: the figures behind each verdict, printed on success too.
    console.log(name + ": " + m.ts.toFixed(3) + " ms for n, " + m.tl.toFixed(3) + " ms for 4n, ratio "
        + m.ratio.toFixed(2) + (again ? " (measured twice)" : "") + ", " + (Date.now() - t0) + " ms in all");
    if (m.ratio > 10)
        failures.push(name + ": " + m.ts.toFixed(2) + " ms for n, " + m.tl.toFixed(2) + " ms for 4n (ratio " + m.ratio.toFixed(1) + ")");
}

scales("charCodeAt", function (s) { var x = 0; for (var i = 0; i < s.length; i++) x += s.charCodeAt(i); return x; });
scales("codePointAt", function (s) { var x = 0; for (var i = 0; i < s.length; i++) x += s.codePointAt(i); return x; });
scales("charAt", function (s) { var x = 0; for (var i = 0; i < s.length; i++) x += s.charAt(i) === "a" ? 1 : 0; return x; });
scales("at", function (s) { var x = 0; for (var i = 0; i < s.length; i++) x += s.at(i) === "b" ? 1 : 0; return x; });
scales("s[i]", function (s) { var x = 0; for (var i = 0; i < s.length; i++) x += s[i] === "c" ? 1 : 0; return x; });
scales("slice(i, i + 1)", function (s) { var x = 0; for (var i = 0; i < s.length; i++) x += s.slice(i, i + 1) === "d" ? 1 : 0; return x; });
// A String object reads its primitive value through the same view (it used
// to convert the whole string on every charAt / charCodeAt).
scales("new String(s).charAt", function (s) { var o = new String(s), x = 0; for (var i = 0; i < o.length; i++) x += o.charAt(i) === "a" ? 1 : 0; return x; });
scales("new String(s).charCodeAt", function (s) { var o = new String(s), x = 0; for (var i = 0; i < o.length; i++) x += o.charCodeAt(i); return x; });
scales("new String(s)[i]", function (s) { var o = new String(s), x = 0; for (var i = 0; i < o.length; i++) x += o[i] === "e" ? 1 : 0; return x; });
// The same reads over a string with supplementary characters.
scales("s[i] with pairs", function (s) { s = s + "\u{1F600}"; var x = 0; for (var i = 0; i < s.length; i++) x += s[i] === "f" ? 1 : 0; return x; });
scales("charCodeAt with pairs", function (s) { s = "\u{1F600}" + s; var x = 0; for (var i = 0; i < s.length; i++) x += s.charCodeAt(i); return x; });
scales("Array.prototype.map.call", function (s) { return Array.prototype.map.call(s, function (c) { return c; }).length; });
scales("Array.prototype.join.call", function (s) { return Array.prototype.join.call(s, "").length; });
scales("Array.prototype.indexOf.call", function (s) { return Array.prototype.indexOf.call(s, "z") + s.length; });
scales("Array.prototype.forEach.call", function (s) { var c = 0; Array.prototype.forEach.call(s, function () { c++; }); return c; });
scales("Array.prototype.reduce.call", function (s) { return Array.prototype.reduce.call(s, function (a) { return a + 1; }, 0); });
scales("Array.prototype.slice.call", function (s) { return Array.prototype.slice.call(s).length; });
scales("replace /g", function (s) { return s.replace(/a/g, "b").length + s.length; });
scales("replace /g with a function", function (s) { return s.replace(/e/g, function () { return "E"; }).length + s.length; });
scales("match /g", function (s) { return s.match(/a/g).length; });
scales("exec loop", function (s) { var re = /b/g, c = 0; while (re.exec(s)) c++; return c; });
scales("matchAll", function (s) { return [...s.matchAll(/c/g)].length; });
scales("matchAll with empty matches", function (s) { var c = 0; for (var m of s.matchAll(/x*/g)) c++; return c; });

// Loops that build a new string per iteration and read its length or a unit
// of it: the length of every new string must not cost a scan of it (the view
// of JSStringUnits.h is keyed by string, and each iteration makes another one).
scales("length after +=", function (s) { var t = "", n = s.length; while (t.length < n) t += "ab"; return t.length; });
scales("length after concat()", function (s) { var t = "", n = s.length; while (t.length < n) t = t.concat("ab"); return t.length; });
scales("length after template", function (s) { var t = "", n = s.length; while (t.length < n) t = `${t}ab`; return t.length; });
scales("length after prepend", function (s) { var t = "", n = s.length; while (t.length < n) t = "x" + t; return t.length; });
scales("length after slice(1)", function (s) { var t = s, k = 0; while (t.length > 0) { t = t.slice(1); k++; } return k; });
scales("length after substring", function (s) { var t = s, k = 0; while (t.length > 1) { t = t.substring(0, t.length - 1); k++; } return k; });
scales("length after += emoji", function (s) { var t = "", n = s.length; while (t.length < n) t += "\u{1F600}"; return t.length; });

// The fast paths keep the results.
var e = "a\u{1F600}b";
function check(name, got, expected) {
    if (got !== expected) failures.push(name + ": got " + JSON.stringify(got) + ", expected " + JSON.stringify(expected));
}
check("charCodeAt high surrogate", e.charCodeAt(1), 0xD83D);
check("charCodeAt low surrogate", e.charCodeAt(2), 0xDE00);
check("charCodeAt after the pair", e.charCodeAt(3), 98);
check("charCodeAt out of range", isNaN(e.charCodeAt(4)), true);
check("codePointAt on the pair", e.codePointAt(1), 0x1F600);
check("codePointAt on the low half", e.codePointAt(2), 0xDE00);
check("codePointAt out of range", e.codePointAt(4), undefined);
var longE = "x".repeat(50) + "\u{1F600}" + "y".repeat(50) + "\u{10FFFF}z";
check("long: before the pair", longE.charCodeAt(49), 120);
check("long: high", longE.charCodeAt(50), 0xD83D);
check("long: low", longE.charCodeAt(51), 0xDE00);
check("long: after", longE.charCodeAt(52), 121);
check("long: second pair", longE.codePointAt(102), 0x10FFFF);
check("long: last", longE.charCodeAt(104), 122);
check("map over a pair", Array.prototype.map.call(e, function (c) { return c.charCodeAt(0); }).join(), "97,55357,56832,98");
check("slice over a pair", Array.prototype.slice.call(e, 1, 3).join() === "\uD83D,\uDE00", true);
check("global replace result", "a-b-c".replace(/-/g, "+"), "a+b+c");
check("lastIndex after an exec walk", (function () { var re = /x/g; re.exec("axbx"); re.exec("axbx"); return re.lastIndex; })(), 4);

if (failures.length) {
    for (var f of failures) console.log("FAIL " + f);
    process.exit(1);
}
console.log("string_access_scaling: all checks passed");
