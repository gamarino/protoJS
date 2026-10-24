// A Deferred runs on a protoCore thread of the main thread's space, so what it
// returns is the value itself, never a copy: a structure the main thread
// built, returned by a Deferred, is the same object; a structure a Deferred
// built is the same object the Deferred also stored in a shared one. The
// process holds one ProtoSpace (protoCore.gcStats().liveSpaces).
//
// Deferred is protoJS-specific, so this file does not run under Node.js.
// Asserting test: prints the failures and exits 1 when anything is wrong.

const N = 100000;
const big = [];
for (let i = 0; i < N; i++) big.push({ i: i, s: 'v' + i });

const made = {};
let spacesInside = -1;
const fromMain = new Deferred(() => big);
const fromPool = new Deferred(() => {
    const a = [];
    for (let i = 0; i < N / 2; i++) a.push({ i: i, half: i / 2 });
    made.value = a;
    spacesInside = protoCore.gcStats().liveSpaces;
    return a;
});

let failures = 0;
function fail(msg) { if (failures++ < 10) console.log('FAIL ' + msg); }

Promise.all([fromMain, fromPool]).then(function (r) {
    if (r[0] !== big) fail('the structure built on the main thread came back as another object');
    if (r[1] !== made.value) fail('the structure built by the Deferred is not the one it stored');
    if (r[1].length !== N / 2 || r[1][N / 2 - 1].half !== (N / 2 - 1) / 2)
        fail('the Deferred\'s structure has the wrong contents');
    if (r[0][N - 1].s !== 'v' + (N - 1)) fail('the main structure has the wrong contents');
    const spaces = protoCore.gcStats().liveSpaces;
    if (spaces !== 1) fail('the process holds ' + spaces + ' ProtoSpaces, expected 1');
    if (spacesInside !== 1) fail('inside the Deferred: ' + spacesInside + ' ProtoSpaces, expected 1');
    if (failures) process.exit(1);
    console.log('deferred/result_is_shared: ' + N + ' + ' + (N / 2) + ' elements, no copy, 1 space');
}, function (e) { console.log('FAIL rejected: ' + e); process.exit(1); });
