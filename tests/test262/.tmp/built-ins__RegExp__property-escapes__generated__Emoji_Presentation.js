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
  Unicode property escapes for `Emoji_Presentation`
info: |
  Generated by https://github.com/mathiasbynens/unicode-property-escapes-tests
  Unicode v17.0.0
esid: sec-static-semantics-unicodematchproperty-p
features: [regexp-unicode-property-escapes]
includes: [regExpUtils.js]
---*/

const matchSymbols = buildString({
  loneCodePoints: [
    0x0023F0,
    0x0023F3,
    0x00267F,
    0x002693,
    0x0026A1,
    0x0026CE,
    0x0026D4,
    0x0026EA,
    0x0026F5,
    0x0026FA,
    0x0026FD,
    0x002705,
    0x002728,
    0x00274C,
    0x00274E,
    0x002757,
    0x0027B0,
    0x0027BF,
    0x002B50,
    0x002B55,
    0x01F004,
    0x01F0CF,
    0x01F18E,
    0x01F201,
    0x01F21A,
    0x01F22F,
    0x01F3F4,
    0x01F440,
    0x01F57A,
    0x01F5A4,
    0x01F6CC,
    0x01F7F0,
    0x01FAC8
  ],
  ranges: [
    [0x00231A, 0x00231B],
    [0x0023E9, 0x0023EC],
    [0x0025FD, 0x0025FE],
    [0x002614, 0x002615],
    [0x002648, 0x002653],
    [0x0026AA, 0x0026AB],
    [0x0026BD, 0x0026BE],
    [0x0026C4, 0x0026C5],
    [0x0026F2, 0x0026F3],
    [0x00270A, 0x00270B],
    [0x002753, 0x002755],
    [0x002795, 0x002797],
    [0x002B1B, 0x002B1C],
    [0x01F191, 0x01F19A],
    [0x01F1E6, 0x01F1FF],
    [0x01F232, 0x01F236],
    [0x01F238, 0x01F23A],
    [0x01F250, 0x01F251],
    [0x01F300, 0x01F320],
    [0x01F32D, 0x01F335],
    [0x01F337, 0x01F37C],
    [0x01F37E, 0x01F393],
    [0x01F3A0, 0x01F3CA],
    [0x01F3CF, 0x01F3D3],
    [0x01F3E0, 0x01F3F0],
    [0x01F3F8, 0x01F43E],
    [0x01F442, 0x01F4FC],
    [0x01F4FF, 0x01F53D],
    [0x01F54B, 0x01F54E],
    [0x01F550, 0x01F567],
    [0x01F595, 0x01F596],
    [0x01F5FB, 0x01F64F],
    [0x01F680, 0x01F6C5],
    [0x01F6D0, 0x01F6D2],
    [0x01F6D5, 0x01F6D8],
    [0x01F6DC, 0x01F6DF],
    [0x01F6EB, 0x01F6EC],
    [0x01F6F4, 0x01F6FC],
    [0x01F7E0, 0x01F7EB],
    [0x01F90C, 0x01F93A],
    [0x01F93C, 0x01F945],
    [0x01F947, 0x01F9FF],
    [0x01FA70, 0x01FA7C],
    [0x01FA80, 0x01FA8A],
    [0x01FA8E, 0x01FAC6],
    [0x01FACD, 0x01FADC],
    [0x01FADF, 0x01FAEA],
    [0x01FAEF, 0x01FAF8]
  ]
});
testPropertyEscapes(
  /^\p{Emoji_Presentation}+$/u,
  matchSymbols,
  "\\p{Emoji_Presentation}"
);
testPropertyEscapes(
  /^\p{EPres}+$/u,
  matchSymbols,
  "\\p{EPres}"
);

const nonMatchSymbols = buildString({
  loneCodePoints: [
    0x0026F4,
    0x00274D,
    0x002756,
    0x01F200,
    0x01F237,
    0x01F336,
    0x01F37D,
    0x01F43F,
    0x01F441,
    0x01F54F,
    0x01F93B,
    0x01F946,
    0x01FAC7
  ],
  ranges: [
    [0x00DC00, 0x00DFFF],
    [0x000000, 0x002319],
    [0x00231C, 0x0023E8],
    [0x0023ED, 0x0023EF],
    [0x0023F1, 0x0023F2],
    [0x0023F4, 0x0025FC],
    [0x0025FF, 0x002613],
    [0x002616, 0x002647],
    [0x002654, 0x00267E],
    [0x002680, 0x002692],
    [0x002694, 0x0026A0],
    [0x0026A2, 0x0026A9],
    [0x0026AC, 0x0026BC],
    [0x0026BF, 0x0026C3],
    [0x0026C6, 0x0026CD],
    [0x0026CF, 0x0026D3],
    [0x0026D5, 0x0026E9],
    [0x0026EB, 0x0026F1],
    [0x0026F6, 0x0026F9],
    [0x0026FB, 0x0026FC],
    [0x0026FE, 0x002704],
    [0x002706, 0x002709],
    [0x00270C, 0x002727],
    [0x002729, 0x00274B],
    [0x00274F, 0x002752],
    [0x002758, 0x002794],
    [0x002798, 0x0027AF],
    [0x0027B1, 0x0027BE],
    [0x0027C0, 0x002B1A],
    [0x002B1D, 0x002B4F],
    [0x002B51, 0x002B54],
    [0x002B56, 0x00DBFF],
    [0x00E000, 0x01F003],
    [0x01F005, 0x01F0CE],
    [0x01F0D0, 0x01F18D],
    [0x01F18F, 0x01F190],
    [0x01F19B, 0x01F1E5],
    [0x01F202, 0x01F219],
    [0x01F21B, 0x01F22E],
    [0x01F230, 0x01F231],
    [0x01F23B, 0x01F24F],
    [0x01F252, 0x01F2FF],
    [0x01F321, 0x01F32C],
    [0x01F394, 0x01F39F],
    [0x01F3CB, 0x01F3CE],
    [0x01F3D4, 0x01F3DF],
    [0x01F3F1, 0x01F3F3],
    [0x01F3F5, 0x01F3F7],
    [0x01F4FD, 0x01F4FE],
    [0x01F53E, 0x01F54A],
    [0x01F568, 0x01F579],
    [0x01F57B, 0x01F594],
    [0x01F597, 0x01F5A3],
    [0x01F5A5, 0x01F5FA],
    [0x01F650, 0x01F67F],
    [0x01F6C6, 0x01F6CB],
    [0x01F6CD, 0x01F6CF],
    [0x01F6D3, 0x01F6D4],
    [0x01F6D9, 0x01F6DB],
    [0x01F6E0, 0x01F6EA],
    [0x01F6ED, 0x01F6F3],
    [0x01F6FD, 0x01F7DF],
    [0x01F7EC, 0x01F7EF],
    [0x01F7F1, 0x01F90B],
    [0x01FA00, 0x01FA6F],
    [0x01FA7D, 0x01FA7F],
    [0x01FA8B, 0x01FA8D],
    [0x01FAC9, 0x01FACC],
    [0x01FADD, 0x01FADE],
    [0x01FAEB, 0x01FAEE],
    [0x01FAF9, 0x10FFFF]
  ]
});
testPropertyEscapes(
  /^\P{Emoji_Presentation}+$/u,
  nonMatchSymbols,
  "\\P{Emoji_Presentation}"
);
testPropertyEscapes(
  /^\P{EPres}+$/u,
  nonMatchSymbols,
  "\\P{EPres}"
);
