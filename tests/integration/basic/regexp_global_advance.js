// A global or sticky RegExp walks its input through lastIndex, and every
// method built on that walk (String.prototype.replace, replaceAll, match,
// matchAll, split; RegExp.prototype.exec, test, @@replace, @@match) must
// advance it exactly as the specification does:
//
//   * after a non-empty match, lastIndex is the end of the match;
//   * after an empty match, lastIndex is AdvanceStringIndex(S, lastIndex,
//     fullUnicode): one code unit, or a whole surrogate pair under /u or /v;
//   * RegExpBuiltinExec with lastIndex > length fails: it returns null and,
//     for a global or sticky RegExp, sets lastIndex to 0.
//
// @@replace advanced lastIndex by one more after every match that ended where
// the previous one ended, so 'a//b/c'.replace(/\//g, 'Q') gave 'aQ/bQc'; an
// empty match at the end then left lastIndex past the end, and exec read
// beyond the string ('/'.replace(/\//g, 'Q') crashed, 'abc'.replace(/x*/g, '-')
// never ended). @@match did not advance after an empty match at all.
//
// Asserting test: exits 1 on the first failed check.

var failures = [];
function check(name, got, expected) {
    if (got !== expected) failures.push(name + ": got " + JSON.stringify(got) + ", expected " + JSON.stringify(expected));
}

// replace, global, non-empty matches.
check("adjacent matches", 'a//b/c'.replace(/\//g, 'Q'), 'aQQbQc');
check("match at the end", '/'.replace(/\//g, 'Q'), 'Q');
check("every character", 'aaaa'.replace(/a/g, 'b'), 'bbbb');
check("backslashes", 'C:\\a\\\\b'.replace(/\\/g, '/'), 'C:/a//b');
check("function replacer", 'a1b22c333'.replace(/\d+/g, function (m, i) { return '<' + m + '@' + i + '>'; }),
      'a<1@1>b<22@3>c<333@6>');
check("$& and captures", 'x-y'.replace(/(\w)/g, '[$1$&]'), '[xx]-[yy]');

// replace, global, empty matches.
check("empty matches", 'abc'.replace(/x*/g, '-'), '-a-b-c-');
check("empty pattern", 'abc'.replace(/(?:)/g, '-'), '-a-b-c-');
check("empty match at end only", 'ab'.replace(/$/g, '!'), 'ab!');
check("empty /u over a surrogate pair", 'a\u{1F600}b'.replace(/(?:)/gu, '-'), '-a-\u{1F600}-b-');
check("empty without /u splits the pair", 'a\u{1F600}'.replace(/(?:)/g, '-').length, 7);
check("mixed empty and non-empty", 'aXbX'.replace(/X*/g, '-'), '-a-b-');

// replaceAll goes through the same @@replace.
check("replaceAll adjacent", 'a//b'.replaceAll(/\//g, '_'), 'a__b');
check("replaceAll empty", 'ab'.replaceAll(/(?:)/g, '.'), '.a.b.');

// match and matchAll.
check("match count", 'a//b/'.match(/\//g).length, 3);
check("match empty count", 'xyz'.match(/(?:)/g).length, 4);
check("match empty /u count", '\u{1F600}\u{1F600}'.match(/(?:)/gu).length, 3);
check("match no match", 'abc'.match(/x/g), null);
check("matchAll indices", Array.from('a11b2'.matchAll(/\d/g), function (m) { return m.index; }).join(), '1,2,4');
check("matchAll empty /u", Array.from('a\u{1F600}'.matchAll(/(?:)/gu), function (m) { return m.index; }).join(), '0,1,3');

// split.
check("split adjacent", 'a//b'.split(/\//).join('|'), 'a||b');
check("split empty /u", '\u{1F600}x'.split(/(?:)/u).length, 2);
check("split empty", 'abc'.split(/(?:)/).join('|'), 'a|b|c');

// exec / test past the end.
var g = /a/g;
g.lastIndex = 5;
check("exec past end returns null", g.exec('aa'), null);
check("exec past end resets lastIndex", g.lastIndex, 0);
var y = /a/y;
y.lastIndex = 3;
check("sticky exec past end returns null", y.exec('aa'), null);
check("sticky exec past end resets lastIndex", y.lastIndex, 0);
g.lastIndex = 2;
check("exec at end returns null", g.exec('aa'), null);
var e = /(?:)/g;
e.lastIndex = 2;
check("empty exec at end matches", e.exec('aa') !== null, true);
check("empty exec at end leaves lastIndex", e.lastIndex, 2);
e.lastIndex = 3;
check("empty exec past end returns null", e.exec('aa'), null);
var t = /b/g;
t.lastIndex = 100;
check("test past end", t.test('abc'), false);
check("test past end resets lastIndex", t.lastIndex, 0);

// A loop driven by exec terminates and finds every match.
var re = /o/g, count = 0, m;
while ((m = re.exec('foo boo')) !== null) count++;
check("exec loop", count, 4);

if (failures.length) {
    for (var i = 0; i < failures.length; i++) console.log("FAIL: " + failures[i]);
    process.exit(1);
}
console.log("basic/regexp_global_advance: all checks passed");
