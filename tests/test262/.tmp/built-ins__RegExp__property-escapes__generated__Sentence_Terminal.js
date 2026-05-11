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
  Unicode property escapes for `Sentence_Terminal`
info: |
  Generated by https://github.com/mathiasbynens/unicode-property-escapes-tests
  Unicode v17.0.0
esid: sec-static-semantics-unicodematchproperty-p
features: [regexp-unicode-property-escapes]
includes: [regExpUtils.js]
---*/

const matchSymbols = buildString({
  loneCodePoints: [
    0x000021,
    0x00002E,
    0x00003F,
    0x000589,
    0x0006D4,
    0x0007F9,
    0x000837,
    0x000839,
    0x001362,
    0x00166E,
    0x001803,
    0x001809,
    0x002024,
    0x002E2E,
    0x002E3C,
    0x003002,
    0x00A4FF,
    0x00A6F3,
    0x00A6F7,
    0x00A92F,
    0x00ABEB,
    0x00FE12,
    0x00FE52,
    0x00FF01,
    0x00FF0E,
    0x00FF1F,
    0x00FF61,
    0x0111CD,
    0x0112A9,
    0x011944,
    0x011946,
    0x016AF5,
    0x016B44,
    0x016E98,
    0x01BC9F,
    0x01DA88
  ],
  ranges: [
    [0x00061D, 0x00061F],
    [0x000700, 0x000702],
    [0x00083D, 0x00083E],
    [0x000964, 0x000965],
    [0x00104A, 0x00104B],
    [0x001367, 0x001368],
    [0x001735, 0x001736],
    [0x0017D4, 0x0017D5],
    [0x001944, 0x001945],
    [0x001AA8, 0x001AAB],
    [0x001B4E, 0x001B4F],
    [0x001B5A, 0x001B5B],
    [0x001B5E, 0x001B5F],
    [0x001B7D, 0x001B7F],
    [0x001C3B, 0x001C3C],
    [0x001C7E, 0x001C7F],
    [0x00203C, 0x00203D],
    [0x002047, 0x002049],
    [0x002CF9, 0x002CFB],
    [0x002E53, 0x002E54],
    [0x00A60E, 0x00A60F],
    [0x00A876, 0x00A877],
    [0x00A8CE, 0x00A8CF],
    [0x00A9C8, 0x00A9C9],
    [0x00AA5D, 0x00AA5F],
    [0x00AAF0, 0x00AAF1],
    [0x00FE15, 0x00FE16],
    [0x00FE56, 0x00FE57],
    [0x010A56, 0x010A57],
    [0x010F55, 0x010F59],
    [0x010F86, 0x010F89],
    [0x011047, 0x011048],
    [0x0110BE, 0x0110C1],
    [0x011141, 0x011143],
    [0x0111C5, 0x0111C6],
    [0x0111DE, 0x0111DF],
    [0x011238, 0x011239],
    [0x01123B, 0x01123C],
    [0x0113D4, 0x0113D5],
    [0x01144B, 0x01144C],
    [0x0115C2, 0x0115C3],
    [0x0115C9, 0x0115D7],
    [0x011641, 0x011642],
    [0x01173C, 0x01173E],
    [0x011A42, 0x011A43],
    [0x011A9B, 0x011A9C],
    [0x011C41, 0x011C42],
    [0x011EF7, 0x011EF8],
    [0x011F43, 0x011F44],
    [0x016A6E, 0x016A6F],
    [0x016B37, 0x016B38],
    [0x016D6E, 0x016D6F]
  ]
});
testPropertyEscapes(
  /^\p{Sentence_Terminal}+$/u,
  matchSymbols,
  "\\p{Sentence_Terminal}"
);
testPropertyEscapes(
  /^\p{STerm}+$/u,
  matchSymbols,
  "\\p{STerm}"
);

