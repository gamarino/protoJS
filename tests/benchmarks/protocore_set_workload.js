// A protoCore.Set-heavy workload: adds, membership tests with equal copies
// built at run time, and removals, over integers, long strings and boxed
// doubles. It prints the work it did and a result line that the runner
// checks, so a crash or a wrong answer cannot pass for a fast run.
//
// Usage: protojs protocore_set_workload.js [elements] [rounds]
// Prints: "protocore_set_workload: elements=E rounds=R ops=O hits=H size=S"
// Expected: hits = R * 3 * E, size = R * (3 * E - ceil(E / 2) * 3).

var E = Number(process.argv[2] || 20000);
var R = Number(process.argv[3] || 3);

function str(i) { return ["set-workload-key-", String(i)].join(""); }

var ops = 0, hits = 0, size = 0;
for (var r = 0; r < R; r++) {
    var s = new protoCore.Set();
    for (var i = 0; i < E; i++) {
        s.add(i);
        s.add(str(i));
        s.add(i + 0.5);
        ops += 3;
    }
    for (var i = 0; i < E; i++) {
        if (s.has(i)) hits++;
        if (s.has(str(i))) hits++;
        if (s.has((2 * i + 1) / 2)) hits++;
        ops += 3;
    }
    for (var i = 0; i < E; i += 2) {
        s.remove(i);
        s.remove(str(i));
        s.remove(i + 0.5);
        ops += 3;
    }
    size += s.size();
}
console.log("protocore_set_workload: elements=" + E + " rounds=" + R +
            " ops=" + ops + " hits=" + hits + " size=" + size);
