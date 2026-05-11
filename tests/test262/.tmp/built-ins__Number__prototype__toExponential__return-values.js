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
esid: sec-number.prototype.toexponential
description: >
  Return regular string values
---*/

assert.sameValue((123.456).toExponential(0), "1e+2");
assert.sameValue((123.456).toExponential(1), "1.2e+2");
assert.sameValue((123.456).toExponential(2), "1.23e+2");
assert.sameValue((123.456).toExponential(3), "1.235e+2");
assert.sameValue((123.456).toExponential(4), "1.2346e+2");
assert.sameValue((123.456).toExponential(5), "1.23456e+2");
assert.sameValue((123.456).toExponential(6), "1.234560e+2");
assert.sameValue((123.456).toExponential(7), "1.2345600e+2");
assert.sameValue((123.456).toExponential(17), "1.23456000000000003e+2");
assert.sameValue((123.456).toExponential(20), "1.23456000000000003070e+2");

assert.sameValue((-123.456).toExponential(0), "-1e+2");
assert.sameValue((-123.456).toExponential(1), "-1.2e+2");
assert.sameValue((-123.456).toExponential(2), "-1.23e+2");
assert.sameValue((-123.456).toExponential(3), "-1.235e+2");
assert.sameValue((-123.456).toExponential(4), "-1.2346e+2");
assert.sameValue((-123.456).toExponential(5), "-1.23456e+2");
assert.sameValue((-123.456).toExponential(6), "-1.234560e+2");
assert.sameValue((-123.456).toExponential(7), "-1.2345600e+2");
assert.sameValue((-123.456).toExponential(17), "-1.23456000000000003e+2");
assert.sameValue((-123.456).toExponential(20), "-1.23456000000000003070e+2");

assert.sameValue((0.0001).toExponential(0), "1e-4");
assert.sameValue((0.0001).toExponential(1), "1.0e-4");
assert.sameValue((0.0001).toExponential(2), "1.00e-4");
assert.sameValue((0.0001).toExponential(3), "1.000e-4");
assert.sameValue((0.0001).toExponential(4), "1.0000e-4");
assert.sameValue((0.0001).toExponential(16), "1.0000000000000000e-4");
assert.sameValue((0.0001).toExponential(17), "1.00000000000000005e-4");
assert.sameValue((0.0001).toExponential(18), "1.000000000000000048e-4");
assert.sameValue((0.0001).toExponential(19), "1.0000000000000000479e-4");
assert.sameValue((0.0001).toExponential(20), "1.00000000000000004792e-4");

assert.sameValue((0.9999).toExponential(0), "1e+0");
assert.sameValue((0.9999).toExponential(1), "1.0e+0");
assert.sameValue((0.9999).toExponential(2), "1.00e+0");
assert.sameValue((0.9999).toExponential(3), "9.999e-1");
assert.sameValue((0.9999).toExponential(4), "9.9990e-1");
assert.sameValue((0.9999).toExponential(16), "9.9990000000000001e-1");
assert.sameValue((0.9999).toExponential(17), "9.99900000000000011e-1");
assert.sameValue((0.9999).toExponential(18), "9.999000000000000110e-1");
assert.sameValue((0.9999).toExponential(19), "9.9990000000000001101e-1");
assert.sameValue((0.9999).toExponential(20), "9.99900000000000011013e-1");

assert.sameValue((25).toExponential(0), "3e+1");
assert.sameValue((12345).toExponential(3), "1.235e+4");
