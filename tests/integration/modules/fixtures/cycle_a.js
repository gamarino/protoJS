// Fixture: half of a require cycle (cycle_a -> cycle_b -> cycle_a).
//
// The loader publishes the module record in require.cache BEFORE running the
// body, so when cycle_b requires this module back it receives the exports
// object as it stands at that moment rather than recursing until the stack
// runs out. Node behaves the same way, and the half-initialised view is why
// `exports.before` is visible to cycle_b while `exports.after` is not.
exports.before = 'a-before';

var b = require('./cycle_b.js');

exports.after = 'a-after';
exports.seenFromB = b.seenA;
exports.bValue = b.value;