const nonMatchSymbols = buildString({
  loneCodePoints: [
    0x000838,
    0x01123A,
    0x011945
  ],
  ranges: [
    [0x00DC00, 0x00DFFF],
    [0x000000, 0x000020],
    [0x000022, 0x00002D],
    [0x00002F, 0x00003E],
    [0x000040, 0x000588],
    [0x00058A, 0x00061C],
    [0x000620, 0x0006D3],
    [0x0006D5, 0x0006FF],
    [0x000703, 0x0007F8],
    [0x0007FA, 0x000836],
    [0x00083A, 0x00083C],
    [0x00083F, 0x000963],
    [0x000966, 0x001049],
    [0x00104C, 0x001361],
    [0x001363, 0x001366],
    [0x001369, 0x00166D],
    [0x00166F, 0x001734],
    [0x001737, 0x0017D3],
    [0x0017D6, 0x001802],
    [0x001804, 0x001808],
    [0x00180A, 0x001943],
    [0x001946, 0x001AA7],
    [0x001AAC, 0x001B4D],
    [0x001B50, 0x001B59],
    [0x001B5C, 0x001B5D],
    [0x001B60, 0x001B7C],
    [0x001B80, 0x001C3A],
    [0x001C3D, 0x001C7D],
    [0x001C80, 0x002023],
    [0x002025, 0x00203B],
    [0x00203E, 0x002046],
    [0x00204A, 0x002CF8],
    [0x002CFC, 0x002E2D],
    [0x002E2F, 0x002E3B],
    [0x002E3D, 0x002E52],
    [0x002E55, 0x003001],
    [0x003003, 0x00A4FE],
    [0x00A500, 0x00A60D],
    [0x00A610, 0x00A6F2],
    [0x00A6F4, 0x00A6F6],
    [0x00A6F8, 0x00A875],
    [0x00A878, 0x00A8CD],
    [0x00A8D0, 0x00A92E],
    [0x00A930, 0x00A9C7],
    [0x00A9CA, 0x00AA5C],
    [0x00AA60, 0x00AAEF],
    [0x00AAF2, 0x00ABEA],
    [0x00ABEC, 0x00DBFF],
    [0x00E000, 0x00FE11],
    [0x00FE13, 0x00FE14],
    [0x00FE17, 0x00FE51],
    [0x00FE53, 0x00FE55],
    [0x00FE58, 0x00FF00],
    [0x00FF02, 0x00FF0D],
    [0x00FF0F, 0x00FF1E],
    [0x00FF20, 0x00FF60],
    [0x00FF62, 0x010A55],
    [0x010A58, 0x010F54],
    [0x010F5A, 0x010F85],
    [0x010F8A, 0x011046],
    [0x011049, 0x0110BD],
    [0x0110C2, 0x011140],
    [0x011144, 0x0111C4],
    [0x0111C7, 0x0111CC],
    [0x0111CE, 0x0111DD],
    [0x0111E0, 0x011237],
    [0x01123D, 0x0112A8],
    [0x0112AA, 0x0113D3],
    [0x0113D6, 0x01144A],
    [0x01144D, 0x0115C1],
    [0x0115C4, 0x0115C8],
    [0x0115D8, 0x011640],
    [0x011643, 0x01173B],
    [0x01173F, 0x011943],
    [0x011947, 0x011A41],
    [0x011A44, 0x011A9A],
    [0x011A9D, 0x011C40],
    [0x011C43, 0x011EF6],
    [0x011EF9, 0x011F42],
    [0x011F45, 0x016A6D],
    [0x016A70, 0x016AF4],
    [0x016AF6, 0x016B36],
    [0x016B39, 0x016B43],
    [0x016B45, 0x016D6D],
    [0x016D70, 0x016E97],
    [0x016E99, 0x01BC9E],
    [0x01BCA0, 0x01DA87],
    [0x01DA89, 0x10FFFF]
  ]
});
testPropertyEscapes(
  /^\P{Sentence_Terminal}+$/u,
  nonMatchSymbols,
  "\\P{Sentence_Terminal}"
);
testPropertyEscapes(
  /^\P{STerm}+$/u,
  nonMatchSymbols,
  "\\P{STerm}"
);
