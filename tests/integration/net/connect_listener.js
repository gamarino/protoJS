// The connect listener given to net.createConnection / net.connect /
// socket.connect runs once, on 'connect' (Node: it is added as a 'connect'
// listener with once()).  The call forms: (port, listener),
// (port, host, listener) and (options, listener).  (`new net.Socket()` is
// not available in protoJS, so socket.connect is reached only through these.)
//
// Until 2026-10 protoJS ignored it: the connection was made but the listener
// never ran (socket.on('connect') worked).  Asserting test: exits 1 on any
// failure; a listener that never runs leaves the test to its ctest timeout
// (protoJS has no setTimeout).  Binds loopback port 18851.

var net = require("net");
var failures = [];
function check(name, ok, detail) { if (!ok) failures.push(name + (detail ? ": " + detail : "")); }

var forms = {
    "createConnection(options, cb)": function (cb) { return net.createConnection({ port: 18851, host: "127.0.0.1" }, cb); },
    "createConnection(port, cb)":    function (cb) { return net.createConnection(18851, cb); },
    "connect(port, host, cb)":       function (cb) { return net.connect(18851, "127.0.0.1", cb); }
};
var names = Object.keys(forms);
var calls = {};
var pending = names.length;

var server = net.createServer(function (socket) {
    socket.on("data", function (d) { socket.write("echo:" + d.toString()); });
});
server.listen(18851, function () {
    names.forEach(function (name) {
        calls[name] = 0;
        var client;
        client = forms[name](function () {
            calls[name]++;
            client.write("hi");
        });
        client.on("data", function (d) {
            check(name + ": round trip", d.toString() === "echo:hi", JSON.stringify(d.toString()));
            client.destroy();
            finish();
        });
        client.on("error", function (e) { check(name, false, String(e)); client.destroy(); finish(); });
    });
});

function finish() {
    if (--pending > 0) return;
    names.forEach(function (n) { check(n + ": listener ran once", calls[n] === 1, String(calls[n])); });
    report();
}
function report() {
    server.close();
    if (failures.length) {
        for (var f of failures) console.log("FAIL " + f);
        process.exit(1);
    }
    console.log("connect_listener: all checks passed");
    process.exit(0);
}
