// Adapted from Node.js v22.20.0 test/parallel/test-path-posix-exists.js (MIT licence; see
// node_shim.js for the notice). Node's test harness is replaced by
// ./node_shim.js; every other change is marked 'protoJS:'.
'use strict';
const assert = require('./node_shim').assert;

assert.strictEqual(require('path/posix'), require('path').posix);

// protoJS: the node: prefix names the same module, as in Node.
assert.strictEqual(require('node:path/posix'), require('path').posix);

require('./node_shim').done('path/test-path-posix-exists');
