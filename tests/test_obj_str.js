function make(i) {
    return { name: 'user_' + i };
}
var o = make(1);
console.log(o.name);
