// Deferred runs its function in parallel, on a pool thread of the same space.
//
// `new Promise(executor)` runs on the calling thread; `new Deferred(fn)` runs
// fn on the Deferred pool and settles, on the calling thread, with fn's
// outcome. This test checks the parts of that contract a program can observe:
//
//   - fn runs on another thread (protoCore.threadId), and several Deferreds
//     run on several threads;
//   - results and exceptions are delivered, and a Deferred is a Promise
//     (then/catch/finally, await, the Promise combinators);
//   - objects are shared, not copied: a Deferred reads the caller's objects
//     and returns them with their identity, and the caller sees what it
//     returns;
//   - the realm's intrinsics are the same objects on every thread (null,
//     undefined, prototypes, Symbol.for, iterators, closures' captured cells);
//   - many Deferreds in flight all settle with their own values;
//   - inside fn, natives that schedule work on the owner's event loop throw,
//     and a nested Deferred runs inline.
//
// Asserting test: prints the failures and exits 1 when anything is wrong.

const failures = [];
function check(name, ok, detail) {
    if (!ok) failures.push(name + (detail !== undefined ? ' -- ' + detail : ''));
}

function spin(n) {
    let s = 0;
    for (let i = 0; i < n; i++) s = (s + i * 7) % 1000003;
    return s;
}

