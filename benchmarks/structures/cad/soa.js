// The CAD model of model.js re-encoded for Node.js workers that share it
// without copying: every entity kind becomes columns of typed arrays over
// SharedArrayBuffers (struct of arrays), references become integer indexes,
// variable-length lists become CSR offset/index arrays, and strings are
// left behind (the tasks need only numbers). The four tasks are rewritten
// over the columns and must give the same results as model.js's.
//
// This file is the redesign the shared-memory variant costs: an encoder
// and a second implementation of every task, against model.js's single one.
const { hashValue, hashInt, FNV_OFFSET } = require("../lib/common.js");

function sab(Type, n) { return new Type(new SharedArrayBuffer(Math.max(1, n) * Type.BYTES_PER_ELEMENT)); }

// Object model -> columns. Assemblies are numbered in pre-order (their ids).
function encode(model) {
    const c = model.counts;
    const asms = [];
    (function collect(a) { asms[a.id] = a; for (const ch of a.children) collect(ch); })(model.root);
    const A = c.assemblies, P = c.parts, V = c.vertices, E = c.edges, F = c.faces;
    const s = {
        counts: c, root: model.root.id,
        asmDx: sab(Int32Array, A), asmDy: sab(Int32Array, A), asmDz: sab(Int32Array, A),
        asmChildOff: sab(Int32Array, A + 1), asmChildIdx: sab(Int32Array, A),
        asmPartOff: sab(Int32Array, A + 1), asmPartIdx: sab(Int32Array, P),
        partMaterial: sab(Int32Array, P), partVOff: sab(Int32Array, P + 1), partEOff: sab(Int32Array, P + 1),
        partFOff: sab(Int32Array, P + 1),
        vx: sab(Int32Array, V), vy: sab(Int32Array, V), vz: sab(Int32Array, V),
        ea: sab(Int32Array, E), eb: sab(Int32Array, E),
        faceId: sab(Int32Array, F), faceEOff: sab(Int32Array, F + 1), faceEIdx: sab(Int32Array, 4 * F),
        matDensity: sab(Int32Array, model.materials.length), nMaterials: model.materials.length,
    };
    let ci = 0, pi = 0, vi = 0, ei = 0, fi = 0, fei = 0;
    for (let a = 0; a < A; a++) {
        const asm = asms[a];
        s.asmDx[a] = asm.transform.dx; s.asmDy[a] = asm.transform.dy; s.asmDz[a] = asm.transform.dz;
        s.asmChildOff[a] = ci;
        for (const ch of asm.children) s.asmChildIdx[ci++] = ch.id;
        s.asmPartOff[a] = pi;
        for (const p of asm.parts) {
            s.asmPartIdx[pi++] = p.id;
            s.partMaterial[p.id] = p.material.id;
            // Parts are numbered as built; their vertices, edges and faces
            // are numbered consecutively per part, so offsets are by id.
            s.partVOff[p.id] = p.vertices[0].id; s.partEOff[p.id] = p.edges[0].id; s.partFOff[p.id] = p.faces[0].id;
            for (const v of p.vertices) { s.vx[v.id] = v.x; s.vy[v.id] = v.y; s.vz[v.id] = v.z; vi++; }
            for (const e of p.edges) { s.ea[e.id] = e.a.id; s.eb[e.id] = e.b.id; ei++; }
            for (const f of p.faces) {
                s.faceId[f.id] = f.id;
                s.faceEOff[f.id] = 4 * f.id;
                for (let j = 0; j < 4; j++) s.faceEIdx[4 * f.id + j] = f.edges[j].id;
                fi++; fei += 4;
            }
        }
    }
    s.asmChildOff[A] = ci; s.asmPartOff[A] = pi;
    s.partVOff[P] = V; s.partEOff[P] = E; s.partFOff[P] = F; s.faceEOff[F] = 4 * F;
    for (const mat of model.materials) s.matDensity[mat.id] = mat.density;
    return s;
}

function bounds(s, k) {
    const out = [];
    function walk(a, ox, oy, oz) {
        const tx = ox + s.asmDx[a], ty = oy + s.asmDy[a], tz = oz + s.asmDz[a];
        let b = null;
        const grow = (x, y, z) => {
            if (b === null) { b = [x, y, z, x, y, z]; return; }
            if (x < b[0]) b[0] = x; if (y < b[1]) b[1] = y; if (z < b[2]) b[2] = z;
            if (x > b[3]) b[3] = x; if (y > b[4]) b[4] = y; if (z > b[5]) b[5] = z;
        };
        for (let i = s.asmPartOff[a]; i < s.asmPartOff[a + 1]; i++) {
            const p = s.asmPartIdx[i];
            for (let v = s.partVOff[p]; v < s.partVOff[p + 1]; v++) grow(s.vx[v] + tx, s.vy[v] + ty, s.vz[v] + tz);
        }
        for (let i = s.asmChildOff[a]; i < s.asmChildOff[a + 1]; i++) {
            const cb = walk(s.asmChildIdx[i], tx, ty, tz);
            if (cb) { grow(cb[0], cb[1], cb[2]); grow(cb[3], cb[4], cb[5]); }
        }
        if (b && a % (k + 1) === 0) out.push([a, b[0], b[1], b[2], b[3], b[4], b[5]]);
        return b;
    }
    walk(s.root, 0, 0, 0);
    return out;
}

