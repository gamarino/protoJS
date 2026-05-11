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


// Copyright 2024 Mathias Bynens. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
author: Mathias Bynens
esid: prod-PrivateIdentifier
description: |
  Test that Unicode v6.1.0 ID_Continue characters are accepted as
  identifier part characters in escaped form, i.e.
  - \uXXXX or \u{XXXX} for BMP symbols
  - \u{XXXXXX} for astral symbols
  in private class fields.
info: |
  Generated by https://github.com/mathiasbynens/caniunicode
features: [class, class-fields-private]
---*/

class _ {
  #_\u08E4\u08E5\u08E6\u08E7\u08E8\u08E9\u08EA\u08EB\u08EC\u08ED\u08EE\u08EF\u08F0\u08F1\u08F2\u08F3\u08F4\u08F5\u08F6\u08F7\u08F8\u08F9\u08FA\u08FB\u08FC\u08FD\u08FE\u17B4\u17B5\u1BAB\u1BAC\u1BAD\u1CF3\u1CF4\uA674\uA675\uA676\uA677\uA678\uA679\uA67A\uA67B\uA69F\uAAEB\uAAEC\uAAED\uAAEE\uAAEF\uAAF5\uAAF6\u{110F0}\u{110F1}\u{110F2}\u{110F3}\u{110F4}\u{110F5}\u{110F6}\u{110F7}\u{110F8}\u{110F9}\u{11100}\u{11101}\u{11102}\u{11127}\u{11128}\u{11129}\u{1112A}\u{1112B}\u{1112C}\u{1112D}\u{1112E}\u{1112F}\u{11130}\u{11131}\u{11132}\u{11133}\u{11134}\u{11136}\u{11137}\u{11138}\u{11139}\u{1113A}\u{1113B}\u{1113C}\u{1113D}\u{1113E}\u{1113F}\u{11180}\u{11181}\u{11182}\u{111B3}\u{111B4}\u{111B5}\u{111B6}\u{111B7}\u{111B8}\u{111B9}\u{111BA}\u{111BB}\u{111BC}\u{111BD}\u{111BE}\u{111BF}\u{111C0}\u{111D0}\u{111D1}\u{111D2}\u{111D3}\u{111D4}\u{111D5}\u{111D6}\u{111D7}\u{111D8}\u{111D9}\u{116AB}\u{116AC}\u{116AD}\u{116AE}\u{116AF}\u{116B0}\u{116B1}\u{116B2}\u{116B3}\u{116B4}\u{116B5}\u{116B6}\u{116B7}\u{116C0}\u{116C1}\u{116C2}\u{116C3}\u{116C4}\u{116C5}\u{116C6}\u{116C7}\u{116C8}\u{116C9}\u{16F51}\u{16F52}\u{16F53}\u{16F54}\u{16F55}\u{16F56}\u{16F57}\u{16F58}\u{16F59}\u{16F5A}\u{16F5B}\u{16F5C}\u{16F5D}\u{16F5E}\u{16F5F}\u{16F60}\u{16F61}\u{16F62}\u{16F63}\u{16F64}\u{16F65}\u{16F66}\u{16F67}\u{16F68}\u{16F69}\u{16F6A}\u{16F6B}\u{16F6C}\u{16F6D}\u{16F6E}\u{16F6F}\u{16F70}\u{16F71}\u{16F72}\u{16F73}\u{16F74}\u{16F75}\u{16F76}\u{16F77}\u{16F78}\u{16F79}\u{16F7A}\u{16F7B}\u{16F7C}\u{16F7D}\u{16F7E}\u{16F8F}\u{16F90}\u{16F91}\u{16F92};
};
