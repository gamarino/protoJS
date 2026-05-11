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


// Copyright (C) 2017 Mathias Bynens.  All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.
/*---
description: |
    Collection of functions used to assert the correctness of RegExp objects.
defines: [buildString, testPropertyEscapes, testPropertyOfStrings, testExtendedCharacterClass, matchValidator]
---*/

function buildString(args) {
  // Use member expressions rather than destructuring `args` for improved
  // compatibility with engines that only implement assignment patterns
  // partially or not at all.
  const loneCodePoints = args.loneCodePoints;
  const ranges = args.ranges;
  const CHUNK_SIZE = 10000;
  let result = String.fromCodePoint.apply(null, loneCodePoints);
  for (let i = 0; i < ranges.length; i++) {
    let range = ranges[i];
    let start = range[0];
    let end = range[1];
    let codePoints = [];
    for (let length = 0, codePoint = start; codePoint <= end; codePoint++) {
      codePoints[length++] = codePoint;
      if (length === CHUNK_SIZE) {
        result += String.fromCodePoint.apply(null, codePoints);
        codePoints.length = length = 0;
      }
    }
    result += String.fromCodePoint.apply(null, codePoints);
  }
  return result;
}

function printCodePoint(codePoint) {
  const hex = codePoint
    .toString(16)
    .toUpperCase()
    .padStart(6, "0");
  return `U+${hex}`;
}

function printStringCodePoints(string) {
  const buf = [];
  for (let symbol of string) {
    let formatted = printCodePoint(symbol.codePointAt(0));
    buf.push(formatted);
  }
  return buf.join(' ');
}

function testPropertyEscapes(regExp, string, expression) {
  if (!regExp.test(string)) {
    for (let symbol of string) {
      let formatted = printCodePoint(symbol.codePointAt(0));
      assert(
        regExp.test(symbol),
        `\`${ expression }\` should match ${ formatted } (\`${ symbol }\`)`
      );
    }
  }
}

function testPropertyOfStrings(args) {
  // Use member expressions rather than destructuring `args` for improved
  // compatibility with engines that only implement assignment patterns
  // partially or not at all.
  const regExp = args.regExp;
  const expression = args.expression;
  const matchStrings = args.matchStrings;
  const nonMatchStrings = args.nonMatchStrings;
  const allStrings = matchStrings.join('');
  if (!regExp.test(allStrings)) {
    for (let string of matchStrings) {
      assert(
        regExp.test(string),
        `\`${ expression }\` should match ${ string } (${ printStringCodePoints(string) })`
      );
    }
  }

  if (!nonMatchStrings) return;

  const allNonMatchStrings = nonMatchStrings.join('');
  if (regExp.test(allNonMatchStrings)) {
    for (let string of nonMatchStrings) {
      assert(
        !regExp.test(string),
        `\`${ expression }\` should not match ${ string } (${ printStringCodePoints(string) })`
      );
    }
  }
}

// The exact same logic can be used to test extended character classes
// as enabled through the RegExp `v` flag. This is useful to test not
// just standalone properties of strings, but also string literals, and
// set operations.
const testExtendedCharacterClass = testPropertyOfStrings;

// Returns a function that validates a RegExp match result.
//
// Example:
//
//    var validate = matchValidator(['b'], 1, 'abc');
//    validate(/b/.exec('abc'));
//
function matchValidator(expectedEntries, expectedIndex, expectedInput) {
  return function(match) {
    assert.compareArray(match, expectedEntries, 'Match entries');
    assert.sameValue(match.index, expectedIndex, 'Match index');
    assert.sameValue(match.input, expectedInput, 'Match input');
  }
}


// Copyright 2025 Mathias Bynens. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
author: Mathias Bynens
description: >
  Unicode property escapes for `RGI_Emoji_Modifier_Sequence` (property of strings)
info: |
  Generated by https://github.com/mathiasbynens/unicode-property-escapes-tests
  Unicode v17.0.0
esid: sec-static-semantics-unicodematchproperty-p
features: [regexp-unicode-property-escapes, regexp-v-flag]
includes: [regExpUtils.js]
---*/

