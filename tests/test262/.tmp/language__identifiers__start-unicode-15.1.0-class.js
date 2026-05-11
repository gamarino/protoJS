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
  Test that Unicode v15.1.0 ID_Start characters are accepted as
  identifier start characters in private class fields.
info: |
  Generated by https://github.com/mathiasbynens/caniunicode
features: [class, class-fields-private]
---*/

class _ {
  #𮯰;
  #𮯱;
  #𮯲;
  #𮯳;
  #𮯴;
  #𮯵;
  #𮯶;
  #𮯷;
  #𮯸;
  #𮯹;
  #𮯺;
  #𮯻;
  #𮯼;
  #𮯽;
  #𮯾;
  #𮯿;
  #𮰀;
  #𮰁;
  #𮰂;
  #𮰃;
  #𮰄;
  #𮰅;
  #𮰆;
  #𮰇;
  #𮰈;
  #𮰉;
  #𮰊;
  #𮰋;
  #𮰌;
  #𮰍;
  #𮰎;
  #𮰏;
  #𮰐;
  #𮰑;
  #𮰒;
  #𮰓;
  #𮰔;
  #𮰕;
  #𮰖;
  #𮰗;
  #𮰘;
  #𮰙;
  #𮰚;
  #𮰛;
  #𮰜;
  #𮰝;
  #𮰞;
  #𮰟;
  #𮰠;
  #𮰡;
  #𮰢;
  #𮰣;
  #𮰤;
  #𮰥;
  #𮰦;
  #𮰧;
  #𮰨;
  #𮰩;
  #𮰪;
  #𮰫;
  #𮰬;
  #𮰭;
  #𮰮;
  #𮰯;
  #𮰰;
  #𮰱;
  #𮰲;
  #𮰳;
  #𮰴;
  #𮰵;
  #𮰶;
  #𮰷;
  #𮰸;
  #𮰹;
  #𮰺;
  #𮰻;
  #𮰼;
  #𮰽;
  #𮰾;
  #𮰿;
  #𮱀;
  #𮱁;
  #𮱂;
  #𮱃;
  #𮱄;
  #𮱅;
  #𮱆;
  #𮱇;
  #𮱈;
  #𮱉;
  #𮱊;
  #𮱋;
  #𮱌;
  #𮱍;
  #𮱎;
  #𮱏;
  #𮱐;
  #𮱑;
  #𮱒;
  #𮱓;
  #𮱔;
  #𮱕;
  #𮱖;
  #𮱗;
  #𮱘;
  #𮱙;
  #𮱚;
  #𮱛;
  #𮱜;
  #𮱝;
  #𮱞;
  #𮱟;
  #𮱠;
  #𮱡;
  #𮱢;
  #𮱣;
  #𮱤;
  #𮱥;
  #𮱦;
  #𮱧;
  #𮱨;
  #𮱩;
  #𮱪;
  #𮱫;
  #𮱬;
  #𮱭;
  #𮱮;
  #𮱯;
  #𮱰;
  #𮱱;
  #𮱲;
  #𮱳;
  #𮱴;
  #𮱵;
  #𮱶;
  #𮱷;
  #𮱸;
  #𮱹;
  #𮱺;
  #𮱻;
  #𮱼;
  #𮱽;
  #𮱾;
  #𮱿;
  #𮲀;
  #𮲁;
  #𮲂;
  #𮲃;
  #𮲄;
  #𮲅;
  #𮲆;
  #𮲇;
  #𮲈;
  #𮲉;
  #𮲊;
  #𮲋;
  #𮲌;
  #𮲍;
  #𮲎;
  #𮲏;
  #𮲐;
  #𮲑;
  #𮲒;
  #𮲓;
  #𮲔;
  #𮲕;
  #𮲖;
  #𮲗;
  #𮲘;
  #𮲙;
  #𮲚;
  #𮲛;
  #𮲜;
  #𮲝;
  #𮲞;
  #𮲟;
  #𮲠;
  #𮲡;
  #𮲢;
  #𮲣;
  #𮲤;
  #𮲥;
  #𮲦;
  #𮲧;
  #𮲨;
  #𮲩;
  #𮲪;
  #𮲫;
  #𮲬;
  #𮲭;
  #𮲮;
  #𮲯;
  #𮲰;
  #𮲱;
  #𮲲;
  #𮲳;
  #𮲴;
  #𮲵;
  #𮲶;
  #𮲷;
  #𮲸;
  #𮲹;
  #𮲺;
  #𮲻;
  #𮲼;
  #𮲽;
  #𮲾;
  #𮲿;
  #𮳀;
  #𮳁;
  #𮳂;
  #𮳃;
  #𮳄;
  #𮳅;
  #𮳆;
  #𮳇;
  #𮳈;
  #𮳉;
  #𮳊;
  #𮳋;
  #𮳌;
  #𮳍;
  #𮳎;
  #𮳏;
  #𮳐;
  #𮳑;
  #𮳒;
  #𮳓;
  #𮳔;
  #𮳕;
  #𮳖;
  #𮳗;
  #𮳘;
  #𮳙;
  #𮳚;
  #𮳛;
  #𮳜;
  #𮳝;
  #𮳞;
  #𮳟;
  #𮳠;
  #𮳡;
  #𮳢;
  #𮳣;
  #𮳤;
  #𮳥;
  #𮳦;
  #𮳧;
  #𮳨;
  #𮳩;
  #𮳪;
  #𮳫;
  #𮳬;
  #𮳭;
  #𮳮;
  #𮳯;
  #𮳰;
  #𮳱;
  #𮳲;
  #𮳳;
  #𮳴;
  #𮳵;
  #𮳶;
  #𮳷;
  #𮳸;
  #𮳹;
  #𮳺;
  #𮳻;
  #𮳼;
  #𮳽;
  #𮳾;
  #𮳿;
  #𮴀;
  #𮴁;
  #𮴂;
  #𮴃;
  #𮴄;
  #𮴅;
  #𮴆;
  #𮴇;
  #𮴈;
  #𮴉;
  #𮴊;
  #𮴋;
  #𮴌;
  #𮴍;
  #𮴎;
  #𮴏;
  #𮴐;
  #𮴑;
  #𮴒;
  #𮴓;
  #𮴔;
  #𮴕;
  #𮴖;
  #𮴗;
  #𮴘;
  #𮴙;
  #𮴚;
  #𮴛;
  #𮴜;
  #𮴝;
  #𮴞;
  #𮴟;
  #𮴠;
  #𮴡;
  #𮴢;
  #𮴣;
  #𮴤;
  #𮴥;
  #𮴦;
  #𮴧;
  #𮴨;
  #𮴩;
  #𮴪;
  #𮴫;
  #𮴬;
  #𮴭;
  #𮴮;
  #𮴯;
  #𮴰;
  #𮴱;
  #𮴲;
  #𮴳;
  #𮴴;
  #𮴵;
  #𮴶;
  #𮴷;
  #𮴸;
  #𮴹;
  #𮴺;
  #𮴻;
  #𮴼;
  #𮴽;
  #𮴾;
  #𮴿;
  #𮵀;
  #𮵁;
  #𮵂;
  #𮵃;
  #𮵄;
  #𮵅;
  #𮵆;
  #𮵇;
  #𮵈;
  #𮵉;
  #𮵊;
  #𮵋;
  #𮵌;
  #𮵍;
  #𮵎;
  #𮵏;
  #𮵐;
  #𮵑;
  #𮵒;
  #𮵓;
  #𮵔;
  #𮵕;
  #𮵖;
  #𮵗;
  #𮵘;
  #𮵙;
  #𮵚;
  #𮵛;
  #𮵜;
  #𮵝;
  #𮵞;
  #𮵟;
  #𮵠;
  #𮵡;
  #𮵢;
  #𮵣;
  #𮵤;
  #𮵥;
  #𮵦;
  #𮵧;
  #𮵨;
  #𮵩;
  #𮵪;
  #𮵫;
  #𮵬;
  #𮵭;
  #𮵮;
  #𮵯;
  #𮵰;
  #𮵱;
  #𮵲;
  #𮵳;
  #𮵴;
  #𮵵;
  #𮵶;
  #𮵷;
  #𮵸;
  #𮵹;
  #𮵺;
  #𮵻;
  #𮵼;
  #𮵽;
  #𮵾;
  #𮵿;
  #𮶀;
  #𮶁;
  #𮶂;
  #𮶃;
  #𮶄;
  #𮶅;
  #𮶆;
  #𮶇;
  #𮶈;
  #𮶉;
  #𮶊;
  #𮶋;
  #𮶌;
  #𮶍;
  #𮶎;
  #𮶏;
  #𮶐;
  #𮶑;
  #𮶒;
  #𮶓;
  #𮶔;
  #𮶕;
  #𮶖;
  #𮶗;
  #𮶘;
  #𮶙;
  #𮶚;
  #𮶛;
  #𮶜;
  #𮶝;
  #𮶞;
  #𮶟;
  #𮶠;
  #𮶡;
  #𮶢;
  #𮶣;
  #𮶤;
  #𮶥;
  #𮶦;
  #𮶧;
  #𮶨;
  #𮶩;
  #𮶪;
  #𮶫;
  #𮶬;
  #𮶭;
  #𮶮;
  #𮶯;
  #𮶰;
  #𮶱;
  #𮶲;
  #𮶳;
  #𮶴;
  #𮶵;
  #𮶶;
  #𮶷;
  #𮶸;
  #𮶹;
  #𮶺;
  #𮶻;
  #𮶼;
  #𮶽;
  #𮶾;
  #𮶿;
  #𮷀;
  #𮷁;
  #𮷂;
  #𮷃;
  #𮷄;
  #𮷅;
  #𮷆;
  #𮷇;
  #𮷈;
  #𮷉;
  #𮷊;
  #𮷋;
  #𮷌;
  #𮷍;
  #𮷎;
  #𮷏;
  #𮷐;
  #𮷑;
  #𮷒;
  #𮷓;
  #𮷔;
  #𮷕;
  #𮷖;
  #𮷗;
  #𮷘;
  #𮷙;
  #𮷚;
  #𮷛;
  #𮷜;
  #𮷝;
  #𮷞;
  #𮷟;
  #𮷠;
  #𮷡;
  #𮷢;
  #𮷣;
  #𮷤;
  #𮷥;
  #𮷦;
  #𮷧;
  #𮷨;
  #𮷩;
  #𮷪;
  #𮷫;
  #𮷬;
  #𮷭;
  #𮷮;
  #𮷯;
  #𮷰;
  #𮷱;
  #𮷲;
  #𮷳;
  #𮷴;
  #𮷵;
  #𮷶;
  #𮷷;
  #𮷸;
  #𮷹;
  #𮷺;
  #𮷻;
  #𮷼;
  #𮷽;
  #𮷾;
  #𮷿;
  #𮸀;
  #𮸁;
  #𮸂;
  #𮸃;
  #𮸄;
  #𮸅;
  #𮸆;
  #𮸇;
  #𮸈;
  #𮸉;
  #𮸊;
  #𮸋;
  #𮸌;
  #𮸍;
  #𮸎;
  #𮸏;
  #𮸐;
  #𮸑;
  #𮸒;
  #𮸓;
  #𮸔;
  #𮸕;
  #𮸖;
  #𮸗;
  #𮸘;
  #𮸙;
  #𮸚;
  #𮸛;
  #𮸜;
  #𮸝;
  #𮸞;
  #𮸟;
  #𮸠;
  #𮸡;
  #𮸢;
  #𮸣;
  #𮸤;
  #𮸥;
  #𮸦;
  #𮸧;
  #𮸨;
  #𮸩;
  #𮸪;
  #𮸫;
  #𮸬;
  #𮸭;
  #𮸮;
  #𮸯;
  #𮸰;
  #𮸱;
  #𮸲;
  #𮸳;
  #𮸴;
  #𮸵;
  #𮸶;
  #𮸷;
  #𮸸;
  #𮸹;
  #𮸺;
  #𮸻;
  #𮸼;
  #𮸽;
  #𮸾;
  #𮸿;
  #𮹀;
  #𮹁;
  #𮹂;
  #𮹃;
  #𮹄;
  #𮹅;
  #𮹆;
  #𮹇;
  #𮹈;
  #𮹉;
  #𮹊;
  #𮹋;
  #𮹌;
  #𮹍;
  #𮹎;
  #𮹏;
  #𮹐;
  #𮹑;
  #𮹒;
  #𮹓;
  #𮹔;
  #𮹕;
  #𮹖;
  #𮹗;
  #𮹘;
  #𮹙;
  #𮹚;
  #𮹛;
  #𮹜;
  #𮹝;
};
