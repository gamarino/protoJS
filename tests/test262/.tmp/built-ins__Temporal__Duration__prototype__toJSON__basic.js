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


// Copyright (C) 2021 the V8 project authors. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-temporal.duration.prototype.tojson
description: Temporal.Duration.prototype.toJSON will return correct iso8601 string for the given duration.
info: |
  1. Let duration be the this value.
  2. Perform ? RequireInternalSlot(duration, [[InitializedTemporalDuration]]).
  3. Return ! TemporalDurationToString(duration.[[Years]], duration.[[Months]], duration.[[Weeks]], duration.[[Days]], duration.[[Hours]], duration.[[Minutes]], duration.[[Seconds]], duration.[[Milliseconds]], duration.[[Microseconds]], duration.[[Nanoseconds]], "auto").
features: [Temporal]
---*/

let d = new Temporal.Duration();
assert.sameValue(d.toJSON(), "PT0S", "blank duration");

d = new Temporal.Duration(1);
assert.sameValue(d.toJSON(), "P1Y", "positive small years");
d = new Temporal.Duration(-1);
assert.sameValue(d.toJSON(), "-P1Y", "negative small years");
d = new Temporal.Duration(1234567890);
assert.sameValue(d.toJSON(), "P1234567890Y", "positive large years");
d = new Temporal.Duration(-1234567890);
assert.sameValue(d.toJSON(), "-P1234567890Y", "negative large years");

d = new Temporal.Duration(1, 2);
assert.sameValue(d.toJSON(), "P1Y2M", "positive years and months");
d = new Temporal.Duration(-1, -2);
assert.sameValue(d.toJSON(), "-P1Y2M", "negative years and months");
d = new Temporal.Duration(0, 2);
assert.sameValue(d.toJSON(), "P2M", "positive small months");
d = new Temporal.Duration(0,-2);
assert.sameValue(d.toJSON(), "-P2M", "negative small months");
d = new Temporal.Duration(0, 1234567890);
assert.sameValue(d.toJSON(), "P1234567890M", "positive large months");
d = new Temporal.Duration(0,-1234567890);
assert.sameValue(d.toJSON(), "-P1234567890M", "negative large months");

d = new Temporal.Duration(1, 2, 3);
assert.sameValue(d.toJSON(), "P1Y2M3W", "positive years, months, weeks");
d = new Temporal.Duration(-1, -2, -3);
assert.sameValue(d.toJSON(), "-P1Y2M3W", "negative years, months, weeks");
d = new Temporal.Duration(0, 0, 3);
assert.sameValue(d.toJSON(), "P3W", "positive small weeks");
d = new Temporal.Duration(0, 0, -3);
assert.sameValue(d.toJSON(), "-P3W", "negative small weeks");
d = new Temporal.Duration(1, 0, 3);
assert.sameValue(d.toJSON(), "P1Y3W", "positive years and weeks");
d = new Temporal.Duration(-1, 0, -3);
assert.sameValue(d.toJSON(), "-P1Y3W", "negative years and weeks");
d = new Temporal.Duration(0, 2, 3);
assert.sameValue(d.toJSON(), "P2M3W", "positive months and weeks");
d = new Temporal.Duration(0, -2, -3);
assert.sameValue(d.toJSON(), "-P2M3W", "negative months and weeks");
d = new Temporal.Duration(0, 0, 1234567890);
assert.sameValue(d.toJSON(), "P1234567890W", "positive large weeks");
d = new Temporal.Duration(0, 0, -1234567890);
assert.sameValue(d.toJSON(), "-P1234567890W", "negative large weeks");

