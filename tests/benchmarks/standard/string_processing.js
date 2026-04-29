// Realistic benchmark: string building and parsing.
// Simulates CSV generation and basic field parsing.
// Exercises: string concatenation, substring, charAt, parseInt, mixed types.
// Uses only protoJS-compatible string APIs (no split/join).

const ITERATIONS = 5;
const ROWS = 100;

function buildCSV(rows) {
    var out = 'id,name,value,flag\n';
    for (var i = 0; i < rows; i++) {
        out = out + i + ',item_' + i + ',' + ((i * 13) % 999) + ',' + (i % 2 === 0 ? 'true' : 'false') + '\n';
    }
    return out;
}

function parseCSV(csv) {
    var records = [];
    var n = csv.length;
    var i = 0;
    // skip header line
    while (i < n && csv.charAt(i) !== '\n') i++;
    i++; // skip newline
    while (i < n) {
        // parse id
        var start = i;
        while (i < n && csv.charAt(i) !== ',') i++;
        var id = parseInt(csv.substring(start, i));
        i++; // skip comma
        // parse name
        start = i;
        while (i < n && csv.charAt(i) !== ',') i++;
        var name = csv.substring(start, i);
        i++;
        // parse value
        start = i;
        while (i < n && csv.charAt(i) !== ',') i++;
        var value = parseInt(csv.substring(start, i));
        i++;
        // parse flag
        start = i;
        while (i < n && csv.charAt(i) !== '\n') i++;
        var flag = csv.substring(start, i) === 'true';
        i++; // skip newline
        records.push({ id: id, name: name, value: value, flag: flag });
    }
    return records;
}

var times = [];
for (var k = 0; k < ITERATIONS; k++) {
    var start = Date.now();
    var csv = buildCSV(ROWS);
    var parsed = parseCSV(csv);
    times.push(Date.now() - start);
}
times.sort(function(a, b) { return a - b; });
var median = times[Math.floor(ITERATIONS / 2)];
var result = { name: 'string_processing', time_ms: median, iterations: ITERATIONS, rows: ROWS };
console.log('__BENCH_RESULT__' + JSON.stringify(result));
