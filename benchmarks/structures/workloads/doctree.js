// (c) Document transform: a JSON-like nested document (sections, paragraphs,
// tables, lists, mixed-type arrays). Each task walks it, rewrites and
// normalises every node into a NEW tree (creating many objects) and hashes
// the result.
const { makeRng, hashString, hashInt, FNV_OFFSET, makeWord } = require("../lib/common.js");

const KINDS = ["Section", "Paragraph", "Table", "List", "Figure", "Note"];

function makeNode(rng, depth) {
    const kind = KINDS[rng.int(KINDS.length)];
    const node = { Kind: kind, ID: "n" + rng.next(), Meta: { Author: makeWord(rng, 5), Rev: rng.int(50), Draft: rng.int(4) === 0 } };
    if (rng.int(3) === 0) node.Meta.Labels = [makeWord(rng, 4), makeWord(rng, 3)];
    if (depth === 0 || kind === "Paragraph" || kind === "Note") {
        node.Text = makeWord(rng, 4) + " " + makeWord(rng, 6) + " " + makeWord(rng, 5);
        node.Values = [rng.int(1000), makeWord(rng, 3), rng.int(2) === 0, null];
        return node;
    }
    const n = 2 + rng.int(4);
    node.Children = [];
    for (let i = 0; i < n; i++) node.Children.push(makeNode(rng, depth - 1));
    return node;
}

function build(scale) {
    const rng = makeRng(4242);
    const roots = [];
    for (let i = 0; i < 40 * scale; i++) roots.push(makeNode(rng, 5));
    return { title: "Document", roots };
}

function normalise(node, k) {
    const out = {
        kind: node.Kind.toLowerCase(),
        id: node.ID,
        author: node.Meta.Author.toUpperCase(),
        revision: node.Meta.Rev + k,
        draft: node.Meta.Draft ? 1 : 0,
        labels: node.Meta.Labels ? node.Meta.Labels.map((l) => l + "-" + k) : [],
    };
    if (node.Text !== undefined) {
        out.words = node.Text.split(" ");
        const values = [];
        for (const v of node.Values) {
            if (v === null) continue;
            values.push(typeof v === "number" ? { num: v * 2 } : typeof v === "string" ? { str: v } : { flag: v ? 1 : 0 });
        }
        out.values = values;
    }
    if (node.Children) out.children = node.Children.map((c) => normalise(c, k));
    return out;
}

function hashTree(h, n) {
    h = hashString(h, n.kind);
    h = hashString(h, n.id);
    h = hashString(h, n.author);
    h = hashInt(h, n.revision);
    h = hashInt(h, n.draft);
    for (const l of n.labels) h = hashString(h, l);
    if (n.words) for (const w of n.words) h = hashString(h, w);
    if (n.values) {
        for (const v of n.values) {
            if (v.num !== undefined) h = hashInt(h, v.num);
            else if (v.str !== undefined) h = hashString(h, v.str);
            else h = hashInt(h, v.flag + 7);
        }
    }
    if (n.children) for (const c of n.children) h = hashTree(h, c);
    return h;
}

function count(n) {
    let c = 1;
    if (n.children) for (const ch of n.children) c += count(ch);
    return c;
}

function task(data, k) {
    const roots = data.roots.map((r) => normalise(r, k));
    let h = hashString(FNV_OFFSET, data.title.toLowerCase());
    let nodes = 0;
    for (const r of roots) { h = hashTree(h, r); nodes += count(r); }
    return { k, checksum: h, nodes };
}

module.exports = { name: "doctree", build, task };
