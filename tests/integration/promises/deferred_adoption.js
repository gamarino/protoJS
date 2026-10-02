// A Deferred is a thenable: `await` adopts it, the Promise combinators accept
// it, and Promise.resolve wraps it -- all through NewPromiseResolveThenableJob,
// which works for any object with a callable `then`.  Node has no Deferred, so
// this fixture asserts its results instead of comparing with Node's output.
const failures = [];
const check = (name, ok) => { if (!ok) failures.push(name); };

async function main() {
    check('await a Deferred', (await new Deferred(() => 21)) === 21);
    try {
        await new Deferred(() => { throw new Error('rejected deferred'); });
        check('await a rejecting Deferred throws', false);
    } catch (e) {
        check('await a rejecting Deferred throws', e instanceof Error && e.message === 'rejected deferred');
    }
    const all = await Promise.all([new Deferred(() => 1), Promise.resolve(2), 3]);
    check('Promise.all', all.length === 3 && all[0] === 1 && all[1] === 2 && all[2] === 3);
    check('Promise.race', (await Promise.race([new Deferred(() => 'd')])) === 'd');
    const settled = await Promise.allSettled([new Deferred(() => { throw new Error('x'); }),
                                              new Deferred(() => 'y')]);
    check('Promise.allSettled', settled[0].status === 'rejected' && settled[0].reason.message === 'x'
                                && settled[1].status === 'fulfilled' && settled[1].value === 'y');
    check('Promise.any', (await Promise.any([new Deferred(() => { throw 1; }), new Deferred(() => 'ok')])) === 'ok');
    const wrapped = Promise.resolve(new Deferred(() => 5));
    check('Promise.resolve wraps a Deferred in a Promise', wrapped instanceof Promise);
    check('Promise.resolve adopts the Deferred', (await wrapped) === 5);
    const chained = await new Promise(r => r(new Deferred(() => 'adopted')));
    check('resolve(deferred) adopts', chained === 'adopted');
}

main().then(() => {
    if (failures.length) { console.log('FAILED: ' + failures.join(', ')); process.exit(1); }
    console.log('deferred_adoption: all checks passed');
}, e => { console.log('FAILED: main threw ' + (e && e.message)); process.exit(1); });
