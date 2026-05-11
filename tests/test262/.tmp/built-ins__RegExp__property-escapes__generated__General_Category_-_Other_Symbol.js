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
  Unicode property escapes for `General_Category=Other_Symbol`
info: |
  Generated by https://github.com/mathiasbynens/unicode-property-escapes-tests
  Unicode v17.0.0
esid: sec-static-semantics-unicodematchproperty-p
features: [regexp-unicode-property-escapes]
includes: [regExpUtils.js]
---*/

const matchSymbols = buildString({
  loneCodePoints: [
    0x0000A6,
    0x0000A9,
    0x0000AE,
    0x0000B0,
    0x000482,
    0x0006DE,
    0x0006E9,
    0x0007F6,
    0x0009FA,
    0x000B70,
    0x000BFA,
    0x000C7F,
    0x000D4F,
    0x000D79,
    0x000F13,
    0x000F34,
    0x000F36,
    0x000F38,
    0x00166D,
    0x001940,
    0x002114,
    0x002125,
    0x002127,
    0x002129,
    0x00212E,
    0x00214A,
    0x00214F,
    0x0021D3,
    0x003004,
    0x003020,
    0x0031EF,
    0x003250,
    0x00A839,
    0x00FFE4,
    0x00FFE8,
    0x0101A0,
    0x010AC8,
    0x01173F,
    0x016B45,
    0x01BC9C,
    0x01D245,
    0x01E14F,
    0x01ECAC,
    0x01ED2E,
    0x01F7F0,
    0x01FAC8,
    0x01FBFA
  ],
  ranges: [
    [0x00058D, 0x00058E],
    [0x00060E, 0x00060F],
    [0x0006FD, 0x0006FE],
    [0x000BF3, 0x000BF8],
    [0x000F01, 0x000F03],
    [0x000F15, 0x000F17],
    [0x000F1A, 0x000F1F],
    [0x000FBE, 0x000FC5],
    [0x000FC7, 0x000FCC],
    [0x000FCE, 0x000FCF],
    [0x000FD5, 0x000FD8],
    [0x00109E, 0x00109F],
    [0x001390, 0x001399],
    [0x0019DE, 0x0019FF],
    [0x001B61, 0x001B6A],
    [0x001B74, 0x001B7C],
    [0x002100, 0x002101],
    [0x002103, 0x002106],
    [0x002108, 0x002109],
    [0x002116, 0x002117],
    [0x00211E, 0x002123],
    [0x00213A, 0x00213B],
    [0x00214C, 0x00214D],
    [0x00218A, 0x00218B],
    [0x002195, 0x002199],
    [0x00219C, 0x00219F],
    [0x0021A1, 0x0021A2],
    [0x0021A4, 0x0021A5],
    [0x0021A7, 0x0021AD],
    [0x0021AF, 0x0021CD],
    [0x0021D0, 0x0021D1],
    [0x0021D5, 0x0021F3],
    [0x002300, 0x002307],
    [0x00230C, 0x00231F],
    [0x002322, 0x002328],
    [0x00232B, 0x00237B],
    [0x00237D, 0x00239A],
    [0x0023B4, 0x0023DB],
    [0x0023E2, 0x002429],
    [0x002440, 0x00244A],
    [0x00249C, 0x0024E9],
    [0x002500, 0x0025B6],
    [0x0025B8, 0x0025C0],
    [0x0025C2, 0x0025F7],
    [0x002600, 0x00266E],
    [0x002670, 0x002767],
    [0x002794, 0x0027BF],
    [0x002800, 0x0028FF],
    [0x002B00, 0x002B2F],
    [0x002B45, 0x002B46],
    [0x002B4D, 0x002B73],
    [0x002B76, 0x002BFF],
    [0x002CE5, 0x002CEA],
    [0x002E50, 0x002E51],
    [0x002E80, 0x002E99],
    [0x002E9B, 0x002EF3],
    [0x002F00, 0x002FD5],
    [0x002FF0, 0x002FFF],
    [0x003012, 0x003013],
    [0x003036, 0x003037],
    [0x00303E, 0x00303F],
    [0x003190, 0x003191],
    [0x003196, 0x00319F],
    [0x0031C0, 0x0031E5],
    [0x003200, 0x00321E],
    [0x00322A, 0x003247],
    [0x003260, 0x00327F],
    [0x00328A, 0x0032B0],
    [0x0032C0, 0x0033FF],
    [0x004DC0, 0x004DFF],
    [0x00A490, 0x00A4C6],
    [0x00A828, 0x00A82B],
    [0x00A836, 0x00A837],
    [0x00AA77, 0x00AA79],
    [0x00FBC3, 0x00FBD2],
    [0x00FD40, 0x00FD4F],
    [0x00FD90, 0x00FD91],
    [0x00FDC8, 0x00FDCF],
    [0x00FDFD, 0x00FDFF],
    [0x00FFED, 0x00FFEE],
    [0x00FFFC, 0x00FFFD],
    [0x010137, 0x01013F],
    [0x010179, 0x010189],
    [0x01018C, 0x01018E],
    [0x010190, 0x01019C],
    [0x0101D0, 0x0101FC],
    [0x010877, 0x010878],
    [0x010ED1, 0x010ED8],
    [0x011FD5, 0x011FDC],
    [0x011FE1, 0x011FF1],
    [0x016B3C, 0x016B3F],
    [0x01CC00, 0x01CCEF],
    [0x01CCFA, 0x01CCFC],
    [0x01CD00, 0x01CEB3],
    [0x01CEBA, 0x01CED0],
    [0x01CEE0, 0x01CEEF],
    [0x01CF50, 0x01CFC3],
    [0x01D000, 0x01D0F5],
    [0x01D100, 0x01D126],
    [0x01D129, 0x01D164],
    [0x01D16A, 0x01D16C],
    [0x01D183, 0x01D184],
    [0x01D18C, 0x01D1A9],
    [0x01D1AE, 0x01D1EA],
    [0x01D200, 0x01D241],
    [0x01D300, 0x01D356],
    [0x01D800, 0x01D9FF],
    [0x01DA37, 0x01DA3A],
    [0x01DA6D, 0x01DA74],
    [0x01DA76, 0x01DA83],
    [0x01DA85, 0x01DA86],
    [0x01F000, 0x01F02B],
    [0x01F030, 0x01F093],
    [0x01F0A0, 0x01F0AE],
    [0x01F0B1, 0x01F0BF],
    [0x01F0C1, 0x01F0CF],
    [0x01F0D1, 0x01F0F5],
    [0x01F10D, 0x01F1AD],
    [0x01F1E6, 0x01F202],
    [0x01F210, 0x01F23B],
    [0x01F240, 0x01F248],
    [0x01F250, 0x01F251],
    [0x01F260, 0x01F265],
    [0x01F300, 0x01F3FA],
    [0x01F400, 0x01F6D8],
    [0x01F6DC, 0x01F6EC],
    [0x01F6F0, 0x01F6FC],
    [0x01F700, 0x01F7D9],
    [0x01F7E0, 0x01F7EB],
    [0x01F800, 0x01F80B],
    [0x01F810, 0x01F847],
    [0x01F850, 0x01F859],
    [0x01F860, 0x01F887],
    [0x01F890, 0x01F8AD],
    [0x01F8B0, 0x01F8BB],
    [0x01F8C0, 0x01F8C1],
    [0x01F900, 0x01FA57],
    [0x01FA60, 0x01FA6D],
    [0x01FA70, 0x01FA7C],
    [0x01FA80, 0x01FA8A],
    [0x01FA8E, 0x01FAC6],
    [0x01FACD, 0x01FADC],
    [0x01FADF, 0x01FAEA],
    [0x01FAEF, 0x01FAF8],
    [0x01FB00, 0x01FB92],
    [0x01FB94, 0x01FBEF]
  ]
});
testPropertyEscapes(
  /^\p{General_Category=Other_Symbol}+$/u,
  matchSymbols,
  "\\p{General_Category=Other_Symbol}"
);
testPropertyEscapes(
  /^\p{General_Category=So}+$/u,
  matchSymbols,
  "\\p{General_Category=So}"
);
testPropertyEscapes(
  /^\p{gc=Other_Symbol}+$/u,
  matchSymbols,
  "\\p{gc=Other_Symbol}"
);
testPropertyEscapes(
  /^\p{gc=So}+$/u,
  matchSymbols,
  "\\p{gc=So}"
);
testPropertyEscapes(
  /^\p{Other_Symbol}+$/u,
  matchSymbols,
  "\\p{Other_Symbol}"
);
testPropertyEscapes(
  /^\p{So}+$/u,
  matchSymbols,
  "\\p{So}"
);

