// String operations test

console.log("=== String Tests ===");

const str1 = "Hello";
const str2 = "World";
const combined = str1 + " " + str2;
console.log("Combined:", combined);

// String methods
console.log("Length:", combined.length);
console.log("UpperCase:", combined.toUpperCase());
console.log("LowerCase:", combined.toLowerCase());

// Template literals
const name = "protoJS";
const message = `Welcome to ${name}!`;
console.log(message);

console.log("=== String Tests Complete ===");
