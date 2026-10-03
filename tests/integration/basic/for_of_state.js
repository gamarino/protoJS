// for-of loop state: each loop keeps its iterator in slots past its frame
// (forOfStateSlot in src/runtime/ProtoInterpreter.cpp). Nested loops,
// recursion, generators and async functions that suspend inside a loop must
// each see their own state; and a call to a function containing a for-of
// must stay cheap (the state used to live at slot 0x10000 + pc, so every
// such call allocated and filled a 512 KB slot array). Passes under Node.js.

let failures = 0;
let checks = 0;
function check(name, cond) {
    checks++;
    if (!cond) { failures++; console.log("FAILED: " + name); }
}

// Nested loops over arrays, a Map, a Set, a string and a generator.
function nested() {
    const out = [];
    for (const a of [1, 2]) {
        for (const [k, v] of new Map([["x", 10], ["y", 20]])) {
            for (const s of new Set([a * v])) {
                for (const ch of k) out.push(ch + s);
            }
        }
    }
    return out.join();
}
check("nested loops", nested() === "x10,y20,x20,y40");

// Recursion: every activation has its own loop state.
function sumTree(node) {
    let s = node.v;
    for (const c of node.children) s += sumTree(c);
    return s;
}
const tree = { v: 1, children: [{ v: 2, children: [{ v: 4, children: [] }] }, { v: 3, children: [] }] };
check("recursion", sumTree(tree) === 10);

// break, continue and return inside loops, and the loop after them.
function earlyExit(xs) {
    let seen = 0;
    for (const x of xs) {
        if (x === 2) continue;
        if (x === 5) break;
        seen += x;
    }
    for (const x of xs) if (x === 4) return seen * 100 + x;
    return -1;
}
check("break / continue / return", earlyExit([1, 2, 3, 4, 5, 6]) === 804);

// A generator suspended inside two nested loops.
function* pairs(xs, ys) {
    for (const x of xs) for (const y of ys) yield x + y;
}
const g1 = pairs(["a", "b"], ["1", "2"]);
const g2 = pairs(["c"], ["3", "4"]);
const interleaved = [g1.next().value, g2.next().value, g1.next().value, g2.next().value, g1.next().value, g1.next().value];
check("generators keep separate loop state", interleaved.join() === "a1,c3,a2,c4,b1,b2" && g1.next().done && g2.next().done);

// An async function awaiting inside a loop, two at once.
async function collect(tag, xs) {
    const out = [];
    for (const x of xs) {
        await null;
        out.push(tag + x);
    }
    return out.join("");
}

// Many calls of a function with a loop: linear and cheap.
function countIn(arr) {
    let n = 0;
    for (const x of arr) n += x;
    return n;
}
let total = 0;
const small = [1, 2, 3];
const t0 = Date.now();
for (let i = 0; i < 200000; i++) total += countIn(small);
const elapsed = Date.now() - t0;
check("200,000 calls computed", total === 1200000);

Promise.all([collect("p", [1, 2, 3]), collect("q", [4, 5])]).then(([a, b]) => {
    check("async functions keep separate loop state", a === "p1p2p3" && b === "q4q5");
    if (failures === 0) {
        console.log("for_of_state: all " + checks + " checks passed (200,000 calls in " + elapsed + " ms)");
    } else {
        console.log("for_of_state: " + failures + " of " + checks + " checks FAILED");
        if (typeof process !== "undefined") process.exit(1);
    }
});
