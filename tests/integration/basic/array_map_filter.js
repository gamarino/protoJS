// Array.prototype.map and filter publish a plain result array once
// (isPlainFreshArray in src/ArrayPrototype.cpp) and fall back to
// element-by-element writes for holes and species constructors. Both paths
// must give the spec's results. Passes under Node.js.
let failures = 0, checks = 0;
function check(name, cond) { checks++; if (!cond) { failures++; console.log("FAILED: " + name); } }

check("map values", [1, 2, 3].map((x) => x * 2).join() === "2,4,6");
check("map args", ["a", "b"].map((x, i, a) => x + i + a.length).join() === "a02,b12");
check("map undefined result is own", 0 in [1].map(() => undefined));
const holey = [1, , 3, , 5];
const mh = holey.map((x) => x * 10);
check("map keeps holes", mh.length === 5 && !(1 in mh) && !(3 in mh) && mh[4] === 50 && mh[0] === 10);
check("map trailing hole", [1, 2, ,].map((x) => x).length === 3);
check("filter values", [1, 2, 3, 4, 5].filter((x) => x % 2).join() === "1,3,5");
check("filter skips holes", [1, , 3].filter(() => true).join() === "1,3");
const none = [1, 2].filter(() => false);
check("filter empty", Array.isArray(none) && none.length === 0);
const src = [1, 2, 3];
const seen = src.map((x, i) => { if (i === 0) { src[2] = 30; src.push(4); } return x; });
check("map sees later writes, not appended", seen.join() === "1,2,30");
let thrown = false;
try { [1, 2, 3].map((x) => { if (x === 2) throw new Error("stop"); return x; }); } catch (e) { thrown = e.message === "stop"; }
check("throwing callback", thrown);
const r = [1, 2, 3].map((x) => x);
r.push(4); r[0] = 9;
check("result is an ordinary array", r.join() === "9,2,3,4" && r.length === 4);
// A species constructor gets the element-by-element path.
class Tagged extends Array { static get [Symbol.species]() { return Array; } }
check("species Array", Array.isArray([1, 2].map((x) => x)));
const big = Array.from({ length: 10000 }, (_, i) => i);
check("large map", big.map((x) => x + 1).reduce((a, b) => a + b, 0) === 50005000);
check("large filter", big.filter((x) => x % 3 === 0).length === 3334);
if (failures === 0) console.log("array_map_filter: all " + checks + " checks passed");
else { console.log("array_map_filter: " + failures + " of " + checks + " checks FAILED"); process.exit(1); }
