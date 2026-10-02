// Async benchmark on pending promises: the work the old synchronous model
// could not do at all (a reaction registered on a pending promise never ran).
//
// 1,000 producers each resolve a pending promise from a setImmediate callback;
// each promise carries a chain of 20 then() reactions and is awaited by an
// async consumer. Reports and checks the total: a wrong result exits 1.
const P = 1000, CHAIN = 20;
let total = 0, consumers = 0;
const resolvers = [];
for (let i = 0; i < P; i++) {
    let r;
    const p = new Promise(res => { r = res; });
    resolvers.push(r);
    let c = p;
    for (let k = 0; k < CHAIN; k++) c = c.then(v => v + 1);
    (async () => { const v = await c; total += v; consumers++; })();
}
setImmediate(() => { for (let i = 0; i < P; i++) resolvers[i](i); });
setImmediate(() => setImmediate(() => {
    const expected = P * (P - 1) / 2 + P * CHAIN;
    if (total !== expected || consumers !== P) {
        console.log('pending_chains: FAIL total=' + total + ' consumers=' + consumers);
        process.exit(1);
    }
    console.log('pending_chains: total=' + total + ' (verified: ' + P + ' promises x ' + CHAIN + ' reactions)');
}));
