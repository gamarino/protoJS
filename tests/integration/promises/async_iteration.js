// for await...of over sync iterables (values and promises) and async
// iterables, and async generators: requests are queued and served in order.
const out = [];
async function* gen() {
    out.push('gen start');
    yield 1;
    yield await Promise.resolve(2);
    try {
        yield 3;
    } finally {
        out.push('gen finally');
    }
    return 'ret';
}
async function main() {
    for await (const v of [1, Promise.resolve(2), 3]) out.push('sync-iterable ' + v);
    for await (const v of gen()) out.push('gen ' + v);
    const it = gen();
    const results = await Promise.all([it.next(), it.next(), it.next(), it.next(), it.next()]);
    out.push('queued ' + JSON.stringify(results));
    const it2 = gen();
    out.push('first ' + JSON.stringify(await it2.next()));
    out.push('return ' + JSON.stringify(await it2.return('early')));
    out.push('after return ' + JSON.stringify(await it2.next()));
    const it3 = gen();
    await it3.next();
    try { await it3.throw(new Error('thrown in')); } catch (e) { out.push('throw ' + e.message); }
    for await (const v of gen()) { out.push('break at ' + v); if (v === 1) break; }
    const custom = {
        [Symbol.asyncIterator]() {
            let i = 0;
            return { next() { i++; return Promise.resolve({ value: i * 10, done: i > 2 }); } };
        }
    };
    for await (const v of custom) out.push('custom ' + v);
    console.log(out.join('\n'));
}
main().catch(e => console.log('main failed', e && e.message));
console.log('typeof gen().next:', typeof gen().next, 'is promise:', gen().next() instanceof Promise);
