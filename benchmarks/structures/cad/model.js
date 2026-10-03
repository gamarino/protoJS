// A CAD-like model held as ordinary JavaScript objects, and the analysis
// tasks run over it. Shared by protoJS (protojs_cad.js) and Node.js
// (node_cad.js, node_cad_worker.js), so both run the same algorithm.
//
// The model: materials and layers; an assembly tree (a root, `branching`
// sub-assemblies per level, `depth` levels, each with a translation); every
// leaf assembly holds parts; every part is a box solid with its own
// topology -- 8 vertices with integer coordinates (micrometres), 12 edges
// referring to two vertices, 6 faces referring to four edges -- plus a
// material, a layer and a metadata dictionary. All arithmetic is on
// integers small enough to be exact in doubles, so both runtimes produce
// identical results.
const { makeRng, hashValue, hashString, hashInt, FNV_OFFSET } = require("../lib/common.js");

// Box corner order: bit 0 = x, bit 1 = y, bit 2 = z.
const BOX_EDGES = [[0, 1], [2, 3], [4, 5], [6, 7], [0, 2], [1, 3], [4, 6], [5, 7], [0, 4], [1, 5], [2, 6], [3, 7]];
// Each face as four edge indexes forming a closed loop.
const BOX_FACES = [[0, 5, 1, 4], [2, 7, 3, 6], [0, 9, 2, 8], [1, 11, 3, 10], [4, 10, 6, 8], [5, 11, 7, 9]];

function build(nParts, opts) {
    const rng = makeRng(31337);
    const materials = [];
    for (let i = 0; i < 40; i++) materials.push({ id: i, name: "mat-" + i, density: 1 + rng.int(20) });
    const layers = [];
    for (let i = 0; i < 64; i++) layers.push({ id: i, name: "layer-" + i, visible: i % 7 !== 0 });
    const depth = 3, branching = 8;
    let nextAsm = 0, nextPart = 0, nextVertex = 0, nextEdge = 0, nextFace = 0;
    const leaves = [];
    function makeAsm(level) {
        const asm = { id: nextAsm++, name: "asm-" + level + "-" + nextAsm,
                      transform: { dx: rng.int(1000000), dy: rng.int(1000000), dz: rng.int(1000000) },
                      children: [], parts: [] };
        if (level < depth) for (let i = 0; i < branching; i++) asm.children.push(makeAsm(level + 1));
        else leaves.push(asm);
        return asm;
    }
    const root = makeAsm(0);
    for (let p = 0; p < nParts; p++) {
        const x0 = rng.int(500000), y0 = rng.int(500000), z0 = rng.int(500000);
        const sx = 1 + rng.int(2000), sy = 1 + rng.int(2000), sz = 1 + rng.int(2000);
        const vertices = [];
        for (let c = 0; c < 8; c++)
            vertices.push({ id: nextVertex++, x: x0 + (c & 1 ? sx : 0), y: y0 + (c & 2 ? sy : 0), z: z0 + (c & 4 ? sz : 0) });
        const layer = layers[rng.int(layers.length)];
        const edges = BOX_EDGES.map(([a, b]) => ({ id: nextEdge++, a: vertices[a], b: vertices[b], layer }));
        const material = materials[rng.int(materials.length)];
        const faces = BOX_FACES.map((loop) => ({ id: nextFace++, edges: loop.map((e) => edges[e]), material, layer,
                                                 meta: { finish: rng.int(4), rev: rng.int(9) } }));
        const part = { id: nextPart++, name: "part-" + p, material, layer, vertices, edges, faces,
                       metadata: { supplier: "s" + rng.int(300), batch: rng.int(10000), tags: ["t" + rng.int(20)] } };
        leaves[rng.int(leaves.length)].parts.push(part);
    }
    return { root, materials, layers, counts: { assemblies: nextAsm, parts: nextPart, vertices: nextVertex,
                                                edges: nextEdge, faces: nextFace } };
}

