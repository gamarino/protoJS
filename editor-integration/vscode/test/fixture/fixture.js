"use strict";
function twice(n) {
  const doubled = n * 2;
  return doubled;
}
function risky(n) {
  throw new Error("boom " + n);
}
console.log("e2e-before");
console.log(twice(21));
let r;
try { risky(7); } catch (e) { r = "caught " + e.message; }
console.log(r);
console.log("e2e-after");
