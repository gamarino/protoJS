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
  Unicode property escapes for `General_Category=Other_Number`
info: |
  Generated by https://github.com/mathiasbynens/unicode-property-escapes-tests
  Unicode v17.0.0
esid: sec-static-semantics-unicodematchproperty-p
features: [regexp-unicode-property-escapes]
includes: [regExpUtils.js]
---*/

const matchSymbols = buildString({
  loneCodePoints: [
    0x0000B9,
    0x0019DA,
    0x002070,
    0x002189,
    0x002CFD
  ],
  ranges: [
    [0x0000B2, 0x0000B3],
    [0x0000BC, 0x0000BE],
    [0x0009F4, 0x0009F9],
    [0x000B72, 0x000B77],
    [0x000BF0, 0x000BF2],
    [0x000C78, 0x000C7E],
    [0x000D58, 0x000D5E],
    [0x000D70, 0x000D78],
    [0x000F2A, 0x000F33],
    [0x001369, 0x00137C],
    [0x0017F0, 0x0017F9],
    [0x002074, 0x002079],
    [0x002080, 0x002089],
    [0x002150, 0x00215F],
    [0x002460, 0x00249B],
    [0x0024EA, 0x0024FF],
    [0x002776, 0x002793],
    [0x003192, 0x003195],
    [0x003220, 0x003229],
    [0x003248, 0x00324F],
    [0x003251, 0x00325F],
    [0x003280, 0x003289],
    [0x0032B1, 0x0032BF],
    [0x00A830, 0x00A835],
    [0x010107, 0x010133],
    [0x010175, 0x010178],
    [0x01018A, 0x01018B],
    [0x0102E1, 0x0102FB],
    [0x010320, 0x010323],
    [0x010858, 0x01085F],
    [0x010879, 0x01087F],
    [0x0108A7, 0x0108AF],
    [0x0108FB, 0x0108FF],
    [0x010916, 0x01091B],
    [0x0109BC, 0x0109BD],
    [0x0109C0, 0x0109CF],
    [0x0109D2, 0x0109FF],
    [0x010A40, 0x010A48],
    [0x010A7D, 0x010A7E],
    [0x010A9D, 0x010A9F],
    [0x010AEB, 0x010AEF],
    [0x010B58, 0x010B5F],
    [0x010B78, 0x010B7F],
    [0x010BA9, 0x010BAF],
    [0x010CFA, 0x010CFF],
    [0x010E60, 0x010E7E],
    [0x010F1D, 0x010F26],
    [0x010F51, 0x010F54],
    [0x010FC5, 0x010FCB],
    [0x011052, 0x011065],
    [0x0111E1, 0x0111F4],
    [0x01173A, 0x01173B],
    [0x0118EA, 0x0118F2],
    [0x011C5A, 0x011C6C],
    [0x011FC0, 0x011FD4],
    [0x016B5B, 0x016B61],
    [0x016E80, 0x016E96],
    [0x01D2C0, 0x01D2D3],
    [0x01D2E0, 0x01D2F3],
    [0x01D360, 0x01D378],
    [0x01E8C7, 0x01E8CF],
    [0x01EC71, 0x01ECAB],
    [0x01ECAD, 0x01ECAF],
    [0x01ECB1, 0x01ECB4],
    [0x01ED01, 0x01ED2D],
    [0x01ED2F, 0x01ED3D],
    [0x01F100, 0x01F10C]
  ]
});
testPropertyEscapes(
  /^\p{General_Category=Other_Number}+$/u,
  matchSymbols,
  "\\p{General_Category=Other_Number}"
);
testPropertyEscapes(
  /^\p{General_Category=No}+$/u,
  matchSymbols,
  "\\p{General_Category=No}"
);
testPropertyEscapes(
  /^\p{gc=Other_Number}+$/u,
  matchSymbols,
  "\\p{gc=Other_Number}"
);
testPropertyEscapes(
  /^\p{gc=No}+$/u,
  matchSymbols,
  "\\p{gc=No}"
);
testPropertyEscapes(
  /^\p{Other_Number}+$/u,
  matchSymbols,
  "\\p{Other_Number}"
);
testPropertyEscapes(
  /^\p{No}+$/u,
  matchSymbols,
  "\\p{No}"
);

