// Copyright Joyent, Inc. and other Node contributors.
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the
// "Software"), to deal in the Software without restriction, including
// without limitation the rights to use, copy, modify, merge, publish,
// distribute, sublicense, and/or sell copies of the Software, and to permit
// persons to whom the Software is furnished to do so, subject to the
// following conditions:
//
// The above copyright notice and this permission notice shall be included
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN
// NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM,
// DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR
// OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE
// USE OR OTHER DEALINGS IN THE SOFTWARE.

// Adapted from Node.js v22.20.0 test/parallel/test-path.js (MIT licence; see
// node_shim.js for the notice). Node's test harness is replaced by
// ./node_shim.js; every other change is marked 'protoJS:'.
'use strict';
// protoJS: `fn(...args)` instead of `fn.apply(null, args)`: protoJS's native
// module functions do not inherit Function.prototype (no call/apply/bind).
const common = require('./node_shim').common;
const assert = require('./node_shim').assert;
// protoJS: assert.throws is redefined here so that its try statement is in
// the same module as the functions that throw. protoJS loses an exception
// thrown in one module and caught by a try statement in another (see
// node_shim.js); the checks are still the shim's (assert.checkThrown).
assert.throws = function(fn, expected) {
  let threw = false;
  let error;
  try {
    fn();
  } catch (e) {
    threw = true;
    error = e;
  }
  assert.checkThrown(threw, error, expected);
};
const path = require('path');

// Test thrown TypeErrors
const typeErrorTests = [true, false, 7, null, {}, undefined, [], NaN];

function fail(fn) {
  const args = Array.from(arguments).slice(1);

  assert.throws(() => {
    fn(...args);
  }, { code: 'ERR_INVALID_ARG_TYPE', name: 'TypeError' });
}

for (const test of typeErrorTests) {
  for (const namespace of [path.posix, path.win32]) {
    fail(namespace.join, test);
    fail(namespace.resolve, test);
    fail(namespace.normalize, test);
    fail(namespace.isAbsolute, test);
    fail(namespace.relative, test, 'foo');
    fail(namespace.relative, 'foo', test);
    fail(namespace.parse, test);
    fail(namespace.dirname, test);
    fail(namespace.basename, test);
    fail(namespace.extname, test);

    // Undefined is a valid value as the second argument to basename
    if (test !== undefined) {
      fail(namespace.basename, 'foo', test);
    }
  }
}

// path.sep tests
// windows
assert.strictEqual(path.win32.sep, '\\');
// posix
assert.strictEqual(path.posix.sep, '/');

// path.delimiter tests
// windows
assert.strictEqual(path.win32.delimiter, ';');
// posix
assert.strictEqual(path.posix.delimiter, ':');

if (common.isWindows)
  assert.strictEqual(path, path.win32);
else
  assert.strictEqual(path, path.posix);

require('./node_shim').done('path/test-path');
