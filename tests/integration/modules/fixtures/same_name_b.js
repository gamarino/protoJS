// Fixture: paired with same_name_a.js — identical structure, different
// results, so a cross-module bytecode-ID collision produces a wrong answer
// rather than an error. See same_name_a.js for the full rationale.
module.exports = {
    whoami: function whoami() { return 'B'; },
    scale: function scale(n) { return n * 3; }
};
