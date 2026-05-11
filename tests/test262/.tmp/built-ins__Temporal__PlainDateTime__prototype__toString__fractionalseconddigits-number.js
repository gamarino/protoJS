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


// Copyright (C) 2022 Igalia, S.L. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-temporal.plaindatetime.prototype.tostring
description: Number for fractionalSecondDigits option
features: [Temporal]
---*/

const fewSeconds = new Temporal.PlainDateTime(1976, 2, 4, 5, 3, 1);
const zeroSeconds = new Temporal.PlainDateTime(1976, 11, 18, 15, 23);
const wholeSeconds = new Temporal.PlainDateTime(1976, 11, 18, 15, 23, 30);
const subSeconds = new Temporal.PlainDateTime(1976, 11, 18, 15, 23, 30, 123, 400);

assert.sameValue(fewSeconds.toString({ fractionalSecondDigits: 0 }), "1976-02-04T05:03:01",
  "pads parts with 0");
assert.sameValue(subSeconds.toString({ fractionalSecondDigits: 0 }), "1976-11-18T15:23:30",
  "truncates 4 decimal places to 0");
assert.sameValue(zeroSeconds.toString({ fractionalSecondDigits: 1 }), "1976-11-18T15:23:00.0",
  "pads zero seconds to 1 decimal place");
assert.sameValue(zeroSeconds.toString({ fractionalSecondDigits: 2 }), "1976-11-18T15:23:00.00",
  "pads zero seconds to 2 decimal places");
assert.sameValue(wholeSeconds.toString({ fractionalSecondDigits: 2 }), "1976-11-18T15:23:30.00",
  "pads whole seconds to 2 decimal places");
assert.sameValue(subSeconds.toString({ fractionalSecondDigits: 2 }), "1976-11-18T15:23:30.12",
  "truncates 4 decimal places to 2");
assert.sameValue(subSeconds.toString({ fractionalSecondDigits: 3 }), "1976-11-18T15:23:30.123",
  "truncates 4 decimal places to 3");
assert.sameValue(subSeconds.toString({ fractionalSecondDigits: 4 }), "1976-11-18T15:23:30.1234",
  "does not pad");
assert.sameValue(subSeconds.toString({ fractionalSecondDigits: 5 }), "1976-11-18T15:23:30.12340",
  "pads 4 decimal places to 5");
assert.sameValue(subSeconds.toString({ fractionalSecondDigits: 6 }), "1976-11-18T15:23:30.123400",
  "pads 4 decimal places to 6");
assert.sameValue(zeroSeconds.toString({ fractionalSecondDigits: 7 }), "1976-11-18T15:23:00.0000000",
  "pads zero seconds to 7 decimal places");
assert.sameValue(wholeSeconds.toString({ fractionalSecondDigits: 7 }), "1976-11-18T15:23:30.0000000",
  "pads whole seconds to 7 decimal places");
assert.sameValue(subSeconds.toString({ fractionalSecondDigits: 7 }), "1976-11-18T15:23:30.1234000",
  "pads 4 decimal places to 7");
assert.sameValue(subSeconds.toString({ fractionalSecondDigits: 8 }), "1976-11-18T15:23:30.12340000",
  "pads 4 decimal places to 8");
assert.sameValue(subSeconds.toString({ fractionalSecondDigits: 9 }), "1976-11-18T15:23:30.123400000",
  "pads 4 decimal places to 9");
