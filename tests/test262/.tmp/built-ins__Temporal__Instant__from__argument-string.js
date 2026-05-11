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
esid: sec-temporal.instant.from
description: Various string arguments.
features: [Temporal]
---*/

const tests = [
  ['1976-11-18T15:23z', 217178580000000000n],
  ['1976-11-18T15:23:30.1Z', 217178610100000000n],
  ['1976-11-18T15:23:30.12Z', 217178610120000000n],
  ['1976-11-18T15:23:30.123Z', 217178610123000000n],
  ['1976-11-18T15:23:30.1234Z', 217178610123400000n],
  ['1976-11-18T15:23:30.12345Z', 217178610123450000n],
  ['1976-11-18T15:23:30.123456Z', 217178610123456000n],
  ['1976-11-18T15:23:30.1234567Z', 217178610123456700n],
  ['1976-11-18T15:23:30.12345678Z', 217178610123456780n],
  ['1976-11-18T15:23:30.123456789Z', 217178610123456789n],
  ['1976-11-18T15:23:30,12Z', 217178610120000000n],
  ['1976-11-18T15:23:30.12-02:00', 217185810120000000n],
  ['-009999-11-18T15:23:30.12Z', -377677326989880000000n],
  ['19761118T15:23:30.1+00:00', 217178610100000000n],
  ['1976-11-18T152330.1+00:00', 217178610100000000n],
  ['1976-11-18T15:23:30.1+0000', 217178610100000000n],
  ['1976-11-18T152330.1+0000', 217178610100000000n],
  ['19761118T15:23:30.1+0000', 217178610100000000n],
  ['19761118T152330.1+00:00', 217178610100000000n],
  ['+0019761118T15:23:30.1+00:00', 217178610100000000n],
  ['+001976-11-18T152330.1+00:00', 217178610100000000n],
  ['+001976-11-18T15:23:30.1+0000', 217178610100000000n],
  ['+001976-11-18T152330.1+0000', 217178610100000000n],
  ['+0019761118T15:23:30.1+0000', 217178610100000000n],
  ['+0019761118T152330.1+00:00', 217178610100000000n],
  ['+0019761118T152330.1+0000', 217178610100000000n],
  ['1976-11-18T15:23:30+00', 217178610000000000n],
  ['1976-11-18T15:23:30.123456789-00:00:00', 217178610123456789n],
  ['1976-11-18T15:23:30.123456789-00:00:00.0', 217178610123456789n],
  ['1976-11-18T15:23:30.123456789-00:00:00.00', 217178610123456789n],
  ['1976-11-18T15:23:30.123456789-00:00:00.000', 217178610123456789n],
  ['1976-11-18T15:23:30.123456789-00:00:00.0000', 217178610123456789n],
  ['1976-11-18T15:23:30.123456789-00:00:00.00000', 217178610123456789n],
  ['1976-11-18T15:23:30.123456789-00:00:00.000000', 217178610123456789n],
  ['1976-11-18T15:23:30.123456789-00:00:00.0000000', 217178610123456789n],
  ['1976-11-18T15:23:30.123456789-00:00:00.00000000', 217178610123456789n],
  ['1976-11-18T15:23:30.123456789-00:00:00.000000000', 217178610123456789n],
  ['1976-11-18T15:23:30.123456789-00:00:00.1', 217178610223456789n],
  ['1976-11-18T15:23:30.123456789-00:00:00.01', 217178610133456789n],
  ['1976-11-18T15:23:30.123456789-00:00:00.001', 217178610124456789n],
  ['1976-11-18T15:23:30.123456789-00:00:00.0001', 217178610123556789n],
  ['1976-11-18T15:23:30.123456789-00:00:00.00001', 217178610123466789n],
  ['1976-11-18T15:23:30.123456789-00:00:00.000001', 217178610123457789n],
  ['1976-11-18T15:23:30.123456789-00:00:00.0000001', 217178610123456889n],
  ['1976-11-18T15:23:30.123456789-00:00:00.00000001', 217178610123456799n],
  ['1976-11-18T15:23:30.123456789-00:00:00.000000001', 217178610123456790n],
  ['1976-11-18T15:23:30.123456789-00:00:00.100000000', 217178610223456789n],
  ['1976-11-18T15:23:30.123456789-00:00:00.010000000', 217178610133456789n],
  ['1976-11-18T15:23:30.123456789-00:00:00.001000000', 217178610124456789n],
  ['1976-11-18T15:23:30.123456789-00:00:00.000100000', 217178610123556789n],
  ['1976-11-18T15:23:30.123456789-00:00:00.000010000', 217178610123466789n],
  ['1976-11-18T15:23:30.123456789-00:00:00.000001000', 217178610123457789n],
  ['1976-11-18T15:23:30.123456789-00:00:00.000000100', 217178610123456889n],
  ['1976-11-18T15:23:30.123456789-00:00:00.000000010', 217178610123456799n],
  ['1976-11-18T15Z', 217177200000000000n],
  ['1976-11-18T15:23:30.123456789Z[u-ca=discord]', 217178610123456789n],
  ['1976-11-18T15:23:30.123456789Z[+00]', 217178610123456789n],
  ['1976-11-18T15:23:30.123456789Z[-00]', 217178610123456789n],
  ['1976-11-18T15:23:30.123456789Z[-00:00]', 217178610123456789n],
  ['1976-11-18T15:23:30.123456789Z[+12]', 217178610123456789n],
  ['1976-11-18T15:23:30.123456789Z[NotATimeZone]', 217178610123456789n],
];

for (const [arg, expected] of tests) {
  const result = Temporal.Instant.from(arg);
  assert.sameValue(result.epochNanoseconds, expected, `Instant.from(${arg})`);
}
