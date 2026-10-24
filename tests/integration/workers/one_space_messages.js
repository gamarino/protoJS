// A worker runs on a protoCore thread of the main thread's ProtoSpace. A
// message is the structured clone of the posted value, made in that one space:
// the receiver gets an equal value, never the sender's object; numbers keep
// their kind, undefined stays undefined, a value reached twice is cloned once
// (so a cycle survives); a function cannot be cloned (DataCloneError). Both
// sides see one ProtoSpace in the process.
//
// Asserting test: prints the failures and exits 1 when anything is wrong.
'use strict';
const { Worker } = require('worker_threads');

let failures = 0;
function fail(msg) { if (failures++ < 10) console.log('FAIL ' + msg); }

const shared = { tag: 'shared' };
const sent = { n: 1.5, i: 7, s: 'text', u: undefined, z: null, b: true,
               list: [1, 'two', { three: 3 }], a: shared, b2: shared };
sent.self = sent;

const w = new Worker(__dirname + '/one_space_messages_child.js', { workerData: sent });
let got = null;
w.on('message', function (m) { got = m; });
w.on('error', function (e) { fail('worker error: ' + e); });
w.on('exit', function () {
    if (!got) { fail('no message from the worker'); process.exit(1); }
    const echo = got.echo;
    if (echo === sent) fail('the echo is the sent object itself, not a clone');
    if (echo.n !== 1.5 || typeof echo.n !== 'number') fail('1.5 came back as ' + typeof echo.n + ' ' + echo.n);
    if (echo.i !== 7 || echo.s !== 'text' || echo.b !== true || echo.z !== null) fail('a primitive changed');
    if (!('u' in echo) || echo.u !== undefined) fail('undefined was not kept');
    if (echo.list.length !== 3 || echo.list[1] !== 'two' || echo.list[2].three !== 3) fail('the array changed');
    if (echo.self !== echo) fail('the cycle was not kept');
    if (echo.a !== echo.b2 || echo.a.tag !== 'shared') fail('the shared part was cloned twice');
    if (echo.a === shared) fail('the shared part is the sender\'s object');
    if (got.spaces !== 1) fail('the worker saw ' + got.spaces + ' ProtoSpaces, expected 1');
    if (got.isMainThread !== false) fail('isMainThread was ' + got.isMainThread + ' in the worker');
    if (got.cloneError !== 'DataCloneError') fail('posting a function threw ' + got.cloneError);
    const spaces = protoCore.gcStats().liveSpaces;
    if (spaces !== 1) fail('the main thread sees ' + spaces + ' ProtoSpaces, expected 1');
    try {
        w.postMessage(function () {});
        fail('posting a function to the worker did not throw');
    } catch (e) {
        if (e.name !== 'DataCloneError') fail('posting a function threw ' + e.name);
    }
    if (failures) process.exit(1);
    console.log('workers/one_space_messages: structured clone in one space');
});