d = new Temporal.Duration(1, 2, 3, 4);
assert.sameValue(d.toJSON(), "P1Y2M3W4D", "positive years, months, weeks, days");
d = new Temporal.Duration(-1, -2, -3, -4);
assert.sameValue(d.toJSON(), "-P1Y2M3W4D", "negative years, months, weeks, days");
d = new Temporal.Duration(0, 0, 0, 1234567890);
assert.sameValue(d.toJSON(), "P1234567890D", "positive large days");
d = new Temporal.Duration(0, 0, 0, -1234567890);
assert.sameValue(d.toJSON(), "-P1234567890D", "negative large days");
d = new Temporal.Duration(0, 0, 0, 4);
assert.sameValue(d.toJSON(), "P4D", "positive small days");
d = new Temporal.Duration(0, 0, 0, -4);
assert.sameValue(d.toJSON(), "-P4D", "negative small days");
d = new Temporal.Duration(1, 0, 0, 4);
assert.sameValue(d.toJSON(), "P1Y4D", "positive years and days");
d = new Temporal.Duration(-1, 0, 0, -4);
assert.sameValue(d.toJSON(), "-P1Y4D", "negative years and days");
d = new Temporal.Duration(0, 2, 0, 4);
assert.sameValue(d.toJSON(), "P2M4D", "positive months and days");
d = new Temporal.Duration(0, -2, 0, -4);
assert.sameValue(d.toJSON(), "-P2M4D", "negative months and days");
d = new Temporal.Duration(0, 0, 3, 4);
assert.sameValue(d.toJSON(), "P3W4D", "positive weeks and days");
d = new Temporal.Duration(0, 0, -3, -4);
assert.sameValue(d.toJSON(), "-P3W4D", "negative weeks and days");

d = new Temporal.Duration(0, 0, 0, 0, 5);
assert.sameValue(d.toJSON(), "PT5H", "positive hours");
d = new Temporal.Duration(0, 0, 0, 0, -5);
assert.sameValue(d.toJSON(), "-PT5H", "negative hours");
d = new Temporal.Duration(1, 0, 0, 0, 5);
assert.sameValue(d.toJSON(), "P1YT5H", "positive years and hours");
d = new Temporal.Duration(-1, 0, 0, 0, -5);
assert.sameValue(d.toJSON(), "-P1YT5H", "negative years and hours");
d = new Temporal.Duration(0, 2, 0, 0, 5);
assert.sameValue(d.toJSON(), "P2MT5H", "positive months and hours");
d = new Temporal.Duration(0, -2, 0, 0, -5);
assert.sameValue(d.toJSON(), "-P2MT5H", "negative months and hours");

d = new Temporal.Duration(0, 0, 0, 0, 0, 6);
assert.sameValue(d.toJSON(), "PT6M", "positive minutes");
d = new Temporal.Duration(0, 0, 0, 0, 0, -6);
assert.sameValue(d.toJSON(), "-PT6M", "negative minutes");
d = new Temporal.Duration(0, 0, 0, 0, 5, 6);
assert.sameValue(d.toJSON(), "PT5H6M", "positive hours and minutes");
d = new Temporal.Duration(0, 0, 0, 0, -5, -6);
assert.sameValue(d.toJSON(), "-PT5H6M", "negative hours and minutes");
d = new Temporal.Duration(0, 0, 3, 0, 0, 6);
assert.sameValue(d.toJSON(), "P3WT6M", "positive weeks and minutes");
d = new Temporal.Duration(0, 0, -3, 0, 0, -6);
assert.sameValue(d.toJSON(), "-P3WT6M", "negative weeks and minutes");
d = new Temporal.Duration(0, 0, 0, 4, 0, 6);
assert.sameValue(d.toJSON(), "P4DT6M", "positive days and minutes");
d = new Temporal.Duration(0, 0, 0, -4, 0, -6);
assert.sameValue(d.toJSON(), "-P4DT6M", "negative days and minutes");

