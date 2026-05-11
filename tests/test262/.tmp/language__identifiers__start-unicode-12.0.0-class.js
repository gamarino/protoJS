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
esid: prod-PrivateIdentifier
description: |
  Test that Unicode v12.0.0 ID_Start characters are accepted as
  identifier start characters in private class fields.
info: |
  Generated by https://github.com/mathiasbynens/caniunicode
features: [class, class-fields-private]
---*/

class _ {
  #ຆ;
  #ຉ;
  #ຌ;
  #ຎ;
  #ຏ;
  #ຐ;
  #ຑ;
  #ຒ;
  #ຓ;
  #ຘ;
  #ຠ;
  #ຨ;
  #ຩ;
  #ຬ;
  #ᳲ;
  #ᳳ;
  #ᳺ;
  #Ꞻ;
  #ꞻ;
  #Ꞽ;
  #ꞽ;
  #Ꞿ;
  #ꞿ;
  #Ꟃ;
  #ꟃ;
  #Ꞔ;
  #Ʂ;
  #Ᶎ;
  #ꭦ;
  #ꭧ;
  #𐿠;
  #𐿡;
  #𐿢;
  #𐿣;
  #𐿤;
  #𐿥;
  #𐿦;
  #𐿧;
  #𐿨;
  #𐿩;
  #𐿪;
  #𐿫;
  #𐿬;
  #𐿭;
  #𐿮;
  #𐿯;
  #𐿰;
  #𐿱;
  #𐿲;
  #𐿳;
  #𐿴;
  #𐿵;
  #𐿶;
  #𑑟;
  #𑚸;
  #𑦠;
  #𑦡;
  #𑦢;
  #𑦣;
  #𑦤;
  #𑦥;
  #𑦦;
  #𑦧;
  #𑦪;
  #𑦫;
  #𑦬;
  #𑦭;
  #𑦮;
  #𑦯;
  #𑦰;
  #𑦱;
  #𑦲;
  #𑦳;
  #𑦴;
  #𑦵;
  #𑦶;
  #𑦷;
  #𑦸;
  #𑦹;
  #𑦺;
  #𑦻;
  #𑦼;
  #𑦽;
  #𑦾;
  #𑦿;
  #𑧀;
  #𑧁;
  #𑧂;
  #𑧃;
  #𑧄;
  #𑧅;
  #𑧆;
  #𑧇;
  #𑧈;
  #𑧉;
  #𑧊;
  #𑧋;
  #𑧌;
  #𑧍;
  #𑧎;
  #𑧏;
  #𑧐;
  #𑧡;
  #𑧣;
  #𑪄;
  #𑪅;
  #𖽅;
  #𖽆;
  #𖽇;
  #𖽈;
  #𖽉;
  #𖽊;
  #𖿣;
  #𘟲;
  #𘟳;
  #𘟴;
  #𘟵;
  #𘟶;
  #𘟷;
  #𛅐;
  #𛅑;
  #𛅒;
  #𛅤;
  #𛅥;
  #𛅦;
  #𛅧;
  #𞄀;
  #𞄁;
  #𞄂;
  #𞄃;
  #𞄄;
  #𞄅;
  #𞄆;
  #𞄇;
  #𞄈;
  #𞄉;
  #𞄊;
  #𞄋;
  #𞄌;
  #𞄍;
  #𞄎;
  #𞄏;
  #𞄐;
  #𞄑;
  #𞄒;
  #𞄓;
  #𞄔;
  #𞄕;
  #𞄖;
  #𞄗;
  #𞄘;
  #𞄙;
  #𞄚;
  #𞄛;
  #𞄜;
  #𞄝;
  #𞄞;
  #𞄟;
  #𞄠;
  #𞄡;
  #𞄢;
  #𞄣;
  #𞄤;
  #𞄥;
  #𞄦;
  #𞄧;
  #𞄨;
  #𞄩;
  #𞄪;
  #𞄫;
  #𞄬;
  #𞄷;
  #𞄸;
  #𞄹;
  #𞄺;
  #𞄻;
  #𞄼;
  #𞄽;
  #𞅎;
  #𞋀;
  #𞋁;
  #𞋂;
  #𞋃;
  #𞋄;
  #𞋅;
  #𞋆;
  #𞋇;
  #𞋈;
  #𞋉;
  #𞋊;
  #𞋋;
  #𞋌;
  #𞋍;
  #𞋎;
  #𞋏;
  #𞋐;
  #𞋑;
  #𞋒;
  #𞋓;
  #𞋔;
  #𞋕;
  #𞋖;
  #𞋗;
  #𞋘;
  #𞋙;
  #𞋚;
  #𞋛;
  #𞋜;
  #𞋝;
  #𞋞;
  #𞋟;
  #𞋠;
  #𞋡;
  #𞋢;
  #𞋣;
  #𞋤;
  #𞋥;
  #𞋦;
  #𞋧;
  #𞋨;
  #𞋩;
  #𞋪;
  #𞋫;
  #𞥋;
};
