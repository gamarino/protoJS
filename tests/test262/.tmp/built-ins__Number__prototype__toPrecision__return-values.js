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
  Return regular string values
info: |
  Number.prototype.toPrecision ( precision )

  1. Let x be ? thisNumberValue(this value).
  [...]
  5. Let s be the empty String.
  [...]
  11. If e = p-1, return the concatenation of the Strings s and m.
  12. If e ≥ 0, then
    a. Let m be the concatenation of the first e+1 elements of m, the code unit
    0x002E (FULL STOP), and the remaining p- (e+1) elements of m.
  13. Else e < 0,
    a. Let m be the String formed by the concatenation of code unit 0x0030
    (DIGIT ZERO), code unit 0x002E (FULL STOP), -(e+1) occurrences of code unit
    0x0030 (DIGIT ZERO), and the String m.
  14. Return the String that is the concatenation of s and m. 
---*/

assert.sameValue((7).toPrecision(1), "7");
assert.sameValue((7).toPrecision(2), "7.0");
assert.sameValue((7).toPrecision(3), "7.00");
assert.sameValue((7).toPrecision(19), "7.000000000000000000");
assert.sameValue((7).toPrecision(20), "7.0000000000000000000");
assert.sameValue((7).toPrecision(21), "7.00000000000000000000");

assert.sameValue((-7).toPrecision(1), "-7");
assert.sameValue((-7).toPrecision(2), "-7.0");
assert.sameValue((-7).toPrecision(3), "-7.00");
assert.sameValue((-7).toPrecision(19), "-7.000000000000000000");
assert.sameValue((-7).toPrecision(20), "-7.0000000000000000000");
assert.sameValue((-7).toPrecision(21), "-7.00000000000000000000");

assert.sameValue((10).toPrecision(2), "10");
assert.sameValue((11).toPrecision(2), "11");
assert.sameValue((17).toPrecision(2), "17");
assert.sameValue((19).toPrecision(2), "19");
assert.sameValue((20).toPrecision(2), "20");

assert.sameValue((-10).toPrecision(2), "-10");
assert.sameValue((-11).toPrecision(2), "-11");
assert.sameValue((-17).toPrecision(2), "-17");
assert.sameValue((-19).toPrecision(2), "-19");
assert.sameValue((-20).toPrecision(2), "-20");

assert.sameValue((42).toPrecision(2), "42");
assert.sameValue((-42).toPrecision(2), "-42");

assert.sameValue((100).toPrecision(3), "100");
assert.sameValue((100).toPrecision(7), "100.0000");
assert.sameValue((1000).toPrecision(7), "1000.000");
assert.sameValue((10000).toPrecision(7), "10000.00");
assert.sameValue((100000).toPrecision(7), "100000.0");

assert.sameValue((0.000001).toPrecision(1), "0.000001");
assert.sameValue((0.000001).toPrecision(2), "0.0000010");
assert.sameValue((0.000001).toPrecision(3), "0.00000100");
