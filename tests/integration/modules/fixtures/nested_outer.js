// Fixture: requires a sibling module, so the nested specifier must resolve
// relative to THIS file rather than to the entry script.
var inner = require('./nested_inner.js');

module.exports = { v: inner.v * 2 };
