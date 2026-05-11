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


// Copyright (C) 2025 the V8 project authors. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
esid: sec-date.prototype.setutcminutes
description: Return value for valid dates
info: |
  1. Let dateObject be the this value.
  2. Perform ? RequireInternalSlot(dateObject, [[DateValue]]).
  3. Let t be dateObject.[[DateValue]].
  4. Let m be ? ToNumber(min).
  5. If sec is present, let s be ? ToNumber(sec).
  6. If ms is present, let milli be ? ToNumber(ms).
  7. If t is NaN, return NaN.
  8. If sec is not present, let s be SecFromTime(t).
  9. If ms is not present, let milli be msFromTime(t).
  10. Let date be MakeDate(Day(t), MakeTime(HourFromTime(t), m, s, milli)).
  11. Let v be TimeClip(date).
  12. Set dateObject.[[DateValue]] to v.
  13. Return v.
---*/

var date = new Date(Date.UTC(2016, 6, 1));
var returnValue, expected;

returnValue = date.setUTCMinutes(23);

expected = Date.UTC(2016, 6, 1, 0, 23);
assert.sameValue(
  returnValue, expected, 'minute within unit boundary (return value)'
);
assert.sameValue(
  date.getTime(), expected, 'minute within unit boundary ([[DateValue]] slot)'
);

returnValue = date.setUTCMinutes(-1);

expected = Date.UTC(2016, 5, 30, 23, 59);
assert.sameValue(
  returnValue, expected, 'minute before time unit boundary (return value)'
);
assert.sameValue(
  date.getTime(),
  expected,
  'minute before time unit boundary ([[DateValue]] slot)'
);

returnValue = date.setUTCMinutes(60);

expected = Date.UTC(2016, 6, 1);
assert.sameValue(
  returnValue, expected, 'minute after time unit boundary (return value)'
);
assert.sameValue(
  date.getTime(),
  expected,
  'minute after time unit boundary ([[DateValue]] slot)'
);

date = new Date(Date.UTC(2016, 6, 1));

returnValue = date.setUTCMinutes(0, 45);

expected = Date.UTC(2016, 6, 1, 0, 0, 45);
assert.sameValue(
  returnValue, expected, 'second within unit boundary (return value)'
);
assert.sameValue(
  date.getTime(), expected, 'second within unit boundary ([[DateValue]] slot)'
);

returnValue = date.setUTCMinutes(0, -1);

expected = Date.UTC(2016, 5, 30, 23, 59, 59);
assert.sameValue(
  returnValue, expected, 'second before time unit boundary (return value)'
);
assert.sameValue(
  date.getTime(),
  expected,
  'second before time unit boundary ([[DateValue]] slot)'
);

returnValue = date.setUTCMinutes(0, 60);

expected = Date.UTC(2016, 5, 30, 23, 1);
assert.sameValue(
  returnValue, expected, 'second after time unit boundary (return value)'
);
assert.sameValue(
  date.getTime(),
  expected,
  'second after time unit boundary ([[DateValue]] slot)'
);

date = new Date(Date.UTC(2016, 6, 1));

returnValue = date.setUTCMinutes(0, 0, 345);

expected = Date.UTC(2016, 6, 1, 0, 0, 0, 345);
assert.sameValue(
  returnValue, expected, 'millisecond within unit boundary (return value)'
);
assert.sameValue(
  date.getTime(),
  expected,
  'millisecond within unit boundary ([[DateValue]] slot)'
);

returnValue = date.setUTCMinutes(0, 0, -1);

expected = Date.UTC(2016, 5, 30, 23, 59, 59, 999);
assert.sameValue(
  returnValue, expected, 'millisecond before time unit boundary (return value)'
);
assert.sameValue(
  date.getTime(),
  expected,
  'millisecond before time unit boundary ([[DateValue]] slot)'
);

returnValue = date.setUTCMinutes(0, 0, 1000);

expected = Date.UTC(2016, 5, 30, 23, 0, 1);
assert.sameValue(
  returnValue, expected, 'millisecond after time unit boundary (return value)'
);
assert.sameValue(
  date.getTime(),
  expected,
  'millisecond after time unit boundary ([[DateValue]] slot)'
);
