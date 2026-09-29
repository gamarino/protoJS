// Fixture: a module that exports classes.
//
// A class constructor is not created by OP_fclosure: QuickJS pushes the raw
// constructor bytecode with OP_push_const and expects OP_define_class to build
// the closure. When that closure is never built, the constructor carries no
// __closure_module__ stamp, so `new User(...)` in the requiring script resolves
// the constructor's bytecode ID against the REQUIRING script's function table
// and silently runs whichever of its functions has the same index.
class User {
    constructor(name) { this.name = name; }
    greet() { return 'Hi, ' + this.name; }
}

class Admin extends User {
    constructor(name) { super(name); this.role = 'admin'; }
}

module.exports = { User: User, Admin: Admin };
