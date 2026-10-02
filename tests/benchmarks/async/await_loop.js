// Async-heavy benchmark: awaits and promise reactions.
//
// An async function awaits 200,000 values in a loop, a chain of 100,000 then()
// reactions runs on a resolved promise, and 2,000 async functions awaiting each
// other in a chain are started at once. Every part runs on promises that are
// already settled, so the program computes the same result under the old
// synchronous promise model (which ran reactions inside then() and unwrapped
// settled promises at an await) and under the job queue -- which is what
// makes a before/after measurement meaningful. It reports and checks the
// values it computed: a wrong result exits 1.
async function sumTo(n) {
    let s = 0;
    for (let i = 0; i < n; i++) s += await i;
    return s;
}

async function link(prev, i) {
    const v = await prev;
    return v + i;
}

async function main() {
    const N = 200000;
    const sum = await sumTo(N);
    let p = Promise.resolve(0);
    const M = 100000;
    for (let i = 0; i < M; i++) p = p.then(v => v + 1);
    const chained = await p;
    const K = 2000;
    let q = Promise.resolve(0);
    for (let i = 1; i <= K; i++) q = link(q, i);
    const linked = await q;
    return [sum, chained, linked];
}

main().then(([sum, chained, linked]) => {
    const expSum = 200000 * 199999 / 2, expLinked = 2000 * 2001 / 2;
    if (sum !== expSum || chained !== 100000 || linked !== expLinked) {
        console.log('await_loop: FAIL sum=' + sum + ' chained=' + chained + ' linked=' + linked);
        process.exit(1);
    }
    console.log('await_loop: sum=' + sum + ' chained=' + chained + ' linked=' + linked +
                ' (verified: 202000 awaits, 100000 reactions)');
}, e => { console.log('await_loop: FAIL ' + e); process.exit(1); });
