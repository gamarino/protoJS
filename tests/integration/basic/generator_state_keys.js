// A suspended generator keeps its pc, slots, operand-stack depth and open
// try ranges on the generator object (src/runtime/GeneratorFrame.h). The keys
// of that state are interned once per thread (genKey, genCatchKey); this
// checks every piece of the state across many yields, nested try ranges,
// interleaved generators and return / throw at a yield.
// Expected values are Node.js 22's. Asserting test: exits 1 on any failure.

var failures = [];
function check(name, got, expected) {
    if (got !== expected) failures.push(name + ": got " + JSON.stringify(got) + ", expected " + JSON.stringify(expected));
}
function* count(n) { for (var i = 0; i < n; i++) yield i; }
var s = 0, c = 0;
for (var x of count(100000)) { s += x; c++; }
check("sum of 100000 yields", s, 4999950000);
check("yields", c, 100000);

function* nested() {
    var log = [];
    try {
        try {
            try {
                log.push(yield "a");
                throw new Error("inner");
            } catch (e) {
                log.push(yield "b:" + e.message);
            } finally {
                log.push(yield "c");
            }
        } finally {
            log.push("f2");
        }
        yield log.join(",");
    } catch (e) {
        yield "outer:" + e;
    }
    return "done";
}
var g = nested(), out = [];
var r = g.next();
while (!r.done) { out.push(r.value); r = g.next(out.length); }
out.push(r.value);
check("nested try ranges", out.join("|"), "a|b:inner|c|1,2,3,f2|done");

var h = nested();
h.next(); h.next(1);
check("throw at a yield inside catch runs the finally", h.throw("T").value, "c");
check("then the throw reaches the outer catch", h.next().value, "outer:T");
var k = nested();
k.next();
var kr = k.return(7);
check("return at a yield inside finally", kr.value + ":" + kr.done, "c:false");
var kn = k.next();
check("then the return completes", kn.value + ":" + kn.done, "7:true");

// Two generators interleaved keep separate state.
var g1 = count(5), g2 = count(5), pairs = [];
for (var i = 0; i < 5; i++) pairs.push(g1.next().value + g2.next().value * 10);
check("interleaved", pairs.join(), "0,11,22,33,44");

// A generator that consumes another one.
function* outer() { for (var v of count(3)) { try { yield v; } finally { } } yield* count(2); }
check("generator in a generator", [...outer()].join(), "0,1,2,0,1");

if (failures.length) {
    for (var f of failures) console.log("FAIL " + f);
    process.exit(1);
}
console.log("generator_state_keys: all checks passed");
