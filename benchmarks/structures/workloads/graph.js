// (e) Graph: nodes holding adjacency arrays of references to other nodes.
// Each task runs breadth-first searches from several sources (shortest path
// lengths in edges) and labels the connected components.
const { makeRng, hashValue, FNV_OFFSET } = require("../lib/common.js");

function build(scale) {
    const n = 20000 * scale;
    const rng = makeRng(2024);
    const nodes = [];
    for (let i = 0; i < n; i++) nodes.push({ id: i, label: "v" + i, edges: [] });
    // A sparse random graph: some nodes stay isolated, so there are many
    // components.
    const m = Math.floor(n * 1.1);
    for (let e = 0; e < m; e++) {
        const a = nodes[rng.int(n)], b = nodes[rng.int(n)];
        if (a === b) continue;
        a.edges.push(b);
        b.edges.push(a);
    }
    return { nodes };
}

function bfs(nodes, source, dist) {
    dist.fill(-1);
    const queue = [source];
    dist[source.id] = 0;
    let head = 0, sum = 0, far = 0;
    while (head < queue.length) {
        const v = queue[head++];
        const d = dist[v.id];
        sum += d;
        if (d > far) far = d;
        for (const w of v.edges) {
            if (dist[w.id] === -1) { dist[w.id] = d + 1; queue.push(w); }
        }
    }
    return [queue.length, sum, far];
}

function task(data, k) {
    const nodes = data.nodes;
    const n = nodes.length;
    const dist = new Array(n).fill(-1);
    const searches = [];
    for (let s = 0; s < 4; s++) searches.push(bfs(nodes, nodes[(k * 7919 + s * 104729) % n], dist));
    // Connected components, labelled by BFS from the lowest unvisited id.
    const comp = new Array(n).fill(-1);
    let components = 0, largest = 0;
    for (let i = 0; i < n; i++) {
        if (comp[i] !== -1) continue;
        const queue = [nodes[i]];
        comp[i] = components;
        let head = 0;
        while (head < queue.length) {
            const v = queue[head++];
            for (const w of v.edges) if (comp[w.id] === -1) { comp[w.id] = components; queue.push(w); }
        }
        if (queue.length > largest) largest = queue.length;
        components++;
    }
    const result = [searches, components, largest];
    return { k, checksum: hashValue(FNV_OFFSET, result), components, largest };
}

module.exports = { name: "graph", build, task };
