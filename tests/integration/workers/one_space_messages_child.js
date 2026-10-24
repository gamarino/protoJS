// Child of one_space_messages.js: echoes workerData, reports what it sees.
const wt = require('worker_threads');
const port = (typeof parentPort !== 'undefined') ? parentPort : wt.parentPort;
const data = (typeof workerData !== 'undefined') ? workerData : wt.workerData;
let cloneError = 'none';
try { port.postMessage(function () {}); } catch (e) { cloneError = e.name; }
port.postMessage({ echo: data, spaces: protoCore.gcStats().liveSpaces,
                   isMainThread: wt.isMainThread(), cloneError: cloneError });
