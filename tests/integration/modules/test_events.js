// require('events') is the EventEmitter constructor, as in Node.js:
// module.exports = EventEmitter, with EventEmitter.EventEmitter = EventEmitter,
// so both `require('events')` and `require('events').EventEmitter` work. Before
// the fix require('events') returned a plain object holding the constructor,
// and once() was an alias of on(). Values were checked against Node.js.
//
// Asserting test: prints the failures and exits 1 when anything is wrong.

var failures = [];
function check(name, condition, detail) {
    if (!condition) failures.push(name + (detail !== undefined ? ' -- ' + detail : ''));
}

var EventEmitter = require('events');
check('module is a function', typeof EventEmitter === 'function');
check('EventEmitter.EventEmitter is the module', EventEmitter.EventEmitter === EventEmitter);
var EE2 = require('events').EventEmitter;
check('destructured form', EE2 === EventEmitter);

var e = new EventEmitter();
check('instance', e instanceof EventEmitter);

var seen = [];
function onA(x, y) { seen.push('a:' + x + ',' + y); }
check('on returns this', e.on('a', onA) === e);
check('emit returns true with a listener', e.emit('a', 1, 2) === true);
check('emit passes the arguments', seen.join('|') === 'a:1,2', seen.join('|'));
check('emit returns false without a listener', e.emit('nobody') === false);

var thisSeen = null;
e.on('t', function () { thisSeen = this; });
e.emit('t');
check('listener this is the emitter', thisSeen === e);

seen = [];
e.once('b', function (v) { seen.push('once:' + v); });
e.emit('b', 1);
e.emit('b', 2);
check('once runs once', seen.join('|') === 'once:1', seen.join('|'));
check('once listener removed', e.listenerCount('b') === 0, e.listenerCount('b'));

seen = [];
e.off('a', onA);
e.emit('a', 3, 4);
check('off removes the listener', seen.length === 0, seen.join('|'));

function onC() { seen.push('c'); }
e.on('c', onC);
e.on('c', onC);
check('listenerCount', e.listenerCount('c') === 2);
e.removeListener('c', onC);
check('removeListener removes one', e.listenerCount('c') === 1);
function onceD() { seen.push('d'); }
e.once('d', onceD);
e.off('d', onceD);
check('off removes a once() listener by its handler', e.listenerCount('d') === 0);
check('listeners() returns the handlers', e.listeners('c')[0] === onC);
e.removeAllListeners('c');
check('removeAllListeners(name)', e.listenerCount('c') === 0);

var order = [];
e.on('o', function () { order.push(1); });
e.on('o', function () { order.push(2); });
e.emit('o');
check('listeners run in order', order.join() === '1,2', order.join());

var threw = null;
try { e.emit('error', new Error('boom')); } catch (err) { threw = err; }
check("unhandled 'error' is thrown", threw && threw.message === 'boom');

class Sub extends EventEmitter {
    constructor() { super(); this.tag = 'sub'; }
}
var s = new Sub();
var got = null;
s.on('x', function (v) { got = this.tag + ':' + v; });
s.emit('x', 7);
check('subclass', s instanceof EventEmitter && got === 'sub:7', got);

if (failures.length) {
    console.log('FAIL test_events:\n  ' + failures.join('\n  '));
    process.exit(1);
}
console.log('test_events: all checks passed');
