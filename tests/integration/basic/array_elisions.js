// Array literals with elisions (holes) and spread elements.
//
// ECMA-262 §13.2.4.1 ArrayAccumulation: an elision advances the next index
// without creating a property, so `[1,,3]` has length 3 and no own property
// "1". A spread element iterates its operand, and the array iterator yields
// `undefined` for a hole of the source, so the hole becomes a present element
// whose value is undefined: `1 in [...[1,,3]]` is true.
//
// QuickJS compiles the literal as OP_array_from for the leading run of plain
// elements and OP_define_field (with an integer atom) for every element after
// the first elision. protoJS stored those later elements as string-keyed
// attributes beside the native __elements__ list, while `length` is read from
// that list, so `[1,,3].length` was 1 and the tail was invisible to every
// consumer of the list.
//
// Asserting test: prints the failures and exits 1 when anything is wrong.

var failures = [];

function check(name, condition, detail) {
    if (!condition) {
        failures.push(name + (detail !== undefined ? " -- " + detail : ""));
    }
}

function own(a, i) { return Object.prototype.hasOwnProperty.call(a, i); }

// ---- Elision in the middle -------------------------------------------------

var a = [1, , 3];
check("[1,,3].length", a.length === 3, "length = " + a.length);
check("[1,,3]: 1 in a is false", (1 in a) === false);
check("[1,,3]: hasOwnProperty(1) is false", own(a, 1) === false);
check("[1,,3][1] is undefined", a[1] === undefined);
check("[1,,3][2]", a[2] === 3, "a[2] = " + a[2]);
check("[1,,3] keys", Object.keys(a).join() === "0,2", "keys = " + Object.keys(a).join());
check("[1,,3] JSON", JSON.stringify(a) === "[1,null,3]", JSON.stringify(a));
var visited = [];
a.forEach(function (v, i) { visited.push(i); });
check("[1,,3].forEach skips the hole", visited.join() === "0,2", "visited " + visited.join());

// ---- Leading elisions ------------------------------------------------------

var b = [, 1];
check("[,1].length", b.length === 2, "length = " + b.length);
check("[,1]: 0 in b is false", (0 in b) === false);
check("[,1][1]", b[1] === 1, "b[1] = " + b[1]);

var b2 = [, , 'x'];
check("[,,'x'].length", b2.length === 3, "length = " + b2.length);
check("[,,'x'][2]", b2[2] === 'x');
check("[,,'x']: no 0, no 1", !(0 in b2) && !(1 in b2));

// ---- Trailing elisions (the last comma is a trailing comma, not a hole) -----

var c = [1, ,];
check("[1,,].length", c.length === 2, "length = " + c.length);
check("[1,,]: 1 in c is false", (1 in c) === false);
var c2 = [1, 2, , ,];
check("[1,2,,,].length", c2.length === 4, "length = " + c2.length);
check("[1,2,,,]: no 2, no 3", !(2 in c2) && !(3 in c2));
var c3 = [, ,];
check("[,,].length", c3.length === 2, "length = " + c3.length);
check("[,,]: no 0", !(0 in c3));
var c4 = [1,];
check("[1,].length", c4.length === 1, "length = " + c4.length);

// ---- Several elisions ------------------------------------------------------

var d = [1, , , 4];
check("[1,,,4].length", d.length === 4, "length = " + d.length);
check("[1,,,4][3]", d[3] === 4, "d[3] = " + d[3]);
check("[1,,,4]: no 1, no 2", !(1 in d) && !(2 in d));
var d2 = [, 'a', , 'b', , ];
check("[,a,,b,,].length", d2.length === 5, "length = " + d2.length);
check("[,a,,b,,] keys", Object.keys(d2).join() === "1,3", "keys = " + Object.keys(d2).join());

// The array stays a normal array after the literal: writes, push and the
// prototype methods see the elements placed after the hole.
var e = [1, , 3];
e.push(4);
check("push after a hole", e.length === 4 && e[3] === 4, "length = " + e.length);
check("indexOf after a hole", e.indexOf(3) === 2, "indexOf(3) = " + e.indexOf(3));
check("map keeps the hole", e.map(function (x) { return x * 2; })[2] === 6);
check("slice after a hole", e.slice(2).join() === "3,4", "slice = " + e.slice(2).join());

// ---- Long literals: elements past the first 32 are emitted the same way ----

var big = [0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,
           26,27,28,29,30,31,32,33,34];
check("35-element literal length", big.length === 35, "length = " + big.length);
check("35-element literal [34]", big[34] === 34, "big[34] = " + big[34]);
var bigHole = [0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,
               26,27,28,29,30,31,32,,34];
check("long literal with a hole: length", bigHole.length === 35, "length = " + bigHole.length);
check("long literal with a hole: no 33", !(33 in bigHole));

// ---- Elements that evaluate to undefined are present, not holes -------------

var u;
var f = [u, 1];
check("[u,1]: 0 in f (u is undefined)", (0 in f) === true);
var f2 = [1, , u];
check("[1,,u]: 2 in f2", (2 in f2) === true && f2.length === 3, "length = " + f2.length);
check("[void 0]: 0 in", (0 in [void 0]) === true);

// ---- Spread of an array with holes ------------------------------------------

var s = [...[1, , 3]];
check("[...[1,,3]].length", s.length === 3, "length = " + s.length);
check("[...[1,,3]]: 1 in s is true", (1 in s) === true);
check("[...[1,,3]][1] is undefined", s[1] === undefined);
check("[...[1,,3]][2]", s[2] === 3);

var g = [0, ...[1, 2], , 5];
check("[0,...[1,2],,5].length", g.length === 5, "length = " + g.length);
check("[0,...[1,2],,5]: 3 in g is false", (3 in g) === false);
check("[0,...[1,2],,5][4]", g[4] === 5, "g[4] = " + g[4]);

var h = [, ...[7]];
check("[,...[7]].length", h.length === 2, "length = " + h.length);
check("[,...[7]]: 0 not in h", (0 in h) === false);
check("[,...[7]][1]", h[1] === 7);

var k = [...[1, ,], ,];
check("[...[1,,],,].length", k.length === 3, "length = " + k.length);
check("[...[1,,],,]: 1 in k (from spread)", (1 in k) === true);
check("[...[1,,],,]: 2 not in k (elision)", (2 in k) === false);

// ---- Result ---------------------------------------------------------------

if (failures.length) {
    console.log("array_elisions: " + failures.length + " check(s) failed");
    for (var i = 0; i < failures.length; i++) {
        console.log("  FAIL: " + failures[i]);
    }
    process.exit(1);
}
console.log("array_elisions: all checks passed");
