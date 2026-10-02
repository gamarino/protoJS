// Promise.prototype.finally: the callback receives no argument, the original
// outcome passes through, a throw or a rejected promise from the callback
// replaces it, and a returned promise is waited for.
const out = [];
Promise.resolve(1).finally(function () { out.push('f1 args=' + arguments.length); })
    .then(v => out.push('then1 ' + v));
Promise.reject(new Error('r')).finally(() => { out.push('f2'); })
    .catch(e => out.push('catch2 ' + e.message));
Promise.resolve(3).finally(() => { throw new Error('thrown'); })
    .catch(e => out.push('catch3 ' + e.message));
Promise.resolve(4).finally(() => Promise.reject(new Error('rejected in finally')))
    .catch(e => out.push('catch4 ' + e.message));
Promise.resolve(5).finally(() => new Promise(r => setImmediate(() => { out.push('waited'); r('ignored'); })))
    .then(v => out.push('then5 ' + v));
Promise.resolve(6).finally(() => 'ignored').then(v => out.push('then6 ' + v));
Promise.resolve().then(() => out.push('tick1')).then(() => out.push('tick2'))
    .then(() => out.push('tick3')).then(() => out.push('tick4'));
setImmediate(() => setImmediate(() => console.log(out.join('\n'))));
