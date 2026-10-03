// (a) Records processing: orders with nested customers, items, tags and a
// status. Each task computes group-by aggregates with Maps, filters, sorts
// with comparator functions and string keys. Task k differs from the others
// only in a parameter (the quantity threshold and how many top customers it
// keeps), so every task does the same amount of work.
const { makeRng, hashValue, FNV_OFFSET, makeWord } = require("../lib/common.js");

const COUNTRIES = ["AR", "BR", "CL", "DE", "ES", "FR", "IT", "JP", "MX", "US", "UY", "ZA"];
const STATUSES = ["new", "paid", "shipped", "delivered", "cancelled", "returned"];
const TAGS = ["gift", "express", "bulk", "promo", "fragile", "intl", "vip", "eco"];

function build(scale) {
    const nOrders = 25000 * scale;
    const nCustomers = Math.max(500, (nOrders / 20) | 0);
    const rng = makeRng(12345);
    const cities = [];
    for (let i = 0; i < 200; i++) cities.push({ city: "c" + makeWord(rng, 6), country: COUNTRIES[rng.int(COUNTRIES.length)] });
    const customers = [];
    for (let i = 0; i < nCustomers; i++) {
        const c = cities[rng.int(cities.length)];
        customers.push({ id: i, name: makeWord(rng, 5) + " " + makeWord(rng, 7),
                         address: { city: c.city, country: c.country } });
    }
    const orders = [];
    for (let i = 0; i < nOrders; i++) {
        const nItems = 1 + rng.int(5);
        const items = [];
        for (let j = 0; j < nItems; j++)
            items.push({ sku: "sku-" + rng.int(5000), qty: 1 + rng.int(9), price: 100 + rng.int(99900) });
        const nTags = rng.int(3);
        const tags = [];
        for (let j = 0; j < nTags; j++) tags.push(TAGS[rng.int(TAGS.length)]);
        orders.push({ id: i, customer: customers[rng.int(nCustomers)], items, tags,
                      status: STATUSES[rng.int(STATUSES.length)] });
    }
    return { orders, customers };
}

function task(data, k) {
    const minQty = 1 + (k % 3);
    const topN = 10 + k;
    const revenueByCountry = new Map();
    const revenueByCustomer = new Map();
    const statusCounts = new Map();
    const tagCounts = new Map();
    let kept = 0;
    for (const o of data.orders) {
        statusCounts.set(o.status, (statusCounts.get(o.status) || 0) + 1);
        if (o.status === "cancelled" || o.status === "returned") continue;
        const lines = o.items.filter((it) => it.qty >= minQty);
        if (lines.length === 0) continue;
        kept++;
        let rev = 0;
        for (const it of lines) rev += it.qty * it.price;
        const country = o.customer.address.country;
        revenueByCountry.set(country, (revenueByCountry.get(country) || 0) + rev);
        const cid = o.customer.id;
        revenueByCustomer.set(cid, (revenueByCustomer.get(cid) || 0) + rev);
        for (const t of o.tags) {
            const key = country + "/" + t;
            tagCounts.set(key, (tagCounts.get(key) || 0) + 1);
        }
    }
    const byCountry = [...revenueByCountry.entries()].sort((a, b) => b[1] - a[1] || (a[0] < b[0] ? -1 : 1));
    const top = [...revenueByCustomer.entries()]
        .sort((a, b) => b[1] - a[1] || a[0] - b[0])
        .slice(0, topN)
        .map(([id, rev]) => [id, data.customers[id].name, rev]);
    const statuses = [...statusCounts.entries()].sort((a, b) => (a[0] < b[0] ? -1 : 1));
    const tags = [...tagCounts.entries()].sort((a, b) => b[1] - a[1] || (a[0] < b[0] ? -1 : 1));
    const result = [kept, byCountry, top, statuses, tags];
    return { k, checksum: hashValue(FNV_OFFSET, result), kept, countries: byCountry.length, topCustomer: top[0][0] };
}

module.exports = { name: "records", build, task };
