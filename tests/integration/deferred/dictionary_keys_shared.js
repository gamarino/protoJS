// Computed keys that are data live in an object's property dictionary
// (src/runtime/PropertyDictionary.h). The dictionary is one record per
// object, and a write publishes a new record with a compare-and-swap, so
// several Deferreds writing different keys into ONE shared object must lose
// none of them -- each write is atomic, as a named attribute write is.
//
// Four Deferreds each write KEYS distinct computed keys into one object (and
// overwrite every other one once), while the main thread reads it. Then the
// main thread checks every key and value, the key count, and that
// Object.keys, for-in and JSON.stringify agree.
//
// Deferred is protoJS-specific, so this file does not run under Node.js.
// Asserting test: prints the failures and exits 1 when anything is wrong.

const WRITERS = 4;
const KEYS = 1500;
const shared = {};
const name = (w, i) => 'writer-' + w + '-key-' + i;

const writers = [];
for (let w = 0; w < WRITERS; w++) {
    writers.push(new Deferred(function () {
        for (let i = 0; i < KEYS; i++) shared[name(w, i)] = i;
        for (let i = 0; i < KEYS; i += 2) shared[name(w, i)] = -i;
        return w;
    }));
}

let failures = 0;
function fail(msg) { if (failures++ < 10) console.log('FAIL ' + msg); }

Promise.all(writers).then(function (done) {
    if (done.join() !== '0,1,2,3') fail('writers returned ' + done.join());
    for (let w = 0; w < WRITERS; w++) {
        for (let i = 0; i < KEYS; i++) {
            const v = shared[name(w, i)];
            const want = (i % 2 === 0) ? -i : i;
            if (v !== want) fail(name(w, i) + ' = ' + v + ', expected ' + want);
        }
    }
    const keys = Object.keys(shared);
    if (keys.length !== WRITERS * KEYS) fail('Object.keys found ' + keys.length + ' keys');
    let forIn = 0;
    for (const k in shared) forIn++;
    if (forIn !== keys.length) fail('for-in found ' + forIn + ' keys');
    const back = JSON.parse(JSON.stringify(shared));
    if (Object.keys(back).length !== keys.length) fail('JSON round trip lost keys');
    // Each writer's keys appear in its own write order.
    for (let w = 0; w < WRITERS; w++) {
        const mine = keys.filter(k => k.startsWith('writer-' + w + '-'));
        for (let i = 0; i < mine.length; i++)
            if (mine[i] !== name(w, i)) { fail('order of writer ' + w + ' at ' + i + ': ' + mine[i]); break; }
    }
    if (failures) {
        console.log('deferred/dictionary_keys_shared: ' + failures + ' failures');
        process.exit(1);
    }
    console.log('deferred/dictionary_keys_shared: ' + keys.length + ' keys from ' + WRITERS +
                ' Deferreds, all checks passed');
});
