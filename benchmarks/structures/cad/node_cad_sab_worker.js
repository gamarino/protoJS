// Worker for node_cad_sab.js: the columns arrive as SharedArrayBuffers
// (shared, not copied); the plain fields of the message are cloned.
const { parentPort } = require("worker_threads");
const soa = require(__dirname + "/soa.js");
parentPort.on("message", (msg) => parentPort.postMessage(soa.task(msg.s, msg.k)));
parentPort.postMessage({ ready: true });
