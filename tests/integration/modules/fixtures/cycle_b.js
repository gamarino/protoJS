// Fixture: the other half of the cycle. Required by cycle_a while cycle_a's
// own body is still running, so it sees cycle_a's partially-filled exports.
var a = require('./cycle_a.js');

exports.value = 'b-value';
// 'a-before' when the cycle is handled Node-style; undefined would mean the
// cache entry was published too late.
exports.seenA = a.before;
// Must be undefined: cycle_a has not reached that assignment yet.
exports.seenAfter = a.after;
