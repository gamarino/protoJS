// net.Socket is a constructor (Node): `new net.Socket()` is an unconnected
// socket that socket.connect() connects, and every socket the module makes
// is an instance of it.  Until 2026-10 net.Socket did not exist ("function
// is not a constructor").
//
// Asserting test: exits 1 on any failure; a connection that never completes
// leaves the test to its ctest timeout.  Binds loopback port 18853.

var net = require("net");
var EventEmitter = require("events");
var failures = [];
function check(name, got, expected) {
    if (got !== expected) failures.push(name + ": got " + String(got) + ", expected " + String(expected));
}

check("typeof net.Socket", typeof net.Socket, "function");
var s = new net.Socket();
check("new net.Socket() instanceof net.Socket", s instanceof net.Socket, true);
check("a Socket is an EventEmitter", s instanceof EventEmitter, true);
check("Socket.prototype.constructor", net.Socket.prototype.constructor, net.Socket);
check("typeof s.connect", typeof s.connect, "function");
var s2 = net.Socket();
check("net.Socket() without new", s2 instanceof net.Socket, true);
s2.destroy();

var server = net.createServer(function (socket) {
    check("server-side socket instanceof net.Socket", socket instanceof net.Socket, true);
    socket.on("data", function (d) { socket.write("echo:" + d.toString()); });
});
server.listen(18853, function () {
    var ret = s.connect(18853, "127.0.0.1", function () {
        check("connect listener this", this, s);
        s.write("hi");
    });
    check("connect returns the socket", ret, s);
    s.on("data", function (d) {
        check("round trip", d.toString(), "echo:hi");
        var c = net.createConnection({ port: 18853, host: "127.0.0.1" });
        check("createConnection result instanceof net.Socket", c instanceof net.Socket, true);
        c.on("connect", function () {
            c.destroy();
            s.destroy();
            server.close();
            report();
        });
    });
    s.on("error", function (e) { check("socket error", String(e), ""); s.destroy(); server.close(); report(); });
});

function report() {
    if (failures.length) {
        for (var f of failures) console.log("FAIL " + f);
        process.exit(1);
    }
    console.log("socket_constructor: all checks passed");
    process.exit(0);
}
