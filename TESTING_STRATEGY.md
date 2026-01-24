# Estrategia de Testing: protoJS

**Versión:** 1.0  
**Fecha:** 2026-01-24

---

## Objetivos de Testing

1. **Validar funcionalidad:** Asegurar que todas las características funcionan correctamente
2. **Demostrar potencial:** Mostrar ventajas de protoCore sobre Node.js
3. **Prevenir regresiones:** Detectar bugs temprano
4. **Documentar comportamiento:** Tests como documentación ejecutable

---

## Estructura de Tests

```
tests/
├── unit/                    # Tests unitarios (C++ con Catch2 o Google Test)
│   ├── TypeBridge/
│   │   ├── test_number.cpp
│   │   ├── test_string.cpp
│   │   ├── test_array.cpp
│   │   ├── test_object.cpp
│   │   └── test_function.cpp
│   ├── Deferred/
│   │   ├── test_basic.cpp
│   │   ├── test_concurrent.cpp
│   │   └── test_immutable_sharing.cpp
│   ├── ExecutionEngine/
│   │   └── test_execution.cpp
│   └── GCBridge/
│       └── test_gc.cpp
│
├── integration/             # Tests de integración (JavaScript)
│   ├── basic/
│   │   ├── hello_world.js
│   │   ├── arithmetic.js
│   │   └── strings.js
│   ├── collections/
│   │   ├── arrays.js
│   │   ├── objects.js
│   │   └── protoCore_collections.js
│   ├── deferred/
│   │   ├── basic_deferred.js
│   │   ├── concurrent_deferred.js
│   │   └── immutable_sharing.js
│   └── modules/
│       ├── console.js
│       ├── protoCore.js
│       └── process.js
│
├── benchmarks/              # Benchmarks de performance
│   ├── array_operations.js
│   ├── string_operations.js
│   ├── concurrent_operations.js
│   └── memory_usage.js
│
└── demos/                   # Scripts de demostración
    ├── hello_world.js
    ├── immutable_arrays.js
    ├── deferred_demo.js
    └── protoCore_collections.js
```

---

## Tests Unitarios (C++)

### Framework: Catch2 o Google Test

**Ejemplo: TypeBridge - Numbers**

```cpp
// tests/unit/TypeBridge/test_number.cpp
#include <catch2/catch.hpp>
#include "TypeBridge.h"
#include "JSContext.h"

TEST_CASE("TypeBridge: Number to SmallInteger", "[TypeBridge]") {
    protojs::JSContextWrapper wrapper;
    JSContext* ctx = wrapper.getJSContext();
    proto::ProtoContext* pCtx = wrapper.getProtoContext();
    
    // Test pequeño entero
    JSValue jsNum = JS_NewInt32(ctx, 42);
    const proto::ProtoObject* protoObj = protojs::TypeBridge::fromJS(ctx, jsNum, pCtx);
    
    REQUIRE(protoObj->isInteger(pCtx));
    REQUIRE(protoObj->asLong(pCtx) == 42);
    
    // Test conversión de vuelta
    JSValue back = protojs::TypeBridge::toJS(ctx, protoObj, pCtx);
    int32_t result;
    JS_ToInt32(ctx, &result, back);
    REQUIRE(result == 42);
    
    JS_FreeValue(ctx, jsNum);
    JS_FreeValue(ctx, back);
}

TEST_CASE("TypeBridge: Number to LargeInteger", "[TypeBridge]") {
    protojs::JSContextWrapper wrapper;
    JSContext* ctx = wrapper.getJSContext();
    proto::ProtoContext* pCtx = wrapper.getProtoContext();
    
    // Test entero grande
    JSValue jsBigNum = JS_NewInt64(ctx, 1LL << 60);
    const proto::ProtoObject* protoObj = protojs::TypeBridge::fromJS(ctx, jsBigNum, pCtx);
    
    REQUIRE(protoObj->isInteger(pCtx));
    REQUIRE(protoObj->asLong(pCtx) == (1LL << 60));
    
    JS_FreeValue(ctx, jsBigNum);
}

TEST_CASE("TypeBridge: Float to Double", "[TypeBridge]") {
    protojs::JSContextWrapper wrapper;
    JSContext* ctx = wrapper.getJSContext();
    proto::ProtoContext* pCtx = wrapper.getProtoContext();
    
    JSValue jsFloat = JS_NewFloat64(ctx, 3.14159);
    const proto::ProtoObject* protoObj = protojs::TypeBridge::fromJS(ctx, jsFloat, pCtx);
    
    REQUIRE(protoObj->isDouble(pCtx));
    REQUIRE(protoObj->asDouble(pCtx) == Approx(3.14159));
    
    JS_FreeValue(ctx, jsFloat);
}
```

