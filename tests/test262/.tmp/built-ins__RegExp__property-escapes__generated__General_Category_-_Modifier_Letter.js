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
  Unicode property escapes for `General_Category=Modifier_Letter`
info: |
  Generated by https://github.com/mathiasbynens/unicode-property-escapes-tests
  Unicode v17.0.0
esid: sec-static-semantics-unicodematchproperty-p
features: [regexp-unicode-property-escapes]
includes: [regExpUtils.js]
---*/

const matchSymbols = buildString({
  loneCodePoints: [
    0x0002EC,
    0x0002EE,
    0x000374,
    0x00037A,
    0x000559,
    0x000640,
    0x0007FA,
    0x00081A,
    0x000824,
    0x000828,
    0x0008C9,
    0x000971,
    0x000E46,
    0x000EC6,
    0x0010FC,
    0x0017D7,
    0x001843,
    0x001AA7,
    0x001D78,
    0x002071,
    0x00207F,
    0x002D6F,
    0x002E2F,
    0x003005,
    0x00303B,
    0x00A015,
    0x00A60C,
    0x00A67F,
    0x00A770,
    0x00A788,
    0x00A9CF,
    0x00A9E6,
    0x00AA70,
    0x00AADD,
    0x00AB69,
    0x00FF70,
    0x010D4E,
    0x010D6F,
    0x010EC5,
    0x011DD9,
    0x016FE3,
    0x01E4EB,
    0x01E6FF,
    0x01E94B
  ],
  ranges: [
    [0x0002B0, 0x0002C1],
    [0x0002C6, 0x0002D1],
    [0x0002E0, 0x0002E4],
    [0x0006E5, 0x0006E6],
    [0x0007F4, 0x0007F5],
    [0x001C78, 0x001C7D],
    [0x001D2C, 0x001D6A],
    [0x001D9B, 0x001DBF],
    [0x002090, 0x00209C],
    [0x002C7C, 0x002C7D],
    [0x003031, 0x003035],
    [0x00309D, 0x00309E],
    [0x0030FC, 0x0030FE],
    [0x00A4F8, 0x00A4FD],
    [0x00A69C, 0x00A69D],
    [0x00A717, 0x00A71F],
    [0x00A7F1, 0x00A7F4],
    [0x00A7F8, 0x00A7F9],
    [0x00AAF3, 0x00AAF4],
    [0x00AB5C, 0x00AB5F],
    [0x00FF9E, 0x00FF9F],
    [0x010780, 0x010785],
    [0x010787, 0x0107B0],
    [0x0107B2, 0x0107BA],
    [0x016B40, 0x016B43],
    [0x016D40, 0x016D42],
    [0x016D6B, 0x016D6C],
    [0x016F93, 0x016F9F],
    [0x016FE0, 0x016FE1],
    [0x016FF2, 0x016FF3],
    [0x01AFF0, 0x01AFF3],
    [0x01AFF5, 0x01AFFB],
    [0x01AFFD, 0x01AFFE],
    [0x01E030, 0x01E06D],
    [0x01E137, 0x01E13D]
  ]
});
testPropertyEscapes(
  /^\p{General_Category=Modifier_Letter}+$/u,
  matchSymbols,
  "\\p{General_Category=Modifier_Letter}"
);
testPropertyEscapes(
  /^\p{General_Category=Lm}+$/u,
  matchSymbols,
  "\\p{General_Category=Lm}"
);
testPropertyEscapes(
  /^\p{gc=Modifier_Letter}+$/u,
  matchSymbols,
  "\\p{gc=Modifier_Letter}"
);
testPropertyEscapes(
  /^\p{gc=Lm}+$/u,
  matchSymbols,
  "\\p{gc=Lm}"
);
testPropertyEscapes(
  /^\p{Modifier_Letter}+$/u,
  matchSymbols,
  "\\p{Modifier_Letter}"
);
testPropertyEscapes(
  /^\p{Lm}+$/u,
  matchSymbols,
  "\\p{Lm}"
);

