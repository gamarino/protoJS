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
esid: sec-temporal.plaintime.prototype.add
description: Duration-like argument that is out of range
features: [Temporal]
---*/

const instance = new Temporal.PlainTime();

const cases = [
  // 2^32 = 4294967296
  ["P4294967296Y", "string with years > max"],
  [{ years: 4294967296 }, "property bag with years > max"],
  ["-P4294967296Y", "string with years < min"],
  [{ years: -4294967296 }, "property bag with years < min"],
  ["P4294967296M", "string with months > max"],
  [{ months: 4294967296 }, "property bag with months > max"],
  ["-P4294967296M", "string with months < min"],
  [{ months: -4294967296 }, "property bag with months < min"],
  ["P4294967296W", "string with weeks > max"],
  [{ weeks: 4294967296 }, "property bag with weeks > max"],
  ["-P4294967296W", "string with weeks < min"],
  [{ weeks: -4294967296 }, "property bag with weeks < min"],

  // ceil(max safe integer / 86400) = 104249991375
  ["P104249991375D", "string with days > max"],
  [{ days: 104249991375 }, "property bag with days > max"],
  ["P104249991374DT24H", "string where hours balance into days > max"],
  [{ days: 104249991374, hours: 24 }, "property bag where hours balance into days > max"],
  ["-P104249991375D", "string with days < min"],
  [{ days: -104249991375 }, "property bag with days < min"],
  ["-P104249991374DT24H", "string where hours balance into days < min"],
  [{ days: -104249991374, hours: -24 }, "property bag where hours balance into days < min"],

  // ceil(max safe integer / 3600) = 2501999792984
  ["PT2501999792984H", "string with hours > max"],
  [{ hours: 2501999792984 }, "property bag with hours > max"],
  ["PT2501999792983H60M", "string where minutes balance into hours > max"],
  [{ hours: 2501999792983, minutes: 60 }, "property bag where minutes balance into hours > max"],
  ["-PT2501999792984H", "string with hours < min"],
  [{ hours: -2501999792984 }, "property bag with hours < min"],
  ["-PT2501999792983H60M", "string where minutes balance into hours < min"],
  [{ hours: -2501999792983, minutes: -60 }, "property bag where minutes balance into hours < min"],

  // ceil(max safe integer / 60) = 150119987579017
  ["PT150119987579017M", "string with minutes > max"],
  [{ minutes: 150119987579017 }, "property bag with minutes > max"],
  ["PT150119987579016M60S", "string where seconds balance into minutes > max"],
  [{ minutes: 150119987579016, seconds: 60 }, "property bag where seconds balance into minutes > max"],
  ["-PT150119987579017M", "string with minutes < min"],
  [{ minutes: -150119987579017 }, "property bag with minutes < min"],
  ["-PT150119987579016M60S", "string where seconds balance into minutes < min"],
  [{ minutes: -150119987579016, seconds: -60 }, "property bag where seconds balance into minutes < min"],

  // 2^53 = 9007199254740992
  ["PT9007199254740992S", "string with seconds > max"],
  [{ seconds: 9007199254740992 }, "property bag with seconds > max"],
  [{ seconds: 9007199254740991, milliseconds: 1000 }, "property bag where milliseconds balance into seconds > max"],
  [{ seconds: 9007199254740991, microseconds: 1000000 }, "property bag where microseconds balance into seconds > max"],
  [{ seconds: 9007199254740991, nanoseconds: 1000000000 }, "property bag where nanoseconds balance into seconds > max"],
  ["-PT9007199254740992S", "string with seconds < min"],
  [{ seconds: -9007199254740992 }, "property bag with seconds < min"],
  [{ seconds: -9007199254740991, milliseconds: -1000 }, "property bag where milliseconds balance into seconds < min"],
  [{ seconds: -9007199254740991, microseconds: -1000000 }, "property bag where microseconds balance into seconds < min"],
  [{ seconds: -9007199254740991, nanoseconds: -1000000000 }, "property bag where nanoseconds balance into seconds < min"],
];

for (const [arg, descr] of cases) {
  assert.throws(RangeError, () => instance.add(arg), `${descr} is out of range`);
}