d = new Temporal.Duration(0, 0, 0, 0, 0, 0, 7);
assert.sameValue(d.toJSON(), "PT7S", "positive seconds");
d = new Temporal.Duration(0, 0, 0, 0, 0, 0, -7);
assert.sameValue(d.toJSON(), "-PT7S", "negative seconds");
d = new Temporal.Duration(0, 0, 0, 0, 5, 0, 7);
assert.sameValue(d.toJSON(), "PT5H7S", "positive hours and seconds");
d = new Temporal.Duration(0, 0, 0, 0, -5, 0, -7);
assert.sameValue(d.toJSON(), "-PT5H7S", "negative hours and seconds");
d = new Temporal.Duration(0, 0, 0, 0, 0, 6, 7);
assert.sameValue(d.toJSON(), "PT6M7S", "positive minutes and seconds");
d = new Temporal.Duration(0, 0, 0, 0, 0, -6, -7);
assert.sameValue(d.toJSON(), "-PT6M7S", "negative minutes and seconds");
d = new Temporal.Duration(0, 0, 0, 0, 5, 6, 7);
assert.sameValue(d.toJSON(), "PT5H6M7S", "positive hours, minutes, seconds");
d = new Temporal.Duration(0, 0, 0, 0, -5, -6, -7);
assert.sameValue(d.toJSON(), "-PT5H6M7S", "negative hours, minutes, seconds");
d = new Temporal.Duration(1, 0, 0, 0, 5, 6, 7);
assert.sameValue(d.toJSON(), "P1YT5H6M7S", "positive years, hours, minutes, seconds");
d = new Temporal.Duration(-1, 0, 0, 0, -5, -6, -7);
assert.sameValue(d.toJSON(), "-P1YT5H6M7S", "negative years, hours, minutes, seconds");

d = new Temporal.Duration(0, 0, 0, 0, 0, 0, 0, 8);
assert.sameValue(d.toJSON(), "PT0.008S", "positive milliseconds");
d = new Temporal.Duration(0, 0, 0, 0, 0, 0, 0, -8);
assert.sameValue(d.toJSON(), "-PT0.008S", "negative milliseconds");
d = new Temporal.Duration(0, 0, 0, 0, 0, 0, 0, 80);
assert.sameValue(d.toJSON(), "PT0.08S", "positive milliseconds multiple of 10");
d = new Temporal.Duration(0, 0, 0, 0, 0, 0, 0, -80);
assert.sameValue(d.toJSON(), "-PT0.08S", "negative milliseconds multiple of 10");
d = new Temporal.Duration(0, 0, 0, 0, 0, 0, 0, 87);
assert.sameValue(d.toJSON(), "PT0.087S", "positive two-digit milliseconds");
d = new Temporal.Duration(0, 0, 0, 0, 0, 0, 0, -87);
assert.sameValue(d.toJSON(), "-PT0.087S", "negative two-digit milliseconds");
d = new Temporal.Duration(0, 0, 0, 0, 0, 0, 0, 876);
assert.sameValue(d.toJSON(), "PT0.876S", "positive three-digit milliseconds");
d = new Temporal.Duration(0, 0, 0, 0, 0, 0, 0, -876);
assert.sameValue(d.toJSON(), "-PT0.876S", "negative three-digit milliseconds");
d = new Temporal.Duration(0, 0, 0, 0, 0, 0, 0, 876543);
assert.sameValue(d.toJSON(), "PT876.543S", "positive large milliseconds");
d = new Temporal.Duration(0, 0, 0, 0, 0, 0, 0, -876543);
assert.sameValue(d.toJSON(), "-PT876.543S", "negative large milliseconds");

