// Promise.try (ES2025) is not in Node 22, so this fixture asserts its result
// instead of comparing with Node's output: the callback runs synchronously with
// the extra arguments, its value fulfils the promise, a throw rejects it, and
// the outcome is observed only from a later job.
const failures = [];
const check = (name, ok) => { if (!ok) failures.push(name); };
let ranSync = false;
const p1 = Promise.try((a, b) => { ranSync = true; return a + b; }, 2, 3);
check('callback runs synchronously', ranSync);
check('returns a promise', p1 instanceof Promise);
let observed = false;
p1.then(v => { observed = true; check('value', v === 5); });
check('outcome not observed synchronously', !observed);
const p2 = Promise.try(() => { throw new Error('tried'); });
p2.catch(e => check('throw rejects', e.message === 'tried'));
const p3 = Promise.try(() => Promise.resolve('adopted'));
p3.then(v => check('returned promise adopted', v === 'adopted'));
setImmediate(() => {
    check('then ran', observed);
    if (failures.length) { console.log('FAILED: ' + failures.join(', ')); process.exit(1); }
    console.log('promise_try: all checks passed');
});