testPropertyOfStrings({
  regExp: /^\p{RGI_Emoji_Modifier_Sequence}+$/v,
  expression: "\\p{RGI_Emoji_Modifier_Sequence}",
  matchStrings: [
    "\u261D\u{1F3FB}",
    "\u261D\u{1F3FC}",
    "\u261D\u{1F3FD}",
    "\u261D\u{1F3FE}",
    "\u261D\u{1F3FF}",
    "\u26F9\u{1F3FB}",
    "\u26F9\u{1F3FC}",
    "\u26F9\u{1F3FD}",
    "\u26F9\u{1F3FE}",
    "\u26F9\u{1F3FF}",
    "\u270A\u{1F3FB}",
    "\u270A\u{1F3FC}",
    "\u270A\u{1F3FD}",
    "\u270A\u{1F3FE}",
    "\u270A\u{1F3FF}",
    "\u270B\u{1F3FB}",
    "\u270B\u{1F3FC}",
    "\u270B\u{1F3FD}",
    "\u270B\u{1F3FE}",
    "\u270B\u{1F3FF}",
    "\u270C\u{1F3FB}",
    "\u270C\u{1F3FC}",
    "\u270C\u{1F3FD}",
    "\u270C\u{1F3FE}",
    "\u270C\u{1F3FF}",
    "\u270D\u{1F3FB}",
    "\u270D\u{1F3FC}",
    "\u270D\u{1F3FD}",
    "\u270D\u{1F3FE}",
    "\u270D\u{1F3FF}",
    "\u{1F385}\u{1F3FB}",
    "\u{1F385}\u{1F3FC}",
    "\u{1F385}\u{1F3FD}",
    "\u{1F385}\u{1F3FE}",
    "\u{1F385}\u{1F3FF}",
    "\u{1F3C2}\u{1F3FB}",
    "\u{1F3C2}\u{1F3FC}",
    "\u{1F3C2}\u{1F3FD}",
    "\u{1F3C2}\u{1F3FE}",
    "\u{1F3C2}\u{1F3FF}",
    "\u{1F3C3}\u{1F3FB}",
    "\u{1F3C3}\u{1F3FC}",
    "\u{1F3C3}\u{1F3FD}",
    "\u{1F3C3}\u{1F3FE}",
    "\u{1F3C3}\u{1F3FF}",
    "\u{1F3C4}\u{1F3FB}",
    "\u{1F3C4}\u{1F3FC}",
    "\u{1F3C4}\u{1F3FD}",
    "\u{1F3C4}\u{1F3FE}",
    "\u{1F3C4}\u{1F3FF}",
    "\u{1F3C7}\u{1F3FB}",
    "\u{1F3C7}\u{1F3FC}",
    "\u{1F3C7}\u{1F3FD}",
    "\u{1F3C7}\u{1F3FE}",
    "\u{1F3C7}\u{1F3FF}",
    "\u{1F3CA}\u{1F3FB}",
    "\u{1F3CA}\u{1F3FC}",
    "\u{1F3CA}\u{1F3FD}",
    "\u{1F3CA}\u{1F3FE}",
    "\u{1F3CA}\u{1F3FF}",
    "\u{1F3CB}\u{1F3FB}",
    "\u{1F3CB}\u{1F3FC}",
    "\u{1F3CB}\u{1F3FD}",
    "\u{1F3CB}\u{1F3FE}",
    "\u{1F3CB}\u{1F3FF}",
    "\u{1F3CC}\u{1F3FB}",
    "\u{1F3CC}\u{1F3FC}",
    "\u{1F3CC}\u{1F3FD}",
    "\u{1F3CC}\u{1F3FE}",
    "\u{1F3CC}\u{1F3FF}",
    "\u{1F442}\u{1F3FB}",
    "\u{1F442}\u{1F3FC}",
    "\u{1F442}\u{1F3FD}",
    "\u{1F442}\u{1F3FE}",
    "\u{1F442}\u{1F3FF}",
    "\u{1F443}\u{1F3FB}",
    "\u{1F443}\u{1F3FC}",
    "\u{1F443}\u{1F3FD}",
    "\u{1F443}\u{1F3FE}",
    "\u{1F443}\u{1F3FF}",
    "\u{1F446}\u{1F3FB}",
    "\u{1F446}\u{1F3FC}",
    "\u{1F446}\u{1F3FD}",
    "\u{1F446}\u{1F3FE}",
    "\u{1F446}\u{1F3FF}",
    "\u{1F447}\u{1F3FB}",
    "\u{1F447}\u{1F3FC}",
    "\u{1F447}\u{1F3FD}",
    "\u{1F447}\u{1F3FE}",
    "\u{1F447}\u{1F3FF}",
    "\u{1F448}\u{1F3FB}",
    "\u{1F448}\u{1F3FC}",
    "\u{1F448}\u{1F3FD}",
    "\u{1F448}\u{1F3FE}",
    "\u{1F448}\u{1F3FF}",
    "\u{1F449}\u{1F3FB}",
    "\u{1F449}\u{1F3FC}",
    "\u{1F449}\u{1F3FD}",
    "\u{1F449}\u{1F3FE}",
    "\u{1F449}\u{1F3FF}",
    "\u{1F44A}\u{1F3FB}",
    "\u{1F44A}\u{1F3FC}",
    "\u{1F44A}\u{1F3FD}",
    "\u{1F44A}\u{1F3FE}",
    "\u{1F44A}\u{1F3FF}",
    "\u{1F44B}\u{1F3FB}",
    "\u{1F44B}\u{1F3FC}",
    "\u{1F44B}\u{1F3FD}",
    "\u{1F44B}\u{1F3FE}",
    "\u{1F44B}\u{1F3FF}",
    "\u{1F44C}\u{1F3FB}",
    "\u{1F44C}\u{1F3FC}",
    "\u{1F44C}\u{1F3FD}",
    "\u{1F44C}\u{1F3FE}",
    "\u{1F44C}\u{1F3FF}",
    "\u{1F44D}\u{1F3FB}",
    "\u{1F44D}\u{1F3FC}",
    "\u{1F44D}\u{1F3FD}",
    "\u{1F44D}\u{1F3FE}",
    "\u{1F44D}\u{1F3FF}",
    "\u{1F44E}\u{1F3FB}",
    "\u{1F44E}\u{1F3FC}",
    "\u{1F44E}\u{1F3FD}",
    "\u{1F44E}\u{1F3FE}",
    "\u{1F44E}\u{1F3FF}",
    "\u{1F44F}\u{1F3FB}",
    "\u{1F44F}\u{1F3FC}",
    "\u{1F44F}\u{1F3FD}",
    "\u{1F44F}\u{1F3FE}",
    "\u{1F44F}\u{1F3FF}",
    "\u{1F450}\u{1F3FB}",
    "\u{1F450}\u{1F3FC}",
    "\u{1F450}\u{1F3FD}",
    "\u{1F450}\u{1F3FE}",
    "\u{1F450}\u{1F3FF}",
    "\u{1F466}\u{1F3FB}",
    "\u{1F466}\u{1F3FC}",
    "\u{1F466}\u{1F3FD}",
    "\u{1F466}\u{1F3FE}",
    "\u{1F466}\u{1F3FF}",
    "\u{1F467}\u{1F3FB}",
    "\u{1F467}\u{1F3FC}",
    "\u{1F467}\u{1F3FD}",
    "\u{1F467}\u{1F3FE}",
    "\u{1F467}\u{1F3FF}",
    "\u{1F468}\u{1F3FB}",
    "\u{1F468}\u{1F3FC}",
    "\u{1F468}\u{1F3FD}",
    "\u{1F468}\u{1F3FE}",
    "\u{1F468}\u{1F3FF}",
    "\u{1F469}\u{1F3FB}",
    "\u{1F469}\u{1F3FC}",
    "\u{1F469}\u{1F3FD}",
    "\u{1F469}\u{1F3FE}",
    "\u{1F469}\u{1F3FF}",
    "\u{1F46B}\u{1F3FB}",
    "\u{1F46B}\u{1F3FC}",
    "\u{1F46B}\u{1F3FD}",
    "\u{1F46B}\u{1F3FE}",
    "\u{1F46B}\u{1F3FF}",
    "\u{1F46C}\u{1F3FB}",
    "\u{1F46C}\u{1F3FC}",
    "\u{1F46C}\u{1F3FD}",
    "\u{1F46C}\u{1F3FE}",
    "\u{1F46C}\u{1F3FF}",
    "\u{1F46D}\u{1F3FB}",
    "\u{1F46D}\u{1F3FC}",
    "\u{1F46D}\u{1F3FD}",
    "\u{1F46D}\u{1F3FE}",
    "\u{1F46D}\u{1F3FF}",
    "\u{1F46E}\u{1F3FB}",
    "\u{1F46E}\u{1F3FC}",
    "\u{1F46E}\u{1F3FD}",
    "\u{1F46E}\u{1F3FE}",
    "\u{1F46E}\u{1F3FF}",
    "\u{1F46F}\u{1F3FB}",
    "\u{1F46F}\u{1F3FC}",
    "\u{1F46F}\u{1F3FD}",
    "\u{1F46F}\u{1F3FE}",
    "\u{1F46F}\u{1F3FF}",
    "\u{1F470}\u{1F3FB}",
    "\u{1F470}\u{1F3FC}",
    "\u{1F470}\u{1F3FD}",
    "\u{1F470}\u{1F3FE}",
    "\u{1F470}\u{1F3FF}",
    "\u{1F471}\u{1F3FB}",
    "\u{1F471}\u{1F3FC}",
    "\u{1F471}\u{1F3FD}",
    "\u{1F471}\u{1F3FE}",
    "\u{1F471}\u{1F3FF}",
    "\u{1F472}\u{1F3FB}",
    "\u{1F472}\u{1F3FC}",
    "\u{1F472}\u{1F3FD}",
    "\u{1F472}\u{1F3FE}",
    "\u{1F472}\u{1F3FF}",
    "\u{1F473}\u{1F3FB}",
    "\u{1F473}\u{1F3FC}",
    "\u{1F473}\u{1F3FD}",
    "\u{1F473}\u{1F3FE}",
    "\u{1F473}\u{1F3FF}",
    "\u{1F474}\u{1F3FB}",
    "\u{1F474}\u{1F3FC}",
    "\u{1F474}\u{1F3FD}",
    "\u{1F474}\u{1F3FE}",
    "\u{1F474}\u{1F3FF}",
    "\u{1F475}\u{1F3FB}",
    "\u{1F475}\u{1F3FC}",
    "\u{1F475}\u{1F3FD}",
    "\u{1F475}\u{1F3FE}",
    "\u{1F475}\u{1F3FF}",
    "\u{1F476}\u{1F3FB}",
    "\u{1F476}\u{1F3FC}",
    "\u{1F476}\u{1F3FD}",
    "\u{1F476}\u{1F3FE}",
    "\u{1F476}\u{1F3FF}",
    "\u{1F477}\u{1F3FB}",
    "\u{1F477}\u{1F3FC}",
    "\u{1F477}\u{1F3FD}",
    "\u{1F477}\u{1F3FE}",
    "\u{1F477}\u{1F3FF}",
    "\u{1F478}\u{1F3FB}",
    "\u{1F478}\u{1F3FC}",
    "\u{1F478}\u{1F3FD}",
    "\u{1F478}\u{1F3FE}",
    "\u{1F478}\u{1F3FF}",
    "\u{1F47C}\u{1F3FB}",
    "\u{1F47C}\u{1F3FC}",
    "\u{1F47C}\u{1F3FD}",
    "\u{1F47C}\u{1F3FE}",
    "\u{1F47C}\u{1F3FF}",
    "\u{1F481}\u{1F3FB}",
    "\u{1F481}\u{1F3FC}",
    "\u{1F481}\u{1F3FD}",
    "\u{1F481}\u{1F3FE}",
    "\u{1F481}\u{1F3FF}",
    "\u{1F482}\u{1F3FB}",
    "\u{1F482}\u{1F3FC}",
    "\u{1F482}\u{1F3FD}",
    "\u{1F482}\u{1F3FE}",
    "\u{1F482}\u{1F3FF}",
    "\u{1F483}\u{1F3FB}",
    "\u{1F483}\u{1F3FC}",
    "\u{1F483}\u{1F3FD}",
    "\u{1F483}\u{1F3FE}",
    "\u{1F483}\u{1F3FF}",
    "\u{1F485}\u{1F3FB}",
    "\u{1F485}\u{1F3FC}",
    "\u{1F485}\u{1F3FD}",
    "\u{1F485}\u{1F3FE}",
    "\u{1F485}\u{1F3FF}",
    "\u{1F486}\u{1F3FB}",
    "\u{1F486}\u{1F3FC}",
    "\u{1F486}\u{1F3FD}",
    "\u{1F486}\u{1F3FE}",
    "\u{1F486}\u{1F3FF}",
    "\u{1F487}\u{1F3FB}",
    "\u{1F487}\u{1F3FC}",
    "\u{1F487}\u{1F3FD}",
    "\u{1F487}\u{1F3FE}",
    "\u{1F487}\u{1F3FF}",
    "\u{1F48F}\u{1F3FB}",
    "\u{1F48F}\u{1F3FC}",
    "\u{1F48F}\u{1F3FD}",
    "\u{1F48F}\u{1F3FE}",
    "\u{1F48F}\u{1F3FF}",
    "\u{1F491}\u{1F3FB}",
    "\u{1F491}\u{1F3FC}",
    "\u{1F491}\u{1F3FD}",
    "\u{1F491}\u{1F3FE}",
    "\u{1F491}\u{1F3FF}",
    "\u{1F4AA}\u{1F3FB}",
    "\u{1F4AA}\u{1F3FC}",
    "\u{1F4AA}\u{1F3FD}",
    "\u{1F4AA}\u{1F3FE}",
    "\u{1F4AA}\u{1F3FF}",
    "\u{1F574}\u{1F3FB}",
    "\u{1F574}\u{1F3FC}",
    "\u{1F574}\u{1F3FD}",
    "\u{1F574}\u{1F3FE}",
    "\u{1F574}\u{1F3FF}",
    "\u{1F575}\u{1F3FB}",
    "\u{1F575}\u{1F3FC}",
    "\u{1F575}\u{1F3FD}",
    "\u{1F575}\u{1F3FE}",
    "\u{1F575}\u{1F3FF}",
    "\u{1F57A}\u{1F3FB}",
    "\u{1F57A}\u{1F3FC}",
    "\u{1F57A}\u{1F3FD}",
    "\u{1F57A}\u{1F3FE}",
    "\u{1F57A}\u{1F3FF}",
    "\u{1F590}\u{1F3FB}",
    "\u{1F590}\u{1F3FC}",
    "\u{1F590}\u{1F3FD}",
    "\u{1F590}\u{1F3FE}",
    "\u{1F590}\u{1F3FF}",
    "\u{1F595}\u{1F3FB}",
    "\u{1F595}\u{1F3FC}",
    "\u{1F595}\u{1F3FD}",
    "\u{1F595}\u{1F3FE}",
    "\u{1F595}\u{1F3FF}",
    "\u{1F596}\u{1F3FB}",
    "\u{1F596}\u{1F3FC}",
    "\u{1F596}\u{1F3FD}",
    "\u{1F596}\u{1F3FE}",
    "\u{1F596}\u{1F3FF}",
    "\u{1F645}\u{1F3FB}",
    "\u{1F645}\u{1F3FC}",
    "\u{1F645}\u{1F3FD}",
    "\u{1F645}\u{1F3FE}",
    "\u{1F645}\u{1F3FF}",
    "\u{1F646}\u{1F3FB}",
    "\u{1F646}\u{1F3FC}",
    "\u{1F646}\u{1F3FD}",
    "\u{1F646}\u{1F3FE}",
    "\u{1F646}\u{1F3FF}",
    "\u{1F647}\u{1F3FB}",
    "\u{1F647}\u{1F3FC}",
    "\u{1F647}\u{1F3FD}",
    "\u{1F647}\u{1F3FE}",
    "\u{1F647}\u{1F3FF}",
    "\u{1F64B}\u{1F3FB}",
    "\u{1F64B}\u{1F3FC}",
    "\u{1F64B}\u{1F3FD}",
    "\u{1F64B}\u{1F3FE}",
    "\u{1F64B}\u{1F3FF}",
    "\u{1F64C}\u{1F3FB}",
    "\u{1F64C}\u{1F3FC}",
    "\u{1F64C}\u{1F3FD}",
    "\u{1F64C}\u{1F3FE}",
    "\u{1F64C}\u{1F3FF}",
    "\u{1F64D}\u{1F3FB}",
    "\u{1F64D}\u{1F3FC}",
    "\u{1F64D}\u{1F3FD}",
    "\u{1F64D}\u{1F3FE}",
    "\u{1F64D}\u{1F3FF}",
    "\u{1F64E}\u{1F3FB}",
    "\u{1F64E}\u{1F3FC}",
    "\u{1F64E}\u{1F3FD}",
    "\u{1F64E}\u{1F3FE}",
    "\u{1F64E}\u{1F3FF}",
    "\u{1F64F}\u{1F3FB}",
    "\u{1F64F}\u{1F3FC}",
    "\u{1F64F}\u{1F3FD}",
    "\u{1F64F}\u{1F3FE}",
    "\u{1F64F}\u{1F3FF}",
    "\u{1F6A3}\u{1F3FB}",
    "\u{1F6A3}\u{1F3FC}",
    "\u{1F6A3}\u{1F3FD}",
    "\u{1F6A3}\u{1F3FE}",
    "\u{1F6A3}\u{1F3FF}",
    "\u{1F6B4}\u{1F3FB}",
    "\u{1F6B4}\u{1F3FC}",
    "\u{1F6B4}\u{1F3FD}",
    "\u{1F6B4}\u{1F3FE}",
    "\u{1F6B4}\u{1F3FF}",
    "\u{1F6B5}\u{1F3FB}",
    "\u{1F6B5}\u{1F3FC}",
    "\u{1F6B5}\u{1F3FD}",
    "\u{1F6B5}\u{1F3FE}",
    "\u{1F6B5}\u{1F3FF}",
    "\u{1F6B6}\u{1F3FB}",
    "\u{1F6B6}\u{1F3FC}",
    "\u{1F6B6}\u{1F3FD}",
    "\u{1F6B6}\u{1F3FE}",
    "\u{1F6B6}\u{1F3FF}",
    "\u{1F6C0}\u{1F3FB}",
    "\u{1F6C0}\u{1F3FC}",
    "\u{1F6C0}\u{1F3FD}",
    "\u{1F6C0}\u{1F3FE}",
    "\u{1F6C0}\u{1F3FF}",
    "\u{1F6CC}\u{1F3FB}",
    "\u{1F6CC}\u{1F3FC}",
    "\u{1F6CC}\u{1F3FD}",
    "\u{1F6CC}\u{1F3FE}",
    "\u{1F6CC}\u{1F3FF}",
    "\u{1F90C}\u{1F3FB}",
    "\u{1F90C}\u{1F3FC}",
    "\u{1F90C}\u{1F3FD}",
    "\u{1F90C}\u{1F3FE}",
    "\u{1F90C}\u{1F3FF}",
    "\u{1F90F}\u{1F3FB}",
    "\u{1F90F}\u{1F3FC}",
    "\u{1F90F}\u{1F3FD}",
    "\u{1F90F}\u{1F3FE}",
    "\u{1F90F}\u{1F3FF}",
    "\u{1F918}\u{1F3FB}",
    "\u{1F918}\u{1F3FC}",
    "\u{1F918}\u{1F3FD}",
    "\u{1F918}\u{1F3FE}",
    "\u{1F918}\u{1F3FF}",
    "\u{1F919}\u{1F3FB}",
    "\u{1F919}\u{1F3FC}",
    "\u{1F919}\u{1F3FD}",
    "\u{1F919}\u{1F3FE}",
    "\u{1F919}\u{1F3FF}",
    "\u{1F91A}\u{1F3FB}",
    "\u{1F91A}\u{1F3FC}",
    "\u{1F91A}\u{1F3FD}",
    "\u{1F91A}\u{1F3FE}",
    "\u{1F91A}\u{1F3FF}",
    "\u{1F91B}\u{1F3FB}",
    "\u{1F91B}\u{1F3FC}",
    "\u{1F91B}\u{1F3FD}",
    "\u{1F91B}\u{1F3FE}",
    "\u{1F91B}\u{1F3FF}",
    "\u{1F91C}\u{1F3FB}",
    "\u{1F91C}\u{1F3FC}",
    "\u{1F91C}\u{1F3FD}",
    "\u{1F91C}\u{1F3FE}",
    "\u{1F91C}\u{1F3FF}",
    "\u{1F91D}\u{1F3FB}",
    "\u{1F91D}\u{1F3FC}",
    "\u{1F91D}\u{1F3FD}",
    "\u{1F91D}\u{1F3FE}",
    "\u{1F91D}\u{1F3FF}",
    "\u{1F91E}\u{1F3FB}",
    "\u{1F91E}\u{1F3FC}",
    "\u{1F91E}\u{1F3FD}",
    "\u{1F91E}\u{1F3FE}",
    "\u{1F91E}\u{1F3FF}",
    "\u{1F91F}\u{1F3FB}",
    "\u{1F91F}\u{1F3FC}",
    "\u{1F91F}\u{1F3FD}",
    "\u{1F91F}\u{1F3FE}",
    "\u{1F91F}\u{1F3FF}",
    "\u{1F926}\u{1F3FB}",
    "\u{1F926}\u{1F3FC}",
    "\u{1F926}\u{1F3FD}",
    "\u{1F926}\u{1F3FE}",
    "\u{1F926}\u{1F3FF}",
    "\u{1F930}\u{1F3FB}",
    "\u{1F930}\u{1F3FC}",
    "\u{1F930}\u{1F3FD}",
    "\u{1F930}\u{1F3FE}",
    "\u{1F930}\u{1F3FF}",
    "\u{1F931}\u{1F3FB}",
    "\u{1F931}\u{1F3FC}",
    "\u{1F931}\u{1F3FD}",
    "\u{1F931}\u{1F3FE}",
    "\u{1F931}\u{1F3FF}",
    "\u{1F932}\u{1F3FB}",
    "\u{1F932}\u{1F3FC}",
    "\u{1F932}\u{1F3FD}",
    "\u{1F932}\u{1F3FE}",
    "\u{1F932}\u{1F3FF}",
    "\u{1F933}\u{1F3FB}",
    "\u{1F933}\u{1F3FC}",
    "\u{1F933}\u{1F3FD}",
    "\u{1F933}\u{1F3FE}",
    "\u{1F933}\u{1F3FF}",
    "\u{1F934}\u{1F3FB}",
    "\u{1F934}\u{1F3FC}",
    "\u{1F934}\u{1F3FD}",
    "\u{1F934}\u{1F3FE}",
    "\u{1F934}\u{1F3FF}",
    "\u{1F935}\u{1F3FB}",
    "\u{1F935}\u{1F3FC}",
    "\u{1F935}\u{1F3FD}",
    "\u{1F935}\u{1F3FE}",
    "\u{1F935}\u{1F3FF}",
    "\u{1F936}\u{1F3FB}",
    "\u{1F936}\u{1F3FC}",
    "\u{1F936}\u{1F3FD}",
    "\u{1F936}\u{1F3FE}",
    "\u{1F936}\u{1F3FF}",
    "\u{1F937}\u{1F3FB}",
    "\u{1F937}\u{1F3FC}",
    "\u{1F937}\u{1F3FD}",
    "\u{1F937}\u{1F3FE}",
    "\u{1F937}\u{1F3FF}",
    "\u{1F938}\u{1F3FB}",
    "\u{1F938}\u{1F3FC}",
    "\u{1F938}\u{1F3FD}",
    "\u{1F938}\u{1F3FE}",
    "\u{1F938}\u{1F3FF}",
    "\u{1F939}\u{1F3FB}",
    "\u{1F939}\u{1F3FC}",
    "\u{1F939}\u{1F3FD}",
    "\u{1F939}\u{1F3FE}",
    "\u{1F939}\u{1F3FF}",
    "\u{1F93C}\u{1F3FB}",
    "\u{1F93C}\u{1F3FC}",
    "\u{1F93C}\u{1F3FD}",
    "\u{1F93C}\u{1F3FE}",
    "\u{1F93C}\u{1F3FF}",
    "\u{1F93D}\u{1F3FB}",
    "\u{1F93D}\u{1F3FC}",
    "\u{1F93D}\u{1F3FD}",
    "\u{1F93D}\u{1F3FE}",
    "\u{1F93D}\u{1F3FF}",
    "\u{1F93E}\u{1F3FB}",
    "\u{1F93E}\u{1F3FC}",
    "\u{1F93E}\u{1F3FD}",
    "\u{1F93E}\u{1F3FE}",
    "\u{1F93E}\u{1F3FF}",
    "\u{1F977}\u{1F3FB}",
    "\u{1F977}\u{1F3FC}",
    "\u{1F977}\u{1F3FD}",
    "\u{1F977}\u{1F3FE}",
    "\u{1F977}\u{1F3FF}",
    "\u{1F9B5}\u{1F3FB}",
    "\u{1F9B5}\u{1F3FC}",
    "\u{1F9B5}\u{1F3FD}",
    "\u{1F9B5}\u{1F3FE}",
    "\u{1F9B5}\u{1F3FF}",
    "\u{1F9B6}\u{1F3FB}",
    "\u{1F9B6}\u{1F3FC}",
    "\u{1F9B6}\u{1F3FD}",
    "\u{1F9B6}\u{1F3FE}",
    "\u{1F9B6}\u{1F3FF}",
    "\u{1F9B8}\u{1F3FB}",
    "\u{1F9B8}\u{1F3FC}",
    "\u{1F9B8}\u{1F3FD}",
    "\u{1F9B8}\u{1F3FE}",
    "\u{1F9B8}\u{1F3FF}",
    "\u{1F9B9}\u{1F3FB}",
    "\u{1F9B9}\u{1F3FC}",
    "\u{1F9B9}\u{1F3FD}",
    "\u{1F9B9}\u{1F3FE}",
    "\u{1F9B9}\u{1F3FF}",
    "\u{1F9BB}\u{1F3FB}",
    "\u{1F9BB}\u{1F3FC}",
    "\u{1F9BB}\u{1F3FD}",
    "\u{1F9BB}\u{1F3FE}",
    "\u{1F9BB}\u{1F3FF}",
    "\u{1F9CD}\u{1F3FB}",
    "\u{1F9CD}\u{1F3FC}",
    "\u{1F9CD}\u{1F3FD}",
    "\u{1F9CD}\u{1F3FE}",
    "\u{1F9CD}\u{1F3FF}",
    "\u{1F9CE}\u{1F3FB}",
    "\u{1F9CE}\u{1F3FC}",
    "\u{1F9CE}\u{1F3FD}",
    "\u{1F9CE}\u{1F3FE}",
    "\u{1F9CE}\u{1F3FF}",
    "\u{1F9CF}\u{1F3FB}",
    "\u{1F9CF}\u{1F3FC}",
    "\u{1F9CF}\u{1F3FD}",
    "\u{1F9CF}\u{1F3FE}",
    "\u{1F9CF}\u{1F3FF}",
    "\u{1F9D1}\u{1F3FB}",
    "\u{1F9D1}\u{1F3FC}",
    "\u{1F9D1}\u{1F3FD}",
    "\u{1F9D1}\u{1F3FE}",
    "\u{1F9D1}\u{1F3FF}",
    "\u{1F9D2}\u{1F3FB}",
    "\u{1F9D2}\u{1F3FC}",
    "\u{1F9D2}\u{1F3FD}",
    "\u{1F9D2}\u{1F3FE}",
    "\u{1F9D2}\u{1F3FF}",
    "\u{1F9D3}\u{1F3FB}",
    "\u{1F9D3}\u{1F3FC}",
    "\u{1F9D3}\u{1F3FD}",
    "\u{1F9D3}\u{1F3FE}",
    "\u{1F9D3}\u{1F3FF}",
    "\u{1F9D4}\u{1F3FB}",
    "\u{1F9D4}\u{1F3FC}",
    "\u{1F9D4}\u{1F3FD}",
    "\u{1F9D4}\u{1F3FE}",
    "\u{1F9D4}\u{1F3FF}",
    "\u{1F9D5}\u{1F3FB}",
    "\u{1F9D5}\u{1F3FC}",
    "\u{1F9D5}\u{1F3FD}",
    "\u{1F9D5}\u{1F3FE}",
    "\u{1F9D5}\u{1F3FF}",
    "\u{1F9D6}\u{1F3FB}",
    "\u{1F9D6}\u{1F3FC}",
    "\u{1F9D6}\u{1F3FD}",
    "\u{1F9D6}\u{1F3FE}",
    "\u{1F9D6}\u{1F3FF}",
    "\u{1F9D7}\u{1F3FB}",
    "\u{1F9D7}\u{1F3FC}",
    "\u{1F9D7}\u{1F3FD}",
    "\u{1F9D7}\u{1F3FE}",
    "\u{1F9D7}\u{1F3FF}",
    "\u{1F9D8}\u{1F3FB}",
    "\u{1F9D8}\u{1F3FC}",
    "\u{1F9D8}\u{1F3FD}",
    "\u{1F9D8}\u{1F3FE}",
    "\u{1F9D8}\u{1F3FF}",
    "\u{1F9D9}\u{1F3FB}",
    "\u{1F9D9}\u{1F3FC}",
    "\u{1F9D9}\u{1F3FD}",
    "\u{1F9D9}\u{1F3FE}",
    "\u{1F9D9}\u{1F3FF}",
    "\u{1F9DA}\u{1F3FB}",
    "\u{1F9DA}\u{1F3FC}",
    "\u{1F9DA}\u{1F3FD}",
    "\u{1F9DA}\u{1F3FE}",
    "\u{1F9DA}\u{1F3FF}",
    "\u{1F9DB}\u{1F3FB}",
    "\u{1F9DB}\u{1F3FC}",
    "\u{1F9DB}\u{1F3FD}",
    "\u{1F9DB}\u{1F3FE}",
    "\u{1F9DB}\u{1F3FF}",
    "\u{1F9DC}\u{1F3FB}",
    "\u{1F9DC}\u{1F3FC}",
    "\u{1F9DC}\u{1F3FD}",
    "\u{1F9DC}\u{1F3FE}",
    "\u{1F9DC}\u{1F3FF}",
    "\u{1F9DD}\u{1F3FB}",
    "\u{1F9DD}\u{1F3FC}",
    "\u{1F9DD}\u{1F3FD}",
    "\u{1F9DD}\u{1F3FE}",
    "\u{1F9DD}\u{1F3FF}",
    "\u{1FAC3}\u{1F3FB}",
    "\u{1FAC3}\u{1F3FC}",
    "\u{1FAC3}\u{1F3FD}",
    "\u{1FAC3}\u{1F3FE}",
    "\u{1FAC3}\u{1F3FF}",
    "\u{1FAC4}\u{1F3FB}",
    "\u{1FAC4}\u{1F3FC}",
    "\u{1FAC4}\u{1F3FD}",
    "\u{1FAC4}\u{1F3FE}",
    "\u{1FAC4}\u{1F3FF}",
    "\u{1FAC5}\u{1F3FB}",
    "\u{1FAC5}\u{1F3FC}",
    "\u{1FAC5}\u{1F3FD}",
    "\u{1FAC5}\u{1F3FE}",
    "\u{1FAC5}\u{1F3FF}",
    "\u{1FAF0}\u{1F3FB}",
    "\u{1FAF0}\u{1F3FC}",
    "\u{1FAF0}\u{1F3FD}",
    "\u{1FAF0}\u{1F3FE}",
    "\u{1FAF0}\u{1F3FF}",
    "\u{1FAF1}\u{1F3FB}",
    "\u{1FAF1}\u{1F3FC}",
    "\u{1FAF1}\u{1F3FD}",
    "\u{1FAF1}\u{1F3FE}",
    "\u{1FAF1}\u{1F3FF}",
    "\u{1FAF2}\u{1F3FB}",
    "\u{1FAF2}\u{1F3FC}",
    "\u{1FAF2}\u{1F3FD}",
    "\u{1FAF2}\u{1F3FE}",
    "\u{1FAF2}\u{1F3FF}",
    "\u{1FAF3}\u{1F3FB}",
    "\u{1FAF3}\u{1F3FC}",
    "\u{1FAF3}\u{1F3FD}",
    "\u{1FAF3}\u{1F3FE}",
    "\u{1FAF3}\u{1F3FF}",
    "\u{1FAF4}\u{1F3FB}",
    "\u{1FAF4}\u{1F3FC}",
    "\u{1FAF4}\u{1F3FD}",
    "\u{1FAF4}\u{1F3FE}",
    "\u{1FAF4}\u{1F3FF}",
    "\u{1FAF5}\u{1F3FB}",
    "\u{1FAF5}\u{1F3FC}",
    "\u{1FAF5}\u{1F3FD}",
    "\u{1FAF5}\u{1F3FE}",
    "\u{1FAF5}\u{1F3FF}",
    "\u{1FAF6}\u{1F3FB}",
    "\u{1FAF6}\u{1F3FC}",
    "\u{1FAF6}\u{1F3FD}",
    "\u{1FAF6}\u{1F3FE}",
    "\u{1FAF6}\u{1F3FF}",
    "\u{1FAF7}\u{1F3FB}",
    "\u{1FAF7}\u{1F3FC}",
    "\u{1FAF7}\u{1F3FD}",
    "\u{1FAF7}\u{1F3FE}",
    "\u{1FAF7}\u{1F3FF}",
    "\u{1FAF8}\u{1F3FB}",
    "\u{1FAF8}\u{1F3FC}",
    "\u{1FAF8}\u{1F3FD}",
    "\u{1FAF8}\u{1F3FE}",
    "\u{1FAF8}\u{1F3FF}"
  ],
  nonMatchStrings: [
    "\u{1F3FB}",
    "\u261D",
    "\u{1F3FC}",
    "\u261D",
    "\u{1F3FD}",
    "\u261D",
    "\u{1F3FE}",
    "\u261D",
    "\u{1F3FF}",
    "\u261D"
  ],
});
