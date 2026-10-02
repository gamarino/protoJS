// A combinator whose capability's resolve throws when the iterator is done
// rejects its promise (IfAbruptRejectPromise) instead of throwing, and does
// not close the finished iterator.
var returnCount = 0, out = [];
var iter = {}; iter[Symbol.iterator] = function () { return { next() { return { done: true }; }, return() { returnCount += 1; return {}; } }; };
function P(executor) { return new Promise(function (_, reject) { executor(function () { throw new Error('resolve threw'); }, reject); }); }
P.resolve = function () { throw new Error('P.resolve'); };
for (const name of ['all', 'allSettled']) {
    try {
        Promise[name].call(P, iter).catch(e => out.push(name + ' rejected: ' + e.message));
        out.push(name + ' returned');
    } catch (e) { out.push(name + ' threw: ' + e.message); }
}
setImmediate(() => console.log(out.join('\n') + '\nreturnCount ' + returnCount));
