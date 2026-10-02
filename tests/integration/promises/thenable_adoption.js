// Resolving a promise with a thenable takes extra jobs: a
// NewPromiseResolveThenableJob calls then(), and the thenable's own reaction
// settles the outer promise. Each step below is counted against a ticker.
let tick = 0;
const ticks = [];
function ticker(n) {
    let p = Promise.resolve();
    for (let i = 1; i <= n; i++) p = p.then(() => { tick = i; });
}
ticker(8);
const log = (what) => ticks.push(what + '@' + tick);

new Promise(r => r(Promise.resolve('p'))).then(v => log('resolve(promise) ' + v));
Promise.resolve().then(() => Promise.resolve('x')).then(v => log('return promise ' + v));
new Promise(r => r({ then(res) { log('thenable.then called'); res('t'); } }))
    .then(v => log('resolve(thenable) ' + v));
Promise.resolve('direct').then(v => log('direct ' + v));
const same = Promise.resolve('same');
console.log('Promise.resolve(promise) is identity:', Promise.resolve(same) === same);
// A thenable whose then getter throws rejects the promise.
const bad = {};
Object.defineProperty(bad, 'then', { get() { throw new Error('getter'); } });
new Promise(r => r(bad)).catch(e => log('then getter threw ' + e.message));
// Resolving a promise with itself is a TypeError.
let selfResolve;
const selfP = new Promise(r => { selfResolve = r; });
selfResolve(selfP);
selfP.catch(e => log('self resolution ' + (e instanceof TypeError)));
setImmediate(() => console.log(ticks.join('\n')));
