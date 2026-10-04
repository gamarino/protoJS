// RegExp patterns whose compiled bytecode uses QuickJS register slots.
//
// libregexp stores two kinds of per-match state in the array passed to
// lre_exec: the capture pointers (2 * capture_count) and, after them, the
// registers used by loop counters and position checks (REOP_set_char_pos,
// REOP_check_advance, quantified lookaheads, bounded repetitions of groups).
// The array must therefore have lre_get_alloc_count(bc) slots. protoJS used to
// allocate 2 * capture_count only, so these patterns wrote past the end of a
// heap block and corrupted the allocator (`realloc(): invalid next size` in
// Test262 annexB/language/literals/regexp/quantifiable-assertion-followed-by.js).
// Under AddressSanitizer the first exec below reports a heap-buffer-overflow.
//
// Expected values were checked against Node.js.

let failures = 0;
function check(actual, expected, label) {
    const a = JSON.stringify(actual), e = JSON.stringify(expected);
    if (a !== e) {
        failures++;
        console.log("FAIL " + label + ": expected " + e + ", got " + a);
    }
}

const subject = 'a bZ cZZ dZZZ eZZZZ';
const cases = [
    [/.(?=Z)*/,      'a'],
    [/.(?=Z)+/,      'b'],
    [/.(?=Z)?/,      'a'],
    [/.(?=Z){2}/,    'b'],
    [/.(?=Z){2,}/,   'b'],
    [/.(?=Z){2,3}/,  'b'],
    [/.(?=Z)*?/,     'a'],
    [/.(?=Z)+?/,     'b'],
    [/.(?=Z)??/,     'a'],
    [/.(?!Z)*/,      'a'],
    [/.(?!Z)+/,      'a'],
];

// Repeat so that a short overflow lands on live allocator metadata even in a
// build without AddressSanitizer.
for (let round = 0; round < 200; round++) {
    for (const [re, expected] of cases) {
        const m = re.exec(subject);
        check(m && m[0], expected, String(re) + " round " + round);
    }
    // Bounded repetition of a capturing group: registers plus captures.
    check(/(a|b){2,3}c/.exec('xababc'), ['babc', 'b'], 'bounded group');
    check(/(?:(a)|b)*?c/.exec('abac'), ['abac', 'a'], 'lazy star group');
    // The split path compiles its own sticky copy of the pattern.
    check('aZbZZc'.split(/(?=Z)*Z/), ['a', 'b', '', 'c'], 'split with quantified lookahead');
    check('a1b22c'.split(/(\d){1,2}/), ['a', '1', 'b', '2', 'c'], 'split bounded group');
    // Global replace re-runs exec with an advancing lastIndex.
    check('aZbZZc'.replace(/.(?=Z)+/g, '#'), '#Z##Zc', 'global replace');
}

if (failures) {
    console.log(failures + " failure(s)");
    process.exit(1);
}
console.log("regexp_register_slots: OK");
