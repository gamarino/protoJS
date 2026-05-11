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


// Copyright (C) 2023 Igalia, S.L. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-temporal.duration.prototype.total
description: RangeError thrown when calendar part of duration added to relativeTo is out of range
features: [Temporal]
info: |
  RoundDuration:
  10.h. Let _isoResult_ be ! AddISODate(_plainRelativeTo_.[[ISOYear]]. _plainRelativeTo_.[[ISOMonth]], _plainRelativeTo_.[[ISODay]], 0, 0, 0, truncate(_fractionalDays_), *"constrain"*).
    i. Let _wholeDaysLater_ be ? CreateTemporalDate(_isoResult_.[[Year]], _isoResult_.[[Month]], _isoResult_.[[Day]], _calendar_).
  ...
  11.h. Let _isoResult_ be ! AddISODate(_plainRelativeTo_.[[ISOYear]]. _plainRelativeTo_.[[ISOMonth]], _plainRelativeTo_.[[ISODay]], 0, 0, 0, truncate(_fractionalDays_), *"constrain"*).
    i. Let _wholeDaysLater_ be ? CreateTemporalDate(_isoResult_.[[Year]], _isoResult_.[[Month]], _isoResult_.[[Day]], _calendar_).
  ...
  12.a. Let _isoResult_ be ! AddISODate(_plainRelativeTo_.[[ISOYear]]. _plainRelativeTo_.[[ISOMonth]], _plainRelativeTo_.[[ISODay]], 0, 0, 0, truncate(_fractionalDays_), *"constrain"*).
    b. Let _wholeDaysLater_ be ? CreateTemporalDate(_isoResult_.[[Year]], _isoResult_.[[Month]], _isoResult_.[[Day]], _calendar_).

  UnbalanceDateDurationRelative:
  11. Let _yearsMonthsWeeksDuration_ be ! CreateTemporalDuration(_years_, _months_, _weeks_, 0, 0, 0, 0, 0, 0, 0).
  12. Let _later_ be ? CalendarDateAdd(_calendaRec_, _plainRelativeTo_, _yearsMonthsWeeksDuration_).
  13. Let _yearsMonthsWeeksInDays_ be DaysUntil(_plainRelativeTo_, _later_).
  14. Return ? CreateDateDurationRecord(0, 0, 0, _days_ + _yearsMonthsWeeksInDays_).
---*/

// Based on a test case by André Bargull <andre.bargull@gmail.com>

const relativeTo = new Temporal.PlainDate(2000, 1, 1);

{
  const instance = new Temporal.Duration(0, 0, 0, /* days = */ 500_000_000);
  assert.throws(RangeError, () => instance.total({relativeTo, unit: "years"}), "days out of range, positive, unit years");
  assert.throws(RangeError, () => instance.total({relativeTo, unit: "months"}), "days out of range, positive, unit months");
  assert.throws(RangeError, () => instance.total({relativeTo, unit: "weeks"}), "days out of range, positive, unit weeks");

  const negInstance = new Temporal.Duration(0, 0, 0, /* days = */ -500_000_000);
  assert.throws(RangeError, () => negInstance.total({relativeTo, unit: "years"}), "days out of range, negative, unit years");
  assert.throws(RangeError, () => negInstance.total({relativeTo, unit: "months"}), "days out of range, negative, unit months");
  assert.throws(RangeError, () => negInstance.total({relativeTo, unit: "weeks"}), "days out of range, negative, unit weeks");
}

{
  const instance = new Temporal.Duration(0, 0, /* weeks = */ 1, /* days = */ Math.trunc((2 ** 53) / 86_400));
  assert.throws(RangeError, () => instance.total({relativeTo, unit: "days"}), "weeks + days out of range, positive");

  const negInstance = new Temporal.Duration(0, 0, /* weeks = */ -1, /* days = */ -Math.trunc((2 ** 53) / 86_400));
  assert.throws(RangeError, () => instance.total({relativeTo, unit: "days"}), "weeks + days out of range, negative");
}
