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
  Unicode property escapes for `Script=Ethiopic`
info: |
  Generated by https://github.com/mathiasbynens/unicode-property-escapes-tests
  Unicode v17.0.0
esid: sec-static-semantics-unicodematchproperty-p
features: [regexp-unicode-property-escapes]
includes: [regExpUtils.js]
---*/

const matchSymbols = buildString({
  loneCodePoints: [
    0x001258,
    0x0012C0
  ],
  ranges: [
    [0x001200, 0x001248],
    [0x00124A, 0x00124D],
    [0x001250, 0x001256],
    [0x00125A, 0x00125D],
    [0x001260, 0x001288],
    [0x00128A, 0x00128D],
    [0x001290, 0x0012B0],
    [0x0012B2, 0x0012B5],
    [0x0012B8, 0x0012BE],
    [0x0012C2, 0x0012C5],
    [0x0012C8, 0x0012D6],
    [0x0012D8, 0x001310],
    [0x001312, 0x001315],
    [0x001318, 0x00135A],
    [0x00135D, 0x00137C],
    [0x001380, 0x001399],
    [0x002D80, 0x002D96],
    [0x002DA0, 0x002DA6],
    [0x002DA8, 0x002DAE],
    [0x002DB0, 0x002DB6],
    [0x002DB8, 0x002DBE],
    [0x002DC0, 0x002DC6],
    [0x002DC8, 0x002DCE],
    [0x002DD0, 0x002DD6],
    [0x002DD8, 0x002DDE],
    [0x00AB01, 0x00AB06],
    [0x00AB09, 0x00AB0E],
    [0x00AB11, 0x00AB16],
    [0x00AB20, 0x00AB26],
    [0x00AB28, 0x00AB2E],
    [0x01E7E0, 0x01E7E6],
    [0x01E7E8, 0x01E7EB],
    [0x01E7ED, 0x01E7EE],
    [0x01E7F0, 0x01E7FE]
  ]
});
testPropertyEscapes(
  /^\p{Script=Ethiopic}+$/u,
  matchSymbols,
  "\\p{Script=Ethiopic}"
);
testPropertyEscapes(
  /^\p{Script=Ethi}+$/u,
  matchSymbols,
  "\\p{Script=Ethi}"
);
testPropertyEscapes(
  /^\p{sc=Ethiopic}+$/u,
  matchSymbols,
  "\\p{sc=Ethiopic}"
);
testPropertyEscapes(
  /^\p{sc=Ethi}+$/u,
  matchSymbols,
  "\\p{sc=Ethi}"
);

const nonMatchSymbols = buildString({
  loneCodePoints: [
    0x001249,
    0x001257,
    0x001259,
    0x001289,
    0x0012B1,
    0x0012BF,
    0x0012C1,
    0x0012D7,
    0x001311,
    0x002DA7,
    0x002DAF,
    0x002DB7,
    0x002DBF,
    0x002DC7,
    0x002DCF,
    0x002DD7,
    0x00AB27,
    0x01E7E7,
    0x01E7EC,
    0x01E7EF
  ],
  ranges: [
    [0x00DC00, 0x00DFFF],
    [0x000000, 0x0011FF],
    [0x00124E, 0x00124F],
    [0x00125E, 0x00125F],
    [0x00128E, 0x00128F],
    [0x0012B6, 0x0012B7],
    [0x0012C6, 0x0012C7],
    [0x001316, 0x001317],
    [0x00135B, 0x00135C],
    [0x00137D, 0x00137F],
    [0x00139A, 0x002D7F],
    [0x002D97, 0x002D9F],
    [0x002DDF, 0x00AB00],
    [0x00AB07, 0x00AB08],
    [0x00AB0F, 0x00AB10],
    [0x00AB17, 0x00AB1F],
    [0x00AB2F, 0x00DBFF],
    [0x00E000, 0x01E7DF],
    [0x01E7FF, 0x10FFFF]
  ]
});
testPropertyEscapes(
  /^\P{Script=Ethiopic}+$/u,
  nonMatchSymbols,
  "\\P{Script=Ethiopic}"
);
testPropertyEscapes(
  /^\P{Script=Ethi}+$/u,
  nonMatchSymbols,
  "\\P{Script=Ethi}"
);
testPropertyEscapes(
  /^\P{sc=Ethiopic}+$/u,
  nonMatchSymbols,
  "\\P{sc=Ethiopic}"
);
testPropertyEscapes(
  /^\P{sc=Ethi}+$/u,
  nonMatchSymbols,
  "\\P{sc=Ethi}"
);
