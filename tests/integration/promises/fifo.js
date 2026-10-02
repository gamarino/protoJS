// Jobs run first in, first out: three chains started in the same turn
// interleave one step at a time, and many independent reactions run in the
// order they were registered.
const out = [];
function chain(name, n) {
    let p = Promise.resolve(0);
    for (let i = 0; i < n; i++) {
        p = p.then(v => { out.push(name + (v + 1)); return v + 1; });
    }
    return p;
}
chain('a', 3);
chain('b', 3);
chain('c', 2);
const order = [];
for (let i = 0; i < 50; i++) {
    Promise.resolve(i).then(v => order.push(v));
}
Promise.resolve().then(() => out.push('single'));
setImmediate(() => {
    console.log(out.join(' '));
    let sorted = true;
    for (let i = 0; i < order.length; i++) if (order[i] !== i) sorted = false;
    console.log('fifo', order.length, sorted);
});
