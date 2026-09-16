// Fixture: counts how many times its body runs. A module is executed once and
// then served from the cache, so the count must stay at 1.
if (typeof globalThis.__module_side_effect__ !== 'number') {
    globalThis.__module_side_effect__ = 0;
}
globalThis.__module_side_effect__ = globalThis.__module_side_effect__ + 1;

module.exports = { runs: globalThis.__module_side_effect__ };
