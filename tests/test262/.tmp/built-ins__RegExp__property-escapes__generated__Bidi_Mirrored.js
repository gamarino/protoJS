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
  Unicode property escapes for `Bidi_Mirrored`
info: |
  Generated by https://github.com/mathiasbynens/unicode-property-escapes-tests
  Unicode v17.0.0
esid: sec-static-semantics-unicodematchproperty-p
features: [regexp-unicode-property-escapes]
includes: [regExpUtils.js]
---*/

const matchSymbols = buildString({
  loneCodePoints: [
    0x00003C,
    0x00003E,
    0x00005B,
    0x00005D,
    0x00007B,
    0x00007D,
    0x0000AB,
    0x0000BB,
    0x002140,
    0x002211,
    0x002224,
    0x002226,
    0x002239,
    0x002262,
    0x002298,
    0x0027C0,
    0x0029B8,
    0x0029C9,
    0x0029E1,
    0x002A24,
    0x002A26,
    0x002A29,
    0x002ADC,
    0x002ADE,
    0x002AF3,
    0x002AFD,
    0x002BFE,
    0x00FF1C,
    0x00FF1E,
    0x00FF3B,
    0x00FF3D,
    0x00FF5B,
    0x00FF5D,
    0x01D6DB,
    0x01D715,
    0x01D74F,
    0x01D789,
    0x01D7C3
  ],
  ranges: [
    [0x000028, 0x000029],
    [0x000F3A, 0x000F3D],
    [0x00169B, 0x00169C],
    [0x002039, 0x00203A],
    [0x002045, 0x002046],
    [0x00207D, 0x00207E],
    [0x00208D, 0x00208E],
    [0x002201, 0x002204],
    [0x002208, 0x00220D],
    [0x002215, 0x002216],
    [0x00221A, 0x00221D],
    [0x00221F, 0x002222],
    [0x00222B, 0x002233],
    [0x00223B, 0x00224C],
    [0x002252, 0x002255],
    [0x00225F, 0x002260],
    [0x002264, 0x00226B],
    [0x00226D, 0x00228C],
    [0x00228F, 0x002292],
    [0x0022A2, 0x0022A3],
    [0x0022A6, 0x0022B8],
    [0x0022BE, 0x0022BF],
    [0x0022C9, 0x0022CD],
    [0x0022D0, 0x0022D1],
    [0x0022D6, 0x0022ED],
    [0x0022F0, 0x0022FF],
    [0x002308, 0x00230B],
    [0x002320, 0x002321],
    [0x002329, 0x00232A],
    [0x002768, 0x002775],
    [0x0027C3, 0x0027C6],
    [0x0027C8, 0x0027C9],
    [0x0027CB, 0x0027CD],
    [0x0027D3, 0x0027D6],
    [0x0027DC, 0x0027DE],
    [0x0027E2, 0x0027EF],
    [0x002983, 0x002998],
    [0x00299B, 0x0029A0],
    [0x0029A2, 0x0029AF],
    [0x0029C0, 0x0029C5],
    [0x0029CE, 0x0029D2],
    [0x0029D4, 0x0029D5],
    [0x0029D8, 0x0029DC],
    [0x0029E3, 0x0029E5],
    [0x0029E8, 0x0029E9],
    [0x0029F4, 0x0029F9],
    [0x0029FC, 0x0029FD],
    [0x002A0A, 0x002A1C],
    [0x002A1E, 0x002A21],
    [0x002A2B, 0x002A2E],
    [0x002A34, 0x002A35],
    [0x002A3C, 0x002A3E],
    [0x002A57, 0x002A58],
    [0x002A64, 0x002A65],
    [0x002A6A, 0x002A6D],
    [0x002A6F, 0x002A70],
    [0x002A73, 0x002A74],
    [0x002A79, 0x002AA3],
    [0x002AA6, 0x002AAD],
    [0x002AAF, 0x002AD6],
    [0x002AE2, 0x002AE6],
    [0x002AEC, 0x002AEE],
    [0x002AF7, 0x002AFB],
    [0x002E02, 0x002E05],
    [0x002E09, 0x002E0A],
    [0x002E0C, 0x002E0D],
    [0x002E1C, 0x002E1D],
    [0x002E20, 0x002E29],
    [0x002E55, 0x002E5C],
    [0x003008, 0x003011],
    [0x003014, 0x00301B],
    [0x00FE59, 0x00FE5E],
    [0x00FE64, 0x00FE65],
    [0x00FF08, 0x00FF09],
    [0x00FF5F, 0x00FF60],
    [0x00FF62, 0x00FF63]
  ]
});
testPropertyEscapes(
  /^\p{Bidi_Mirrored}+$/u,
  matchSymbols,
  "\\p{Bidi_Mirrored}"
);
testPropertyEscapes(
  /^\p{Bidi_M}+$/u,
  matchSymbols,
  "\\p{Bidi_M}"
);

