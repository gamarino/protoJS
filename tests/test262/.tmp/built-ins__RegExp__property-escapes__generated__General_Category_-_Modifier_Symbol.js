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
  Unicode property escapes for `General_Category=Modifier_Symbol`
info: |
  Generated by https://github.com/mathiasbynens/unicode-property-escapes-tests
  Unicode v17.0.0
esid: sec-static-semantics-unicodematchproperty-p
features: [regexp-unicode-property-escapes]
includes: [regExpUtils.js]
---*/

const matchSymbols = buildString({
  loneCodePoints: [
    0x00005E,
    0x000060,
    0x0000A8,
    0x0000AF,
    0x0000B4,
    0x0000B8,
    0x0002ED,
    0x000375,
    0x000888,
    0x001FBD,
    0x00AB5B,
    0x00FF3E,
    0x00FF40,
    0x00FFE3
  ],
  ranges: [
    [0x0002C2, 0x0002C5],
    [0x0002D2, 0x0002DF],
    [0x0002E5, 0x0002EB],
    [0x0002EF, 0x0002FF],
    [0x000384, 0x000385],
    [0x001FBF, 0x001FC1],
    [0x001FCD, 0x001FCF],
    [0x001FDD, 0x001FDF],
    [0x001FED, 0x001FEF],
    [0x001FFD, 0x001FFE],
    [0x00309B, 0x00309C],
    [0x00A700, 0x00A716],
    [0x00A720, 0x00A721],
    [0x00A789, 0x00A78A],
    [0x00AB6A, 0x00AB6B],
    [0x00FBB2, 0x00FBC2],
    [0x01F3FB, 0x01F3FF]
  ]
});
testPropertyEscapes(
  /^\p{General_Category=Modifier_Symbol}+$/u,
  matchSymbols,
  "\\p{General_Category=Modifier_Symbol}"
);
testPropertyEscapes(
  /^\p{General_Category=Sk}+$/u,
  matchSymbols,
  "\\p{General_Category=Sk}"
);
testPropertyEscapes(
  /^\p{gc=Modifier_Symbol}+$/u,
  matchSymbols,
  "\\p{gc=Modifier_Symbol}"
);
testPropertyEscapes(
  /^\p{gc=Sk}+$/u,
  matchSymbols,
  "\\p{gc=Sk}"
);
testPropertyEscapes(
  /^\p{Modifier_Symbol}+$/u,
  matchSymbols,
  "\\p{Modifier_Symbol}"
);
testPropertyEscapes(
  /^\p{Sk}+$/u,
  matchSymbols,
  "\\p{Sk}"
);

const nonMatchSymbols = buildString({
  loneCodePoints: [
    0x00005F,
    0x0002EC,
    0x0002EE,
    0x001FBE,
    0x00FF3F
  ],
  ranges: [
    [0x00DC00, 0x00DFFF],
    [0x000000, 0x00005D],
    [0x000061, 0x0000A7],
    [0x0000A9, 0x0000AE],
    [0x0000B0, 0x0000B3],
    [0x0000B5, 0x0000B7],
    [0x0000B9, 0x0002C1],
    [0x0002C6, 0x0002D1],
    [0x0002E0, 0x0002E4],
    [0x000300, 0x000374],
    [0x000376, 0x000383],
    [0x000386, 0x000887],
    [0x000889, 0x001FBC],
    [0x001FC2, 0x001FCC],
    [0x001FD0, 0x001FDC],
    [0x001FE0, 0x001FEC],
    [0x001FF0, 0x001FFC],
    [0x001FFF, 0x00309A],
    [0x00309D, 0x00A6FF],
    [0x00A717, 0x00A71F],
    [0x00A722, 0x00A788],
    [0x00A78B, 0x00AB5A],
    [0x00AB5C, 0x00AB69],
    [0x00AB6C, 0x00DBFF],
    [0x00E000, 0x00FBB1],
    [0x00FBC3, 0x00FF3D],
    [0x00FF41, 0x00FFE2],
    [0x00FFE4, 0x01F3FA],
    [0x01F400, 0x10FFFF]
  ]
});
testPropertyEscapes(
  /^\P{General_Category=Modifier_Symbol}+$/u,
  nonMatchSymbols,
  "\\P{General_Category=Modifier_Symbol}"
);
testPropertyEscapes(
  /^\P{General_Category=Sk}+$/u,
  nonMatchSymbols,
  "\\P{General_Category=Sk}"
);
testPropertyEscapes(
  /^\P{gc=Modifier_Symbol}+$/u,
  nonMatchSymbols,
  "\\P{gc=Modifier_Symbol}"
);
testPropertyEscapes(
  /^\P{gc=Sk}+$/u,
  nonMatchSymbols,
  "\\P{gc=Sk}"
);
testPropertyEscapes(
  /^\P{Modifier_Symbol}+$/u,
  nonMatchSymbols,
  "\\P{Modifier_Symbol}"
);
testPropertyEscapes(
  /^\P{Sk}+$/u,
  nonMatchSymbols,
  "\\P{Sk}"
);
