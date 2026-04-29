// Tests http.request by spinning up an http.createServer on a random port
// and making a request to it via http.request — all within the same process.
console.log("=== http.request integration test ===");

const PORT = 17483;
let pass = 0, fail = 0;

function check(label, cond, detail) {
    if (cond) { console.log("PASS", label); pass++; }
    else { console.log("FAIL", label, detail || ""); fail++; }
}

const server = http.createServer((req, res) => {
    res.writeHead(200, {'Content-Type': 'application/json'});
    res.end(JSON.stringify({ method: req.method, url: req.url, echo: 'ok' }));
});

server.listen(PORT, () => {
    const req = http.request(
        { hostname: '127.0.0.1', port: PORT, path: '/test', method: 'GET' },
        (res) => {
            check("statusCode is 200", res.statusCode === 200, `got ${res.statusCode}`);
            let body = '';
            res.on('data', (chunk) => { body += chunk; });
            res.on('end', () => {
                let parsed;
                try { parsed = JSON.parse(body); } catch (e) { parsed = null; }
                check("body is valid JSON", parsed !== null, body);
                check("echo field is ok", parsed && parsed.echo === 'ok');
                check("method is GET", parsed && parsed.method === 'GET');
                check("url is /test", parsed && parsed.url === '/test');
                server.close();
                console.log(`\n${pass} passed, ${fail} failed`);
                if (fail > 0) process.exit(1);
            });
        }
    );
    req.end();
});
