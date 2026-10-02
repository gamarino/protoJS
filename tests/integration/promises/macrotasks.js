// The microtask queue is drained after the script and after every macrotask,
// before the next macrotask runs.
const out = [];
setImmediate(() => {
    out.push('immediate 1');
    Promise.resolve().then(() => out.push('micro from immediate 1'))
        .then(() => out.push('micro chained from immediate 1'));
});
setImmediate(() => {
    out.push('immediate 2');
    Promise.resolve().then(() => {
        out.push('micro from immediate 2');
        setImmediate(() => {
            out.push('immediate 3');
            console.log(out.join('\n'));
        });
    });
});
Promise.resolve().then(() => out.push('micro from script'));
queueMicrotask(() => out.push('queueMicrotask from script'));
out.push('script end');
