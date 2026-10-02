// Promise.all / allSettled / any / race with values settling at different
// times: synchronously, after a few jobs, and in later macrotasks.
const later = (v, fail) => new Promise((res, rej) => setImmediate(() => fail ? rej(v) : res(v)));
const afterJobs = (v) => Promise.resolve().then(() => v).then(x => x);
const out = [];
const done = [];
// Settled records print as status:value (property order is not compared).
function show(v) {
    if (Array.isArray(v)) return '[' + v.map(show).join(',') + ']';
    if (v && typeof v === 'object' && 'status' in v)
        return v.status + ':' + JSON.stringify(v.status === 'fulfilled' ? v.value : v.reason);
    return JSON.stringify(v);
}
// Engines word their own TypeError messages differently: report only the name.
function describe(e) {
    if (e instanceof TypeError) return e.name;
    if (e && e.message !== undefined)
        return e.name + ':' + e.message + (e.errors ? ' errors=' + JSON.stringify(e.errors) : '');
    return e;
}
function report(name, p) {
    done.push(p.then(v => out.push(name + ' fulfilled ' + show(v)),
                     e => out.push(name + ' rejected ' + describe(e))));
}
report('all', Promise.all([later('slow'), 1, afterJobs('jobs'), Promise.resolve('now')]));
report('all-reject', Promise.all([later('slow'), later(new Error('late failure'), true), afterJobs('x')]));
report('all-empty', Promise.all([]));
report('allSettled', Promise.allSettled([later('ok'), Promise.reject('no'), 3]));
report('any', Promise.any([Promise.reject(new Error('e1')), later('winner'), later('second')]));
report('any-none', Promise.any([Promise.reject('x'), later('y', true)]));
report('any-empty', Promise.any([]));
report('race', Promise.race([later('slow'), afterJobs('jobs')]));
report('race-reject', Promise.race([later('slow'), Promise.reject(new Error('fast failure'))]));
report('race-value', Promise.race([later('slow'), 'plain']));
report('all-iterable', Promise.all(new Set([1, Promise.resolve(2)])));
report('all-string', Promise.all('ab'));
report('all-not-iterable', Promise.all(5));
Promise.resolve().then(() => out.push('tick1')).then(() => out.push('tick2')).then(() => out.push('tick3'));
const wr = Promise.withResolvers();
report('withResolvers', wr.promise);
wr.resolve('resolved externally');
Promise.all(done).then(() => console.log(out.join('\n')));
