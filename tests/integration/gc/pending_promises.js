// GC safety: promise reactions, queued jobs and suspended async functions.
//
// Everything a pending promise will need when it settles -- its reactions, the
// handlers and derived promises they name, the jobs queued for them, and the
// frame of an async function suspended at an await -- lives only in the job
// queue and in the promises themselves. None of it is reachable from the
// script's variables. If the collector cannot see it, a reaction runs a freed
// handler, a resumed async function finds its locals gone, or a job is lost and
// the sums below come out wrong.
//
// Thousands of pending promises are created, the program churns garbage across
// many macrotasks while they wait, and they are then settled in batches. Run by
// tests/cli/gc-stress.sh under PROTOCORE_HEAP_LIMIT_CELLS; also passes
// standalone. Prints "ok pending_promises" or the failures and exits 1.

var failures = 0;
function fail(msg) { if (failures++ < 5) console.log('FAIL ' + msg); }

var N = 3000;
var resolvers = [];
var thenSum = 0, awaitSum = 0, chainSum = 0, catchSum = 0, settled = 0;

function churn(n) {
    var junk = [];
    for (var i = 0; i < n; i++) junk.push({ k: 'garbage' + i, v: [i, i + 1] });
    return junk.length;
}

async function waiter(i, p) {
    var local = { index: i, label: 'waiter' + i };
    var v = await p;
    churn(10);
    var w = await Promise.resolve(v * 2);
    if (local.label !== 'waiter' + i) fail('local lost in waiter ' + i);
    awaitSum += w + local.index;
    settled++;
}

for (var i = 0; i < N; i++) {
    (function (i) {
        var resolve, reject;
        var p = new Promise(function (res, rej) { resolve = res; reject = rej; });
        resolvers.push(i % 7 === 0 ? function () { reject(new Error('r' + i)); }
                                   : function () { resolve(i); });
        if (i % 7 === 0) {
            p.catch(function (e) { catchSum += Number(e.message.slice(1)); settled++; });
        } else {
            p.then(function (v) { thenSum += v; settled++; });
            p.then(function (v) { return v + 1; })
             .then(function (v) { return { x: v }; })
             .then(function (o) { chainSum += o.x; settled++; });
            waiter(i, p);
        }
    })(i);
}

var expectedThen = 0, expectedCatch = 0, expectedChain = 0, expectedAwait = 0, expectedSettled = 0;
for (var i = 0; i < N; i++) {
    if (i % 7 === 0) { expectedCatch += i; expectedSettled += 1; }
    else {
        expectedThen += i; expectedChain += i + 1; expectedAwait += 2 * i + i;
        expectedSettled += 3;
    }
}

var round = 0;
function step() {
    churn(2000);
    if (round < 10) { round++; setImmediate(step); return; }
    // Settle in batches, one macrotask each, with garbage in between.
    var start = (round - 10) * 500;
    for (var i = start; i < start + 500 && i < N; i++) resolvers[i]();
    round++;
    if (start + 500 < N) { setImmediate(step); return; }
    setImmediate(function () { setImmediate(finish); });
}
function finish() {
    if (thenSum !== expectedThen) fail('then sum ' + thenSum + ' != ' + expectedThen);
    if (catchSum !== expectedCatch) fail('catch sum ' + catchSum + ' != ' + expectedCatch);
    if (chainSum !== expectedChain) fail('chain sum ' + chainSum + ' != ' + expectedChain);
    if (awaitSum !== expectedAwait) fail('await sum ' + awaitSum + ' != ' + expectedAwait);
    if (settled !== expectedSettled) fail('settled ' + settled + ' != ' + expectedSettled);
    if (failures) process.exit(1);
    console.log('ok pending_promises');
}
setImmediate(step);
