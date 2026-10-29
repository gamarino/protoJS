// A server closed right after listen() must close promptly, and a server that
// is not closed must still accept connections.
//
// Until 2026-10 close() woke the accept loop by closing the listening
// descriptor under it. On macOS shutdown() is a no-op on a listening socket and
// close() waits for a thread inside accept() on that descriptor to drain, which
// it sometimes did not: the main thread sat in close(), the loop in accept()
// (sampled on the macOS runner; cli/blocking-joins-and-finalizers'
// http-server-collected case timed out about one run in ten there). The loop
// now polls a stop flag and the descriptor is closed after the join.
//
// The close cycles fail (time out) only where that race bites, i.e. macOS;
// the round trips check the accept path on every platform. Binds loopback
// ports 18845-18848, like the cli fixtures. Asserting test: exits 1 on any
// failure, and prints what it measured.

var failures = [];
function check(name, ok, detail) { if (!ok) failures.push(name + (detail ? ": " + detail : "")); }

// The stall needs close() to meet the loop thread as it enters accept(), so
// the delay between listen() and close() sweeps from nothing to about a
// millisecond (a spin of growing length).
var CYCLES = 200;
var worstHttp = 0, worstNet = 0, sink = 0;
function spin(n) { for (var k = 0; k < n; k++) sink += k; }
for (var i = 0; i < CYCLES; i++) {
    var delay = (i % 100) * 40;
    var hs = http.createServer(function (req, res) { res.end("x"); });
    hs.listen(18845);
    check("http listen " + i, hs.__fd__ !== undefined && hs.__fd__ >= 0, "fd " + hs.__fd__);
    spin(delay);
    var t = Date.now();
    hs.close();
    worstHttp = Math.max(worstHttp, Date.now() - t);
    check("http fd after close " + i, hs.__fd__ === -1, "fd " + hs.__fd__);

    var ns = net.createServer(function () {});
    ns.listen(18846);
    spin(delay);
    t = Date.now();
    ns.close();
    worstNet = Math.max(worstNet, Date.now() - t);
}
console.log("listen+close x" + CYCLES + ": slowest http close " + worstHttp + " ms, slowest net close " + worstNet + " ms");
// A close joins a loop woken at once (POSIX) or within 50 ms (Windows); 2 s is a stall.
check("http close bounded", worstHttp < 2000, worstHttp + " ms");
check("net close bounded", worstNet < 2000, worstNet + " ms");

// The accept path still works: one HTTP round trip and one TCP round trip.
var pending = 2;
function done() {
    if (--pending > 0) return;
    if (failures.length) {
        for (var f of failures) console.log("FAIL " + f);
        process.exit(1);
    }
    console.log("listen_close_cycles: all checks passed");
}

var server = http.createServer(function (req, res) { res.end("pong:" + req.url); });
server.listen(18847, function () {
    var req = http.request({ hostname: "127.0.0.1", port: 18847, path: "/ping", method: "GET" }, function (res) {
        var body = "";
        res.on("data", function (c) { body += c; });
        res.on("end", function () {
            check("http round trip", body === "pong:/ping", JSON.stringify(body));
            server.close();
            done();
        });
    });
    req.end();
});

var tcp = net.createServer(function (socket) {
    socket.on("data", function (d) { socket.write("echo:" + d.toString()); });
});
tcp.listen(18848, function () {
    // 'connect' is emitted asynchronously, so a listener added here sees it.
    // (protoJS ignores a connect listener passed to createConnection.)
    var client = net.createConnection({ port: 18848, host: "127.0.0.1" });
    client.on("connect", function () { client.write("hi"); });
    client.on("data", function (d) {
        check("tcp round trip", d.toString() === "echo:hi", JSON.stringify(d.toString()));
        client.destroy();
        tcp.close();
        done();
    });
    client.on("error", function (e) { check("tcp client", false, String(e)); client.destroy(); tcp.close(); done(); });
});