d = new Temporal.Duration(0, 0, 0, 0, 0, 0, 0, 0, 9);
assert.sameValue(d.toJSON(), "PT0.000009S", "positive microseconds");
d = new Temporal.Duration(0, 0, 0, 0, 0, 0, 0, 0, -9);
assert.sameValue(d.toJSON(), "-PT0.000009S", "negative microseconds");
d = new Temporal.Duration(0, 0, 0, 0, 0, 0, 0, 0, 90);
assert.sameValue(d.toJSON(), "PT0.00009S", "positive microseconds multiple of 10");
d = new Temporal.Duration(0, 0, 0, 0, 0, 0, 0, 0, -90);
assert.sameValue(d.toJSON(), "-PT0.00009S", "negative microseconds multiple of 10");
d = new Temporal.Duration(0, 0, 0, 0, 0, 0, 0, 0, 98);
assert.sameValue(d.toJSON(), "PT0.000098S", "positive two-digit microseconds");
d = new Temporal.Duration(0, 0, 0, 0, 0, 0, 0, 0, -98);
assert.sameValue(d.toJSON(), "-PT0.000098S", "negative two-digit microseconds");
d = new Temporal.Duration(0, 0, 0, 0, 0, 0, 0, 0, 900);
assert.sameValue(d.toJSON(), "PT0.0009S", "positive microseconds multiple of 100");
d = new Temporal.Duration(0, 0, 0, 0, 0, 0, 0, 0, -900);
assert.sameValue(d.toJSON(), "-PT0.0009S", "negative microseconds multiple of 100");
d = new Temporal.Duration(0, 0, 0, 0, 0, 0, 0, 0, 987);
assert.sameValue(d.toJSON(), "PT0.000987S", "positive three-digit microseconds");
d = new Temporal.Duration(0, 0, 0, 0, 0, 0, 0, 0, -987);
assert.sameValue(d.toJSON(), "-PT0.000987S", "negative three-digit microseconds");
d = new Temporal.Duration(0, 0, 0, 0, 0, 0, 0, 0, 987654);
assert.sameValue(d.toJSON(), "PT0.987654S", "positive large microseconds");
d = new Temporal.Duration(0, 0, 0, 0, 0, 0, 0, 0, -987654);
assert.sameValue(d.toJSON(), "-PT0.987654S", "negative large microseconds");
d = new Temporal.Duration(0, 0, 0, 0, 0, 0, 0, 0, 987654321);
assert.sameValue(d.toJSON(), "PT987.654321S", "positive larger microseconds");
d = new Temporal.Duration(0, 0, 0, 0, 0, 0, 0, 0, -987654321);
assert.sameValue(d.toJSON(), "-PT987.654321S", "negative larger microseconds");

d = new Temporal.Duration(0, 0, 0, 0, 0, 0, 0, 0, 0, 1);
assert.sameValue(d.toJSON(), "PT0.000000001S", "positive nanoseconds");
d = new Temporal.Duration(0, 0, 0, 0, 0, 0, 0, 0, 0, -1);
assert.sameValue(d.toJSON(), "-PT0.000000001S", "negative nanoseconds");
d = new Temporal.Duration(0, 0, 0, 0, 0, 0, 0, 0, 0, 10);
assert.sameValue(d.toJSON(), "PT0.00000001S", "positive nanoseconds multiple of 10");
d = new Temporal.Duration(0, 0, 0, 0, 0, 0, 0, 0, 0, -10);
assert.sameValue(d.toJSON(), "-PT0.00000001S", "negative nanoseconds multiple of 10");
d = new Temporal.Duration(0, 0, 0, 0, 0, 0, 0, 0, 0, 12);
assert.sameValue(d.toJSON(), "PT0.000000012S", "positive two-digit nanoseconds");
d = new Temporal.Duration(0, 0, 0, 0, 0, 0, 0, 0, 0, -12);
assert.sameValue(d.toJSON(), "-PT0.000000012S", "negative two-digit nanoseconds");
d = new Temporal.Duration(0, 0, 0, 0, 0, 0, 0, 0, 0, 100);
assert.sameValue(d.toJSON(), "PT0.0000001S", "positive nanoseconds multiple of 100");
d = new Temporal.Duration(0, 0, 0, 0, 0, 0, 0, 0, 0, -100);
assert.sameValue(d.toJSON(), "-PT0.0000001S", "negative nanoseconds multiple of 100");
d = new Temporal.Duration(0, 0, 0, 0, 0, 0, 0, 0, 0, 123);
assert.sameValue(d.toJSON(), "PT0.000000123S", "positive three-digit nanoseconds");
d = new Temporal.Duration(0, 0, 0, 0, 0, 0, 0, 0, 0, -123);
assert.sameValue(d.toJSON(), "-PT0.000000123S", "negative three-digit nanoseconds");
d = new Temporal.Duration(0, 0, 0, 0, 0, 0, 0, 0, 0, 123456);
assert.sameValue(d.toJSON(), "PT0.000123456S", "positive large nanoseconds");
d = new Temporal.Duration(0, 0, 0, 0, 0, 0, 0, 0, 0, -123456);
assert.sameValue(d.toJSON(), "-PT0.000123456S", "negative large nanoseconds");
d = new Temporal.Duration(0, 0, 0, 0, 0, 0, 0, 0, 0, 123456789);
assert.sameValue(d.toJSON(), "PT0.123456789S", "positive larger nanoseconds");
d = new Temporal.Duration(0, 0, 0, 0, 0, 0, 0, 0, 0, -123456789);
assert.sameValue(d.toJSON(), "-PT0.123456789S", "negative larger nanoseconds");
d = new Temporal.Duration(0, 0, 0, 0, 0, 0, 0, 0, 0, 1234567891);
assert.sameValue(d.toJSON(), "PT1.234567891S", "positive even larger nanoseconds");
d = new Temporal.Duration(0, 0, 0, 0, 0, 0, 0, 0, 0, -1234567891);
assert.sameValue(d.toJSON(), "-PT1.234567891S", "negative even larger nanoseconds");

