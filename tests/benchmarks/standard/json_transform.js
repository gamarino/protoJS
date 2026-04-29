// Realistic benchmark: JSON data transformation pipeline.
// Simulates a REST API handler: build records, filter, map, serialize.
// Self-contained; no external dependencies.

const ITERATIONS = 5;
const RECORDS = 5000;

function makeRecord(i) {
    return {
        id: i,
        name: 'user_' + i,
        score: (i * 17) % 100,
        active: (i % 3) !== 0,
        tags: ['tag' + (i % 5), 'cat' + (i % 3)]
    };
}

function pipeline(records) {
    // Filter active users with score >= 50
    var filtered = [];
    for (var i = 0; i < records.length; i++) {
        if (records[i].active && records[i].score >= 50) {
            filtered.push(records[i]);
        }
    }
    // Map to summary objects
    var summaries = [];
    for (var j = 0; j < filtered.length; j++) {
        var r = filtered[j];
        summaries.push({ id: r.id, label: r.name + ':' + r.score, tagCount: r.tags.length });
    }
    return JSON.stringify(summaries);
}

var times = [];
for (var k = 0; k < ITERATIONS; k++) {
    // Build dataset fresh each iteration
    var records = [];
    for (var i = 0; i < RECORDS; i++) {
        records.push(makeRecord(i));
    }
    var start = Date.now();
    var result = pipeline(records);
    times.push(Date.now() - start);
}
times.sort(function(a, b) { return a - b; });
var median = times[Math.floor(ITERATIONS / 2)];
var result2 = { name: 'json_transform', time_ms: median, iterations: ITERATIONS, records: RECORDS };
console.log('__BENCH_RESULT__' + JSON.stringify(result2));