function eachPart(s, a, fn) {
    for (let i = s.asmPartOff[a]; i < s.asmPartOff[a + 1]; i++) fn(s.asmPartIdx[i]);
    for (let i = s.asmChildOff[a]; i < s.asmChildOff[a + 1]; i++) eachPart(s, s.asmChildIdx[i], fn);
}

function mass(s, k) {
    const area = new Map(), mass = new Map();
    eachPart(s, s.root, (p) => {
        const m = s.partMaterial[p];
        let a = 0;
        for (let f = s.partFOff[p]; f < s.partFOff[p + 1]; f++) {
            const e0 = s.faceEIdx[s.faceEOff[f]], e1 = s.faceEIdx[s.faceEOff[f] + 1];
            const v0 = s.ea[e0], v1 = s.eb[e0], v2 = s.eb[e1];
            const d1 = Math.abs(s.vx[v1] - s.vx[v0]) + Math.abs(s.vy[v1] - s.vy[v0]) + Math.abs(s.vz[v1] - s.vz[v0]);
            const d2 = Math.abs(s.vx[v2] - s.vx[v1]) + Math.abs(s.vy[v2] - s.vy[v1]) + Math.abs(s.vz[v2] - s.vz[v1]);
            a += d1 * d2;
        }
        const v0 = s.partVOff[p], v7 = v0 + 7;
        const vol = (s.vx[v7] - s.vx[v0]) * (s.vy[v7] - s.vy[v0]) * (s.vz[v7] - s.vz[v0]);
        area.set(m, (area.get(m) || 0) + (a % 1000000007));
        mass.set(m, (mass.get(m) || 0) + ((vol % 1000000007) * s.matDensity[m] + k) % 1000000007);
    });
    const rows = [];
    for (let m = 0; m < s.nMaterials; m++) rows.push([m, area.get(m) || 0, mass.get(m) || 0]);
    return rows;
}

function integrity(s, k) {
    let faces = 0, bad = 0, h = FNV_OFFSET;
    eachPart(s, s.root, (p) => {
        const vlo = s.partVOff[p], vhi = s.partVOff[p + 1];
        for (let e = s.partEOff[p]; e < s.partEOff[p + 1]; e++) {
            const a = s.ea[e], b = s.eb[e];
            if (a < vlo || a >= vhi || b < vlo || b >= vhi || a === b) bad++;
        }
        for (let f = s.partFOff[p]; f < s.partFOff[p + 1]; f++) {
            faces++;
            const deg = new Map();
            for (let j = s.faceEOff[f]; j < s.faceEOff[f + 1]; j++) {
                const e = s.faceEIdx[j];
                deg.set(s.ea[e], (deg.get(s.ea[e]) || 0) + 1);
                deg.set(s.eb[e], (deg.get(s.eb[e]) || 0) + 1);
            }
            if (deg.size !== 4) bad++;
            for (const d of deg.values()) if (d !== 2) bad++;
            if (s.faceId[f] % 97 === k) h = hashInt(h, s.faceId[f]);
        }
    });
    return [faces, bad, h];
}

function buckets(s, k) {
    const cell = 25000 + 1000 * k;
    const bk = new Map();
    eachPart(s, s.root, (p) => {
        for (let v = s.partVOff[p]; v < s.partVOff[p + 1]; v++) {
            const key = Math.floor(s.vx[v] / cell) + ":" + Math.floor(s.vy[v] / cell) + ":" + Math.floor(s.vz[v] / cell);
            bk.set(key, (bk.get(key) || 0) + 1);
        }
    });
    const top = [...bk.entries()].sort((a, b) => b[1] - a[1] || (a[0] < b[0] ? -1 : 1)).slice(0, 10);
    return [bk.size, top];
}

const TASKS = [bounds, mass, integrity, buckets];

function task(s, k) {
    return { k, checksum: hashValue(FNV_OFFSET, TASKS[k % TASKS.length](s, k)) };
}

module.exports = { encode, task };
