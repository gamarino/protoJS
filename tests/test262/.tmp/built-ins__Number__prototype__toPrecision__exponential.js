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


// Copyright (C) 2016 The V8 Project authors. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-number.prototype.toprecision
description: >
  Return string values using exponential character
info: |
  Number.prototype.toPrecision ( precision )

  1. Let x be ? thisNumberValue(this value).
  [...]
  5. Let s be the empty String.
  [...]
  9. If x = 0, then
    [...]
  10. Else x ≠ 0,
    [...]
    c. If e < -6 or e ≥ p, then
      [...]
      vii. Return the concatenation of s, m, code unit 0x0065 (LATIN SMALL
      LETTER E), c, and d.
  [...]
---*/

assert.sameValue((10).toPrecision(1), "1e+1");
assert.sameValue((11).toPrecision(1), "1e+1");
assert.sameValue((17).toPrecision(1), "2e+1");
assert.sameValue((19).toPrecision(1), "2e+1");
assert.sameValue((20).toPrecision(1), "2e+1");

assert.sameValue((100).toPrecision(1), "1e+2");
assert.sameValue((1000).toPrecision(1), "1e+3");
assert.sameValue((10000).toPrecision(1), "1e+4");
assert.sameValue((100000).toPrecision(1), "1e+5");

assert.sameValue((100).toPrecision(2), "1.0e+2");
assert.sameValue((1000).toPrecision(2), "1.0e+3");
assert.sameValue((10000).toPrecision(2), "1.0e+4");
assert.sameValue((100000).toPrecision(2), "1.0e+5");

assert.sameValue((1000).toPrecision(3), "1.00e+3");
assert.sameValue((10000).toPrecision(3), "1.00e+4");
assert.sameValue((100000).toPrecision(3), "1.00e+5");

assert.sameValue((42).toPrecision(1), "4e+1");
assert.sameValue((-42).toPrecision(1), "-4e+1");

assert.sameValue((1.2345e+27).toPrecision(1), "1e+27");
assert.sameValue((1.2345e+27).toPrecision(2), "1.2e+27");
assert.sameValue((1.2345e+27).toPrecision(3), "1.23e+27");
assert.sameValue((1.2345e+27).toPrecision(4), "1.234e+27");
assert.sameValue((1.2345e+27).toPrecision(5), "1.2345e+27");
assert.sameValue((1.2345e+27).toPrecision(6), "1.23450e+27");
assert.sameValue((1.2345e+27).toPrecision(7), "1.234500e+27");
assert.sameValue((1.2345e+27).toPrecision(16), "1.234500000000000e+27");
assert.sameValue((1.2345e+27).toPrecision(17), "1.2345000000000000e+27");
assert.sameValue((1.2345e+27).toPrecision(18), "1.23449999999999996e+27");
assert.sameValue((1.2345e+27).toPrecision(19), "1.234499999999999962e+27");
assert.sameValue((1.2345e+27).toPrecision(20), "1.2344999999999999618e+27");
assert.sameValue((1.2345e+27).toPrecision(21), "1.23449999999999996184e+27");

assert.sameValue((-1.2345e+27).toPrecision(1), "-1e+27");
assert.sameValue((-1.2345e+27).toPrecision(2), "-1.2e+27");
assert.sameValue((-1.2345e+27).toPrecision(3), "-1.23e+27");
assert.sameValue((-1.2345e+27).toPrecision(4), "-1.234e+27");
assert.sameValue((-1.2345e+27).toPrecision(5), "-1.2345e+27");
assert.sameValue((-1.2345e+27).toPrecision(6), "-1.23450e+27");
assert.sameValue((-1.2345e+27).toPrecision(7), "-1.234500e+27");
assert.sameValue((-1.2345e+27).toPrecision(16), "-1.234500000000000e+27");
assert.sameValue((-1.2345e+27).toPrecision(17), "-1.2345000000000000e+27");
assert.sameValue((-1.2345e+27).toPrecision(18), "-1.23449999999999996e+27");
assert.sameValue((-1.2345e+27).toPrecision(19), "-1.234499999999999962e+27");
assert.sameValue((-1.2345e+27).toPrecision(20), "-1.2344999999999999618e+27");
assert.sameValue((-1.2345e+27).toPrecision(21), "-1.23449999999999996184e+27");

var n = new Number("1000000000000000000000"); // 1e+21
assert.sameValue((n).toPrecision(1), "1e+21");
assert.sameValue((n).toPrecision(2), "1.0e+21");
assert.sameValue((n).toPrecision(3), "1.00e+21");
assert.sameValue((n).toPrecision(4), "1.000e+21");
assert.sameValue((n).toPrecision(5), "1.0000e+21");
assert.sameValue((n).toPrecision(6), "1.00000e+21");
assert.sameValue((n).toPrecision(7), "1.000000e+21");
assert.sameValue((n).toPrecision(16), "1.000000000000000e+21");
assert.sameValue((n).toPrecision(17), "1.0000000000000000e+21");
assert.sameValue((n).toPrecision(18), "1.00000000000000000e+21");
assert.sameValue((n).toPrecision(19), "1.000000000000000000e+21");
assert.sameValue((n).toPrecision(20), "1.0000000000000000000e+21");
assert.sameValue((n).toPrecision(21), "1.00000000000000000000e+21");

var n = new Number("0.000000000000000000001"); // 1e-21
assert.sameValue((n).toPrecision(1), "1e-21");
assert.sameValue((n).toPrecision(2), "1.0e-21");
assert.sameValue((n).toPrecision(3), "1.00e-21");
assert.sameValue((n).toPrecision(4), "1.000e-21");
assert.sameValue((n).toPrecision(5), "1.0000e-21");
assert.sameValue((n).toPrecision(6), "1.00000e-21");
assert.sameValue((n).toPrecision(7), "1.000000e-21");
assert.sameValue((n).toPrecision(16), "9.999999999999999e-22");
assert.sameValue((n).toPrecision(17), "9.9999999999999991e-22");
assert.sameValue((n).toPrecision(18), "9.99999999999999908e-22");
assert.sameValue((n).toPrecision(19), "9.999999999999999075e-22");
assert.sameValue((n).toPrecision(20), "9.9999999999999990754e-22");
assert.sameValue((n).toPrecision(21), "9.99999999999999907537e-22");

assert.sameValue((0.00000001).toPrecision(1), "1e-8");
assert.sameValue((-0.00000001).toPrecision(1), "-1e-8");
