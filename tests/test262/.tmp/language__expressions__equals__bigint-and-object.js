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
description: Non-strict equality comparison of BigInt values and non-primitive objects
esid: sec-abstract-equality-comparison
info: |
  10. If Type(x) is either String, Number, BigInt, or Symbol and Type(y) is Object, return the result of the comparison x == ? ToPrimitive(y).
  11. If Type(x) is Object and Type(y) is either String, Number, BigInt, or Symbol, return the result of the comparison ? ToPrimitive(x) == y.

  then after the recursion:

  1. If Type(x) is the same as Type(y), then
    a. Return the result of performing Strict Equality Comparison x === y.
  ...
  6. If Type(x) is BigInt and Type(y) is String,
    a. Let n be StringToBigInt(y).
    b. If n is NaN, return false.
    c. Return the result of x == n.
  7. If Type(x) is String and Type(y) is BigInt, return the result of y == x.

features: [BigInt]
---*/
assert.sameValue(0n == Object(0n), true, 'The result of (0n == Object(0n)) is true');
assert.sameValue(Object(0n) == 0n, true, 'The result of (Object(0n) == 0n) is true');
assert.sameValue(0n == Object(1n), false, 'The result of (0n == Object(1n)) is false');
assert.sameValue(Object(1n) == 0n, false, 'The result of (Object(1n) == 0n) is false');
assert.sameValue(1n == Object(0n), false, 'The result of (1n == Object(0n)) is false');
assert.sameValue(Object(0n) == 1n, false, 'The result of (Object(0n) == 1n) is false');
assert.sameValue(1n == Object(1n), true, 'The result of (1n == Object(1n)) is true');
assert.sameValue(Object(1n) == 1n, true, 'The result of (Object(1n) == 1n) is true');
assert.sameValue(2n == Object(0n), false, 'The result of (2n == Object(0n)) is false');
assert.sameValue(Object(0n) == 2n, false, 'The result of (Object(0n) == 2n) is false');
assert.sameValue(2n == Object(1n), false, 'The result of (2n == Object(1n)) is false');
assert.sameValue(Object(1n) == 2n, false, 'The result of (Object(1n) == 2n) is false');
assert.sameValue(2n == Object(2n), true, 'The result of (2n == Object(2n)) is true');
assert.sameValue(Object(2n) == 2n, true, 'The result of (Object(2n) == 2n) is true');
assert.sameValue(0n == {}, false, 'The result of (0n == {}) is false');
assert.sameValue({} == 0n, false, 'The result of (({}) == 0n) is false');

assert.sameValue(0n == {
  valueOf: function() {
    return 0n;
  }
}, true, 'The result of (0n == {valueOf: function() {return 0n;}}) is true');

assert.sameValue({
  valueOf: function() {
    return 0n;
  }
} == 0n, true, 'The result of (({valueOf: function() {return 0n;}}) == 0n) is true');

assert.sameValue(0n == {
  valueOf: function() {
    return 1n;
  }
}, false, 'The result of (0n == {valueOf: function() {return 1n;}}) is false');

assert.sameValue({
  valueOf: function() {
    return 1n;
  }
} == 0n, false, 'The result of (({valueOf: function() {return 1n;}}) == 0n) is false');

assert.sameValue(0n == {
  toString: function() {
    return '0';
  }
}, true, 'The result of (0n == {toString: function() {return "0";}}) is true');

assert.sameValue({
  toString: function() {
    return '0';
  }
} == 0n, true, 'The result of (({toString: function() {return "0";}}) == 0n) is true');

assert.sameValue(0n == {
  toString: function() {
    return '1';
  }
}, false, 'The result of (0n == {toString: function() {return "1";}}) is false');

assert.sameValue({
  toString: function() {
    return '1';
  }
} == 0n, false, 'The result of (({toString: function() {return "1";}}) == 0n) is false');

assert.sameValue(900719925474099101n == {
  valueOf: function() {
    return 900719925474099101n;
  }
}, true, 'The result of (900719925474099101n == {valueOf: function() {return 900719925474099101n;}}) is true');

assert.sameValue({
  valueOf: function() {
    return 900719925474099101n;
  }
} == 900719925474099101n, true, 'The result of (({valueOf: function() {return 900719925474099101n;}}) == 900719925474099101n) is true');

assert.sameValue(900719925474099101n == {
  valueOf: function() {
    return 900719925474099102n;
  }
}, false, 'The result of (900719925474099101n == {valueOf: function() {return 900719925474099102n;}}) is false');

assert.sameValue({
  valueOf: function() {
    return 900719925474099102n;
  }
} == 900719925474099101n, false, 'The result of (({valueOf: function() {return 900719925474099102n;}}) == 900719925474099101n) is false');

assert.sameValue(900719925474099101n == {
  toString: function() {
    return '900719925474099101';
  }
}, true, 'The result of (900719925474099101n == {toString: function() {return "900719925474099101";}}) is true');

assert.sameValue({
  toString: function() {
    return '900719925474099101';
  }
} == 900719925474099101n, true, 'The result of (({toString: function() {return "900719925474099101";}}) == 900719925474099101n) is true');

assert.sameValue(900719925474099101n == {
  toString: function() {
    return '900719925474099102';
  }
}, false, 'The result of (900719925474099101n == {toString: function() {return "900719925474099102";}}) is false');

assert.sameValue({
  toString: function() {
    return '900719925474099102';
  }
} == 900719925474099101n, false, 'The result of (({toString: function() {return "900719925474099102";}}) == 900719925474099101n) is false');
