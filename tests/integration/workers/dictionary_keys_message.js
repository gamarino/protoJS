// An object whose keys are data (computed at run time, so they live in its
// property dictionary, src/runtime/PropertyDictionary.h) crosses a
// worker_threads boundary both ways (workerData in, postMessage out) with every key and value, in
// insertion order.
//
// Asserting test: exits 1 when the echoed object differs.
'use strict';
const { Worker } = require('worker_threads');
// Key spellings are built, not written as literals: a literal is program
// vocabulary and stays a named attribute.
const EXTRA = 'from-' + 'worker-key';
const sent = {};
for (let i = 0; i < 300; i++) sent['message-key-' + i] = 'value-' + i;
const w = new Worker(__dirname + '/dictionary_keys_message_child.js', { workerData: sent });
let got = null;
w.on('message', function (m) { got = m; });
w.on('error', function (e) { console.log('FAIL worker error: ' + e); process.exit(1); });
w.on('exit', function () {
    const keys = got ? Object.keys(got) : [];
    const want = Object.keys(sent).concat([EXTRA]);
    let ok = got && keys.length === want.length && keys.every((k, i) => k === want[i]);
    for (let i = 0; ok && i < 300; i++) ok = got['message-key-' + i] === 'value-' + i;
    ok = ok && got[EXTRA] === 300;
    if (!ok) {
        console.log('FAIL got ' + JSON.stringify(got).slice(0, 300));
        process.exit(1);
    }
    console.log('workers/dictionary_keys_message: ' + keys.length + ' keys round-tripped');
});
