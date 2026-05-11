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
description: BalanceTimeDuration computes on exact mathematical values.
features: [BigInt, Temporal]
---*/

const seconds = 8692288669465520;

{
  const milliseconds = 513;
  const d = new Temporal.Duration(0, 0, 0, 0, 0, 0, seconds, milliseconds);

  const result = d.total({ unit: "milliseconds" });

  // The result should be the nearest Number value to 8692288669465520512
  const expectedMilliseconds = Number(BigInt(seconds) * 1000n + BigInt(milliseconds));
  assert.sameValue(expectedMilliseconds, 8692288669465520_513, "check expected value (ms)");

  assert.sameValue(
    result, expectedMilliseconds,
    "BalanceTimeDuration should implement floating-point calculation correctly for largestUnit milliseconds"
  );
}

{
  const microseconds = 373761;
  const d = new Temporal.Duration(0, 0, 0, 0, 0, 0, seconds, 0, microseconds);

  const result = d.total({ unit: "microseconds" });

  // The result should be the nearest Number value to 8692288669465520373761
  const expectedMicroseconds = Number(BigInt(seconds) * 1_000_000n + BigInt(microseconds));
  assert.sameValue(expectedMicroseconds, 8692288669465520_373_761, "check expected value (µs)");

  assert.sameValue(
    result, expectedMicroseconds,
    "BalanceTimeDuration should implement floating-point calculation correctly for largestUnit microseconds"
  );
}

{
  const nanoseconds = 321_414_345;
  const d = new Temporal.Duration(0, 0, 0, 0, 0, 0, seconds, 0, 0, nanoseconds);

  const result = d.total({ unit: "nanoseconds" });

  // The result should be the nearest Number value to 8692288669465520321414345
  const expectedNanoseconds = Number(BigInt(seconds) * 1_000_000_000n + BigInt(nanoseconds));
  assert.sameValue(expectedNanoseconds, 8692288669465520_321_414_345, "check expected value (ns)");

  assert.sameValue(
    result, expectedNanoseconds,
    "BalanceTimeDuration should implement floating-point calculation correctly for largestUnit nanoseconds"
  );
}

{
  const d = new Temporal.Duration(0, 0, 5, 5);

  const result = d.total({ unit: "months", relativeTo: "1972-01-31" })

/*
Expected months checked using Decimals in Python:

>>> from decimal import *
>>> getcontext().prec = 18
>>> dest_epoch_ns = Decimal(69120000000000000)
>>> start_epoch_ns = Decimal(68169600000000000)
>>> end_epoch_ns = 70848000000000000
>>> progress = ((dest_epoch_ns - start_epoch_ns) / (end_epoch_ns - start_epoch_ns))
>>> progress
Decimal('0.354838709677419355')
>>> Decimal(1) + progress
Decimal('1.35483870967741936')

The result should be truncated.
*/
  const expectedMonths = 1.3548387096774193;

  assert.sameValue(result, expectedMonths,
    "NudgeToCalendarUnit should implement floating-point calculation correctly for largestUnit months");
}