async function main() {
    const mainThread = protoCore.threadId();

    // ---- It runs on another thread --------------------------------------
    const where = await new Deferred(() => protoCore.threadId());
    check('fn runs on a thread other than the caller', where !== mainThread,
          'main ' + mainThread + ', fn ' + where);
    check('the caller is back on its own thread after await', protoCore.threadId() === mainThread);

    // Several busy Deferreds started together run on several threads. Each one
    // spins until it has seen another Deferred start, or gives up; the pool has
    // more than one thread on every machine with more than one CPU.
    const shared = { started: 0 };
    const ids = await Promise.all([0, 1, 2, 3].map(() => new Deferred(() => {
        shared.started = shared.started + 1;  // a racy counter is enough here
        let guard = 0;
        while (shared.started < 2 && guard < 20000000) guard++;
        return protoCore.threadId();
    })));
    const distinct = new Set(ids).size;
    check('concurrent Deferreds run on more than one thread', distinct > 1,
          'thread ids ' + ids.join(','));

    // ---- Results, exceptions, and the Promise protocol --------------------
    check('fulfils with the return value', (await new Deferred(() => 6 * 7)) === 42);
    try {
        await new Deferred(() => { throw new TypeError('bad'); });
        check('a throw rejects', false, 'no rejection');
    } catch (e) {
        check('a throw rejects with the thrown object', e instanceof TypeError && e.message === 'bad',
              String(e));
    }
    try {
        await new Deferred(() => { throw 7; });
        check('a primitive throw rejects', false);
    } catch (e) {
        check('a primitive throw rejects with the primitive', e === 7, String(e));
    }
    const d = new Deferred(() => 'v');
    check('a Deferred is a Deferred', d instanceof Deferred);
    check('a Deferred is a Promise', d instanceof Promise);
    check('Object.prototype.toString', Object.prototype.toString.call(d) === '[object Deferred]',
          Object.prototype.toString.call(d));
    check('then returns a Promise', d.then(() => {}) instanceof Promise);
    let finallyRan = false;
    check('finally passes the value through',
          (await d.finally(() => { finallyRan = true; })) === 'v' && finallyRan);
    check('chained then transforms the value',
          (await new Deferred(() => 2).then(x => x * 10).then(x => x + 1)) === 21);
    const mixed = await Promise.all([new Deferred(() => 1), Promise.resolve(2), 3,
                                     new Deferred(() => 4)]);
    check('Promise.all over Deferreds and Promises', mixed.join(',') === '1,2,3,4', mixed.join(','));
    const settled = await Promise.allSettled([new Deferred(() => { throw new Error('x'); }),
                                              new Deferred(() => 'y')]);
    check('Promise.allSettled', settled[0].status === 'rejected' && settled[0].reason.message === 'x' &&
          settled[1].status === 'fulfilled' && settled[1].value === 'y');
    check('Promise.any', (await Promise.any([new Deferred(() => { throw 1; }),
                                            new Deferred(() => 'ok')])) === 'ok');
    check('Promise.race', (await Promise.race([new Deferred(() => 'r')])) === 'r');
    check('a Deferred resolved with a Deferred adopts it',
          (await new Promise(r => r(new Deferred(() => 'adopted')))) === 'adopted');
    check('an async function as fn is awaited',
          (await new Deferred(async () => { const v = await Promise.resolve(5); return v + 1; })) === 6);

    // ---- Shared, not copied -----------------------------------------------
    const big = [];
    for (let i = 0; i < 20000; i++) big.push({ id: i, v: i % 13 });
    let expected = 0;
    for (const o of big) expected += o.v;
    const sums = await Promise.all([0, 1, 2, 3, 4, 5].map(() => new Deferred(() => {
        let s = 0;
        for (let i = 0; i < big.length; i++) s += big[i].v;
        return s;
    })));
    check('N Deferreds read the same captured array', sums.every(s => s === expected),
          sums.join(',') + ' expected ' + expected);
    const same = await new Deferred(() => big[123]);
    check('an object read by fn comes back with its identity (no copy)', same === big[123]);
    const made = await new Deferred(() => ({ list: [1, 2, 3], nested: { k: 'v' } }));
    check('an object made by fn is an ordinary object',
          Object.getPrototypeOf(made) === Object.prototype && Array.isArray(made.list) &&
          made.list.length === 3 && made.nested.k === 'v');
    const target = { hits: 0 };
    await new Deferred(() => { target.hits = 1; target.added = 'yes'; });
    check('a write by fn to a shared object is visible to the caller after it settles',
          target.hits === 1 && target.added === 'yes');

    // ---- One realm: the same intrinsics on every thread --------------------
    check('null is null in fn', (await new Deferred(() => null)) === null);
    check('undefined is undefined in fn', (await new Deferred(() => undefined)) === undefined);
    check('fn sees the caller\'s undefined', await new Deferred(() => big[999999] === undefined));
    check('Symbol.for is shared', (await new Deferred(() => Symbol.for('protojs.test'))) ===
          Symbol.for('protojs.test'));
    const iterProto = Object.getPrototypeOf([][Symbol.iterator]());
    check('%ArrayIteratorPrototype% is shared',
          (await new Deferred(() => Object.getPrototypeOf([1][Symbol.iterator]()))) === iterProto);
    // Wrapped in an array: Promise.prototype is itself a thenable, and a
    // promise resolved with it would try to adopt it.
    check('%Promise.prototype% is shared',
          (await new Deferred(() => [Object.getPrototypeOf(Promise.resolve(1))]))[0] === Promise.prototype);
    let counter = 10;
    const bump = () => { counter = counter + 1; return counter; };
    check('a closure made on the main thread works in fn (captured cells)',
          (await new Deferred(() => bump())) === 11 && counter === 11);
    check('a function\'s lazy prototype is one object',
          (await new Deferred(() => { function F() {} return F.prototype.constructor === F; })));
    function Point(x) { this.x = x; }
    const pt = await new Deferred(() => new Point(3));
    check('an instance made in fn has the caller\'s prototype', pt instanceof Point && pt.x === 3);
    class Box { constructor(v) { this.v = v; } get double() { return this.v * 2; } }
    check('class instances cross threads', (await new Deferred(() => new Box(4))).double === 8);
    check('BigInt in fn', (await new Deferred(() => 2n ** 70n)) === 2n ** 70n);
    check('JSON in fn', (await new Deferred(() => JSON.parse('{"a":[1,{"b":2}]}').a[1].b)) === 2);
    check('RegExp in fn', (await new Deferred(() => /b+(c)/.exec('abbbc')[1])) === 'c');
    check('Map and Set in fn', (await new Deferred(() => {
        const m = new Map([[1, 'a']]); const s = new Set([1, 2]);
        return m.get(1) + s.size + [...s].join('');
    })) === 'a212');

    // ---- Many in flight ----------------------------------------------------
    const many = [];
    for (let i = 0; i < 400; i++) many.push(new Deferred(() => i * 3 + spin(200)));
    const manyResults = await Promise.all(many);
    let manyOk = true;
    for (let i = 0; i < 400; i++) if (manyResults[i] !== i * 3 + spin(200)) { manyOk = false; break; }
    check('400 Deferreds in flight each settle with their own value', manyOk);

    // ---- Inside fn ---------------------------------------------------------
    try {
        await new Deferred(() => { setImmediate(() => {}); });
        check('setImmediate inside fn throws', false);
    } catch (e) {
        check('setImmediate inside fn throws', /not available inside a Deferred/.test(e.message), e.message);
    }
    try {
        await new Deferred(() => require('path'));
        check('require inside fn throws', false);
    } catch (e) {
        check('require inside fn throws', /not available inside a Deferred/.test(e.message), e.message);
    }
    const nested = await new Deferred(() => {
        const inner = new Deferred(() => protoCore.threadId());
        return inner.then(id => [id, protoCore.threadId()]);
    });
    check('a nested Deferred runs inline on the same pool thread', nested[0] === nested[1] &&
          nested[0] !== mainThread, String(nested));
    check('a non-callable argument throws TypeError synchronously', (() => {
        try { new Deferred(42); return false; } catch (e) { return e instanceof TypeError; }
    })());
}

main().then(() => {
    if (failures.length) {
        console.log('parallel_deferred: ' + failures.length + ' check(s) failed');
        for (const f of failures) console.log('  FAIL: ' + f);
        process.exit(1);
    }
    console.log('parallel_deferred: all checks passed');
}, (e) => {
    console.log('parallel_deferred: main threw ' + (e && e.stack || e));
    process.exit(1);
});
