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


// Copyright 2009 the Sputnik authors.  All rights reserved.
// This code is governed by the BSD license found in the LICENSE file.

/*---
info: |
    Result of String conversion from Object value is conversion
    from primitive value
es5id: 9.8_A5_T2
description: Some objects convert to String by implicit transformation
---*/

// CHECK#1
if (new Number() + "" !== "0") {
  throw new Test262Error('#1: new Number() + "" === "0". Actual: ' + (new Number() + ""));
}

// CHECK#2
if (new Number(0) + "" !== "0") {
  throw new Test262Error('#2: new Number(0) + "" === "0". Actual: ' + (new Number(0) + ""));
}

// CHECK#3
if (new Number(Number.NaN) + "" !== "NaN") {
  throw new Test262Error('#3: new Number(Number.NaN) + "" === "NaN". Actual: ' + (new Number(Number.NaN) + ""));
}

// CHECK#4
if (new Number(null) + "" !== "0") {
  throw new Test262Error('#4: new Number(null) + "" === "0". Actual: ' + (new Number(null) + "")); 
}

// CHECK#5
if (new Number(void 0) + "" !== "NaN") {
  throw new Test262Error('#5: new Number(void 0) + "" === "NaN. Actual: ' + (new Number(void 0) + ""));
}

// CHECK#6
if (new Number(true) + "" !== "1") {
  throw new Test262Error('#6: new Number(true) + "" === "1". Actual: ' + (new Number(true) + ""));
}

// CHECK#7
if (new Number(false) + "" !== "0") {
  throw new Test262Error('#7: new Number(false) + "" === "0". Actual: ' + (new Number(false) + ""));
}

// CHECK#8
if (new Boolean(true) + "" !== "true") {
  throw new Test262Error('#8: new Boolean(true) + "" === "true". Actual: ' + (new Boolean(true) + ""));
}

// CHECK#9
if (new Boolean(false) + "" !== "false") {
  throw new Test262Error('#9: Number(new Boolean(false)) === "false". Actual: ' + (Number(new Boolean(false))));
}

// CHECK#10
if (new Array(2,4,8,16,32) + "" !== "2,4,8,16,32") {
  throw new Test262Error('#10: new Array(2,4,8,16,32) + "" === "2,4,8,16,32". Actual: ' + (new Array(2,4,8,16,32) + ""));
}

// CHECK#11
var myobj1 = {
                toNumber : function(){return 12345;}, 
                toString : function(){return 67890;},
                valueOf  : function(){return "[object MyObj]";} 
            };

if (myobj1 + "" !== "[object MyObj]"){
  throw new Test262Error('#11: myobj1 + "" calls ToPrimitive with hint Number. Exptected: "[object MyObj]". Actual: ' + (myobj1 + ""));
}

// CHECK#12
var myobj2 = {
                toNumber : function(){return 12345;},
                toString : function(){return 67890}, 
                valueOf  : function(){return {}} 
            };

if (myobj2 + "" !== "67890"){
  throw new Test262Error('#12: myobj2 + "" calls ToPrimitive with hint Number. Exptected: "67890". Actual: ' + (myobj2 + ""));
}

// CHECK#13
var myobj3 = {
                toNumber : function(){return 12345;} 
            };

if (myobj3 + "" !== "[object Object]"){
  throw new Test262Error('#13: myobj3 + "" calls ToPrimitive with hint Number.  Exptected: "[object Object]". Actual: ' + (myobj3 + ""));
}
