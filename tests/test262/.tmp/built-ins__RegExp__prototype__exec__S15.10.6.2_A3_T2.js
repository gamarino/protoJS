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
    RegExp.prototype.exec behavior depends on global property.
    If global is true and lastIndex not changed manually,
    next exec calling start to match from position where current match finished
es5id: 15.10.6.2_A3_T2
description: RegExp is /[Nn]evermore/g and tested string is very long string
---*/

var __the__raven = "	Once upon a midnight dreary, while I pondered weak and weary," +
        "Over many a quaint and curious volume of forgotten lore," +
        "While I nodded, nearly napping, suddenly there came a tapping," +
        "As of some one gently rapping, rapping at my chamber door." +
        "`'Tis some visitor,' I muttered, `tapping at my chamber door -" +
        "Only this, and nothing more.'" +
        "Ah, distinctly I remember it was in the bleak December," +
        "And each separate dying ember wrought its ghost upon the floor." +
        "Eagerly I wished the morrow; - vainly I had sought to borrow" +
        "From my books surcease of sorrow - sorrow for the lost Lenore -" +
        "For the rare and radiant maiden whom the angels named Lenore -" +
        "Nameless here for evermore." +
        "\n" +
        "And the silken sad uncertain rustling of each purple curtain" +
        "Thrilled me - filled me with fantastic terrors never felt before;" +
        "So that now, to still the beating of my heart, I stood repeating" +
        "`'Tis some visitor entreating entrance at my chamber door -" +
        "Some late visitor entreating entrance at my chamber door; -" +
        "This it is, and nothing more,'" +
        "Presently my soul grew stronger; hesitating then no longer," +
        "`Sir,' said I, `or Madam, truly your forgiveness I implore;" +
        "But the fact is I was napping, and so gently you came rapping," +
        "And so faintly you came tapping, tapping at my chamber door," +
        "That I scarce was sure I heard you' - here I opened wide the door; -" +
        "Darkness there, and nothing more." +
        "\n" +
        "Deep into that darkness peering, long I stood there wondering, fearing," +
        "Doubting, dreaming dreams no mortal ever dared to dream before" +
        "But the silence was unbroken, and the darkness gave no token," +
        "And the only word there spoken was the whispered word, `Lenore!'" +
        "This I whispered, and an echo murmured back the word, `Lenore!'" +
        "Merely this and nothing more." +
        "\n" +
        "Back into the chamber turning, all my soul within me burning," +
        "Soon again I heard a tapping somewhat louder than before." +
        "`Surely,' said I, `surely that is something at my window lattice;" +
        "Let me see then, what thereat is, and this mystery explore -" +
        "Let my heart be still a moment and this mystery explore; -" +
        "'Tis the wind and nothing more!'" +
        "\n" +
        "Open here I flung the shutter, when, with many a flirt and flutter," +
        "In there stepped a stately raven of the saintly days of yore." +
        "Not the least obeisance made he; not a minute stopped or stayed he;" +
        "But, with mien of lord or lady, perched above my chamber door -" +
        "Perched upon a bust of Pallas just above my chamber door -" +
        "Perched, and sat, and nothing more." +
        "\n" +
        "Then this ebony bird beguiling my sad fancy into smiling," +
        "By the grave and stern decorum of the countenance it wore," +
        "`Though thy crest be shorn and shaven, thou,' I said, `art sure no craven." +
        "Ghastly grim and ancient raven wandering from the nightly shore -" +
        "Tell me what thy lordly name is on the Night's Plutonian shore!'" +
        "Quoth the raven, `Nevermore.'" +
        "-n" +
        "Much I marvelled this ungainly fowl to hear discourse so plainly," +
        "Though its answer little meaning - little relevancy bore;" +
        "For we cannot help agreeing that no living human being" +
        "Ever yet was blessed with seeing bird above his chamber door -" +
        "Bird or beast above the sculptured bust above his chamber door," +
        "With such name as `Nevermore.'" +
        "\n" +
        "But the raven, sitting lonely on the placid bust, spoke only," +
        "That one word, as if his soul in that one word he did outpour." +
        "Nothing further then he uttered - not a feather then he fluttered -" +
        "Till I scarcely more than muttered `Other friends have flown before -" +
        "On the morrow he will leave me, as my hopes have flown before.'" +
        "Then the bird said, `Nevermore.'" +
        "\n" +
        "Startled at the stillness broken by reply so aptly spoken," +
        "`Doubtless,' said I, `what it utters is its only stock and store," +
        "Caught from some unhappy master whom unmerciful disaster" +
        "Followed fast and followed faster till his songs one burden bore -" +
        "Till the dirges of his hope that melancholy burden bore" +
        "Of \"Never-nevermore.\"'" +
        "\n" +
        "But the raven still beguiling all my sad soul into smiling," +
        "Straight I wheeled a cushioned seat in front of bird and bust and door;" +
        "Then, upon the velvet sinking, I betook myself to linking" +
        "Fancy unto fancy, thinking what this ominous bird of yore -" +
        "What this grim, ungainly, ghastly, gaunt, and ominous bird of yore" +
        "Meant in croaking `Nevermore.'" +
        "\n" +
        "This I sat engaged in guessing, but no syllable expressing" +
        "To the fowl whose fiery eyes now burned into my bosom's core;" +
        "This and more I sat divining, with my head at ease reclining" +
        "On the cushion's velvet lining that the lamp-light gloated o'er," +
        "But whose velvet violet lining with the lamp-light gloating o'er," +
        "She shall press, ah, nevermore!" +
        "\n" +
        "Then, methought, the air grew denser, perfumed from an unseen censer" +
        "Swung by Seraphim whose foot-falls tinkled on the tufted floor." +
        "`Wretch,' I cried, `thy God hath lent thee - by these angels he has sent thee" +
        "Respite - respite and nepenthe from thy memories of Lenore!" +
        "Quaff, oh quaff this kind nepenthe, and forget this lost Lenore!'" +
        "Quoth the raven, `Nevermore.'" +
        "\n" +
        "`Prophet!' said I, `thing of evil! - prophet still, if bird or devil! -" +
        "Whether tempter sent, or whether tempest tossed thee here ashore," +
        "Desolate yet all undaunted, on this desert land enchanted -" +
        "On this home by horror haunted - tell me truly, I implore -" +
        "Is there - is there balm in Gilead? - tell me - tell me, I implore!'" +
        "Quoth the raven, `Nevermore.'" +
        "\n" +
        "`Prophet!' said I, `thing of evil! - prophet still, if bird or devil!" +
        "By that Heaven that bends above us - by that God we both adore -" +
        "Tell this soul with sorrow laden if, within the distant Aidenn," +
        "It shall clasp a sainted maiden whom the angels named Lenore -" +
        "Clasp a rare and radiant maiden, whom the angels named Lenore?'" +
        "Quoth the raven, `Nevermore.'" +
        "\n" +
        "`Be that word our sign of parting, bird or fiend!' I shrieked upstarting -" +
        "`Get thee back into the tempest and the Night's Plutonian shore!" +
        "Leave no black plume as a token of that lie thy soul hath spoken!" +
        "Leave my loneliness unbroken! - quit the bust above my door!" +
        "Take thy beak from out my heart, and take thy form from off my door!'" +
        "Quoth the raven, `Nevermore.'" +
        "\n" +
        "And the raven, never flitting, still is sitting, still is sitting" +
        "On the pallid bust of Pallas just above my chamber door;" +
        "And his eyes have all the seeming of a demon's that is dreaming," +
        "And the lamp-light o'er him streaming throws his shadow on the floor;" +
        "And my soul from out that shadow that lies floating on the floor" +
        "Shall be lifted - nevermore!"

var NEVERMORE=11;

var __re = /[Nn]evermore/g;
var __matched=0;

do{
    var __executed = __re.exec(__the__raven);
    if (__executed !== null) {
        __matched++;
    } else {
        break;
    }
}while(1);

assert.sameValue(__matched, NEVERMORE, 'The value of __matched is expected to equal the value of NEVERMORE');
