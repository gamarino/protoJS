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


// Copyright (C) 2015 the V8 project authors. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
info: Correct interpretation of RUSSIAN ALPHABET
es6id: 11.6
description: Check RUSSIAN CAPITAL ALPHABET
---*/

var \u{410} = 1;
assert.sameValue(А, 1);

var \u{411} = 1;
assert.sameValue(Б, 1);

var \u{412} = 1;
assert.sameValue(В, 1);

var \u{413} = 1;
assert.sameValue(Г, 1);

var \u{414} = 1;
assert.sameValue(Д, 1);

var \u{415} = 1;
assert.sameValue(Е, 1);

var \u{416} = 1;
assert.sameValue(Ж, 1);

var \u{417} = 1;
assert.sameValue(З, 1);

var \u{418} = 1;
assert.sameValue(И, 1);

var \u{419} = 1;
assert.sameValue(Й, 1);

var \u{41A} = 1;
assert.sameValue(К, 1);

var \u{41B} = 1;
assert.sameValue(Л, 1);

var \u{41C} = 1;
assert.sameValue(М, 1);

var \u{41D} = 1;
assert.sameValue(Н, 1);

var \u{41E} = 1;
assert.sameValue(О, 1);

var \u{41F} = 1;
assert.sameValue(П, 1);

var \u{420} = 1;
assert.sameValue(Р, 1);

var \u{421} = 1;
assert.sameValue(С, 1);

var \u{422} = 1;
assert.sameValue(Т, 1);

var \u{423} = 1;
assert.sameValue(У, 1);

var \u{424} = 1;
assert.sameValue(Ф, 1);

var \u{425} = 1;
assert.sameValue(Х, 1);

var \u{426} = 1;
assert.sameValue(Ц, 1);

var \u{427} = 1;
assert.sameValue(Ч, 1);

var \u{428} = 1;
assert.sameValue(Ш, 1);

var \u{429} = 1;
assert.sameValue(Щ, 1);

var \u{42A} = 1;
assert.sameValue(Ъ, 1);

var \u{42B} = 1;
assert.sameValue(Ы, 1);

var \u{42C} = 1;
assert.sameValue(Ь, 1);

var \u{42D} = 1;
assert.sameValue(Э, 1);

var \u{42E} = 1;
assert.sameValue(Ю, 1);

var \u{42F} = 1;
assert.sameValue(Я, 1);

var \u{401} = 1;
assert.sameValue(Ё, 1);
