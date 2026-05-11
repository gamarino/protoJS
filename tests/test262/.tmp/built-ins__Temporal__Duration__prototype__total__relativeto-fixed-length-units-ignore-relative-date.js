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


// Copyright (C) 2018 Bloomberg LP. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
esid: sec-temporal.duration.prototype.total
description: relativeTo option not required to round fixed-length units in durations without variable units.
features: [Temporal]
---*/

const d2 = new Temporal.Duration(0, 0, 0, 5, 5, 5, 5, 5, 5, 5);
const d2Nanoseconds = d2.days * 24 * 3600000000000 + d2.hours * 3600000000000 + d2.minutes * 60000000000 + d2.seconds * 1000000000 + d2.milliseconds * 1000000 + d2.microseconds * 1000 + d2.nanoseconds;
const totalD2 = {
  days: d2Nanoseconds / (24 * 3600000000000),
  hours: d2Nanoseconds / 3600000000000,
  minutes: d2Nanoseconds / 60000000000,
  seconds: d2Nanoseconds / 1000000000,
  milliseconds: d2Nanoseconds / 1000000,
  microseconds: d2Nanoseconds / 1000,
  nanoseconds: d2Nanoseconds
};

assert(Math.abs(d2.total({ unit: "days" }) - totalD2.days) < Number.EPSILON);
assert(Math.abs(d2.total({ unit: "hours" }) - totalD2.hours) < Number.EPSILON);
assert(Math.abs(d2.total({ unit: "minutes" }) - totalD2.minutes) < Number.EPSILON);
assert(Math.abs(d2.total({ unit: "seconds" }) - totalD2.seconds) < Number.EPSILON);
assert(Math.abs(d2.total({ unit: "milliseconds" }) - totalD2.milliseconds) < Number.EPSILON);
assert(Math.abs(d2.total({ unit: "microseconds" }) - totalD2.microseconds) < Number.EPSILON);
assert.sameValue(d2.total({ unit: "nanoseconds" }), totalD2.nanoseconds);

const negativeD2 = d2.negated();
assert(Math.abs(negativeD2.total({ unit: "days" }) - -totalD2.days) < Number.EPSILON);
assert(Math.abs(negativeD2.total({ unit: "hours" }) - -totalD2.hours) < Number.EPSILON);
assert(Math.abs(negativeD2.total({ unit: "minutes" }) - -totalD2.minutes) < Number.EPSILON);
assert(Math.abs(negativeD2.total({ unit: "seconds" }) - -totalD2.seconds) < Number.EPSILON);
assert(Math.abs(negativeD2.total({ unit: "milliseconds" }) - -totalD2.milliseconds) < Number.EPSILON);
assert(Math.abs(negativeD2.total({ unit: "microseconds" }) - -totalD2.microseconds) < Number.EPSILON);
assert.sameValue(negativeD2.total({ unit: "nanoseconds" }), -totalD2.nanoseconds);
