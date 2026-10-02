// Edge cases: non-callable handlers, a settled promise ignores later calls,
// resolve function shape, iterator errors and closing in Promise.all, thenable
// returns, finally with await, async generator return through finally,
// rejected values in for await, and a then getter on a native promise.
const out = [];
const log = (...a) => out.push(a.join(' '));
Promise.resolve(1).then(null, 5).then(v => log('nc', v));
Promise.reject(2).then(undefined).catch(v => log('rc', v));
new Promise((res, rej) => { res('first'); rej('second'); throw new Error('ignored'); }).then(v => log('once', v));
let rf; new Promise(r => { rf = r; }); log('len', rf.length, JSON.stringify(rf.name), typeof rf);
const it = { [Symbol.iterator]() { return { next() { throw new Error('next threw'); } }; } };
Promise.all(it).catch(e => log('all-next', e.message));
let closed = false;
const it2 = { [Symbol.iterator]() { return { next() { return { value: 1, done: false }; }, return() { closed = true; return {}; } }; } };
const BadP = function (ex) { return new Promise(ex); }; BadP.resolve = function () { throw new Error('resolve threw'); };
Promise.all.call(BadP, it2).catch(e => log('closed', closed, e.message));
async function retThenable() { return { then(r) { r('thenable-ret'); } }; }
retThenable().then(v => log(v));
async function fin() { try { await 1; return 'try'; } finally { await 2; log('finally-await'); } }
fin().then(v => log('fin', v));
async function finOverride() { try { return 'a'; } finally { return 'b'; } }
finOverride().then(v => log('override', v));
async function* g() { try { yield 1; yield 2; } finally { await null; log('gen-finally'); } }
(async () => { const i = g(); log('g1', (await i.next()).value); const r = await i.return('R'); log('gret', r.value, r.done); })();
(async () => { try { for await (const x of [Promise.reject(new Error('rej-in-iter'))]) log('x', x); } catch (e) { log('caught', e.message); } })();
const thenGetter = Promise.resolve(); Object.defineProperty(thenGetter, 'then', { get() { log('then-getter'); return Promise.prototype.then; } });
Promise.resolve(thenGetter);
(async () => { await thenGetter; log('after-await-getter'); })();
log('sync-done');
setImmediate(() => setImmediate(() => console.log(out.join('\n'))));