const nonMatchSymbols = buildString({
  loneCodePoints: [
    0x0002ED,
    0x010786,
    0x0107B1,
    0x016FE2,
    0x01AFF4,
    0x01AFFC
  ],
  ranges: [
    [0x00DC00, 0x00DFFF],
    [0x000000, 0x0002AF],
    [0x0002C2, 0x0002C5],
    [0x0002D2, 0x0002DF],
    [0x0002E5, 0x0002EB],
    [0x0002EF, 0x000373],
    [0x000375, 0x000379],
    [0x00037B, 0x000558],
    [0x00055A, 0x00063F],
    [0x000641, 0x0006E4],
    [0x0006E7, 0x0007F3],
    [0x0007F6, 0x0007F9],
    [0x0007FB, 0x000819],
    [0x00081B, 0x000823],
    [0x000825, 0x000827],
    [0x000829, 0x0008C8],
    [0x0008CA, 0x000970],
    [0x000972, 0x000E45],
    [0x000E47, 0x000EC5],
    [0x000EC7, 0x0010FB],
    [0x0010FD, 0x0017D6],
    [0x0017D8, 0x001842],
    [0x001844, 0x001AA6],
    [0x001AA8, 0x001C77],
    [0x001C7E, 0x001D2B],
    [0x001D6B, 0x001D77],
    [0x001D79, 0x001D9A],
    [0x001DC0, 0x002070],
    [0x002072, 0x00207E],
    [0x002080, 0x00208F],
    [0x00209D, 0x002C7B],
    [0x002C7E, 0x002D6E],
    [0x002D70, 0x002E2E],
    [0x002E30, 0x003004],
    [0x003006, 0x003030],
    [0x003036, 0x00303A],
    [0x00303C, 0x00309C],
    [0x00309F, 0x0030FB],
    [0x0030FF, 0x00A014],
    [0x00A016, 0x00A4F7],
    [0x00A4FE, 0x00A60B],
    [0x00A60D, 0x00A67E],
    [0x00A680, 0x00A69B],
    [0x00A69E, 0x00A716],
    [0x00A720, 0x00A76F],
    [0x00A771, 0x00A787],
    [0x00A789, 0x00A7F0],
    [0x00A7F5, 0x00A7F7],
    [0x00A7FA, 0x00A9CE],
    [0x00A9D0, 0x00A9E5],
    [0x00A9E7, 0x00AA6F],
    [0x00AA71, 0x00AADC],
    [0x00AADE, 0x00AAF2],
    [0x00AAF5, 0x00AB5B],
    [0x00AB60, 0x00AB68],
    [0x00AB6A, 0x00DBFF],
    [0x00E000, 0x00FF6F],
    [0x00FF71, 0x00FF9D],
    [0x00FFA0, 0x01077F],
    [0x0107BB, 0x010D4D],
    [0x010D4F, 0x010D6E],
    [0x010D70, 0x010EC4],
    [0x010EC6, 0x011DD8],
    [0x011DDA, 0x016B3F],
    [0x016B44, 0x016D3F],
    [0x016D43, 0x016D6A],
    [0x016D6D, 0x016F92],
    [0x016FA0, 0x016FDF],
    [0x016FE4, 0x016FF1],
    [0x016FF4, 0x01AFEF],
    [0x01AFFF, 0x01E02F],
    [0x01E06E, 0x01E136],
    [0x01E13E, 0x01E4EA],
    [0x01E4EC, 0x01E6FE],
    [0x01E700, 0x01E94A],
    [0x01E94C, 0x10FFFF]
  ]
});
testPropertyEscapes(
  /^\P{General_Category=Modifier_Letter}+$/u,
  nonMatchSymbols,
  "\\P{General_Category=Modifier_Letter}"
);
testPropertyEscapes(
  /^\P{General_Category=Lm}+$/u,
  nonMatchSymbols,
  "\\P{General_Category=Lm}"
);
testPropertyEscapes(
  /^\P{gc=Modifier_Letter}+$/u,
  nonMatchSymbols,
  "\\P{gc=Modifier_Letter}"
);
testPropertyEscapes(
  /^\P{gc=Lm}+$/u,
  nonMatchSymbols,
  "\\P{gc=Lm}"
);
testPropertyEscapes(
  /^\P{Modifier_Letter}+$/u,
  nonMatchSymbols,
  "\\P{Modifier_Letter}"
);
testPropertyEscapes(
  /^\P{Lm}+$/u,
  nonMatchSymbols,
  "\\P{Lm}"
);
