// The reference ordering: synchronous code first, then promise reactions in
// the order they were queued, each a job of its own. A reaction registered on a
// pending promise runs once the promise is resolved; an async function that
// throws after an await rejects its promise instead of throwing to its caller.
const log = (...a) => console.log(...a);
log('1 sync start');
const p1 = new Promise(r => r('inmediata'));
p1.then(v => log('4 then p1:', v));
let resolveLater;
const p2 = new Promise(r => { resolveLater = r; });
p2.then(v => log('5 then p2 (resuelta despues):', v), e => log('rejected p2', e));
resolveLater('tarde');
async function f() { await null; throw new Error('boom'); }
f().then(() => log('f ok?'), e => log('6 f rechazada:', e.message));
Promise.resolve().then(() => log('3 microtask'));
log('2 sync end');
