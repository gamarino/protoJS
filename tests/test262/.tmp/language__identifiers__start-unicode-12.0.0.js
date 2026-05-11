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


// Copyright 2024 Mathias Bynens. All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
author: Mathias Bynens
esid: sec-names-and-keywords
description: |
  Test that Unicode v12.0.0 ID_Start characters are accepted as
  identifier start characters.
info: |
  Generated by https://github.com/mathiasbynens/caniunicode
---*/

var ຆ;
var ຉ;
var ຌ;
var ຎ;
var ຏ;
var ຐ;
var ຑ;
var ຒ;
var ຓ;
var ຘ;
var ຠ;
var ຨ;
var ຩ;
var ຬ;
var ᳲ;
var ᳳ;
var ᳺ;
var Ꞻ;
var ꞻ;
var Ꞽ;
var ꞽ;
var Ꞿ;
var ꞿ;
var Ꟃ;
var ꟃ;
var Ꞔ;
var Ʂ;
var Ᶎ;
var ꭦ;
var ꭧ;
var 𐿠;
var 𐿡;
var 𐿢;
var 𐿣;
var 𐿤;
var 𐿥;
var 𐿦;
var 𐿧;
var 𐿨;
var 𐿩;
var 𐿪;
var 𐿫;
var 𐿬;
var 𐿭;
var 𐿮;
var 𐿯;
var 𐿰;
var 𐿱;
var 𐿲;
var 𐿳;
var 𐿴;
var 𐿵;
var 𐿶;
var 𑑟;
var 𑚸;
var 𑦠;
var 𑦡;
var 𑦢;
var 𑦣;
var 𑦤;
var 𑦥;
var 𑦦;
var 𑦧;
var 𑦪;
var 𑦫;
var 𑦬;
var 𑦭;
var 𑦮;
var 𑦯;
var 𑦰;
var 𑦱;
var 𑦲;
var 𑦳;
var 𑦴;
var 𑦵;
var 𑦶;
var 𑦷;
var 𑦸;
var 𑦹;
var 𑦺;
var 𑦻;
var 𑦼;
var 𑦽;
var 𑦾;
var 𑦿;
var 𑧀;
var 𑧁;
var 𑧂;
var 𑧃;
var 𑧄;
var 𑧅;
var 𑧆;
var 𑧇;
var 𑧈;
var 𑧉;
var 𑧊;
var 𑧋;
var 𑧌;
var 𑧍;
var 𑧎;
var 𑧏;
var 𑧐;
var 𑧡;
var 𑧣;
var 𑪄;
var 𑪅;
var 𖽅;
var 𖽆;
var 𖽇;
var 𖽈;
var 𖽉;
var 𖽊;
var 𖿣;
var 𘟲;
var 𘟳;
var 𘟴;
var 𘟵;
var 𘟶;
var 𘟷;
var 𛅐;
var 𛅑;
var 𛅒;
var 𛅤;
var 𛅥;
var 𛅦;
var 𛅧;
var 𞄀;
var 𞄁;
var 𞄂;
var 𞄃;
var 𞄄;
var 𞄅;
var 𞄆;
var 𞄇;
var 𞄈;
var 𞄉;
var 𞄊;
var 𞄋;
var 𞄌;
var 𞄍;
var 𞄎;
var 𞄏;
var 𞄐;
var 𞄑;
var 𞄒;
var 𞄓;
var 𞄔;
var 𞄕;
var 𞄖;
var 𞄗;
var 𞄘;
var 𞄙;
var 𞄚;
var 𞄛;
var 𞄜;
var 𞄝;
var 𞄞;
var 𞄟;
var 𞄠;
var 𞄡;
var 𞄢;
var 𞄣;
var 𞄤;
var 𞄥;
var 𞄦;
var 𞄧;
var 𞄨;
var 𞄩;
var 𞄪;
var 𞄫;
var 𞄬;
var 𞄷;
var 𞄸;
var 𞄹;
var 𞄺;
var 𞄻;
var 𞄼;
var 𞄽;
var 𞅎;
var 𞋀;
var 𞋁;
var 𞋂;
var 𞋃;
var 𞋄;
var 𞋅;
var 𞋆;
var 𞋇;
var 𞋈;
var 𞋉;
var 𞋊;
var 𞋋;
var 𞋌;
var 𞋍;
var 𞋎;
var 𞋏;
var 𞋐;
var 𞋑;
var 𞋒;
var 𞋓;
var 𞋔;
var 𞋕;
var 𞋖;
var 𞋗;
var 𞋘;
var 𞋙;
var 𞋚;
var 𞋛;
var 𞋜;
var 𞋝;
var 𞋞;
var 𞋟;
var 𞋠;
var 𞋡;
var 𞋢;
var 𞋣;
var 𞋤;
var 𞋥;
var 𞋦;
var 𞋧;
var 𞋨;
var 𞋩;
var 𞋪;
var 𞋫;
var 𞥋;
