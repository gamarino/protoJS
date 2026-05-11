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
    The with statement adds a computed object to the front of the
    scope chain of the current execution context
es5id: 12.10_A1.7_T5
description: >
    Calling a function within "with" statement declared within the
    statement, leading to completion by exception
flags: [noStrict]
---*/

this.p1 = 1;
this.p2 = 2;
this.p3 = 3;
var result = "result";
var myObj = {p1: 'a', 
             p2: 'b', 
             p3: 'c',
             value: 'myObj_value',
             valueOf : function(){return 'obj_valueOf';},
             parseInt : function(){return 'obj_parseInt';},
             NaN : 'obj_NaN',
             Infinity : 'obj_Infinity',
             eval     : function(){return 'obj_eval';},
             parseFloat : function(){return 'obj_parseFloat';},
             isNaN      : function(){return 'obj_isNaN';},
             isFinite   : function(){return 'obj_isFinite';}
}
var del;
var st_p1 = "p1";
var st_p2 = "p2";
var st_p3 = "p3";
var st_parseInt = "parseInt";
var st_NaN = "NaN";
var st_Infinity = "Infinity";
var st_eval = "eval";
var st_parseFloat = "parseFloat";
var st_isNaN = "isNaN";
var st_isFinite = "isFinite";

try {
  with(myObj){
    var f = function(){
      throw value;
      st_p1 = p1;
      st_p2 = p2;
      st_p3 = p3;
      st_parseInt = parseInt;
      st_NaN = NaN;
      st_Infinity = Infinity;
      st_eval = eval;
      st_parseFloat = parseFloat;
      st_isNaN = isNaN;
      st_isFinite = isFinite;
      p1 = 'x1';
      this.p2 = 'x2';
      del = delete p3;
      var p4 = 'x4';
      p5 = 'x5';
      var value = 'value';
    }
    f();
  }
} catch(e){
  result = e;
}

if(!(result === undefined)){
  throw new Test262Error('#0: result === undefined. Actual:  result ==='+ result  );
}

if(!(p1 === 1)){
  throw new Test262Error('#1: p1 === 1. Actual:  p1 ==='+ p1  );
}

if(!(p2 === 2)){
  throw new Test262Error('#2: p2 === 2. Actual:  p2 ==='+ p2  );
}

if(!(p3 === 3)){
  throw new Test262Error('#3: p3 === 3. Actual:  p3 ==='+ p3  );
}

try {
  p4;
  throw new Test262Error('#4: p4 is not defined');
} catch(e) {    
}

try {
  p5;
  throw new Test262Error('#5: p5 is not defined');
} catch(e) {    
}

if(!(myObj.p1 === "a")){
  throw new Test262Error('#6: myObj.p1 === "a". Actual:  myObj.p1 ==='+ myObj.p1  );
}

if(!(myObj.p2 === "b")){
  throw new Test262Error('#7: myObj.p2 === "b". Actual:  myObj.p2 ==='+ myObj.p2  );
}

if(!(myObj.p3 === "c")){
  throw new Test262Error('#8: myObj.p3 === "c". Actual:  myObj.p3 ==='+ myObj.p3  );
}

if(!(myObj.p4 === undefined)){
  throw new Test262Error('#9: myObj.p4 === undefined. Actual:  myObj.p4 ==='+ myObj.p4  );
}

if(!(myObj.p5 === undefined)){
  throw new Test262Error('#10: myObj.p5 === undefined. Actual:  myObj.p5 ==='+ myObj.p5  );
}

if(!(st_parseInt === "parseInt")){
  throw new Test262Error('#11: myObj.parseInt === "parseInt". Actual:  myObj.parseInt ==='+ myObj.parseInt  );
}

if(!(st_NaN === "NaN")){
  throw new Test262Error('#12: st_NaN === "NaN". Actual:  st_NaN ==='+ st_NaN  );
}

if(!(st_Infinity === "Infinity")){
  throw new Test262Error('#13: st_Infinity === "Infinity". Actual:  st_Infinity ==='+ st_Infinity  );
}

if(!(st_eval === "eval")){
  throw new Test262Error('#14: st_eval === "eval". Actual:  st_eval ==='+ st_eval  );
}

if(!(st_parseFloat === "parseFloat")){
  throw new Test262Error('#15: st_parseFloat === "parseFloat". Actual:  st_parseFloat ==='+ st_parseFloat  );
}

if(!(st_isNaN === "isNaN")){
  throw new Test262Error('#16: st_isNaN === "isNaN". Actual:  st_isNaN ==='+ st_isNaN  );
}

if(!(st_isFinite === "isFinite")){
  throw new Test262Error('#17: st_isFinite === "isFinite". Actual:  st_isFinite ==='+ st_isFinite  );
}

try{
  value;
  throw new Test262Error('#18: value is not defined');
}
catch(e){
}

if(!(myObj.value === "myObj_value")){
  throw new Test262Error('#19: myObj.value === "myObj_value". Actual:  myObj.value ==='+ myObj.value  );
}
