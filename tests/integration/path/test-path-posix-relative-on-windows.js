// Adapted from Node.js v22.20.0 test/parallel/test-path-posix-relative-on-windows.js (MIT licence; see
// node_shim.js for the notice). Node's test harness is replaced by
// ./node_shim.js; every other change is marked 'protoJS:'.
'use strict';

const assert = require('./node_shim').assert;
const path = require('path');

// Refs: https://github.com/nodejs/node/issues/13683

const relativePath = path.posix.relative('a/b/c', '../../x');
assert.match(relativePath, /^(\.\.\/){3,5}x$/);

require('./node_shim').done('path/test-path-posix-relative-on-windows');
