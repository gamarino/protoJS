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
esid: sec-temporal.duration.from
description: Maximum allowed duration
features: [Temporal]
---*/

const maxCases = [
  ["P4294967295Y104249991374DT7H36M31.999999999S", "string with max years"],
  [{ years: 4294967295, days: 104249991374, nanoseconds: 27391999999999 }, "property bag with max years"],
  ["P4294967295M104249991374DT7H36M31.999999999S", "string with max months"],
  [{ months: 4294967295, days: 104249991374, nanoseconds: 27391999999999 }, "property bag with max months"],
  ["P4294967295W104249991374DT7H36M31.999999999S", "string with max weeks"],
  [{ weeks: 4294967295, days: 104249991374, nanoseconds: 27391999999999 }, "property bag with max weeks"],
  ["P104249991374DT7H36M31.999999999S", "string with max days"],
  [{ days: 104249991374, nanoseconds: 27391999999999 }, "property bag with max days"],
  ["PT2501999792983H36M31.999999999S", "string with max hours"],
  [{ hours: 2501999792983, nanoseconds: 2191999999999 }, "property bag with max hours"],
  ["PT150119987579016M31.999999999S", "string with max minutes"],
  [{ minutes: 150119987579016, nanoseconds: 31999999999 }, "property bag with max minutes"],
  ["PT9007199254740991.999999999S", "string with max seconds"],
  [{ seconds: 9007199254740991, nanoseconds: 999999999 }, "property bag with max seconds"],
];

for (const [arg, descr] of maxCases) {
  const result = Temporal.Duration.from(arg);
  assert.sameValue(result.with({ years: 0, months: 0, weeks: 0 }).total("seconds"), 9007199254740991.999999999, `operation succeeds with ${descr}`);
}

const minCases = [
  ["-P4294967295Y104249991374DT7H36M31.999999999S", "string with min years"],
  [{ years: -4294967295, days: -104249991374, nanoseconds: -27391999999999 }, "property bag with min years"],
  ["-P4294967295M104249991374DT7H36M31.999999999S", "string with min months"],
  [{ months: -4294967295, days: -104249991374, nanoseconds: -27391999999999 }, "property bag with min months"],
  ["-P4294967295W104249991374DT7H36M31.999999999S", "string with min weeks"],
  [{ weeks: -4294967295, days: -104249991374, nanoseconds: -27391999999999 }, "property bag with min weeks"],
  ["-P104249991374DT7H36M31.999999999S", "string with min days"],
  [{ days: -104249991374, nanoseconds: -27391999999999 }, "property bag with min days"],
  ["-PT2501999792983H36M31.999999999S", "string with min hours"],
  [{ hours: -2501999792983, nanoseconds: -2191999999999 }, "property bag with min hours"],
  ["-PT150119987579016M31.999999999S", "string with min minutes"],
  [{ minutes: -150119987579016, nanoseconds: -31999999999 }, "property bag with min minutes"],
  ["-PT9007199254740991.999999999S", "string with min seconds"],
  [{ seconds: -9007199254740991, nanoseconds: -999999999 }, "property bag with min seconds"],
];

for (const [arg, descr] of minCases) {
  const result = Temporal.Duration.from(arg);
  assert.sameValue(result.with({ years: 0, months: 0, weeks: 0 }).total("seconds"), -9007199254740991.999999999, `operation succeeds with ${descr}`);
}
