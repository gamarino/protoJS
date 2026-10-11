// Deferreds under a small heap budget: the controller takes over at the first
// Deferred, and the heap stays within the budget.
//
// Run with PROTOCORE_HEAP_LIMIT_CELLS=1000000 (64 MB of 64-byte cells; ctest
// sets it).  The first Deferred switches the space to protoCore's adaptive heap
// controller with that budget (docs/API_REFERENCE.md, "Memory"), and the tasks
// below allocate several times the budget in short-lived objects while keeping
// only a checksum each.  The protoCore heap (process.memoryUsage().heapTotal)
// must end within the budget, give or take the allocation batches the threads
// hold, and every task must compute the same checksum as a sequential run.

const budgetCells = Number(process.env.PROTOCORE_HEAP_LIMIT_CELLS || 0);
if (!budgetCells) {
    console.log("adaptive_heap_budget: PROTOCORE_HEAP_LIMIT_CELLS is not set");
    process.exit(1);
}

const TASKS = 8;
const ROUNDS = 40;
const PER_ROUND = 1000;

function work(seed) {
    let sum = 0;
    for (let r = 0; r < ROUNDS; r++) {
        const batch = [];
        for (let i = 0; i < PER_ROUND; i++)
            batch.push({ id: i, name: "item" + i, tags: [seed, r, i % 7] });
        for (const o of batch) sum = (sum + o.id + o.tags[2] + o.name.length) % 1000000007;
    }
    return sum;
}

// The checksum does not depend on the seed: one sequential run gives every
// task's expected value.
const before = protoCore.gcStats();
const reference = work(0);
const expected = [];
for (let t = 0; t < TASKS; t++) expected.push(reference);

const tasks = [];
for (let t = 0; t < TASKS; t++) tasks.push(new Deferred(() => work(t)));
const after = protoCore.gcStats();

Promise.all(tasks).then((results) => {
    const heapTotal = process.memoryUsage().heapTotal;
    const budgetBytes = budgetCells * 64;
    // Allocated: (TASKS + 1) * ROUNDS * PER_ROUND objects, each with an array
    // and a string, several cells apiece -- a multiple of the budget.
    const allocatedObjects = (TASKS + 1) * ROUNDS * PER_ROUND;
    console.log(JSON.stringify({
        budgetMB: Math.round(budgetBytes / 1e6),
        heapTotalMB: Math.round(heapTotal / 1e6),
        allocatedObjects,
        checksumsMatch: JSON.stringify(results) === JSON.stringify(expected),
    }));
    let ok = true;
    // The sequential part ran under the fixed limit; the first Deferred
    // switched the space to the controller, whose budget is the variable.
    if (before.adaptiveHeap !== false || after.adaptiveHeap !== true) {
        console.log("FAIL adaptiveHeap before/after the first Deferred: "
            + before.adaptiveHeap + "/" + after.adaptiveHeap + ", expected false/true");
        ok = false;
    }
    if (after.heapLimitCells > budgetCells) {
        console.log("FAIL heapLimitCells " + after.heapLimitCells + " above the budget " + budgetCells);
        ok = false;
    }
    if (JSON.stringify(results) !== JSON.stringify(expected)) {
        console.log("FAIL checksums differ: " + JSON.stringify(results) + " vs " + JSON.stringify(expected));
        ok = false;
    }
    // A quarter of the budget for the cells the threads hold in their batches.
    if (heapTotal > budgetBytes * 1.25) {
        console.log("FAIL heapTotal " + heapTotal + " exceeds the budget " + budgetBytes);
        ok = false;
    }
    if (!ok) process.exit(1);
    console.log("adaptive_heap_budget: OK");
});
