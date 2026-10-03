// Worker for node_cad.js VARIANT=workers: the model arrives by postMessage.
const { parentPort } = require("worker_threads");
const { performance } = require("perf_hooks");
const m = require(__dirname + "/model.js");
parentPort.on("message", (msg) => {
    const recvAt = performance.timeOrigin + performance.now();
    const t0 = performance.now();
    const result = m.task(msg.model, msg.k);
    parentPort.postMessage({ result, recvAt, computeMs: performance.now() - t0 });
});
parentPort.postMessage({ ready: true });
