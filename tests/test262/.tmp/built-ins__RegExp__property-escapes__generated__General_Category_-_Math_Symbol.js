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
  Unicode property escapes for `General_Category=Math_Symbol`
info: |
  Generated by https://github.com/mathiasbynens/unicode-property-escapes-tests
  Unicode v17.0.0
esid: sec-static-semantics-unicodematchproperty-p
features: [regexp-unicode-property-escapes]
includes: [regExpUtils.js]
---*/

const matchSymbols = buildString({
  loneCodePoints: [
    0x00002B,
    0x00007C,
    0x00007E,
    0x0000AC,
    0x0000B1,
    0x0000D7,
    0x0000F7,
    0x0003F6,
    0x002044,
    0x002052,
    0x002118,
    0x00214B,
    0x0021A0,
    0x0021A3,
    0x0021A6,
    0x0021AE,
    0x0021D2,
    0x0021D4,
    0x00237C,
    0x0025B7,
    0x0025C1,
    0x00266F,
    0x00FB29,
    0x00FE62,
    0x00FF0B,
    0x00FF5C,
    0x00FF5E,
    0x00FFE2,
    0x01CEF0,
    0x01D6C1,
    0x01D6DB,
    0x01D6FB,
    0x01D715,
    0x01D735,
    0x01D74F,
    0x01D76F,
    0x01D789,
    0x01D7A9,
    0x01D7C3
  ],
  ranges: [
    [0x00003C, 0x00003E],
    [0x000606, 0x000608],
    [0x00207A, 0x00207C],
    [0x00208A, 0x00208C],
    [0x002140, 0x002144],
    [0x002190, 0x002194],
    [0x00219A, 0x00219B],
    [0x0021CE, 0x0021CF],
    [0x0021F4, 0x0022FF],
    [0x002320, 0x002321],
    [0x00239B, 0x0023B3],
    [0x0023DC, 0x0023E1],
    [0x0025F8, 0x0025FF],
    [0x0027C0, 0x0027C4],
    [0x0027C7, 0x0027E5],
    [0x0027F0, 0x0027FF],
    [0x002900, 0x002982],
    [0x002999, 0x0029D7],
    [0x0029DC, 0x0029FB],
    [0x0029FE, 0x002AFF],
    [0x002B30, 0x002B44],
    [0x002B47, 0x002B4C],
    [0x00FE64, 0x00FE66],
    [0x00FF1C, 0x00FF1E],
    [0x00FFE9, 0x00FFEC],
    [0x010D8E, 0x010D8F],
    [0x01EEF0, 0x01EEF1],
    [0x01F8D0, 0x01F8D8]
  ]
});
testPropertyEscapes(
  /^\p{General_Category=Math_Symbol}+$/u,
  matchSymbols,
  "\\p{General_Category=Math_Symbol}"
);
testPropertyEscapes(
  /^\p{General_Category=Sm}+$/u,
  matchSymbols,
  "\\p{General_Category=Sm}"
);
testPropertyEscapes(
  /^\p{gc=Math_Symbol}+$/u,
  matchSymbols,
  "\\p{gc=Math_Symbol}"
);
testPropertyEscapes(
  /^\p{gc=Sm}+$/u,
  matchSymbols,
  "\\p{gc=Sm}"
);
testPropertyEscapes(
  /^\p{Math_Symbol}+$/u,
  matchSymbols,
  "\\p{Math_Symbol}"
);
testPropertyEscapes(
  /^\p{Sm}+$/u,
  matchSymbols,
  "\\p{Sm}"
);

const nonMatchSymbols = buildString({
  loneCodePoints: [
    0x00007D,
    0x0021D3,
    0x00FE63,
    0x00FF5D
  ],
  ranges: [
    [0x00DC00, 0x00DFFF],
    [0x000000, 0x00002A],
    [0x00002C, 0x00003B],
    [0x00003F, 0x00007B],
    [0x00007F, 0x0000AB],
    [0x0000AD, 0x0000B0],
    [0x0000B2, 0x0000D6],
    [0x0000D8, 0x0000F6],
    [0x0000F8, 0x0003F5],
    [0x0003F7, 0x000605],
    [0x000609, 0x002043],
    [0x002045, 0x002051],
    [0x002053, 0x002079],
    [0x00207D, 0x002089],
    [0x00208D, 0x002117],
    [0x002119, 0x00213F],
    [0x002145, 0x00214A],
    [0x00214C, 0x00218F],
    [0x002195, 0x002199],
    [0x00219C, 0x00219F],
    [0x0021A1, 0x0021A2],
    [0x0021A4, 0x0021A5],
    [0x0021A7, 0x0021AD],
    [0x0021AF, 0x0021CD],
    [0x0021D0, 0x0021D1],
    [0x0021D5, 0x0021F3],
    [0x002300, 0x00231F],
    [0x002322, 0x00237B],
    [0x00237D, 0x00239A],
    [0x0023B4, 0x0023DB],
    [0x0023E2, 0x0025B6],
    [0x0025B8, 0x0025C0],
    [0x0025C2, 0x0025F7],
    [0x002600, 0x00266E],
    [0x002670, 0x0027BF],
    [0x0027C5, 0x0027C6],
    [0x0027E6, 0x0027EF],
    [0x002800, 0x0028FF],
    [0x002983, 0x002998],
    [0x0029D8, 0x0029DB],
    [0x0029FC, 0x0029FD],
    [0x002B00, 0x002B2F],
    [0x002B45, 0x002B46],
    [0x002B4D, 0x00DBFF],
    [0x00E000, 0x00FB28],
    [0x00FB2A, 0x00FE61],
    [0x00FE67, 0x00FF0A],
    [0x00FF0C, 0x00FF1B],
    [0x00FF1F, 0x00FF5B],
    [0x00FF5F, 0x00FFE1],
    [0x00FFE3, 0x00FFE8],
    [0x00FFED, 0x010D8D],
    [0x010D90, 0x01CEEF],
    [0x01CEF1, 0x01D6C0],
    [0x01D6C2, 0x01D6DA],
    [0x01D6DC, 0x01D6FA],
    [0x01D6FC, 0x01D714],
    [0x01D716, 0x01D734],
    [0x01D736, 0x01D74E],
    [0x01D750, 0x01D76E],
    [0x01D770, 0x01D788],
    [0x01D78A, 0x01D7A8],
    [0x01D7AA, 0x01D7C2],
    [0x01D7C4, 0x01EEEF],
    [0x01EEF2, 0x01F8CF],
    [0x01F8D9, 0x10FFFF]
  ]
});
testPropertyEscapes(
  /^\P{General_Category=Math_Symbol}+$/u,
  nonMatchSymbols,
  "\\P{General_Category=Math_Symbol}"
);
testPropertyEscapes(
  /^\P{General_Category=Sm}+$/u,
  nonMatchSymbols,
  "\\P{General_Category=Sm}"
);
testPropertyEscapes(
  /^\P{gc=Math_Symbol}+$/u,
  nonMatchSymbols,
  "\\P{gc=Math_Symbol}"
);
testPropertyEscapes(
  /^\P{gc=Sm}+$/u,
  nonMatchSymbols,
  "\\P{gc=Sm}"
);
testPropertyEscapes(
  /^\P{Math_Symbol}+$/u,
  nonMatchSymbols,
  "\\P{Math_Symbol}"
);
testPropertyEscapes(
  /^\P{Sm}+$/u,
  nonMatchSymbols,
  "\\P{Sm}"
);
