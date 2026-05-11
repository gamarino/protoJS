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
description: Verify that the year is appropriately formatted as 4 or 6 digits
features: [Temporal]
---*/

let instance = new Temporal.PlainDateTime(-100000, 12, 3, 4, 56, 7, 890);
assert.sameValue(instance.toString(), "-100000-12-03T04:56:07.89", "large negative year formatted as 6-digit");

instance = new Temporal.PlainDateTime(-10000, 4, 5, 6, 7, 8, 910);
assert.sameValue(instance.toString(), "-010000-04-05T06:07:08.91", "smallest 5-digit negative year formatted as 6-digit");

instance = new Temporal.PlainDateTime(-9999, 6, 7, 8, 9, 10, 987);
assert.sameValue(instance.toString(), "-009999-06-07T08:09:10.987", "largest 4-digit negative year formatted as 6-digit");

instance = new Temporal.PlainDateTime(-1000, 8, 9, 10, 9, 8, 765);
assert.sameValue(instance.toString(), "-001000-08-09T10:09:08.765", "smallest 4-digit negative year formatted as 6-digit");

instance = new Temporal.PlainDateTime(-999, 10, 9, 8, 7, 6, 543);
assert.sameValue(instance.toString(), "-000999-10-09T08:07:06.543", "largest 3-digit negative year formatted as 6-digit");

instance = new Temporal.PlainDateTime(-1, 8, 7, 6, 54, 32, 100);
assert.sameValue(instance.toString(), "-000001-08-07T06:54:32.1", "year -1 formatted as 6-digit");

instance = new Temporal.PlainDateTime(0, 6, 5, 4, 32, 10, 123);
assert.sameValue(instance.toString(), "0000-06-05T04:32:10.123", "year 0 formatted as 4-digit");

instance = new Temporal.PlainDateTime(1, 4, 3, 21, 0, 12, 345);
assert.sameValue(instance.toString(), "0001-04-03T21:00:12.345", "year 1 formatted as 4-digit");

instance = new Temporal.PlainDateTime(999, 2, 10, 12, 34, 56, 789);
assert.sameValue(instance.toString(), "0999-02-10T12:34:56.789", "largest 3-digit positive year formatted as 4-digit");

instance = new Temporal.PlainDateTime(1000, 1, 23, 4, 56, 7, 890);
assert.sameValue(instance.toString(), "1000-01-23T04:56:07.89", "smallest 4-digit positive year formatted as 4-digit");

instance = new Temporal.PlainDateTime(9999, 4, 5, 6, 7, 8, 910);
assert.sameValue(instance.toString(), "9999-04-05T06:07:08.91", "largest 4-digit positive year formatted as 4-digit");

instance = new Temporal.PlainDateTime(10000, 6, 7, 8, 9, 10, 987);
assert.sameValue(instance.toString(), "+010000-06-07T08:09:10.987", "smallest 5-digit positive year formatted as 6-digit");

instance = new Temporal.PlainDateTime(100000, 8, 9, 10, 9, 8, 765);
assert.sameValue(instance.toString(), "+100000-08-09T10:09:08.765", "large positive year formatted as 6-digit");
