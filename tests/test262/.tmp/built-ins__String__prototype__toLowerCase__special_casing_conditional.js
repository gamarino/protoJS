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
description: >
    Check if String.prototype.toLowerCase supports conditional mappings defined in SpecialCasings
info: |
    The result must be derived according to the locale-insensitive case mappings in the Unicode Character
    Database (this explicitly includes not only the UnicodeData.txt file, but also all locale-insensitive
    mappings in the SpecialCasings.txt file that accompanies it).
es5id: 15.5.4.16
es6id: 21.1.3.22
---*/

// SpecialCasing.txt, conditional, language-insensitive mappings.

// <code>; <lower>; <title>; <upper>; (<condition_list>;)? # <comment>
// 03A3; 03C2; 03A3; 03A3; Final_Sigma; # GREEK CAPITAL LETTER SIGMA
// 03A3; 03C3; 03A3; 03A3; # GREEK CAPITAL LETTER SIGMA

// Final_Sigma is defined in Unicode 5.1, 3.13 Default Case Algorithms.

assert.sameValue(
  "\u03A3".toLowerCase(),
  "\u03C3",
  "Single GREEK CAPITAL LETTER SIGMA"
);

// Sigma preceded by Cased and zero or more Case_Ignorable.
assert.sameValue(
  "A\u03A3".toLowerCase(),
  "a\u03C2",
  "Sigma preceded by LATIN CAPITAL LETTER A"
);
assert.sameValue(
  "\uD835\uDCA2\u03A3".toLowerCase(),
  "\uD835\uDCA2\u03C2",
  "Sigma preceded by MATHEMATICAL SCRIPT CAPITAL G (D835 DCA2 = 1D4A2)"
);
assert.sameValue(
  "A.\u03A3".toLowerCase(),
  "a.\u03C2",
  "Sigma preceded by FULL STOP"
);
assert.sameValue(
  "A\u00AD\u03A3".toLowerCase(),
  "a\u00AD\u03C2",
  "Sigma preceded by SOFT HYPHEN (00AD)"
);
assert.sameValue(
  "A\uD834\uDE42\u03A3".toLowerCase(),
  "a\uD834\uDE42\u03C2",
  "Sigma preceded by COMBINING GREEK MUSICAL TRISEME (D834 DE42 = 1D242)"
);
assert.sameValue(
  "\u0345\u03A3".toLowerCase(),
  "\u0345\u03C3",
  "Sigma preceded by COMBINING GREEK YPOGEGRAMMENI (0345)"
);
assert.sameValue(
  "\u0391\u0345\u03A3".toLowerCase(),
  "\u03B1\u0345\u03C2",
  "Sigma preceded by GREEK CAPITAL LETTER ALPHA (0391), COMBINING GREEK YPOGEGRAMMENI (0345)"
);

// Sigma not followed by zero or more Case_Ignorable and then Cased.
assert.sameValue(
  "A\u03A3B".toLowerCase(),
  "a\u03C3b",
  "Sigma followed by LATIN CAPITAL LETTER B"
);
assert.sameValue(
  "A\u03A3\uD835\uDCA2".toLowerCase(),
  "a\u03C3\uD835\uDCA2",
  "Sigma followed by MATHEMATICAL SCRIPT CAPITAL G (D835 DCA2 = 1D4A2)"
);
assert.sameValue(
  "A\u03A3.b".toLowerCase(),
  "a\u03C3.b",
  "Sigma followed by FULL STOP"
);
assert.sameValue(
  "A\u03A3\u00ADB".toLowerCase(),
  "a\u03C3\u00ADb",
  "Sigma followed by SOFT HYPHEN (00AD)"
);
assert.sameValue(
  "A\u03A3\uD834\uDE42B".toLowerCase(),
  "a\u03C3\uD834\uDE42b",
  "Sigma followed by COMBINING GREEK MUSICAL TRISEME (D834 DE42 = 1D242)"
);
assert.sameValue(
  "A\u03A3\u0345".toLowerCase(),
  "a\u03C2\u0345",
  "Sigma followed by COMBINING GREEK YPOGEGRAMMENI (0345)"
);
assert.sameValue(
  "A\u03A3\u0345\u0391".toLowerCase(),
  "a\u03C3\u0345\u03B1",
  "Sigma followed by COMBINING GREEK YPOGEGRAMMENI (0345), GREEK CAPITAL LETTER ALPHA (0391)"
);