const nonMatchSymbols = buildString({
  loneCodePoints: [
    0x003250,
    0x01ECAC,
    0x01ECB0,
    0x01ED2E
  ],
  ranges: [
    [0x00DC00, 0x00DFFF],
    [0x000000, 0x0000B1],
    [0x0000B4, 0x0000B8],
    [0x0000BA, 0x0000BB],
    [0x0000BF, 0x0009F3],
    [0x0009FA, 0x000B71],
    [0x000B78, 0x000BEF],
    [0x000BF3, 0x000C77],
    [0x000C7F, 0x000D57],
    [0x000D5F, 0x000D6F],
    [0x000D79, 0x000F29],
    [0x000F34, 0x001368],
    [0x00137D, 0x0017EF],
    [0x0017FA, 0x0019D9],
    [0x0019DB, 0x00206F],
    [0x002071, 0x002073],
    [0x00207A, 0x00207F],
    [0x00208A, 0x00214F],
    [0x002160, 0x002188],
    [0x00218A, 0x00245F],
    [0x00249C, 0x0024E9],
    [0x002500, 0x002775],
    [0x002794, 0x002CFC],
    [0x002CFE, 0x003191],
    [0x003196, 0x00321F],
    [0x00322A, 0x003247],
    [0x003260, 0x00327F],
    [0x00328A, 0x0032B0],
    [0x0032C0, 0x00A82F],
    [0x00A836, 0x00DBFF],
    [0x00E000, 0x010106],
    [0x010134, 0x010174],
    [0x010179, 0x010189],
    [0x01018C, 0x0102E0],
    [0x0102FC, 0x01031F],
    [0x010324, 0x010857],
    [0x010860, 0x010878],
    [0x010880, 0x0108A6],
    [0x0108B0, 0x0108FA],
    [0x010900, 0x010915],
    [0x01091C, 0x0109BB],
    [0x0109BE, 0x0109BF],
    [0x0109D0, 0x0109D1],
    [0x010A00, 0x010A3F],
    [0x010A49, 0x010A7C],
    [0x010A7F, 0x010A9C],
    [0x010AA0, 0x010AEA],
    [0x010AF0, 0x010B57],
    [0x010B60, 0x010B77],
    [0x010B80, 0x010BA8],
    [0x010BB0, 0x010CF9],
    [0x010D00, 0x010E5F],
    [0x010E7F, 0x010F1C],
    [0x010F27, 0x010F50],
    [0x010F55, 0x010FC4],
    [0x010FCC, 0x011051],
    [0x011066, 0x0111E0],
    [0x0111F5, 0x011739],
    [0x01173C, 0x0118E9],
    [0x0118F3, 0x011C59],
    [0x011C6D, 0x011FBF],
    [0x011FD5, 0x016B5A],
    [0x016B62, 0x016E7F],
    [0x016E97, 0x01D2BF],
    [0x01D2D4, 0x01D2DF],
    [0x01D2F4, 0x01D35F],
    [0x01D379, 0x01E8C6],
    [0x01E8D0, 0x01EC70],
    [0x01ECB5, 0x01ED00],
    [0x01ED3E, 0x01F0FF],
    [0x01F10D, 0x10FFFF]
  ]
});
testPropertyEscapes(
  /^\P{General_Category=Other_Number}+$/u,
  nonMatchSymbols,
  "\\P{General_Category=Other_Number}"
);
testPropertyEscapes(
  /^\P{General_Category=No}+$/u,
  nonMatchSymbols,
  "\\P{General_Category=No}"
);
testPropertyEscapes(
  /^\P{gc=Other_Number}+$/u,
  nonMatchSymbols,
  "\\P{gc=Other_Number}"
);
testPropertyEscapes(
  /^\P{gc=No}+$/u,
  nonMatchSymbols,
  "\\P{gc=No}"
);
testPropertyEscapes(
  /^\P{Other_Number}+$/u,
  nonMatchSymbols,
  "\\P{Other_Number}"
);
testPropertyEscapes(
  /^\P{No}+$/u,
  nonMatchSymbols,
  "\\P{No}"
);
