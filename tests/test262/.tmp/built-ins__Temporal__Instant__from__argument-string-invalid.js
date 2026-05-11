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


// Copyright (C) 2022 Igalia S.L. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-temporal.instant.from
description: >
  RangeError thrown if an invalid ISO string (or syntactically valid ISO string
  that is not supported) is used as an Instant
features: [Temporal, arrow-function]
---*/

const invalidStrings = [
  // invalid ISO strings:
  "",
  "invalid iso8601",
  "2020-01-00T00:00Z",
  "2020-01-32T00:00Z",
  "2020-02-30T00:00Z",
  "2021-02-29T00:00Z",
  "2020-00-01T00:00Z",
  "2020-13-01T00:00Z",
  "2020-01-01TZ",
  "2020-01-01T25:00:00Z",
  "2020-01-01T01:60:00Z",
  "2020-01-01T01:60:61Z",
  "2020-01-01T00:00Zjunk",
  "2020-01-01T00:00:00Zjunk",
  "2020-01-01T00:00:00.000000000Zjunk",
  "2020-01-01T00:00:00+00:00junk",
  "2020-01-01T00:00:00+00:00[UTC]junk",
  "2020-01-01T00:00:00+00:00[UTC][u-ca=iso8601]junk",
  "02020-01-01T00:00Z",
  "2020-001-01T00:00Z",
  "2020-01-001T00:00Z",
  "2020-01-01T001Z",
  "2020-01-01T01:001Z",
  "2020-01-01T01:01:001Z",
  "2020-01-01T00:00-24:00",
  "2020-01-01T00:00+24:00",
  // valid, but forms not supported in Temporal:
  "2020-W01-1T00:00Z",
  "2020-001T00:00Z",
  "+0002020-01-01T00:00Z",
  // may be valid in other contexts, but insufficient information for Instant:
  "2020-01",
  "+002020-01",
  "01-01",
  "2020-W01",
  "P1Y",
  "-P12Y",
  "2020-01-01",
  "2020-01-01T00",
  "2020-01-01T00:00",
  "2020-01-01T00:00:00",
  "2020-01-01T00:00:00.000000000",
  // valid, but outside the supported range:
  "-999999-01-01T00:00Z",
  "+999999-01-01T00:00Z",
  // "00:0000" is invalid (the hour/minute and minute/second separator
  // or lack thereof needs to match).
  "2025-01-01T00:00:00+00:0000",
];
for (const arg of invalidStrings) {
  assert.throws(
    RangeError,
    () => Temporal.Instant.from(arg),
    `"${arg}" should not be a valid ISO string for an Instant`
  );
}