d = new Temporal.Duration(0, 0, 0, 0, 0, 0, 4, 3, 2, 1);
assert.sameValue(d.toJSON(), "PT4.003002001S", "positive seconds and subseconds");
d = new Temporal.Duration(0, 0, 0, 0, 0, 0, -4, -3, -2, -1);
assert.sameValue(d.toJSON(), "-PT4.003002001S", "negative seconds and subseconds");
d = new Temporal.Duration(0, 0, 0, 0, 0, 0, 4, 3, 2, 90001);
assert.sameValue(d.toJSON(), "PT4.003092001S", "positive seconds and large subseconds");
d = new Temporal.Duration(0, 0, 0, 0, 0, 0, -4, -3, -2, -90001);
assert.sameValue(d.toJSON(), "-PT4.003092001S", "negative seconds and large subseconds");
d = new Temporal.Duration(0, 0, 0, 0, 0, 0, 4, 3, 2, 90080001);
assert.sameValue(d.toJSON(), "PT4.093082001S", "positive seconds and larger subseconds");
d = new Temporal.Duration(0, 0, 0, 0, 0, 0, -4, -3, -2, -90080001);
assert.sameValue(d.toJSON(), "-PT4.093082001S", "negative seconds and larger subseconds");

d = new Temporal.Duration(1, 2, 3, 4, 5, 6, 7, 8, 9, 1);
assert.sameValue(d.toJSON(), "P1Y2M3W4DT5H6M7.008009001S", "all fields positive");
d = new Temporal.Duration(-1, -2, -3, -4, -5, -6, -7, -8, -9, -1);
assert.sameValue(d.toJSON(), "-P1Y2M3W4DT5H6M7.008009001S", "all fields negative");

d = new Temporal.Duration(1234, 2345, 3456, 4567, 5678, 6789, 7890, 890, 901, 123);
assert.sameValue(d.toJSON(), "P1234Y2345M3456W4567DT5678H6789M7890.890901123S", "all fields large and positive");
d = new Temporal.Duration(-1234, -2345, -3456, -4567, -5678, -6789, -7890, -890, -901, -123);
assert.sameValue(d.toJSON(), "-P1234Y2345M3456W4567DT5678H6789M7890.890901123S", "all fields large and negative");
