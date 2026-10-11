// Child of dictionary_keys_message.js: echoes the object it was given as
// workerData, with one more computed key of its own. parentPort and
// workerData are globals in a protoJS worker; Node.js has them in the module.
const wt = (typeof parentPort === 'undefined') ? require('worker_threads') : null;
const port = wt ? wt.parentPort : parentPort;
const m = wt ? wt.workerData : workerData;
const k = 'from-' + 'worker-key';
m[k] = Object.keys(m).length;
port.postMessage(m);
