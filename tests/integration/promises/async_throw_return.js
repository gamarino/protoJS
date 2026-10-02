// A throw inside an async function rejects its promise -- before or after the
// first await -- and a return resolves it, adopting a returned promise.
async function throwsBefore() { throw new Error('before'); }
async function throwsAfter() { await 0; throw new Error('after'); }
async function returnsValue() { await 0; return 42; }
async function returnsPromise() { return Promise.resolve('adopted'); }
async function returnsNothing() { await 0; }
async function catches() {
    try { await Promise.reject(new Error('rejected')); }
    catch (e) { return 'caught ' + e.message; }
}
async function finallyRuns() {
    try { await 0; return 'try'; }
    finally { console.log('finally ran'); }
}
async function rethrows() {
    try { await Promise.reject(new TypeError('inner')); }
    catch (e) { throw new RangeError('outer from ' + e.message); }
}
let p;
try { p = throwsBefore(); console.log('throwsBefore did not throw synchronously'); }
catch (e) { console.log('WRONG: threw synchronously'); }
p.catch(e => console.log('throwsBefore rejected:', e.message));
throwsAfter().catch(e => console.log('throwsAfter rejected:', e.message));
returnsValue().then(v => console.log('returnsValue:', v));
returnsPromise().then(v => console.log('returnsPromise:', v));
returnsNothing().then(v => console.log('returnsNothing:', v));
catches().then(v => console.log('catches:', v));
finallyRuns().then(v => console.log('finallyRuns:', v));
rethrows().catch(e => console.log('rethrows:', e.name, e.message));
// Async arrows, methods, class methods and static methods.
const arrow = async (x) => { await 0; return x * 2; };
arrow(21).then(v => console.log('arrow:', v));
const obj = { base: 10, async m(x) { await 0; return this.base + x; } };
obj.m(5).then(v => console.log('method:', v));
class C {
    constructor() { this.k = 3; }
    async get(x) { await null; return this.k * x; }
    static async s() { await null; return 'static'; }
}
new C().get(7).then(v => console.log('class method:', v));
C.s().then(v => console.log('static method:', v));
// Awaiting a thenable calls its then.
async function awaitsThenable() {
    return await { then(r) { r('from thenable'); } };
}
awaitsThenable().then(v => console.log('thenable:', v));
// Parameter initialisers that throw reject the promise.
async function badParam(a = (() => { throw new Error('param'); })()) { return a; }
badParam().catch(e => console.log('badParam rejected:', e.message));
