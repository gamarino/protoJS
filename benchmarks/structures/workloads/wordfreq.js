// (d) Text / dictionary work: word frequencies over a deterministic corpus of
// lines (split, toLowerCase, punctuation stripped, Map counting, sort by
// count then key).
const { makeRng, hashValue, FNV_OFFSET, makeWord } = require("../lib/common.js");

function build(scale) {
    const rng = makeRng(99);
    const vocab = [];
    for (let i = 0; i < 6000; i++) vocab.push(makeWord(rng, 2 + rng.int(9)));
    const lines = [];
    for (let i = 0; i < 6000 * scale; i++) {
        const n = 6 + rng.int(14);
        const words = [];
        for (let j = 0; j < n; j++) {
            // Zipf-like skew: low indexes are much more frequent.
            const r = rng.int(6000);
            let w = vocab[Math.floor((r * r) / 6000)];
            const c = rng.int(10);
            if (c === 0) w = w[0].toUpperCase() + w.slice(1);
            else if (c === 1) w = w + ",";
            else if (c === 2) w = w + ".";
            words.push(w);
        }
        lines.push(words.join(" "));
    }
    return { lines };
}

function task(data, k) {
    const counts = new Map();
    let total = 0;
    for (const line of data.lines) {
        const words = line.split(" ");
        for (let w of words) {
            w = w.toLowerCase();
            const last = w[w.length - 1];
            if (last === "," || last === ".") w = w.slice(0, w.length - 1);
            if (w.length === 0) continue;
            counts.set(w, (counts.get(w) || 0) + 1);
            total++;
        }
    }
    const entries = [...counts.entries()].sort((a, b) => b[1] - a[1] || (a[0] < b[0] ? -1 : a[0] > b[0] ? 1 : 0));
    const top = entries.slice(0, 20 + k);
    const result = [total, entries.length, top];
    return { k, checksum: hashValue(FNV_OFFSET, result), total, distinct: entries.length };
}

module.exports = { name: "wordfreq", build, task };
