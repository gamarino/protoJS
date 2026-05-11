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


// Copyright 2012 Norbert Lindenberg. All rights reserved.
// Copyright 2012 Mozilla Corporation. All rights reserved.
// Copyright 2013 Microsoft Corporation. All rights reserved.
// Copyright (C) 2022 Richard Gibson. All rights reserved.
// This code is governed by the license found in the LICENSE file.

/*---
description: >
    String.prototype.localeCompare must return 0 when
    comparing Strings that are considered canonically equivalent by
    the Unicode Standard.
esid: sec-string.prototype.localecompare
info: |
    String.prototype.localeCompare ( _that_ [ , _reserved1_ [ , _reserved2_ ] ] )

    This function must treat Strings that are canonically equivalent
    according to the Unicode standard as identical and must return `0`
    when comparing Strings that are considered canonically equivalent.
---*/

// pairs with characters not in Unicode 3.0 are commented out
var pairs = [
  // example from Unicode 5.0, section 3.7, definition D70
  ["o\u0308", "ö"],
  // examples from Unicode 5.0, chapter 3.11
  ["ä\u0323", "a\u0323\u0308"],
  ["a\u0308\u0323", "a\u0323\u0308"],
  ["ạ\u0308", "a\u0323\u0308"],
  ["ä\u0306", "a\u0308\u0306"],
  ["ă\u0308", "a\u0306\u0308"],
  // example from Unicode 5.0, chapter 3.12
  ["\u1111\u1171\u11B6", "퓛"],
  // examples from UTS 10, Unicode Collation Algorithm
  ["Å", "Å"],
  ["Å", "A\u030A"],
  ["x\u031B\u0323", "x\u0323\u031B"],
  ["ự", "ụ\u031B"],
  ["ự", "u\u031B\u0323"],
  ["ự", "ư\u0323"],
  ["ự", "u\u0323\u031B"],
  // examples from UAX 15, Unicode Normalization Forms
  ["Ç", "C\u0327"],
  ["q\u0307\u0323", "q\u0323\u0307"],
  ["가", "\u1100\u1161"],
  ["Å", "A\u030A"],
  ["Ω", "Ω"],
  ["Å", "A\u030A"],
  ["ô", "o\u0302"],
  ["ṩ", "s\u0323\u0307"],
  ["ḋ\u0323", "d\u0323\u0307"],
  ["ḋ\u0323", "ḍ\u0307"],
  ["q\u0307\u0323", "q\u0323\u0307"],
  // examples involving supplementary characters from UCD NormalizationTest.txt
  //  ["\uD834\uDD5E", "\uD834\uDD57\uD834\uDD65"],
  //  ["\uD87E\uDC2B", "北"]
];

var i;
for (i = 0; i < pairs.length; i++) {
  var pair = pairs[i];
  if (pair[0].localeCompare(pair[1]) !== 0) {
    throw new Test262Error("String.prototype.localeCompare considers " + pair[0] + " (" + toU(pair[0]) +
      ") ≠ " + pair[1] + " (" + toU(pair[1]) + ").");
  }
}

function toU(s) {
  var result = "";
  var escape = "\\u0000";
  var i;
  for (i = 0; i < s.length; i++) {
    var hex = s.charCodeAt(i).toString(16);
    result += escape.substring(0, escape.length - hex.length) + hex;
  }
  return result;
}