const nonMatchSymbols = buildString({
  loneCodePoints: [
    0x00003D,
    0x00005C,
    0x00007C,
    0x00221E,
    0x002223,
    0x002225,
    0x00223A,
    0x002261,
    0x002263,
    0x00226C,
    0x0027C7,
    0x0027CA,
    0x0029A1,
    0x0029D3,
    0x0029E2,
    0x002A1D,
    0x002A25,
    0x002A2A,
    0x002A6E,
    0x002AAE,
    0x002ADD,
    0x002AFC,
    0x002E0B,
    0x00FF1D,
    0x00FF3C,
    0x00FF5C,
    0x00FF5E,
    0x00FF61
  ],
  ranges: [
    [0x00DC00, 0x00DFFF],
    [0x000000, 0x000027],
    [0x00002A, 0x00003B],
    [0x00003F, 0x00005A],
    [0x00005E, 0x00007A],
    [0x00007E, 0x0000AA],
    [0x0000AC, 0x0000BA],
    [0x0000BC, 0x000F39],
    [0x000F3E, 0x00169A],
    [0x00169D, 0x002038],
    [0x00203B, 0x002044],
    [0x002047, 0x00207C],
    [0x00207F, 0x00208C],
    [0x00208F, 0x00213F],
    [0x002141, 0x002200],
    [0x002205, 0x002207],
    [0x00220E, 0x002210],
    [0x002212, 0x002214],
    [0x002217, 0x002219],
    [0x002227, 0x00222A],
    [0x002234, 0x002238],
    [0x00224D, 0x002251],
    [0x002256, 0x00225E],
    [0x00228D, 0x00228E],
    [0x002293, 0x002297],
    [0x002299, 0x0022A1],
    [0x0022A4, 0x0022A5],
    [0x0022B9, 0x0022BD],
    [0x0022C0, 0x0022C8],
    [0x0022CE, 0x0022CF],
    [0x0022D2, 0x0022D5],
    [0x0022EE, 0x0022EF],
    [0x002300, 0x002307],
    [0x00230C, 0x00231F],
    [0x002322, 0x002328],
    [0x00232B, 0x002767],
    [0x002776, 0x0027BF],
    [0x0027C1, 0x0027C2],
    [0x0027CE, 0x0027D2],
    [0x0027D7, 0x0027DB],
    [0x0027DF, 0x0027E1],
    [0x0027F0, 0x002982],
    [0x002999, 0x00299A],
    [0x0029B0, 0x0029B7],
    [0x0029B9, 0x0029BF],
    [0x0029C6, 0x0029C8],
    [0x0029CA, 0x0029CD],
    [0x0029D6, 0x0029D7],
    [0x0029DD, 0x0029E0],
    [0x0029E6, 0x0029E7],
    [0x0029EA, 0x0029F3],
    [0x0029FA, 0x0029FB],
    [0x0029FE, 0x002A09],
    [0x002A22, 0x002A23],
    [0x002A27, 0x002A28],
    [0x002A2F, 0x002A33],
    [0x002A36, 0x002A3B],
    [0x002A3F, 0x002A56],
    [0x002A59, 0x002A63],
    [0x002A66, 0x002A69],
    [0x002A71, 0x002A72],
    [0x002A75, 0x002A78],
    [0x002AA4, 0x002AA5],
    [0x002AD7, 0x002ADB],
    [0x002ADF, 0x002AE1],
    [0x002AE7, 0x002AEB],
    [0x002AEF, 0x002AF2],
    [0x002AF4, 0x002AF6],
    [0x002AFE, 0x002BFD],
    [0x002BFF, 0x002E01],
    [0x002E06, 0x002E08],
    [0x002E0E, 0x002E1B],
    [0x002E1E, 0x002E1F],
    [0x002E2A, 0x002E54],
    [0x002E5D, 0x003007],
    [0x003012, 0x003013],
    [0x00301C, 0x00DBFF],
    [0x00E000, 0x00FE58],
    [0x00FE5F, 0x00FE63],
    [0x00FE66, 0x00FF07],
    [0x00FF0A, 0x00FF1B],
    [0x00FF1F, 0x00FF3A],
    [0x00FF3E, 0x00FF5A],
    [0x00FF64, 0x01D6DA],
    [0x01D6DC, 0x01D714],
    [0x01D716, 0x01D74E],
    [0x01D750, 0x01D788],
    [0x01D78A, 0x01D7C2],
    [0x01D7C4, 0x10FFFF]
  ]
});
testPropertyEscapes(
  /^\P{Bidi_Mirrored}+$/u,
  nonMatchSymbols,
  "\\P{Bidi_Mirrored}"
);
testPropertyEscapes(
  /^\P{Bidi_M}+$/u,
  nonMatchSymbols,
  "\\P{Bidi_M}"
);