const nonMatchSymbols = buildString({
  loneCodePoints: [
    0x0000AF,
    0x000BF9,
    0x000F14,
    0x000F35,
    0x000F37,
    0x000FC6,
    0x000FCD,
    0x002102,
    0x002107,
    0x002115,
    0x002124,
    0x002126,
    0x002128,
    0x00214B,
    0x00214E,
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
    0x002E9A,
    0x00A838,
    0x01018F,
    0x01DA75,
    0x01DA84,
    0x01F0C0,
    0x01F0D0,
    0x01FAC7,
    0x01FB93
  ],
  ranges: [
    [0x00DC00, 0x00DFFF],
    [0x000000, 0x0000A5],
    [0x0000A7, 0x0000A8],
    [0x0000AA, 0x0000AD],
    [0x0000B1, 0x000481],
    [0x000483, 0x00058C],
    [0x00058F, 0x00060D],
    [0x000610, 0x0006DD],
    [0x0006DF, 0x0006E8],
    [0x0006EA, 0x0006FC],
    [0x0006FF, 0x0007F5],
    [0x0007F7, 0x0009F9],
    [0x0009FB, 0x000B6F],
    [0x000B71, 0x000BF2],
    [0x000BFB, 0x000C7E],
    [0x000C80, 0x000D4E],
    [0x000D50, 0x000D78],
    [0x000D7A, 0x000F00],
    [0x000F04, 0x000F12],
    [0x000F18, 0x000F19],
    [0x000F20, 0x000F33],
    [0x000F39, 0x000FBD],
    [0x000FD0, 0x000FD4],
    [0x000FD9, 0x00109D],
    [0x0010A0, 0x00138F],
    [0x00139A, 0x00166C],
    [0x00166E, 0x00193F],
    [0x001941, 0x0019DD],
    [0x001A00, 0x001B60],
    [0x001B6B, 0x001B73],
    [0x001B7D, 0x0020FF],
    [0x00210A, 0x002113],
    [0x002118, 0x00211D],
    [0x00212A, 0x00212D],
    [0x00212F, 0x002139],
    [0x00213C, 0x002149],
    [0x002150, 0x002189],
    [0x00218C, 0x002194],
    [0x00219A, 0x00219B],
    [0x0021CE, 0x0021CF],
    [0x0021F4, 0x0022FF],
    [0x002308, 0x00230B],
    [0x002320, 0x002321],
    [0x002329, 0x00232A],
    [0x00239B, 0x0023B3],
    [0x0023DC, 0x0023E1],
    [0x00242A, 0x00243F],
    [0x00244B, 0x00249B],
    [0x0024EA, 0x0024FF],
    [0x0025F8, 0x0025FF],
    [0x002768, 0x002793],
    [0x0027C0, 0x0027FF],
    [0x002900, 0x002AFF],
    [0x002B30, 0x002B44],
    [0x002B47, 0x002B4C],
    [0x002B74, 0x002B75],
    [0x002C00, 0x002CE4],
    [0x002CEB, 0x002E4F],
    [0x002E52, 0x002E7F],
    [0x002EF4, 0x002EFF],
    [0x002FD6, 0x002FEF],
    [0x003000, 0x003003],
    [0x003005, 0x003011],
    [0x003014, 0x00301F],
    [0x003021, 0x003035],
    [0x003038, 0x00303D],
    [0x003040, 0x00318F],
    [0x003192, 0x003195],
    [0x0031A0, 0x0031BF],
    [0x0031E6, 0x0031EE],
    [0x0031F0, 0x0031FF],
    [0x00321F, 0x003229],
    [0x003248, 0x00324F],
    [0x003251, 0x00325F],
    [0x003280, 0x003289],
    [0x0032B1, 0x0032BF],
    [0x003400, 0x004DBF],
    [0x004E00, 0x00A48F],
    [0x00A4C7, 0x00A827],
    [0x00A82C, 0x00A835],
    [0x00A83A, 0x00AA76],
    [0x00AA7A, 0x00DBFF],
    [0x00E000, 0x00FBC2],
    [0x00FBD3, 0x00FD3F],
    [0x00FD50, 0x00FD8F],
    [0x00FD92, 0x00FDC7],
    [0x00FDD0, 0x00FDFC],
    [0x00FE00, 0x00FFE3],
    [0x00FFE5, 0x00FFE7],
    [0x00FFE9, 0x00FFEC],
    [0x00FFEF, 0x00FFFB],
    [0x00FFFE, 0x010136],
    [0x010140, 0x010178],
    [0x01018A, 0x01018B],
    [0x01019D, 0x01019F],
    [0x0101A1, 0x0101CF],
    [0x0101FD, 0x010876],
    [0x010879, 0x010AC7],
    [0x010AC9, 0x010ED0],
    [0x010ED9, 0x01173E],
    [0x011740, 0x011FD4],
    [0x011FDD, 0x011FE0],
    [0x011FF2, 0x016B3B],
    [0x016B40, 0x016B44],
    [0x016B46, 0x01BC9B],
    [0x01BC9D, 0x01CBFF],
    [0x01CCF0, 0x01CCF9],
    [0x01CCFD, 0x01CCFF],
    [0x01CEB4, 0x01CEB9],
    [0x01CED1, 0x01CEDF],
    [0x01CEF0, 0x01CF4F],
    [0x01CFC4, 0x01CFFF],
    [0x01D0F6, 0x01D0FF],
    [0x01D127, 0x01D128],
    [0x01D165, 0x01D169],
    [0x01D16D, 0x01D182],
    [0x01D185, 0x01D18B],
    [0x01D1AA, 0x01D1AD],
    [0x01D1EB, 0x01D1FF],
    [0x01D242, 0x01D244],
    [0x01D246, 0x01D2FF],
    [0x01D357, 0x01D7FF],
    [0x01DA00, 0x01DA36],
    [0x01DA3B, 0x01DA6C],
    [0x01DA87, 0x01E14E],
    [0x01E150, 0x01ECAB],
    [0x01ECAD, 0x01ED2D],
    [0x01ED2F, 0x01EFFF],
    [0x01F02C, 0x01F02F],
    [0x01F094, 0x01F09F],
    [0x01F0AF, 0x01F0B0],
    [0x01F0F6, 0x01F10C],
    [0x01F1AE, 0x01F1E5],
    [0x01F203, 0x01F20F],
    [0x01F23C, 0x01F23F],
    [0x01F249, 0x01F24F],
    [0x01F252, 0x01F25F],
    [0x01F266, 0x01F2FF],
    [0x01F3FB, 0x01F3FF],
    [0x01F6D9, 0x01F6DB],
    [0x01F6ED, 0x01F6EF],
    [0x01F6FD, 0x01F6FF],
    [0x01F7DA, 0x01F7DF],
    [0x01F7EC, 0x01F7EF],
    [0x01F7F1, 0x01F7FF],
    [0x01F80C, 0x01F80F],
    [0x01F848, 0x01F84F],
    [0x01F85A, 0x01F85F],
    [0x01F888, 0x01F88F],
    [0x01F8AE, 0x01F8AF],
    [0x01F8BC, 0x01F8BF],
    [0x01F8C2, 0x01F8FF],
    [0x01FA58, 0x01FA5F],
    [0x01FA6E, 0x01FA6F],
    [0x01FA7D, 0x01FA7F],
    [0x01FA8B, 0x01FA8D],
    [0x01FAC9, 0x01FACC],
    [0x01FADD, 0x01FADE],
    [0x01FAEB, 0x01FAEE],
    [0x01FAF9, 0x01FAFF],
    [0x01FBF0, 0x01FBF9],
    [0x01FBFB, 0x10FFFF]
  ]
});
testPropertyEscapes(
  /^\P{General_Category=Other_Symbol}+$/u,
  nonMatchSymbols,
  "\\P{General_Category=Other_Symbol}"
);
testPropertyEscapes(
  /^\P{General_Category=So}+$/u,
  nonMatchSymbols,
  "\\P{General_Category=So}"
);
testPropertyEscapes(
  /^\P{gc=Other_Symbol}+$/u,
  nonMatchSymbols,
  "\\P{gc=Other_Symbol}"
);
testPropertyEscapes(
  /^\P{gc=So}+$/u,
  nonMatchSymbols,
  "\\P{gc=So}"
);
testPropertyEscapes(
  /^\P{Other_Symbol}+$/u,
  nonMatchSymbols,
  "\\P{Other_Symbol}"
);
testPropertyEscapes(
  /^\P{So}+$/u,
  nonMatchSymbols,
  "\\P{So}"
);
