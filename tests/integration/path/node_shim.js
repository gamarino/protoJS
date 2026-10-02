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

// The test-path-*.js files in this directory are Node.js's own tests of the
// `path` module (test/parallel/test-path-*.js, Node.js v22.20.0, MIT
// licence, notice above), adapted only where they reach for Node's test
// harness. This module stands in for that harness: `assert` (the subset the
// path tests use) and `common` (isWindows and invalidArgTypeHelper). Each
// test calls done() last, which prints how many assertions ran; the first
// failing assertion throws, so the process exits non-zero. Every file also
// runs unchanged under Node itself, which is how the adaptation is checked.
'use strict';

let count = 0;

function fail(message) {
  throw new Error('AssertionError: ' + message);
}

function show(v) {
  if (typeof v === 'string') return JSON.stringify(v);
  return String(v);
}

function assert(value, message) {
  count++;
  if (!value) fail(message || 'expected a truthy value, got ' + show(value));
}

assert.strictEqual = function(actual, expected, message) {
  count++;
  // Object.is semantics, as Node's strictEqual.
  const same = actual === expected ?
    (actual !== 0 || 1 / actual === 1 / expected) :
    (actual !== actual && expected !== expected);
  if (!same) {
    fail((message ? message + '\n  ' : '') +
         'expected ' + show(expected) + ', got ' + show(actual));
  }
};

function deepEqual(a, b) {
  if (a === b) return true;
  if (typeof a !== 'object' || typeof b !== 'object' || a === null || b === null)
    return false;
  if (Array.isArray(a) !== Array.isArray(b)) return false;
  const ka = Object.keys(a);
  const kb = Object.keys(b);
  if (ka.length !== kb.length) return false;
  for (let i = 0; i < ka.length; i++) {
    const k = ka[i];
    if (kb.indexOf(k) === -1) return false;
    if (!deepEqual(a[k], b[k])) return false;
  }
  return true;
}

assert.deepStrictEqual = function(actual, expected, message) {
  count++;
  if (!deepEqual(actual, expected)) {
    fail((message ? message + '\n  ' : '') + 'expected ' +
         JSON.stringify(expected) + ', got ' + JSON.stringify(actual));
  }
};

assert.match = function(string, regexp, message) {
  count++;
  if (!regexp.test(string)) fail(message || show(string) + ' does not match ' + regexp);
};

// assert.throws(fn, { code, name, message }): every listed property of the
// thrown error must be equal.
function checkThrown(threw, error, expected) {
  count++;
  if (!threw) fail('expected the function to throw');
  if (expected) {
    const keys = Object.keys(expected);
    for (let i = 0; i < keys.length; i++) {
      const k = keys[i];
      const got = error === null || error === undefined ? undefined : error[k];
      if (got !== expected[k]) {
        fail('thrown error .' + k + ': expected ' + show(expected[k]) +
             ', got ' + show(got));
      }
    }
  }
}

assert.throws = function(fn, expected) {
  let threw = false;
  let error;
  try {
    fn();
  } catch (e) {
    threw = true;
    error = e;
  }
  checkThrown(threw, error, expected);
};

const platform = typeof process.platform === 'function' ?
  process.platform() : process.platform;

// Node's test/common invalidArgTypeHelper: the " Received ..." tail of an
// ERR_INVALID_ARG_TYPE message, for the values these tests pass.
function invalidArgTypeHelper(input) {
  if (input == null) return ' Received ' + input;
  if (typeof input === 'function') return ' Received function ' + input.name;
  if (typeof input === 'object') {
    if (input.constructor && input.constructor.name)
      return ' Received an instance of ' + input.constructor.name;
    return ' Received ' + String(input);
  }
  let inspected = typeof input === 'string' ? "'" + input + "'" : String(input);
  if (inspected.length > 28) inspected = inspected.slice(0, 25) + '...';
  return ' Received type ' + typeof input + ' (' + inspected + ')';
}

const common = {
  isWindows: platform === 'win32',
  invalidArgTypeHelper: invalidArgTypeHelper,
};

function done(name) {
  console.log(name + ': ' + count + ' assertions passed');
}

module.exports = { assert: assert, common: common, done: done };
