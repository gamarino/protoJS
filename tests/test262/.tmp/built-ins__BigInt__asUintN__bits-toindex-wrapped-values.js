// Copyright (C) 2017 Ecma International.  All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
description: |
    Collection of assertion functions used throughout test262
defines: [assert]
---*/


function assert(mustBeTrue, message) {
  if (mustBeTrue === true) {
    return;
  }

  if (message === undefined) {
    message = 'Expected true but got ' + assert._toString(mustBeTrue);
  }
  throw new Test262Error(message);
}

assert._isSameValue = function (a, b) {
  if (a === b) {
    // Handle +/-0 vs. -/+0
    return a !== 0 || 1 / a === 1 / b;
  }

  // Handle NaN vs. NaN
  return a !== a && b !== b;
};

assert.sameValue = function (actual, expected, message) {
  try {
    if (assert._isSameValue(actual, expected)) {
      return;
    }
  } catch (error) {
    throw new Test262Error(message + ' (_isSameValue operation threw) ' + error);
    return;
  }

  if (message === undefined) {
    message = '';
  } else {
    message += ' ';
  }

  message += 'Expected SameValue(«' + assert._toString(actual) + '», «' + assert._toString(expected) + '») to be true';

  throw new Test262Error(message);
};

assert.notSameValue = function (actual, unexpected, message) {
  if (!assert._isSameValue(actual, unexpected)) {
    return;
  }

  if (message === undefined) {
    message = '';
  } else {
    message += ' ';
  }

  message += 'Expected SameValue(«' + assert._toString(actual) + '», «' + assert._toString(unexpected) + '») to be false';

  throw new Test262Error(message);
};

assert.throws = function (expectedErrorConstructor, func, message) {
  var expectedName, actualName;
  if (typeof func !== "function") {
    throw new Test262Error('assert.throws requires two arguments: the error constructor ' +
      'and a function to run');
    return;
  }
  if (message === undefined) {
    message = '';
  } else {
    message += ' ';
  }

  try {
    func();
  } catch (thrown) {
    if (typeof thrown !== 'object' || thrown === null) {
      message += 'Thrown value was not an object!';
      throw new Test262Error(message);
    } else if (thrown.constructor !== expectedErrorConstructor) {
      expectedName = expectedErrorConstructor.name;
      actualName = thrown.constructor.name;
      if (expectedName === actualName) {
        message += 'Expected a ' + expectedName + ' but got a different error constructor with the same name';
      } else {
        message += 'Expected a ' + expectedName + ' but got a ' + actualName;
      }
      throw new Test262Error(message);
    }
    return;
  }

  message += 'Expected a ' + expectedErrorConstructor.name + ' to be thrown but no exception was thrown at all';
  throw new Test262Error(message);
};

function isPrimitive(value) {
  return !value || (typeof value !== 'object' && typeof value !== 'function');
}

assert.compareArray = function (actual, expected, message) {
  message = message === undefined ? '' : message;

  if (typeof message === 'symbol') {
    message = message.toString();
  }

  if (isPrimitive(actual)) {
    assert(false, `Actual argument [${actual}] shouldn't be primitive. ${message}`);
  } else if (isPrimitive(expected)) {
    assert(false, `Expected argument [${expected}] shouldn't be primitive. ${message}`);
  }
  var result = compareArray(actual, expected);
  if (result) return;

  var format = compareArray.format;
  assert(false, `Actual ${format(actual)} and expected ${format(expected)} should have the same contents. ${message}`);
};

function compareArray(a, b) {
  if (b.length !== a.length) {
    return false;
  }
  for (var i = 0; i < a.length; i++) {
    if (!assert._isSameValue(b[i], a[i])) {
      return false;
    }
  }
  return true;
}

compareArray.format = function (arrayLike) {
  return `[${Array.prototype.map.call(arrayLike, String).join(', ')}]`;
};

assert._formatIdentityFreeValue = function formatIdentityFreeValue(value) {
  switch (value === null ? 'null' : typeof value) {
    case 'string':
      return typeof JSON !== "undefined" ? JSON.stringify(value) : `"${value}"`;
    case 'bigint':
      return `${value}n`;
    case 'number':
      if (value === 0 && 1 / value === -Infinity) return '-0';
      // falls through
    case 'boolean':
    case 'undefined':
    case 'null':
      return String(value);
  }
};

assert._toString = function (value) {
  var basic = assert._formatIdentityFreeValue(value);
  if (basic) return basic;
  try {
    return String(value);
  } catch (err) {
    if (err.name === 'TypeError') {
      return Object.prototype.toString.call(value);
    }
    throw err;
  }
};


