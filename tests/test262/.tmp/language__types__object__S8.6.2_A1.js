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
    Native ECMAScript objects have an internal property called [[Prototype]]. The value of this property is
    either null or an object and is used for implementing inheritance
es5id: 8.6.2_A1
description: Check [[Prototype]] property of object
---*/

//////////////////////////////////////////////////////////////////////////////
//CHECK#1
var __obj={};
if (!Object.prototype.isPrototypeOf(__obj)){
  throw new Test262Error('#1: Native ECMAScript objects have an internal property called [[Prototype]]. ');
};
//
//////////////////////////////////////////////////////////////////////////////

//Establish proto (base) object
/*function ProtoObj(){

};*/
var protoObj={};
//Establish foo object
function FooObj(){};

//////////////////////////////////////////////////////////////////////////////
//CHECK#2
// Invoke instance of foo object
var obj__= new FooObj;

if (!Object.prototype.isPrototypeOf(obj__)){
  throw new Test262Error('#2.1: protoObj={}; function FooObj(){}; var obj__= new FooObj; Object.prototype.isPrototypeOf(obj__) === true. Actual: ' + (Object.prototype.isPrototypeOf(obj__)));
};

if (!FooObj.prototype.isPrototypeOf(obj__)){
  throw new Test262Error('#2.2: protoObj={}; function FooObj(){}; var obj__= new FooObj; FooObj.prototype.isPrototypeOf(obj__) === true. Actual: ' + (FooObj.prototype.isPrototypeOf(obj__)));
};

if (protoObj.isPrototypeOf(obj__)){
  throw new Test262Error('#2.3: protoObj={}; function FooObj(){}; var obj__= new FooObj; protoObj.isPrototypeOf(obj__) === false. Actual: ' + (protoObj.isPrototypeOf(obj__)));
};
// Establish inheritance from proto object
FooObj.prototype=protoObj;

if (protoObj.isPrototypeOf(obj__)){
  throw new Test262Error('#2.4: protoObj={}; function FooObj(){}; var obj__= new FooObj; FooObj.prototype=protoObj; protoObj.isPrototypeOf(obj__) === false. Actual: ' + (protoObj.isPrototypeOf(obj__)));
};
//
//////////////////////////////////////////////////////////////////////////////

//////////////////////////////////////////////////////////////////////////////
//CHECK#3

// Invoke instance of foo object
var __foo=new FooObj;

if (!Object.prototype.isPrototypeOf(__foo)){
  throw new Test262Error('#3.1: protoObj={}; function FooObj(){}; var obj__= new FooObj; FooObj.prototype=protoObj; var __foo=new FooObj; Object.prototype.isPrototypeOf(__foo) === true. Actual: ' + (Object.prototype.isPrototypeOf(__foo)));
};

if (!FooObj.prototype.isPrototypeOf(__foo)){
  throw new Test262Error('#3.2: protoObj={}; function FooObj(){}; var obj__= new FooObj; FooObj.prototype=protoObj; var __foo=new FooObj; FooObj.prototype.isPrototypeOf(__foo) === true. Actual: ' + (FooObj.prototype.isPrototypeOf(__foo)));
};

if (!protoObj.isPrototypeOf(__foo)){
  throw new Test262Error('#3.3: protoObj={}; function FooObj(){}; var obj__= new FooObj; FooObj.prototype=protoObj; var __foo=new FooObj; protoObj.isPrototypeOf(__foo) === true. Actual: ' + (protoObj.isPrototypeOf(__foo)));
};
//
//////////////////////////////////////////////////////////////////////////////
