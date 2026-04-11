// Manual test suite for array destructuring (Phase 15)

// Array destructuring — basic
const [a, b, c] = [10, 20, 30];
if (a !== 10) throw new Error("a should be 10, got: " + a);
if (b !== 20) throw new Error("b should be 20, got: " + b);
if (c !== 30) throw new Error("c should be 30, got: " + c);
console.log("PASS: basic array destructuring");

// Array destructuring with rest
const [first, ...rest] = [1, 2, 3, 4];
if (first !== 1) throw new Error("first should be 1, got: " + first);
if (rest.length !== 3) throw new Error("rest.length should be 3, got: " + rest.length);
if (rest[0] !== 2) throw new Error("rest[0] should be 2, got: " + rest[0]);
if (rest[1] !== 3) throw new Error("rest[1] should be 3, got: " + rest[1]);
if (rest[2] !== 4) throw new Error("rest[2] should be 4, got: " + rest[2]);
console.log("PASS: rest element destructuring");

// Array destructuring with skip (hole)
const [x, , z] = [7, 8, 9];
if (x !== 7) throw new Error("x should be 7, got: " + x);
if (z !== 9) throw new Error("z should be 9, got: " + z);
console.log("PASS: skip element destructuring");

// for-of with destructuring
const pairs = [[1, 'a'], [2, 'b'], [3, 'c']];
const results = [];
for (const [num, letter] of pairs) {
    results.push(num + letter);
}
const expected = ['1a', '2b', '3c'].join(',');
const actual = results.join(',');
if (actual !== expected) throw new Error("for-of dstr: expected " + expected + ", got: " + actual);
console.log("PASS: for-of with destructuring");

// Assignment destructuring
let p, q;
[p, q] = [100, 200];
if (p !== 100) throw new Error("p should be 100, got: " + p);
if (q !== 200) throw new Error("q should be 200, got: " + q);
console.log("PASS: assignment destructuring");

// Function parameter destructuring
function sum([x2, y2, z2]) { return x2 + y2 + z2; }
const s = sum([1, 2, 3]);
if (s !== 6) throw new Error("sum should be 6, got: " + s);
console.log("PASS: function parameter destructuring");

console.log("ALL TESTS PASSED");