**Ejemplo: Deferred - Concurrent Execution**

```cpp
// tests/unit/Deferred/test_concurrent.cpp
#include <catch2/catch.hpp>
#include "Deferred.h"
#include "JSContext.h"
#include <thread>
#include <atomic>

TEST_CASE("Deferred: Concurrent execution", "[Deferred]") {
    protojs::JSContextWrapper wrapper;
    JSContext* ctx = wrapper.getJSContext();
    
    std::atomic<int> counter{0};
    const int numDeferreds = 10;
    std::vector<JSValue> deferreds;
    
    // Crear múltiples deferreds
    for (int i = 0; i < numDeferreds; i++) {
        // Crear función que incrementa counter
        const char* code = R"(
            (function(resolve) {
                // Simular trabajo
                let sum = 0;
                for (let i = 0; i < 1000000; i++) {
                    sum += i;
                }
                resolve(sum);
            })
        )";
        
        JSValue func = JS_Eval(ctx, code, strlen(code), "test", JS_EVAL_TYPE_GLOBAL);
        JSValue deferred = /* crear Deferred con func */;
        deferreds.push_back(deferred);
    }
    
    // Esperar a que todos completen
    // (implementar wait mechanism)
    
    // Verificar que todos ejecutaron
    REQUIRE(counter.load() == numDeferreds);
    
    // Limpiar
    for (auto d : deferreds) {
        JS_FreeValue(ctx, d);
    }
}
```

---

## Tests de Integración (JavaScript)

### Framework: Ejecutar scripts y verificar salida

**Ejemplo: Basic - Hello World**

```javascript
// tests/integration/basic/hello_world.js
console.log("Hello, protoJS!");

// Verificar que console.log funciona
const output = captureConsoleOutput(() => {
    console.log("Test message");
});
assert(output === "Test message\n");
```

**Ejemplo: Collections - Immutable Arrays**

```javascript
// tests/integration/collections/immutable_arrays.js

// Test que arrays son inmutables por defecto
const arr1 = [1, 2, 3];
const arr2 = arr1.push(4); // Debe retornar nuevo array

assert(arr1.length === 3); // Original no cambió
assert(arr2.length === 4); // Nuevo array tiene el elemento

// Test structural sharing
const arr3 = [1, 2, 3];
const arr4 = arr3.slice(0, 2); // Comparte estructura
// Verificar que comparten memoria (usando protoCore internals)
```

**Ejemplo: Deferred - Basic**

```javascript
// tests/integration/deferred/basic_deferred.js

const deferred = new Deferred((resolve, reject) => {
    // Trabajo pesado
    let sum = 0;
    for (let i = 0; i < 10000000; i++) {
        sum += i;
    }
    resolve(sum);
});

deferred.then(value => {
    assert(value === 49999995000000);
    console.log("Deferred completed successfully");
});

// Esperar a que complete
await deferred;
```

**Ejemplo: Deferred - Immutable Sharing**

```javascript
// tests/integration/deferred/immutable_sharing.js

// Crear array grande inmutable
const largeArray = Array.from({length: 1000000}, (_, i) => i);

// Crear múltiples deferreds que usan el mismo array
const deferreds = [];
for (let i = 0; i < 10; i++) {
    const d = new Deferred((resolve) => {
        // Todos acceden al mismo array (compartido, no copiado)
        const sum = largeArray.reduce((a, b) => a + b, 0);
        resolve(sum);
    });
    deferreds.push(d);
}

// Esperar a que todos completen
const results = await Promise.all(deferreds);
assert(results.every(r => r === 499999500000));

// Verificar que array no fue copiado (usando protoCore internals)
// Memoria usada debe ser ~O(n) no O(n*threads)
```

**Ejemplo: protoCore Module**

