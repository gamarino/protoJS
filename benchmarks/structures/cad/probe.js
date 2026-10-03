// Quick size probe: build the model and report the counts and memory.
const m = require(__dirname + "/model.js");
const n = Number(process.env.PARTS || 10000);
const t0 = Date.now();
const model = m.build(n);
const ms = Date.now() - t0;
const mu = process.memoryUsage();
const out = { parts: n, buildMs: ms, counts: model.counts, rssMB: (mu.rss / 1048576) | 0, heapUsedMB: (mu.heapUsed / 1048576) | 0 };
const t1 = Date.now();
out.checks = [0, 1, 2, 3].map((k) => m.task(model, k).checksum);
out.tasksMs = Date.now() - t1;
console.log(JSON.stringify(out));
