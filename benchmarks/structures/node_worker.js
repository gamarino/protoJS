// Node.js worker for node_bench.js MODE=par: receives the data by
// postMessage (a structured clone), runs one task, posts the result back.
const { parentPort, workerData } = require("worker_threads");
const { performance } = require("perf_hooks");
const wl = require(__dirname + "/workloads/" + workerData.workload + ".js");

parentPort.on("message", (msg) => {
    // The message event fires once the clone has been deserialized.
    const recvAt = performance.timeOrigin + performance.now();
    const t0 = performance.now();
    const result = wl.task(msg.data, msg.k);
    const computeMs = performance.now() - t0;
    parentPort.postMessage({ result, recvAt, computeMs });
});
parentPort.postMessage({ ready: true });