// Copyright (c) 2012 Ecma International.  All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
description: |
    Provides both:

    - An error class to avoid false positives when testing for thrown exceptions
    - A function to explicitly throw an exception using the Test262Error class
defines: [Test262Error, $DONOTEVALUATE]
---*/


function Test262Error(message) {
  this.message = message || "";
}

Test262Error.prototype.toString = function () {
  return "Test262Error: " + this.message;
};

Test262Error.thrower = function (message) {
  throw new Test262Error(message);
};

function $DONOTEVALUATE() {
  throw "Test262: This statement should not be evaluated.";
}


// Copyright (C) 2017 Josh Wolfe. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
description: BigInt.asUintN type coercion for bits parameter
esid: sec-bigint.asuintn
info: |
  BigInt.asUintN ( bits, bigint )

  1. Let bits be ? ToIndex(bits).
features: [BigInt, computed-property-names, Symbol, Symbol.toPrimitive]
---*/

assert.sameValue(BigInt.asUintN(Object(0), 1n), 0n, "ToPrimitive: unbox object with internal slot");
assert.sameValue(BigInt.asUintN({
  [Symbol.toPrimitive]: function() {
    return 0;
  }
}, 1n), 0n, "ToPrimitive: @@toPrimitive");
assert.sameValue(BigInt.asUintN({
  valueOf: function() {
    return 0;
  }
}, 1n), 0n, "ToPrimitive: valueOf");
assert.sameValue(BigInt.asUintN({
  toString: function() {
    return 0;
  }
}, 1n), 0n, "ToPrimitive: toString");
assert.sameValue(BigInt.asUintN(Object(NaN), 1n), 0n,
  "ToIndex: unbox object with internal slot => NaN => 0");
assert.sameValue(BigInt.asUintN({
  [Symbol.toPrimitive]: function() {
    return NaN;
  }
}, 1n), 0n, "ToIndex: @@toPrimitive => NaN => 0");
assert.sameValue(BigInt.asUintN({
  valueOf: function() {
    return NaN;
  }
}, 1n), 0n, "ToIndex: valueOf => NaN => 0");
assert.sameValue(BigInt.asUintN({
  toString: function() {
    return NaN;
  }
}, 1n), 0n, "ToIndex: toString => NaN => 0");
assert.sameValue(BigInt.asUintN({
  [Symbol.toPrimitive]: function() {
    return undefined;
  }
}, 1n), 0n, "ToIndex: @@toPrimitive => undefined => NaN => 0");
assert.sameValue(BigInt.asUintN({
  valueOf: function() {
    return undefined;
  }
}, 1n), 0n, "ToIndex: valueOf => undefined => NaN => 0");
assert.sameValue(BigInt.asUintN({
  toString: function() {
    return undefined;
  }
}, 1n), 0n, "ToIndex: toString => undefined => NaN => 0");
assert.sameValue(BigInt.asUintN({
  [Symbol.toPrimitive]: function() {
    return null;
  }
}, 1n), 0n, "ToIndex: @@toPrimitive => null => 0");
assert.sameValue(BigInt.asUintN({
  valueOf: function() {
    return null;
  }
}, 1n), 0n, "ToIndex: valueOf => null => 0");
assert.sameValue(BigInt.asUintN({
  toString: function() {
    return null;
  }
}, 1n), 0n, "ToIndex: toString => null => 0");
assert.sameValue(BigInt.asUintN(Object(true), 1n), 1n,
  "ToIndex: unbox object with internal slot => true => 1");
assert.sameValue(BigInt.asUintN({
  [Symbol.toPrimitive]: function() {
    return true;
  }
}, 1n), 1n, "ToIndex: @@toPrimitive => true => 1");
assert.sameValue(BigInt.asUintN({
  valueOf: function() {
    return true;
  }
}, 1n), 1n, "ToIndex: valueOf => true => 1");
assert.sameValue(BigInt.asUintN({
  toString: function() {
    return true;
  }
}, 1n), 1n, "ToIndex: toString => true => 1");
assert.sameValue(BigInt.asUintN(Object("1"), 1n), 1n,
  "ToIndex: unbox object with internal slot => parse Number");
assert.sameValue(BigInt.asUintN({
  [Symbol.toPrimitive]: function() {
    return "1";
  }
}, 1n), 1n, "ToIndex: @@toPrimitive => parse Number");
assert.sameValue(BigInt.asUintN({
  valueOf: function() {
    return "1";
  }
}, 1n), 1n, "ToIndex: valueOf => parse Number");
assert.sameValue(BigInt.asUintN({
  toString: function() {
    return "1";
  }
}, 1n), 1n, "ToIndex: toString => parse Number");
