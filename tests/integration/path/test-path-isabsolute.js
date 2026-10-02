// Adapted from Node.js v22.20.0 test/parallel/test-path-isabsolute.js (MIT licence; see
// node_shim.js for the notice). Node's test harness is replaced by
// ./node_shim.js; every other change is marked 'protoJS:'.
'use strict';
const assert = require('./node_shim').assert;
const path = require('path');

assert.strictEqual(path.win32.isAbsolute('/'), true);
assert.strictEqual(path.win32.isAbsolute('//'), true);
assert.strictEqual(path.win32.isAbsolute('//server'), true);
assert.strictEqual(path.win32.isAbsolute('//server/file'), true);
assert.strictEqual(path.win32.isAbsolute('\\\\server\\file'), true);
assert.strictEqual(path.win32.isAbsolute('\\\\server'), true);
assert.strictEqual(path.win32.isAbsolute('\\\\'), true);
assert.strictEqual(path.win32.isAbsolute('c'), false);
assert.strictEqual(path.win32.isAbsolute('c:'), false);
assert.strictEqual(path.win32.isAbsolute('c:\\'), true);
assert.strictEqual(path.win32.isAbsolute('c:/'), true);
assert.strictEqual(path.win32.isAbsolute('c://'), true);
assert.strictEqual(path.win32.isAbsolute('C:/Users/'), true);
assert.strictEqual(path.win32.isAbsolute('C:\\Users\\'), true);
assert.strictEqual(path.win32.isAbsolute('C:cwd/another'), false);
assert.strictEqual(path.win32.isAbsolute('C:cwd\\another'), false);
assert.strictEqual(path.win32.isAbsolute('directory/directory'), false);
assert.strictEqual(path.win32.isAbsolute('directory\\directory'), false);

assert.strictEqual(path.posix.isAbsolute('/home/foo'), true);
assert.strictEqual(path.posix.isAbsolute('/home/foo/..'), true);
assert.strictEqual(path.posix.isAbsolute('bar/'), false);
assert.strictEqual(path.posix.isAbsolute('./baz'), false);

require('./node_shim').done('path/test-path-isabsolute');
