gdb -batch -ex "run tests/benchmarks/standard/object_property.js &" -ex "sleep 2" -ex "interrupt" -ex "bt" -ex "kill" -ex "quit" ./build/protojs
