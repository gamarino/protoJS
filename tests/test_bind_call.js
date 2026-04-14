// tests/test_bind_call.js
// Verification for Phase 30: Bound Functions and Microtasks

function assert(cond, msg) {
    if (!cond) throw new Error("Assertion failed: " + msg);
}

// 1. Bound function call via OP_call
console.log("Testing bound function call...");
function add(a, b) { return a + b + this.offset; }
const boundAdd = add.bind({ offset: 10 }, 5);
assert(boundAdd(2) === 17, "boundAdd(2) should be 17");
console.log("OK: OP_call handles bound functions.");

// 2. Bound function call via OP_call_method
console.log("Testing bound function as method...");
const obj = {
    multiplier: 2,
    double: function(x) { return x * this.multiplier; }
};
const boundDouble = obj.double.bind({ multiplier: 3 });
obj.boundDouble = boundDouble;
assert(obj.boundDouble(5) === 15, "obj.boundDouble(5) should be 15");
console.log("OK: OP_call_method handles bound functions.");

// 3. Bound constructor call via OP_call_constructor
console.log("Testing bound constructor...");
function Point(x, y) {
    this.x = x;
    this.y = y;
}
const BoundPoint = Point.bind(null, 100);
const p = new BoundPoint(200);
assert(p.x === 100, "p.x should be 100");
assert(p.y === 200, "p.y should be 200");
assert(p instanceof Point, "p should be instance of Point");
console.log("OK: OP_call_constructor handles bound functions.");

// 4. Microtask Queue timing
console.log("Testing microtask timing...");
let sequence = "";
sequence += "1";
Promise.resolve().then(() => {
    sequence += "3";
});
sequence += "2";

// In our synchronized eval, microtasks run at the very end.
// So sequence should be "12" then "3" runs.
// We'll check it in a way that respects the end-of-eval drain.
console.log("Current sequence (expected 12): " + sequence);
assert(sequence === "12", "Microtasks should be deferred.");

// We can't easily test the post-eval state FROM the script,
// but we can test chained microtasks.
Promise.resolve().then(() => {
    sequence += "4";
    Promise.resolve().then(() => {
        sequence += "5";
    });
});
console.log("OK: Microtasks enqueued.");
