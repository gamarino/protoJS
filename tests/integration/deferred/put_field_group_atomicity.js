// A run of writes to one object is published as one version
// (markPutFieldGroups / "Write groups" in src/runtime/ProtoInterpreter.cpp):
// other threads see all of it or none of it, and writers on different
// fields of the same object lose nothing.
//
// Three Deferreds each write their own group of three fields, `o.aN = k;
// o.bN = k; o.cN = k` for k = 1..ROUNDS, on ONE shared object, while two
// reader Deferreds take snapshots of it with Object.entries and check that
// the three fields of every group agree.  Written field by field, readers
// saw hundreds of partial groups per run; published as one version, none.
//
// Deferred is protoJS-specific, so this file does not run under Node.js.
// Asserting test: prints the failures and exits 1 when anything is wrong.

const ROUNDS = 20000;
const WRITERS = 3;
const o = { a0: 0, b0: 0, c0: 0, a1: 0, b1: 0, c1: 0, a2: 0, b2: 0, c2: 0 };
// One flag object per writer, so no two threads write the same object here.
const done = [{ v: false }, { v: false }, { v: false }];

function w0(t, k) { t.a0 = k; t.b0 = k; t.c0 = k; }
function w1(t, k) { t.a1 = k; t.b1 = k; t.c1 = k; }
function w2(t, k) { t.a2 = k; t.b2 = k; t.c2 = k; }

const writers = [w0, w1, w2].map((w, i) => new Deferred(function () {
    for (let k = 1; k <= ROUNDS; k++) w(o, k);
    done[i].v = true;
    return i;
}));

function reader() {
    let reads = 0, torn = 0, midway = 0, example = "";
    // Read until every writer is done, and at least 1000 times.
    while (reads < 1000 || !(done[0].v && done[1].v && done[2].v)) {
        const m = {};
        for (const [k, x] of Object.entries(o)) m[k] = x;
        reads++;
        for (let g = 0; g < WRITERS; g++) {
            const a = m["a" + g], b = m["b" + g], c = m["c" + g];
            if (a !== b || b !== c) {
                torn++;
                if (!example) example = "group " + g + ": " + a + "," + b + "," + c;
            } else if (a > 0 && a < ROUNDS) {
                midway++;
            }
        }
        if (reads > 5000000) break;  // a writer that never finishes
    }
    return { reads, torn, midway, example };
}
const readers = [new Deferred(reader), new Deferred(reader)];

Promise.all(writers.concat(readers)).then(function (results) {
    const failures = [];
    const r = results.slice(WRITERS);
    const torn = r[0].torn + r[1].torn;
    const midway = r[0].midway + r[1].midway;
    if (torn !== 0)
        failures.push(torn + " snapshots showed a partial group (" + (r[0].example || r[1].example) + ")");
    // The readers must have run while the writers did, or the check proved
    // nothing.
    if (midway === 0)
        failures.push("no snapshot was taken while the writers were running");
    for (let g = 0; g < WRITERS; g++)
        for (const f of ["a", "b", "c"])
            if (o[f + g] !== ROUNDS) failures.push("lost update: o." + f + g + " = " + o[f + g]);
    if (failures.length) {
        console.log("put_field_group_atomicity: " + failures.length + " check(s) failed");
        for (const f of failures) console.log("  FAIL: " + f);
        process.exit(1);
    }
    console.log("put_field_group_atomicity: all checks passed (" + (r[0].reads + r[1].reads)
                + " snapshots, " + midway + " taken mid-run)");
}, function (e) {
    console.log("put_field_group_atomicity: a Deferred rejected: " + e);
    process.exit(1);
});