```javascript
// tests/integration/modules/protoCore.js

// Test ProtoSet
const set = new protoCore.Set([1, 2, 3, 3, 4]);
assert(set.size === 4); // Duplicados eliminados
assert(set.has(1));
assert(!set.has(5));

// Test ProtoMultiset
const multiset = new protoCore.Multiset([1, 1, 2, 3]);
assert(multiset.count(1) === 2);
assert(multiset.size === 4); // Total incluyendo duplicados

// Test ProtoTuple (inmutable)
const tuple = protoCore.Tuple([1, 2, 3]);
assert(tuple.length === 3);
// tuple.push(4); // Debe fallar (inmutable)

// Test mutabilidad
const mutable = protoCore.MutableObject({a: 1});
mutable.a = 2; // OK
assert(mutable.a === 2);

const immutable = protoCore.ImmutableObject({a: 1});
// immutable.a = 2; // Debe fallar o crear nuevo objeto
```

---

## Benchmarks

### Framework: Comparar con Node.js

**Ejemplo: Array Operations**

```javascript
// tests/benchmarks/array_operations.js

const size = 1000000;
const iterations = 100;

// Test: Crear y modificar arrays
console.time("protoJS: Array operations");
for (let i = 0; i < iterations; i++) {
    const arr = Array.from({length: size}, (_, i) => i);
    const arr2 = arr.map(x => x * 2);
    const arr3 = arr2.filter(x => x % 2 === 0);
}
console.timeEnd("protoJS: Array operations");

// Comparar con Node.js ejecutando el mismo código
// Objetivo: protoJS debe ser 2-5x más rápido para operaciones inmutables
```

**Ejemplo: Concurrent Operations**

```javascript
// tests/benchmarks/concurrent_operations.js

const numTasks = 100;
const workPerTask = 1000000;

// Test con Deferred (protoJS)
console.time("protoJS: Concurrent with Deferred");
const deferreds = [];
for (let i = 0; i < numTasks; i++) {
    const d = new Deferred((resolve) => {
        let sum = 0;
        for (let j = 0; j < workPerTask; j++) {
            sum += j;
        }
        resolve(sum);
    });
    deferreds.push(d);
}
await Promise.all(deferreds);
console.timeEnd("protoJS: Concurrent with Deferred");

// Test con Promise (Node.js equivalente)
console.time("Node.js: Concurrent with Promise");
const promises = [];
for (let i = 0; i < numTasks; i++) {
    const p = new Promise((resolve) => {
        // Mismo trabajo pero en thread principal
        let sum = 0;
        for (let j = 0; j < workPerTask; j++) {
            sum += j;
        }
        resolve(sum);
    });
    promises.push(p);
}
await Promise.all(promises);
console.timeEnd("Node.js: Concurrent with Promise");

// Objetivo: protoJS debe ser 5-10x más rápido en multi-core
```

**Ejemplo: Memory Usage**

```javascript
// tests/benchmarks/memory_usage.js

// Test: Crear muchos arrays compartiendo estructura
const baseArray = Array.from({length: 1000000}, (_, i) => i);

const arrays = [];
for (let i = 0; i < 100; i++) {
    // Cada array comparte estructura con baseArray
    const arr = baseArray.map(x => x + i);
    arrays.push(arr);
}

// Medir memoria usada
const memory = protoCore.getGCMemory();
console.log(`Memory used: ${memory.used} bytes`);
console.log(`Arrays created: ${arrays.length}`);
console.log(`Memory per array: ${memory.used / arrays.length} bytes`);

// Objetivo: Memoria debe ser ~O(n) no O(n*arrays)
// Gracias a structural sharing
```

---

## Scripts de Demostración

### Hello World

```javascript
// tests/demos/hello_world.js
console.log("Hello, protoJS!");
console.log("Running on protoCore runtime");
```

### Immutable Arrays

```javascript
// tests/demos/immutable_arrays.js

console.log("=== Immutable Arrays Demo ===");

// Crear array
const arr1 = [1, 2, 3];
console.log("Original array:", arr1);

// Operaciones retornan nuevos arrays
const arr2 = arr1.push(4);
console.log("After push(4):");
console.log("  Original:", arr1); // [1, 2, 3] - no cambió
console.log("  New:", arr2);       // [1, 2, 3, 4]

// Structural sharing
const arr3 = arr1.slice(0, 2);
console.log("Slice [0,2]:", arr3); // [1, 2]
// arr3 comparte estructura con arr1 (no copia)
```

