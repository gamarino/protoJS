// Reactions registered on a pending promise run when it is resolved later --
// in a later macrotask too -- and a reaction registered after settlement still
// runs asynchronously, never inside then().
let resolveA, rejectB;
const a = new Promise(r => { resolveA = r; });
const b = new Promise((_, rej) => { rejectB = rej; });
a.then(v => console.log('a1', v));
a.then(v => console.log('a2', v));
b.catch(e => console.log('b caught', e));
setImmediate(() => {
    console.log('immediate 1');
    resolveA('late');
    resolveA('ignored');
    rejectB('later');
    console.log('immediate 1 end');
});
setImmediate(() => {
    console.log('immediate 2');
    let ran = false;
    a.then(v => { ran = true; console.log('a3', v); });
    console.log('then on settled ran synchronously:', ran);
});