// Bounding box of every assembly (its parts and sub-assemblies, translated).
function taskBounds(model, k) {
    const out = [];
    function walk(asm, ox, oy, oz) {
        const tx = ox + asm.transform.dx, ty = oy + asm.transform.dy, tz = oz + asm.transform.dz;
        let b = null;
        const grow = (x, y, z) => {
            if (b === null) { b = [x, y, z, x, y, z]; return; }
            if (x < b[0]) b[0] = x; if (y < b[1]) b[1] = y; if (z < b[2]) b[2] = z;
            if (x > b[3]) b[3] = x; if (y > b[4]) b[4] = y; if (z > b[5]) b[5] = z;
        };
        for (const p of asm.parts) for (const v of p.vertices) grow(v.x + tx, v.y + ty, v.z + tz);
        for (const c of asm.children) {
            const cb = walk(c, tx, ty, tz);
            if (cb) { grow(cb[0], cb[1], cb[2]); grow(cb[3], cb[4], cb[5]); }
        }
        if (b && asm.id % (k + 1) === 0) out.push([asm.id, b[0], b[1], b[2], b[3], b[4], b[5]]);
        return b;
    }
    walk(model.root, 0, 0, 0);
    return out;
}

// Area of every face and mass of every part, aggregated by material.
function taskMass(model, k) {
    const area = new Map(), mass = new Map();
    function visit(asm) {
        for (const p of asm.parts) {
            const m = p.material.id;
            let a = 0;
            for (const f of p.faces) {
                const v0 = f.edges[0].a, v1 = f.edges[0].b, v2 = f.edges[1].b;
                const d1 = Math.abs(v1.x - v0.x) + Math.abs(v1.y - v0.y) + Math.abs(v1.z - v0.z);
                const d2 = Math.abs(v2.x - v1.x) + Math.abs(v2.y - v1.y) + Math.abs(v2.z - v1.z);
                a += d1 * d2;
            }
            const v = p.vertices;
            const vol = (v[7].x - v[0].x) * (v[7].y - v[0].y) * (v[7].z - v[0].z);
            area.set(m, (area.get(m) || 0) + (a % 1000000007));
            mass.set(m, (mass.get(m) || 0) + ((vol % 1000000007) * p.material.density + k) % 1000000007);
        }
        for (const c of asm.children) visit(c);
    }
    visit(model.root);
    const rows = [];
    for (const mat of model.materials) rows.push([mat.id, area.get(mat.id) || 0, mass.get(mat.id) || 0]);
    return rows;
}

// Reference integrity: every face is a closed loop of its part's edges, and
// every edge joins two vertices of its part.
function taskIntegrity(model, k) {
    let faces = 0, bad = 0, h = FNV_OFFSET;
    function visit(asm) {
        for (const p of asm.parts) {
            const own = new Set(p.vertices);
            for (const e of p.edges) if (!own.has(e.a) || !own.has(e.b) || e.a === e.b) bad++;
            for (const f of p.faces) {
                faces++;
                const deg = new Map();
                for (const e of f.edges) {
                    deg.set(e.a, (deg.get(e.a) || 0) + 1);
                    deg.set(e.b, (deg.get(e.b) || 0) + 1);
                }
                if (deg.size !== 4) bad++;
                for (const d of deg.values()) if (d !== 2) bad++;
                if (f.id % 97 === k) h = hashInt(h, f.id);
            }
        }
        for (const c of asm.children) visit(c);
    }
    visit(model.root);
    return [faces, bad, h];
}

// Spatial bucketing of vertex positions on a grid; the most populated cells.
function taskBuckets(model, k) {
    const cell = 25000 + 1000 * k;
    const buckets = new Map();
    function visit(asm) {
        for (const p of asm.parts) for (const v of p.vertices) {
            const key = Math.floor(v.x / cell) + ":" + Math.floor(v.y / cell) + ":" + Math.floor(v.z / cell);
            buckets.set(key, (buckets.get(key) || 0) + 1);
        }
        for (const c of asm.children) visit(c);
    }
    visit(model.root);
    const top = [...buckets.entries()].sort((a, b) => b[1] - a[1] || (a[0] < b[0] ? -1 : 1)).slice(0, 10);
    return [buckets.size, top];
}

const TASKS = [taskBounds, taskMass, taskIntegrity, taskBuckets];

function task(model, k) {
    const result = TASKS[k % TASKS.length](model, k);
    return { k, checksum: hashValue(FNV_OFFSET, result) };
}

module.exports = { build, task, TASKS, BOX_EDGES, BOX_FACES };
