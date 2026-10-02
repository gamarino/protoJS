// How many jobs each construct takes before its continuation runs, counted
// against a ticker; Node's counts are the expectation (await of a native
// promise 1, of a thenable 2, returning a promise from an async function 3,
// finally 4, ...).
let tick = 0; const out = [];
(function t(n){ let p = Promise.resolve(); for (let i = 1; i <= n; i++) p = p.then(() => { tick = i; }); })(12);
const at = (s) => out.push(s + '@' + tick);
(async () => { await Promise.resolve(); at('await native'); })();
(async () => { await { then(r) { r(); } }; at('await thenable'); })();
(async () => { await 1; at('await value'); })();
(async () => { return Promise.resolve('x'); })().then(() => at('return promise'));
(async () => { return await Promise.resolve('x'); })().then(() => at('return await promise'));
(async () => { return 1; })().then(() => at('return value'));
class MyP extends Promise {}
(async () => { await MyP.resolve(1); at('await subclass'); })();
Promise.resolve().finally(() => {}).then(() => at('finally'));
Promise.reject(0).catch(() => {}).then(() => at('catch-then'));
Promise.all([1]).then(() => at('all1'));
Promise.race([1]).then(() => at('race1'));
Promise.any([1]).then(() => at('any1'));
Promise.allSettled([1]).then(() => at('allSettled1'));
async function* ag() { yield 1; } const gi = ag(); gi.next().then(() => at('asyncgen next'));
(async () => { for await (const v of [1]) at('for-await body'); at('for-await end'); })();
queueMicrotask(() => at('queueMicrotask'));
setImmediate(() => console.log(out.join('\n')));
