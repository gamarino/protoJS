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
  Test that Unicode v14.0.0 ID_Start characters are accepted as
  identifier start characters in private class fields.
info: |
  Generated by https://github.com/mathiasbynens/caniunicode
features: [class, class-fields-private]
---*/

class _ {
  #ࡰ;
  #ࡱ;
  #ࡲ;
  #ࡳ;
  #ࡴ;
  #ࡵ;
  #ࡶ;
  #ࡷ;
  #ࡸ;
  #ࡹ;
  #ࡺ;
  #ࡻ;
  #ࡼ;
  #ࡽ;
  #ࡾ;
  #ࡿ;
  #ࢀ;
  #ࢁ;
  #ࢂ;
  #ࢃ;
  #ࢄ;
  #ࢅ;
  #ࢆ;
  #ࢇ;
  #ࢉ;
  #ࢊ;
  #ࢋ;
  #ࢌ;
  #ࢍ;
  #ࢎ;
  #ࢵ;
  #ࣈ;
  #ࣉ;
  #ౝ;
  #ೝ;
  #ᜍ;
  #ᜟ;
  #ᭌ;
  #Ⱟ;
  #ⱟ;
  #鿽;
  #鿾;
  #鿿;
  #Ꟁ;
  #ꟁ;
  #Ꟑ;
  #ꟑ;
  #ꟓ;
  #ꟕ;
  #Ꟗ;
  #ꟗ;
  #Ꟙ;
  #ꟙ;
  #ꟲ;
  #ꟳ;
  #ꟴ;
  #𐕰;
  #𐕱;
  #𐕲;
  #𐕳;
  #𐕴;
  #𐕵;
  #𐕶;
  #𐕷;
  #𐕸;
  #𐕹;
  #𐕺;
  #𐕼;
  #𐕽;
  #𐕾;
  #𐕿;
  #𐖀;
  #𐖁;
  #𐖂;
  #𐖃;
  #𐖄;
  #𐖅;
  #𐖆;
  #𐖇;
  #𐖈;
  #𐖉;
  #𐖊;
  #𐖌;
  #𐖍;
  #𐖎;
  #𐖏;
  #𐖐;
  #𐖑;
  #𐖒;
  #𐖔;
  #𐖕;
  #𐖗;
  #𐖘;
  #𐖙;
  #𐖚;
  #𐖛;
  #𐖜;
  #𐖝;
  #𐖞;
  #𐖟;
  #𐖠;
  #𐖡;
  #𐖣;
  #𐖤;
  #𐖥;
  #𐖦;
  #𐖧;
  #𐖨;
  #𐖩;
  #𐖪;
  #𐖫;
  #𐖬;
  #𐖭;
  #𐖮;
  #𐖯;
  #𐖰;
  #𐖱;
  #𐖳;
  #𐖴;
  #𐖵;
  #𐖶;
  #𐖷;
  #𐖸;
  #𐖹;
  #𐖻;
  #𐖼;
  #𐞀;
  #𐞁;
  #𐞂;
  #𐞃;
  #𐞄;
  #𐞅;
  #𐞇;
  #𐞈;
  #𐞉;
  #𐞊;
  #𐞋;
  #𐞌;
  #𐞍;
  #𐞎;
  #𐞏;
  #𐞐;
  #𐞑;
  #𐞒;
  #𐞓;
  #𐞔;
  #𐞕;
  #𐞖;
  #𐞗;
  #𐞘;
  #𐞙;
  #𐞚;
  #𐞛;
  #𐞜;
  #𐞝;
  #𐞞;
  #𐞟;
  #𐞠;
  #𐞡;
  #𐞢;
  #𐞣;
  #𐞤;
  #𐞥;
  #𐞦;
  #𐞧;
  #𐞨;
  #𐞩;
  #𐞪;
  #𐞫;
  #𐞬;
  #𐞭;
  #𐞮;
  #𐞯;
  #𐞰;
  #𐞲;
  #𐞳;
  #𐞴;
  #𐞵;
  #𐞶;
  #𐞷;
  #𐞸;
  #𐞹;
  #𐞺;
  #𐽰;
  #𐽱;
  #𐽲;
  #𐽳;
  #𐽴;
  #𐽵;
  #𐽶;
  #𐽷;
  #𐽸;
  #𐽹;
  #𐽺;
  #𐽻;
  #𐽼;
  #𐽽;
  #𐽾;
  #𐽿;
  #𐾀;
  #𐾁;
  #𑁱;
  #𑁲;
  #𑁵;
  #𑝀;
  #𑝁;
  #𑝂;
  #𑝃;
  #𑝄;
  #𑝅;
  #𑝆;
  #𑪰;
  #𑪱;
  #𑪲;
  #𑪳;
  #𑪴;
  #𑪵;
  #𑪶;
  #𑪷;
  #𑪸;
  #𑪹;
  #𑪺;
  #𑪻;
  #𑪼;
  #𑪽;
  #𑪾;
  #𑪿;
  #𒾐;
  #𒾑;
  #𒾒;
  #𒾓;
  #𒾔;
  #𒾕;
  #𒾖;
  #𒾗;
  #𒾘;
  #𒾙;
  #𒾚;
  #𒾛;
  #𒾜;
  #𒾝;
  #𒾞;
  #𒾟;
  #𒾠;
  #𒾡;
  #𒾢;
  #𒾣;
  #𒾤;
  #𒾥;
  #𒾦;
  #𒾧;
  #𒾨;
  #𒾩;
  #𒾪;
  #𒾫;
  #𒾬;
  #𒾭;
  #𒾮;
  #𒾯;
  #𒾰;
  #𒾱;
  #𒾲;
  #𒾳;
  #𒾴;
  #𒾵;
  #𒾶;
  #𒾷;
  #𒾸;
  #𒾹;
  #𒾺;
  #𒾻;
  #𒾼;
  #𒾽;
  #𒾾;
  #𒾿;
  #𒿀;
  #𒿁;
  #𒿂;
  #𒿃;
  #𒿄;
  #𒿅;
  #𒿆;
  #𒿇;
  #𒿈;
  #𒿉;
  #𒿊;
  #𒿋;
  #𒿌;
  #𒿍;
  #𒿎;
  #𒿏;
  #𒿐;
  #𒿑;
  #𒿒;
  #𒿓;
  #𒿔;
  #𒿕;
  #𒿖;
  #𒿗;
  #𒿘;
  #𒿙;
  #𒿚;
  #𒿛;
  #𒿜;
  #𒿝;
  #𒿞;
  #𒿟;
  #𒿠;
  #𒿡;
  #𒿢;
  #𒿣;
  #𒿤;
  #𒿥;
  #𒿦;
  #𒿧;
  #𒿨;
  #𒿩;
  #𒿪;
  #𒿫;
  #𒿬;
  #𒿭;
  #𒿮;
  #𒿯;
  #𒿰;
  #𖩰;
  #𖩱;
  #𖩲;
  #𖩳;
  #𖩴;
  #𖩵;
  #𖩶;
  #𖩷;
  #𖩸;
  #𖩹;
  #𖩺;
  #𖩻;
  #𖩼;
  #𖩽;
  #𖩾;
  #𖩿;
  #𖪀;
  #𖪁;
  #𖪂;
  #𖪃;
  #𖪄;
  #𖪅;
  #𖪆;
  #𖪇;
  #𖪈;
  #𖪉;
  #𖪊;
  #𖪋;
  #𖪌;
  #𖪍;
  #𖪎;
  #𖪏;
  #𖪐;
  #𖪑;
  #𖪒;
  #𖪓;
  #𖪔;
  #𖪕;
  #𖪖;
  #𖪗;
  #𖪘;
  #𖪙;
  #𖪚;
  #𖪛;
  #𖪜;
  #𖪝;
  #𖪞;
  #𖪟;
  #𖪠;
  #𖪡;
  #𖪢;
  #𖪣;
  #𖪤;
  #𖪥;
  #𖪦;
  #𖪧;
  #𖪨;
  #𖪩;
  #𖪪;
  #𖪫;
  #𖪬;
  #𖪭;
  #𖪮;
  #𖪯;
  #𖪰;
  #𖪱;
  #𖪲;
  #𖪳;
  #𖪴;
  #𖪵;
  #𖪶;
  #𖪷;
  #𖪸;
  #𖪹;
  #𖪺;
  #𖪻;
  #𖪼;
  #𖪽;
  #𖪾;
  #𚿰;
  #𚿱;
  #𚿲;
  #𚿳;
  #𚿵;
  #𚿶;
  #𚿷;
  #𚿸;
  #𚿹;
  #𚿺;
  #𚿻;
  #𚿽;
  #𚿾;
  #𛄟;
  #𛄠;
  #𛄡;
  #𛄢;
  #𝼀;
  #𝼁;
  #𝼂;
  #𝼃;
  #𝼄;
  #𝼅;
  #𝼆;
  #𝼇;
  #𝼈;
  #𝼉;
  #𝼊;
  #𝼋;
  #𝼌;
  #𝼍;
  #𝼎;
  #𝼏;
  #𝼐;
  #𝼑;
  #𝼒;
  #𝼓;
  #𝼔;
  #𝼕;
  #𝼖;
  #𝼗;
  #𝼘;
  #𝼙;
  #𝼚;
  #𝼛;
  #𝼜;
  #𝼝;
  #𝼞;
  #𞊐;
  #𞊑;
  #𞊒;
  #𞊓;
  #𞊔;
  #𞊕;
  #𞊖;
  #𞊗;
  #𞊘;
  #𞊙;
  #𞊚;
  #𞊛;
  #𞊜;
  #𞊝;
  #𞊞;
  #𞊟;
  #𞊠;
  #𞊡;
  #𞊢;
  #𞊣;
  #𞊤;
  #𞊥;
  #𞊦;
  #𞊧;
  #𞊨;
  #𞊩;
  #𞊪;
  #𞊫;
  #𞊬;
  #𞊭;
  #𞟠;
  #𞟡;
  #𞟢;
  #𞟣;
  #𞟤;
  #𞟥;
  #𞟦;
  #𞟨;
  #𞟩;
  #𞟪;
  #𞟫;
  #𞟭;
  #𞟮;
  #𞟰;
  #𞟱;
  #𞟲;
  #𞟳;
  #𞟴;
  #𞟵;
  #𞟶;
  #𞟷;
  #𞟸;
  #𞟹;
  #𞟺;
  #𞟻;
  #𞟼;
  #𞟽;
  #𞟾;
  #𪛞;
  #𪛟;
  #𫜵;
  #𫜶;
  #𫜷;
  #𫜸;
};
