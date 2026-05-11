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
  Unicode property escapes for `General_Category=Close_Punctuation`
info: |
  Generated by https://github.com/mathiasbynens/unicode-property-escapes-tests
  Unicode v17.0.0
esid: sec-static-semantics-unicodematchproperty-p
features: [regexp-unicode-property-escapes]
includes: [regExpUtils.js]
---*/

const matchSymbols = buildString({
  loneCodePoints: [
    0x000029,
    0x00005D,
    0x00007D,
    0x000F3B,
    0x000F3D,
    0x00169C,
    0x002046,
    0x00207E,
    0x00208E,
    0x002309,
    0x00230B,
    0x00232A,
    0x002769,
    0x00276B,
    0x00276D,
    0x00276F,
    0x002771,
    0x002773,
    0x002775,
    0x0027C6,
    0x0027E7,
    0x0027E9,
    0x0027EB,
    0x0027ED,
    0x0027EF,
    0x002984,
    0x002986,
    0x002988,
    0x00298A,
    0x00298C,
    0x00298E,
    0x002990,
    0x002992,
    0x002994,
    0x002996,
    0x002998,
    0x0029D9,
    0x0029DB,
    0x0029FD,
    0x002E23,
    0x002E25,
    0x002E27,
    0x002E29,
    0x002E56,
    0x002E58,
    0x002E5A,
    0x002E5C,
    0x003009,
    0x00300B,
    0x00300D,
    0x00300F,
    0x003011,
    0x003015,
    0x003017,
    0x003019,
    0x00301B,
    0x00FD3E,
    0x00FE18,
    0x00FE36,
    0x00FE38,
    0x00FE3A,
    0x00FE3C,
    0x00FE3E,
    0x00FE40,
    0x00FE42,
    0x00FE44,
    0x00FE48,
    0x00FE5A,
    0x00FE5C,
    0x00FE5E,
    0x00FF09,
    0x00FF3D,
    0x00FF5D,
    0x00FF60,
    0x00FF63
  ],
  ranges: [
    [0x00301E, 0x00301F]
  ]
});
testPropertyEscapes(
  /^\p{General_Category=Close_Punctuation}+$/u,
  matchSymbols,
  "\\p{General_Category=Close_Punctuation}"
);
testPropertyEscapes(
  /^\p{General_Category=Pe}+$/u,
  matchSymbols,
  "\\p{General_Category=Pe}"
);
testPropertyEscapes(
  /^\p{gc=Close_Punctuation}+$/u,
  matchSymbols,
  "\\p{gc=Close_Punctuation}"
);
testPropertyEscapes(
  /^\p{gc=Pe}+$/u,
  matchSymbols,
  "\\p{gc=Pe}"
);
testPropertyEscapes(
  /^\p{Close_Punctuation}+$/u,
  matchSymbols,
  "\\p{Close_Punctuation}"
);
testPropertyEscapes(
  /^\p{Pe}+$/u,
  matchSymbols,
  "\\p{Pe}"
);

const nonMatchSymbols = buildString({
  loneCodePoints: [
    0x000F3C,
    0x00230A,
    0x00276A,
    0x00276C,
    0x00276E,
    0x002770,
    0x002772,
    0x002774,
    0x0027E8,
    0x0027EA,
    0x0027EC,
    0x0027EE,
    0x002985,
    0x002987,
    0x002989,
    0x00298B,
    0x00298D,
    0x00298F,
    0x002991,
    0x002993,
    0x002995,
    0x002997,
    0x0029DA,
    0x002E24,
    0x002E26,
    0x002E28,
    0x002E57,
    0x002E59,
    0x002E5B,
    0x00300A,
    0x00300C,
    0x00300E,
    0x003010,
    0x003016,
    0x003018,
    0x00301A,
    0x00FE37,
    0x00FE39,
    0x00FE3B,
    0x00FE3D,
    0x00FE3F,
    0x00FE41,
    0x00FE43,
    0x00FE5B,
    0x00FE5D
  ],
  ranges: [
    [0x00DC00, 0x00DFFF],
    [0x000000, 0x000028],
    [0x00002A, 0x00005C],
    [0x00005E, 0x00007C],
    [0x00007E, 0x000F3A],
    [0x000F3E, 0x00169B],
    [0x00169D, 0x002045],
    [0x002047, 0x00207D],
    [0x00207F, 0x00208D],
    [0x00208F, 0x002308],
    [0x00230C, 0x002329],
    [0x00232B, 0x002768],
    [0x002776, 0x0027C5],
    [0x0027C7, 0x0027E6],
    [0x0027F0, 0x002983],
    [0x002999, 0x0029D8],
    [0x0029DC, 0x0029FC],
    [0x0029FE, 0x002E22],
    [0x002E2A, 0x002E55],
    [0x002E5D, 0x003008],
    [0x003012, 0x003014],
    [0x00301C, 0x00301D],
    [0x003020, 0x00DBFF],
    [0x00E000, 0x00FD3D],
    [0x00FD3F, 0x00FE17],
    [0x00FE19, 0x00FE35],
    [0x00FE45, 0x00FE47],
    [0x00FE49, 0x00FE59],
    [0x00FE5F, 0x00FF08],
    [0x00FF0A, 0x00FF3C],
    [0x00FF3E, 0x00FF5C],
    [0x00FF5E, 0x00FF5F],
    [0x00FF61, 0x00FF62],
    [0x00FF64, 0x10FFFF]
  ]
});
testPropertyEscapes(
  /^\P{General_Category=Close_Punctuation}+$/u,
  nonMatchSymbols,
  "\\P{General_Category=Close_Punctuation}"
);
testPropertyEscapes(
  /^\P{General_Category=Pe}+$/u,
  nonMatchSymbols,
  "\\P{General_Category=Pe}"
);
testPropertyEscapes(
  /^\P{gc=Close_Punctuation}+$/u,
  nonMatchSymbols,
  "\\P{gc=Close_Punctuation}"
);
testPropertyEscapes(
  /^\P{gc=Pe}+$/u,
  nonMatchSymbols,
  "\\P{gc=Pe}"
);
testPropertyEscapes(
  /^\P{Close_Punctuation}+$/u,
  nonMatchSymbols,
  "\\P{Close_Punctuation}"
);
testPropertyEscapes(
  /^\P{Pe}+$/u,
  nonMatchSymbols,
  "\\P{Pe}"
);
