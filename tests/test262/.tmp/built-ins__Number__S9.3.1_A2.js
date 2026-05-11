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


// Copyright 2009 the Sputnik authors.  All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
info: "The MV of StringNumericLiteral ::: StrWhiteSpace is 0"
es5id: 9.3.1_A2
description: >
    Strings with various WhiteSpaces convert to Number by explicit
    transformation
---*/
assert.sameValue(
  Number("\u0009\u000C\u0020\u00A0\u000B\u000A\u000D\u2028\u2029\u1680\u2000\u2001\u2002\u2003\u2004\u2005\u2006\u2007\u2008\u2009\u200A\u202F\u205F\u3000"),
  0,
  'Number("u0009u000Cu0020u00A0u000Bu000Au000Du2028u2029u1680u2000u2001u2002u2003u2004u2005u2006u2007u2008u2009u200Au202Fu205Fu3000") must return 0'
);

assert.sameValue(Number(" "), 0, 'Number(" ") must return 0');
assert.sameValue(Number("\t"), 0, 'Number("t") must return 0');
assert.sameValue(Number("\r"), 0, 'Number("r") must return 0');
assert.sameValue(Number("\n"), 0, 'Number("n") must return 0');
assert.sameValue(Number("\f"), 0, 'Number("f") must return 0');
assert.sameValue(Number("\u0009"), 0, 'Number("u0009") must return 0');
assert.sameValue(Number("\u000A"), 0, 'Number("u000A") must return 0');
assert.sameValue(Number("\u000B"), 0, 'Number("u000B") must return 0');
assert.sameValue(Number("\u000C"), 0, 'Number("u000C") must return 0');
assert.sameValue(Number("\u000D"), 0, 'Number("u000D") must return 0');
assert.sameValue(Number("\u00A0"), 0, 'Number("u00A0") must return 0');
assert.sameValue(Number("\u0020"), 0, 'Number("u0020") must return 0');
assert.sameValue(Number("\u2028"), 0, 'Number("u2028") must return 0');
assert.sameValue(Number("\u2029"), 0, 'Number("u2029") must return 0');
assert.sameValue(Number("\u1680"), 0, 'Number("u1680") must return 0');
assert.sameValue(Number("\u2000"), 0, 'Number("u2000") must return 0');
assert.sameValue(Number("\u2001"), 0, 'Number("u2001") must return 0');
assert.sameValue(Number("\u2002"), 0, 'Number("u2002") must return 0');
assert.sameValue(Number("\u2003"), 0, 'Number("u2003") must return 0');
assert.sameValue(Number("\u2004"), 0, 'Number("u2004") must return 0');
assert.sameValue(Number("\u2005"), 0, 'Number("u2005") must return 0');
assert.sameValue(Number("\u2006"), 0, 'Number("u2006") must return 0');
assert.sameValue(Number("\u2007"), 0, 'Number("u2007") must return 0');
assert.sameValue(Number("\u2008"), 0, 'Number("u2008") must return 0');
assert.sameValue(Number("\u2009"), 0, 'Number("u2009") must return 0');
assert.sameValue(Number("\u200A"), 0, 'Number("u200A") must return 0');
assert.sameValue(Number("\u202F"), 0, 'Number("u202F") must return 0');
assert.sameValue(Number("\u205F"), 0, 'Number("u205F") must return 0');
assert.sameValue(Number("\u3000"), 0, 'Number("u3000") must return 0');
