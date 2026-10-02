// Adapted from Node.js v22.20.0 test/parallel/test-path-zero-length-strings.js (MIT licence; see
// node_shim.js for the notice). Node's test harness is replaced by
// ./node_shim.js; every other change is marked 'protoJS:'.
'use strict';

// These testcases are specific to one uncommon behavior in path module. Few
// of the functions in path module, treat '' strings as current working
// directory. This test makes sure that the behavior is intact between commits.
// See: https://github.com/nodejs/node/pull/2106

const assert = require('./node_shim').assert;
const path = require('path');
const pwd = process.cwd();

// Join will internally ignore all the zero-length strings and it will return
// '.' if the joined string is a zero-length string.
assert.strictEqual(path.posix.join(''), '.');
assert.strictEqual(path.posix.join('', ''), '.');
assert.strictEqual(path.win32.join(''), '.');
assert.strictEqual(path.win32.join('', ''), '.');
assert.strictEqual(path.join(pwd), pwd);
assert.strictEqual(path.join(pwd, ''), pwd);

// Normalize will return '.' if the input is a zero-length string
assert.strictEqual(path.posix.normalize(''), '.');
assert.strictEqual(path.win32.normalize(''), '.');
assert.strictEqual(path.normalize(pwd), pwd);

// Since '' is not a valid path in any of the common environments, return false
assert.strictEqual(path.posix.isAbsolute(''), false);
assert.strictEqual(path.win32.isAbsolute(''), false);

// Resolve, internally ignores all the zero-length strings and returns the
// current working directory
assert.strictEqual(path.resolve(''), pwd);
assert.strictEqual(path.resolve('', ''), pwd);

// Relative, internally calls resolve. So, '' is actually the current directory
assert.strictEqual(path.relative('', pwd), '');
assert.strictEqual(path.relative(pwd, ''), '');
assert.strictEqual(path.relative(pwd, pwd), '');

require('./node_shim').done('path/test-path-zero-length-strings');
