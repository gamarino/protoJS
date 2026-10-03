// (b) Join and index: orders and products. Each task builds a dictionary index
// of the products, joins every order line with its product, derives records,
// sorts them, deduplicates suppliers with a Set and returns a summary.
const { makeRng, hashValue, FNV_OFFSET, makeWord } = require("../lib/common.js");

const CATEGORIES = ["audio", "books", "garden", "kitchen", "office", "sports", "tools", "toys", "video", "wear"];

function build(scale) {
    const nProducts = 4000 * scale;
    const nOrders = 12000 * scale;
    const rng = makeRng(777);
    const suppliers = [];
    for (let i = 0; i < 300; i++) suppliers.push("sup-" + makeWord(rng, 6));
    const products = [];
    for (let i = 0; i < nProducts; i++)
        products.push({ sku: "P" + (100000 + i), name: makeWord(rng, 8), category: CATEGORIES[rng.int(CATEGORIES.length)],
                        price: 50 + rng.int(20000), supplier: suppliers[rng.int(suppliers.length)] });
    const orders = [];
    for (let i = 0; i < nOrders; i++) {
        const lines = [];
        const n = 1 + rng.int(4);
        for (let j = 0; j < n; j++) lines.push({ sku: "P" + (100000 + rng.int(nProducts)), qty: 1 + rng.int(5) });
        orders.push({ id: i, region: rng.int(8), lines });
    }
    return { products, orders };
}

function task(data, k) {
    const index = new Map();
    for (const p of data.products) index.set(p.sku, p);
    const category = CATEGORIES[k % CATEGORIES.length];
    const derived = [];
    for (const o of data.orders) {
        for (const l of o.lines) {
            const p = index.get(l.sku);
            if (p === undefined) throw new Error("dangling sku " + l.sku);
            derived.push({ orderId: o.id, sku: p.sku, category: p.category, supplier: p.supplier,
                           revenue: l.qty * p.price, region: o.region });
        }
    }
    derived.sort((a, b) => b.revenue - a.revenue || a.orderId - b.orderId || (a.sku < b.sku ? -1 : a.sku > b.sku ? 1 : 0));
    const suppliers = new Set();
    const perRegion = new Array(8).fill(0);
    let catRevenue = 0, catLines = 0;
    for (const d of derived) {
        if (d.category === category) {
            suppliers.add(d.supplier);
            catRevenue += d.revenue;
            catLines++;
            perRegion[d.region] += d.revenue;
        }
    }
    const top = derived.slice(0, 5).map((d) => [d.orderId, d.sku, d.revenue]);
    const result = [derived.length, catLines, catRevenue, [...suppliers].sort(), perRegion, top];
    return { k, checksum: hashValue(FNV_OFFSET, result), lines: derived.length, suppliers: suppliers.size };
}

module.exports = { name: "join", build, task };