### Deferred Demo

```javascript
// tests/demos/deferred_demo.js

console.log("=== Deferred Demo ===");

// Crear deferred que hace trabajo pesado
const deferred = new Deferred((resolve, reject) => {
    console.log("Deferred started in worker thread");
    
    // Simular trabajo pesado
    let sum = 0;
    for (let i = 0; i < 100000000; i++) {
        sum += i;
    }
    
    console.log("Deferred completed, sum =", sum);
    resolve(sum);
});

console.log("Deferred created, waiting...");

deferred.then(value => {
    console.log("Resolved with value:", value);
});

// Esperar
await deferred;
console.log("Done!");
```

### protoCore Collections

```javascript
// tests/demos/protoCore_collections.js

console.log("=== protoCore Collections Demo ===");

// ProtoSet
const set = new protoCore.Set([1, 2, 3, 3, 4, 4, 5]);
console.log("Set:", Array.from(set)); // [1, 2, 3, 4, 5]

// ProtoMultiset
const multiset = new protoCore.Multiset([1, 1, 2, 2, 2, 3]);
console.log("Multiset count(2):", multiset.count(2)); // 3
console.log("Multiset size:", multiset.size); // 6

// ProtoTuple (inmutable)
const tuple = protoCore.Tuple([1, 2, 3]);
console.log("Tuple:", Array.from(tuple));
// tuple.push(4); // Error: inmutable

// Control de mutabilidad
const mutable = protoCore.MutableObject({a: 1});
mutable.a = 2;
console.log("Mutable object:", mutable);

const immutable = protoCore.ImmutableObject({a: 1});
// immutable.a = 2; // Crea nuevo objeto o error
console.log("Immutable object:", immutable);
```

---

## Criterios de Éxito

### Cobertura de Código

- **Objetivo:** >90% coverage en código crítico
- **Herramienta:** gcov + lcov
- **Archivos críticos:**
  - TypeBridge
  - ExecutionEngine
  - Deferred
  - GCBridge

### Performance

- **Array operations:** 2-5x más rápido que Node.js
- **Concurrent operations:** 5-10x más rápido con Deferred
- **Memory usage:** Similar o mejor que Node.js

### Funcionalidad

- Todos los tipos básicos JS funcionan
- Deferred ejecuta en worker threads
- Módulo protoCore funciona correctamente
- GC no tiene memory leaks

### Estabilidad

- Tests pasan consistentemente
- No hay crashes en tests
- No hay memory leaks detectados
- Performance es consistente

---

## Herramientas de Testing

### C++ Tests

- **Framework:** Catch2 o Google Test
- **Coverage:** gcov + lcov
- **Memory:** Valgrind o AddressSanitizer
- **Threading:** ThreadSanitizer

### JavaScript Tests

- **Runner:** Script custom que ejecuta .js files
- **Assertions:** Implementar assert() básico
- **Comparison:** Comparar salida con expected output

### Benchmarks

- **Framework:** Custom benchmark runner
- **Comparison:** Ejecutar mismo código en Node.js
- **Metrics:** Tiempo, memoria, CPU usage

---

## Proceso de Testing

### Desarrollo

1. Escribir test antes de implementar (TDD cuando sea posible)
2. Implementar feature
3. Ejecutar tests
4. Iterar hasta que pasen

### Pre-commit

1. Ejecutar todos los tests unitarios
2. Ejecutar tests de integración básicos
3. Verificar que no hay memory leaks
4. Verificar coverage mínimo

### CI/CD (Futuro)

1. Ejecutar todos los tests
2. Ejecutar benchmarks
3. Comparar con baseline
4. Generar reporte de coverage
5. Publicar resultados

---

## Métricas a Trackear

1. **Coverage:** % de código cubierto
2. **Test count:** Número de tests
3. **Pass rate:** % de tests que pasan
4. **Performance:** Tiempo de ejecución de benchmarks
5. **Memory:** Uso de memoria en tests
6. **Stability:** Número de crashes/errors

---

## Próximos Pasos

1. Configurar framework de testing (Catch2)
2. Escribir primeros tests unitarios (TypeBridge)
3. Implementar test runner para JavaScript
4. Escribir tests de integración básicos
5. Configurar coverage reporting
6. Escribir benchmarks iniciales
