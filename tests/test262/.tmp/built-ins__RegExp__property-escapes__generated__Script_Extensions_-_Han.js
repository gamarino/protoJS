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
  Unicode property escapes for `Script_Extensions=Han`
info: |
  Generated by https://github.com/mathiasbynens/unicode-property-escapes-tests
  Unicode v17.0.0
esid: sec-static-semantics-unicodematchproperty-p
features: [regexp-unicode-property-escapes]
includes: [regExpUtils.js]
---*/

const matchSymbols = buildString({
  loneCodePoints: [
    0x0000B7,
    0x003030,
    0x0030FB,
    0x0031EF,
    0x0032FF
  ],
  ranges: [
    [0x002E80, 0x002E99],
    [0x002E9B, 0x002EF3],
    [0x002F00, 0x002FD5],
    [0x002FF0, 0x002FFF],
    [0x003001, 0x003003],
    [0x003005, 0x003011],
    [0x003013, 0x00301F],
    [0x003021, 0x00302D],
    [0x003037, 0x00303F],
    [0x003190, 0x00319F],
    [0x0031C0, 0x0031E5],
    [0x003220, 0x003247],
    [0x003280, 0x0032B0],
    [0x0032C0, 0x0032CB],
    [0x003358, 0x003370],
    [0x00337B, 0x00337F],
    [0x0033E0, 0x0033FE],
    [0x003400, 0x004DBF],
    [0x004E00, 0x009FFF],
    [0x00A700, 0x00A707],
    [0x00F900, 0x00FA6D],
    [0x00FA70, 0x00FAD9],
    [0x00FE45, 0x00FE46],
    [0x00FF61, 0x00FF65],
    [0x016FE2, 0x016FE3],
    [0x016FF0, 0x016FF6],
    [0x01D360, 0x01D371],
    [0x01F250, 0x01F251],
    [0x020000, 0x02A6DF],
    [0x02A700, 0x02B81D],
    [0x02B820, 0x02CEAD],
    [0x02CEB0, 0x02EBE0],
    [0x02EBF0, 0x02EE5D],
    [0x02F800, 0x02FA1D],
    [0x030000, 0x03134A],
    [0x031350, 0x033479]
  ]
});
testPropertyEscapes(
  /^\p{Script_Extensions=Han}+$/u,
  matchSymbols,
  "\\p{Script_Extensions=Han}"
);
testPropertyEscapes(
  /^\p{Script_Extensions=Hani}+$/u,
  matchSymbols,
  "\\p{Script_Extensions=Hani}"
);
testPropertyEscapes(
  /^\p{scx=Han}+$/u,
  matchSymbols,
  "\\p{scx=Han}"
);
testPropertyEscapes(
  /^\p{scx=Hani}+$/u,
  matchSymbols,
  "\\p{scx=Hani}"
);

const nonMatchSymbols = buildString({
  loneCodePoints: [
    0x002E9A,
    0x003000,
    0x003004,
    0x003012,
    0x003020,
    0x0033FF
  ],
  ranges: [
    [0x00DC00, 0x00DFFF],
    [0x000000, 0x0000B6],
    [0x0000B8, 0x002E7F],
    [0x002EF4, 0x002EFF],
    [0x002FD6, 0x002FEF],
    [0x00302E, 0x00302F],
    [0x003031, 0x003036],
    [0x003040, 0x0030FA],
    [0x0030FC, 0x00318F],
    [0x0031A0, 0x0031BF],
    [0x0031E6, 0x0031EE],
    [0x0031F0, 0x00321F],
    [0x003248, 0x00327F],
    [0x0032B1, 0x0032BF],
    [0x0032CC, 0x0032FE],
    [0x003300, 0x003357],
    [0x003371, 0x00337A],
    [0x003380, 0x0033DF],
    [0x004DC0, 0x004DFF],
    [0x00A000, 0x00A6FF],
    [0x00A708, 0x00DBFF],
    [0x00E000, 0x00F8FF],
    [0x00FA6E, 0x00FA6F],
    [0x00FADA, 0x00FE44],
    [0x00FE47, 0x00FF60],
    [0x00FF66, 0x016FE1],
    [0x016FE4, 0x016FEF],
    [0x016FF7, 0x01D35F],
    [0x01D372, 0x01F24F],
    [0x01F252, 0x01FFFF],
    [0x02A6E0, 0x02A6FF],
    [0x02B81E, 0x02B81F],
    [0x02CEAE, 0x02CEAF],
    [0x02EBE1, 0x02EBEF],
    [0x02EE5E, 0x02F7FF],
    [0x02FA1E, 0x02FFFF],
    [0x03134B, 0x03134F],
    [0x03347A, 0x10FFFF]
  ]
});
testPropertyEscapes(
  /^\P{Script_Extensions=Han}+$/u,
  nonMatchSymbols,
  "\\P{Script_Extensions=Han}"
);
testPropertyEscapes(
  /^\P{Script_Extensions=Hani}+$/u,
  nonMatchSymbols,
  "\\P{Script_Extensions=Hani}"
);
testPropertyEscapes(
  /^\P{scx=Han}+$/u,
  nonMatchSymbols,
  "\\P{scx=Han}"
);
testPropertyEscapes(
  /^\P{scx=Hani}+$/u,
  nonMatchSymbols,
  "\\P{scx=Hani}"
);
