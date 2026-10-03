// Allocation of a run of writes published as one version
// (markPutFieldGroups / "Write groups" in src/runtime/ProtoInterpreter.cpp).
//
// Each write to a mutable object publishes a new version of it into
// protoCore's mutable table: a new snapshot plus a path copy in the table,
// about 12 cells.  A constructor with five `this.x = ...` paid that five
// times (111 cells per construction measured before write groups); published
// as one group it pays it once (32).  Allocation is read from
// process.memoryUsage().heapUsed (protoCore heap minus its free list, in
// 64-byte cells); ctest runs this file with PROTOCORE_HEAP_LIMIT_CELLS high
// enough that no collection runs during a measurement, and the test refuses
// to conclude if one did.
//
// The limits are about half way between the per-write cost and the grouped
// one, so the test fails if the writes stop being grouped, not if an
// unrelated part of construction changes by a few cells.

const N = 50000;
let failures = 0;

function cellsPerOp(label, fn) {
    fn(100);  // load and warm up
    const cyclesBefore = protoCore.gcStats().cycles;
    const before = process.memoryUsage().heapUsed;
    fn(N);
    const after = process.memoryUsage().heapUsed;
    if (protoCore.gcStats().cycles !== cyclesBefore) {
        console.log("FAILED: " + label + ": a collection ran during the measurement; "
                    + "run with PROTOCORE_HEAP_LIMIT_CELLS=100000000");
        failures++;
        return NaN;
    }
    const cells = (after - before) / 64 / N;
    console.log(label + ": " + cells.toFixed(1) + " cells");
    return cells;
}

function expectBelow(label, cells, limit) {
    if (!(cells < limit)) {
        console.log("FAILED: " + label + ": " + cells.toFixed(1) + " cells, expected below " + limit);
        failures++;
    }
}

class P5 {
    constructor(i) { this.id = i; this.name = "n"; this.qty = i + 1; this.price = 2; this.flag = true; }
}
class V2 {
    constructor(x, y) { this.x = x; this.y = y; }
    move(dx, dy) { this.x = this.x + dx; this.y = this.y + dy; }
}
function bump(p) { p.x += 1; p.y += 1; }

let keep;
const c5 = cellsPerOp("new P5(i), five fields", n => { for (let i = 0; i < n; i++) keep = new P5(i); });
expectBelow("new P5(i)", c5, 70);
const v = new V2(0, 0);
const cm = cellsPerOp("v.move(1, 1), two fields", n => { for (let i = 0; i < n; i++) v.move(1, 1); });
expectBelow("v.move", cm, 17);
const cb = cellsPerOp("p.x += 1; p.y += 1", n => { for (let i = 0; i < n; i++) bump(v); });
expectBelow("p.x += 1; p.y += 1", cb, 17);
if (v.x !== 2 * N + 200 || v.y !== 2 * N + 200) {
    console.log("FAILED: wrong result " + v.x + "," + v.y);
    failures++;
}

if (failures) {
    console.log("put_field_group_cells: " + failures + " check(s) failed");
    process.exit(1);
}
console.log("put_field_group_cells: all checks passed");
