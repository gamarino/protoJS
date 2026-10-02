// A generator that yields inside a for-of loop resumes the loop.
//
// for-of keeps its iterator state in interpreter slots outside the frame
// window a generator saved at a yield, so after the first resumption the loop
// found no iterator and ended: [...g()] gave only the first value.  The
// generator's snapshot now includes those slots (and so does an async
// function's continuation at an await).
//
// Asserting test: prints the failures and exits 1 when anything is wrong.
var failures = [];
function check(name, ok, detail) { if (!ok) failures.push(name + (detail !== undefined ? ' -- ' + detail : '')); }

function* overArray() { for (const x of [1, 2, 3]) yield x * 10; }
check('for-of over an array', [...overArray()].join() === '10,20,30', [...overArray()].join());

function* overSet() { for (const x of new Set(['a', 'b'])) { yield x; } }
var it = overSet();
var r = [it.next(), it.next(), it.next()].map(function (e) { return e.done ? 'done' : e.value; });
check('for-of over a Set', r.join() === 'a,b,done', r.join());

function* nested() {
    for (const a of [1, 2]) for (const b of ['x', 'y']) yield a + b;
}
check('nested for-of', [...nested()].join() === '1x,1y,2x,2y', [...nested()].join());

if (failures.length) {
    console.log('FAILED:\n  ' + failures.join('\n  '));
    process.exit(1);
}
console.log('generator_for_of_yield: all checks passed');
