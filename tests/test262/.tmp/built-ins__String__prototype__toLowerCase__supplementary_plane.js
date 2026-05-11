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


// Copyright (C) 2015 André Bargull. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
description: String.prototype.toLowerCase() iterates over code points
info: |
    21.1.3.22 String.prototype.toLowerCase ( )

    ...
    4. Let cpList be a List containing in order the code points as defined in
       6.1.4 of S, starting at the first element of S.
    5. For each code point c in cpList, if the Unicode Character Database
       provides a language insensitive lower case equivalent of c then replace
       c in cpList with that equivalent code point(s).
es6id: 21.1.3.22
---*/

assert.sameValue("\uD801\uDC00".toLowerCase(), "\uD801\uDC28", "DESERET CAPITAL LETTER LONG I");
assert.sameValue("\uD801\uDC01".toLowerCase(), "\uD801\uDC29", "DESERET CAPITAL LETTER LONG E");
assert.sameValue("\uD801\uDC02".toLowerCase(), "\uD801\uDC2A", "DESERET CAPITAL LETTER LONG A");
assert.sameValue("\uD801\uDC03".toLowerCase(), "\uD801\uDC2B", "DESERET CAPITAL LETTER LONG AH");
assert.sameValue("\uD801\uDC04".toLowerCase(), "\uD801\uDC2C", "DESERET CAPITAL LETTER LONG O");
assert.sameValue("\uD801\uDC05".toLowerCase(), "\uD801\uDC2D", "DESERET CAPITAL LETTER LONG OO");
assert.sameValue("\uD801\uDC06".toLowerCase(), "\uD801\uDC2E", "DESERET CAPITAL LETTER SHORT I");
assert.sameValue("\uD801\uDC07".toLowerCase(), "\uD801\uDC2F", "DESERET CAPITAL LETTER SHORT E");
assert.sameValue("\uD801\uDC08".toLowerCase(), "\uD801\uDC30", "DESERET CAPITAL LETTER SHORT A");
assert.sameValue("\uD801\uDC09".toLowerCase(), "\uD801\uDC31", "DESERET CAPITAL LETTER SHORT AH");
assert.sameValue("\uD801\uDC0A".toLowerCase(), "\uD801\uDC32", "DESERET CAPITAL LETTER SHORT O");
assert.sameValue("\uD801\uDC0B".toLowerCase(), "\uD801\uDC33", "DESERET CAPITAL LETTER SHORT OO");
assert.sameValue("\uD801\uDC0C".toLowerCase(), "\uD801\uDC34", "DESERET CAPITAL LETTER AY");
assert.sameValue("\uD801\uDC0D".toLowerCase(), "\uD801\uDC35", "DESERET CAPITAL LETTER OW");
assert.sameValue("\uD801\uDC0E".toLowerCase(), "\uD801\uDC36", "DESERET CAPITAL LETTER WU");
assert.sameValue("\uD801\uDC0F".toLowerCase(), "\uD801\uDC37", "DESERET CAPITAL LETTER YEE");
assert.sameValue("\uD801\uDC10".toLowerCase(), "\uD801\uDC38", "DESERET CAPITAL LETTER H");
assert.sameValue("\uD801\uDC11".toLowerCase(), "\uD801\uDC39", "DESERET CAPITAL LETTER PEE");
assert.sameValue("\uD801\uDC12".toLowerCase(), "\uD801\uDC3A", "DESERET CAPITAL LETTER BEE");
assert.sameValue("\uD801\uDC13".toLowerCase(), "\uD801\uDC3B", "DESERET CAPITAL LETTER TEE");
assert.sameValue("\uD801\uDC14".toLowerCase(), "\uD801\uDC3C", "DESERET CAPITAL LETTER DEE");
assert.sameValue("\uD801\uDC15".toLowerCase(), "\uD801\uDC3D", "DESERET CAPITAL LETTER CHEE");
assert.sameValue("\uD801\uDC16".toLowerCase(), "\uD801\uDC3E", "DESERET CAPITAL LETTER JEE");
assert.sameValue("\uD801\uDC17".toLowerCase(), "\uD801\uDC3F", "DESERET CAPITAL LETTER KAY");
assert.sameValue("\uD801\uDC18".toLowerCase(), "\uD801\uDC40", "DESERET CAPITAL LETTER GAY");
assert.sameValue("\uD801\uDC19".toLowerCase(), "\uD801\uDC41", "DESERET CAPITAL LETTER EF");
assert.sameValue("\uD801\uDC1A".toLowerCase(), "\uD801\uDC42", "DESERET CAPITAL LETTER VEE");
assert.sameValue("\uD801\uDC1B".toLowerCase(), "\uD801\uDC43", "DESERET CAPITAL LETTER ETH");
assert.sameValue("\uD801\uDC1C".toLowerCase(), "\uD801\uDC44", "DESERET CAPITAL LETTER THEE");
assert.sameValue("\uD801\uDC1D".toLowerCase(), "\uD801\uDC45", "DESERET CAPITAL LETTER ES");
assert.sameValue("\uD801\uDC1E".toLowerCase(), "\uD801\uDC46", "DESERET CAPITAL LETTER ZEE");
assert.sameValue("\uD801\uDC1F".toLowerCase(), "\uD801\uDC47", "DESERET CAPITAL LETTER ESH");
assert.sameValue("\uD801\uDC20".toLowerCase(), "\uD801\uDC48", "DESERET CAPITAL LETTER ZHEE");
assert.sameValue("\uD801\uDC21".toLowerCase(), "\uD801\uDC49", "DESERET CAPITAL LETTER ER");
assert.sameValue("\uD801\uDC22".toLowerCase(), "\uD801\uDC4A", "DESERET CAPITAL LETTER EL");
assert.sameValue("\uD801\uDC23".toLowerCase(), "\uD801\uDC4B", "DESERET CAPITAL LETTER EM");
assert.sameValue("\uD801\uDC24".toLowerCase(), "\uD801\uDC4C", "DESERET CAPITAL LETTER EN");
assert.sameValue("\uD801\uDC25".toLowerCase(), "\uD801\uDC4D", "DESERET CAPITAL LETTER ENG");
assert.sameValue("\uD801\uDC26".toLowerCase(), "\uD801\uDC4E", "DESERET CAPITAL LETTER OI");
assert.sameValue("\uD801\uDC27".toLowerCase(), "\uD801\uDC4F", "DESERET CAPITAL LETTER EW");
