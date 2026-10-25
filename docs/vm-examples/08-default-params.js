function greet(name, greeting = "Hello") {
  return greeting + ", " + name;
}
console.log(greet("Ada"), greet("Alan", "Hi"));
