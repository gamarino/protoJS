// await suspends the function and resumes it from a job: two async functions
// started in the same turn interleave, the caller continues before either
// body resumes, and each await of a plain value costs one job.
const out = [];
async function a() {
    out.push('a1');
    await undefined;
    out.push('a2');
    await 'x';
    out.push('a3');
    return 'A';
}
async function b() {
    out.push('b1');
    await null;
    out.push('b2');
    await Promise.resolve();
    out.push('b3');
    return 'B';
}
const pa = a();
const pb = b();
out.push('sync');
Promise.resolve().then(() => out.push('t1')).then(() => out.push('t2')).then(() => out.push('t3'));
Promise.all([pa, pb]).then(v => {
    out.push('all ' + v.join(','));
    console.log(out.join(' '));
});
console.log('a() returns a promise:', pa instanceof Promise);
// Locals, arguments and closures survive the suspension.
async function keep(x, y) {
    let local = x + y;
    const f = () => local;
    for (let i = 0; i < 3; i++) {
        await i;
        local += i;
    }
    return [local, f(), arguments.length].join('/');
}
keep(1, 2).then(v => console.log('keep', v));
// Nested awaits in expressions.
async function expr() {
    const v = (await 1) + (await Promise.resolve(2)) * (await 3);
    return v;
}
expr().then(v => console.log('expr', v));
