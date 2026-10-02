// Child of promises_in_worker.js: promise reactions and awaits on the worker's
// own job queue, drained at the end of the worker's script.
// parentPort and workerData are globals in a protoJS worker.
const order = [];
async function work(n) {
    let sum = 0;
    for (let i = 1; i <= n; i++) sum += await Promise.resolve(i);
    return sum;
}
Promise.resolve().then(() => order.push('reaction'));
work(workerData).then(sum => {
    order.push('await');
    parentPort.postMessage({ sum: sum, order: order.join(',') });
});
order.push('sync');
