// A net.Socket and a net.Server are EventEmitters (Node): their listeners
// run with the socket or server as `this`, and every EventEmitter method is
// there.  Until 2026-10 each carried a hidden EventEmitter that on/emit
// forwarded to, so `this` in a listener was that hidden object and once(),
// off() and listeners() were missing.  EventEmitter.prototype also inherits
// from Object.prototype now.
//
// Asserting test: exits 1 on any failure; a listener that never runs leaves
// the test to its ctest timeout.  Binds loopback port 18852.

var net = require("net");
var EventEmitter = require("events");
var failures = [];
function check(name, got, expected) {
    if (got !== expected) failures.push(name + ": got " + String(got) + ", expected " + String(expected));
}

check("EventEmitter.prototype inherits Object.prototype",
      Object.getPrototypeOf(EventEmitter.prototype) === Object.prototype, true);
var e = new EventEmitter();
check("emitter.hasOwnProperty", typeof e.hasOwnProperty, "function");
check("emitter instanceof Object", e instanceof Object, true);

var server = net.createServer(function (socket) {
    check("connection listener this", this, server);
    check("server socket is an emitter", socket instanceof EventEmitter, true);
    socket.on("data", function (d) {
        check("server-side data listener this", this, socket);
        socket.write("echo:" + d.toString());
    });
});
check("server instanceof EventEmitter", server instanceof EventEmitter, true);
check("server.once", typeof server.once, "function");
var listeningThis = null;
server.on("listening", function () { listeningThis = this; });

server.listen(18852, function () {
    var client = net.createConnection({ port: 18852, host: "127.0.0.1" });
    check("socket instanceof EventEmitter", client instanceof EventEmitter, true);
    check("socket.off", typeof client.off, "function");
    check("socket.listeners", typeof client.listeners, "function");
    check("socket.hasOwnProperty", typeof client.hasOwnProperty, "function");
    var onceCalls = 0;
    client.once("connect", function () {
        onceCalls++;
        check("connect listener this", this, client);
        client.write("hi");
    });
    function extra() { check("removed listener ran", false, true); }
    client.on("data", extra);
    client.off("data", extra);
    check("off removed the listener", client.listenerCount("data"), 0);
    client.on("data", function (d) {
        check("client data listener this", this, client);
        check("round trip", d.toString(), "echo:hi");
        check("once listener ran once", onceCalls, 1);
        check("listening listener this", listeningThis, server);
        client.destroy();
        server.close();
        report();
    });
    client.on("error", function (err) { check("client error", String(err), ""); client.destroy(); server.close(); report(); });
});

function report() {
    if (failures.length) {
        for (var f of failures) console.log("FAIL " + f);
        process.exit(1);
    }
    console.log("socket_is_emitter: all checks passed");
    process.exit(0);
}
