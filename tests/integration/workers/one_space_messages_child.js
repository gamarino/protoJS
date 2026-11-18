// Child of one_space_messages.js: echoes workerData, reports what it sees.
const wt = require('worker_threads');
const port = (typeof parentPort !== 'undefined') ? parentPort : wt.parentPort;
const data = (typeof workerData !== 'undefined') ? workerData : wt.workerData;
let cloneError = 'none';
try { port.postMessage(function () {}); } catch (e) { cloneError = e.name; }
// Symbols cannot be cloned, the well-known ones included.
const symbolErrors = [Symbol('s'), Symbol.iterator].map(function (s) {
    try { port.postMessage(s); return 'none'; } catch (e) { return e.name; }
}).join(',');
port.postMessage({ echo: data, spaces: protoCore.gcStats().liveSpaces,
                   isMainThread: wt.isMainThread(), cloneError: cloneError,
                   symbolErrors: symbolErrors });
