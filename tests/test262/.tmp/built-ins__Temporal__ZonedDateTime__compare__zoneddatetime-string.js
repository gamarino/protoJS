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


// Copyright (C) 2021 Igalia, S.L. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-temporal.zoneddatetime.compare
description: Conversion of ISO date-time strings to Temporal.ZonedDateTime instances
features: [Temporal]
---*/

const epoch = new Temporal.ZonedDateTime(0n, "UTC");
const hourBefore = new Temporal.ZonedDateTime(-3600_000_000_000n, "UTC");

let str = "1970-01-01T00:00";
assert.throws(RangeError, () => Temporal.ZonedDateTime.compare(str, epoch), "bare date-time string is not a ZonedDateTime (first argument)");
assert.throws(RangeError, () => Temporal.ZonedDateTime.compare(epoch, str), "bare date-time string is not a ZonedDateTime (second argument)");
str = "1970-01-01T00:00Z";
assert.throws(RangeError, () => Temporal.ZonedDateTime.compare(str, epoch), "date-time + Z is not a ZonedDateTime (first argument)");
assert.throws(RangeError, () => Temporal.ZonedDateTime.compare(epoch, str), "date-time + Z is not a ZonedDateTime (second argument)");
str = "1970-01-01T00:00+01:00";
assert.throws(RangeError, () => Temporal.ZonedDateTime.compare(str, epoch), "date-time + offset is not a ZonedDateTime (first argument)");
assert.throws(RangeError, () => Temporal.ZonedDateTime.compare(epoch, str), "date-time + offset is not a ZonedDateTime (second argument)");

str = "1970-01-01T00:00[+01:00]";
const result1 = Temporal.ZonedDateTime.compare(str, hourBefore);
assert.sameValue(result1, 0, "date-time + IANA annotation preserves wall time in the time zone (first argument)");
const result2 = Temporal.ZonedDateTime.compare(hourBefore, str);
assert.sameValue(result2, 0, "date-time + IANA annotation preserves wall time in the time zone (second argument)");

str = "1970-01-01T00:00Z[+01:00]";
const result3 = Temporal.ZonedDateTime.compare(str, epoch);
assert.sameValue(result3, 0, "date-time + Z + IANA annotation preserves exact time in the time zone (first argument)");
const result4 = Temporal.ZonedDateTime.compare(epoch, str);
assert.sameValue(result4, 0, "date-time + Z + IANA annotation preserves exact time in the time zone (second argument)");

str = "1970-01-01T00:00+01:00[+01:00]";
const result5 = Temporal.ZonedDateTime.compare(str, hourBefore);
assert.sameValue(result5, 0, "date-time + offset + IANA annotation ensures both exact and wall time match (first argument)");
const result6 = Temporal.ZonedDateTime.compare(hourBefore, str);
assert.sameValue(result6, 0, "date-time + offset + IANA annotation ensures both exact and wall time match (second argument)");

str = "1970-01-01T00:00-04:15[+01:00]";
assert.throws(RangeError, () => Temporal.ZonedDateTime.compare(str, epoch), "date-time + offset + IANA annotation throws if wall time and exact time mismatch (first argument)");
assert.throws(RangeError, () => Temporal.ZonedDateTime.compare(epoch, str), "date-time + offset + IANA annotation throws if wall time and exact time mismatch (second argument)");
