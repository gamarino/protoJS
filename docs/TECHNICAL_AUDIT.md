# Auditoría Técnica: protoJS

**Fecha:** 2026-03-06
**Versión auditada:** 0.1.0 (Phase 6 Complete)
**Auditor:** Claude Sonnet 4.6 (análisis estático del código fuente)
**Rama:** master

---

## 1. Resumen Ejecutivo

protoJS es un runtime JavaScript experimental que utiliza **QuickJS** como parser/compilador y **protoCore** como motor de ejecución nativo. El proyecto ha completado 6 fases de desarrollo y demuestra una arquitectura original con ventajas reales en escenarios de paralelismo. Sin embargo, presenta deuda técnica significativa en áreas de estabilidad, cobertura de tests y rendimiento en cargas de trabajo single-thread.

**Hallazgos clave:**

| Área                  | Estado      | Criticidad |
|-----------------------|-------------|------------|
| Arquitectura          | Sólida      | Informativo|
| Bugs activos conocidos| 3 confirmados| Alta       |
| Cobertura de tests    | Insuficiente | Alta       |
| Rendimiento ST        | -5.22x vs Node.js | Media |
| Rendimiento MT        | +1.86x vs Node.js  | Positivo|
| Documentación         | Extensa     | Bajo       |
| CI/CD                 | Ausente     | Alta       |
| Seguridad             | Riesgos menores | Media  |

---

## 2. Arquitectura General

### 2.1 Stack de Ejecución

```
JavaScript (ES2020+)
    |
    v
QuickJS (Parse + Compile to bytecode)
    |
    v
ProtoBytecodeLoader  -->  ProtoBytecodeModule
    |
    v
ProtoInterpreter (intérprete custom sobre protoCore)
    |
    v
protoCore (ProtoObject, ProtoContext, ProtoSpace, GC, Threads)
```

Esta arquitectura es coherente y bien documentada. La decisión de usar QuickJS sólo como frontend (parser + compilador a bytecode) y ejecutar todo en protoCore es una apuesta estratégica que permite aprovechar el GC concurrente e inmutabilidad de protoCore.

### 2.2 Componentes Principales

| Componente | Archivo(s) | Propósito |
|---|---|---|
| `JSContextWrapper` | `src/JSContext.cpp` | Encapsula contextos QuickJS y protoCore; inicializa thread pools |
| `TypeBridge` | `src/TypeBridge.cpp` | Conversión bidireccional JS ↔ ProtoObject |
| `GCBridge` | `src/GCBridge.cpp` | Mapa de identidad entre JSValue y ProtoObject |
| `EventLoop` | `src/EventLoop.cpp` | Cola de callbacks en hilo principal (singleton) |
| `Deferred` | `src/Deferred.cpp` | Primitiva de paralelismo; ejecuta en ProtoThread |
| `ProtoInterpreter` | `src/runtime/ProtoInterpreter.cpp` | Intérprete de bytecode QuickJS sobre protoCore |
| `ProtoBytecodeLoader` | `src/runtime/ProtoBytecodeLoader.cpp` | Carga bytecode QuickJS a `ProtoBytecodeModule` |
| Thread Pools | `CPUThreadPool`, `IOThreadPool` | Pools separados para CPU e I/O |

### 2.3 Sistema de Módulos

El sistema de módulos está bien estratificado:
- `CommonJSLoader` — soporte `require()`
- `ESModuleLoader` — soporte `import`
- `ModuleInterop` — interoperabilidad CJS ↔ ESM
- `AsyncModuleLoader` — carga asíncrona
- `ModuleCache` — caché de módulos cargados
- `ModuleResolver` — resolución de paths incluyendo `node_modules`
- `NativeModuleWrapper` + `DynamicLibraryLoader` — addons nativos `.so`

El soporte de módulos nativos via `dlopen` / símbolos exportados con `-rdynamic` es un diseño correcto para compatibilidad con Node-API.

---

## 3. Bugs y Problemas Activos

### BUG-01 (Alta) — `ChildProcessModule::init` llamado dos veces en REPL

**Archivo:** `src/main.cpp:162-163`

```cpp
protojs::DNSModule::init(wrapper.getJSContext());
protojs::ChildProcessModule::init(wrapper.getJSContext());  // <-- primera vez
protojs::MemoryAnalyzer::init(wrapper.getJSContext());
```

En el bloque REPL (aprox. línea 162), `ChildProcessModule::init` se invoca dos veces consecutivas. Si `init` registra funciones globales, la segunda llamada sobrescribe el estado sin liberar el primero, lo que puede causar leaks de `JSValue` o comportamiento indefinido.

### BUG-02 (Alta) — `ProcessModule::init` deshabilitado en modo script

**Archivo:** `src/main.cpp:223-225`

```cpp
// NOTE: ProcessModule::init currently triggers a hang in the CLI path
// ...
// protojs::ProcessModule::init(wrapper.getJSContext(), argc, argv);
```

El módulo `process` está completamente deshabilitado en el modo de ejecución de scripts. Código JavaScript que dependa de `process.env`, `process.argv`, `process.exit()` o `process.cwd()` fallará silenciosamente o lanzará errores de referencia.

**Impacto:** Alta — la mayoría de scripts Node.js reales usan `process`.

### BUG-03 (Media) — BigInt muy grandes truncan a 0

**Archivo:** `src/TypeBridge.cpp:46-47`

```cpp
// For very large BigInt beyond int64_t, we'd need LargeInteger support
// For now, truncate to int64_t
return pContext->fromLong(0); // Placeholder - should use LargeInteger
```

BigInts que exceden `int64_t` se convierten a `0` en lugar de lanzar excepción o usar el tipo correcto. Esto viola la semántica de JavaScript y puede producir resultados incorrectos silenciosos.

---

## 4. Problemas de Calidad de Código

### 4.1 Ruido Excesivo en stderr

**Archivo:** `src/main.cpp` (aprox. 20+ líneas)

Cada módulo inicializado emite un mensaje `[protojs] CLI: X initialized` a `stderr`. En producción, esto contamina la salida estándar de error y dificulta la depuración real. Debería condicionarse a un flag `--verbose` o usarse el sistema de logging existente (`Logger`).

```cpp
// Ejemplo del patrón repetido:
std::cerr << "[protojs] CLI: Console initialized" << std::endl;
std::cerr << "[protojs] CLI: Deferred initialized" << std::endl;
// ... x 20 módulos más
```

### 4.2 Potencial Memory Leak en GCBridge

**Archivo:** `src/GCBridge.cpp:80`

```cpp
JSValue* jsValPtr = new JSValue(JS_DupValue(ctx, jsVal));
```

Se asigna un `JSValue*` en heap. Si el objeto de mapeo que lo referencia es coleccionado por el GC de protoCore sin ejecutar el destructor de C++, el `JSValue` nunca se libera con `JS_FreeValue`. Se requiere verificar que el finalizer correspondiente siempre sea invocado.

### 4.3 Rendimiento del Stack en ProtoInterpreter

**Archivo:** `src/runtime/ProtoInterpreter.cpp:50-95`

El stack de evaluación del intérprete se implementa como una `ProtoList` inmutable. Cada `push` o `pop` crea una nueva lista (structural sharing). Para bytecode de alta frecuencia esto implica una asignación por cada operación de stack, lo que explica parcialmente el gap de rendimiento vs QuickJS (1.41x en single-thread).

Adicionalmente, `slotKey()` convierte un índice entero a string y lo hashea en cada acceso a variable local:

```cpp
static unsigned long slotKey(proto::ProtoContext* ctx, unsigned int index) {
    std::string s = std::to_string(index);  // alloc por acceso a local
    const proto::ProtoObject* o = ctx->fromUTF8String(s.c_str());
    // ...
}
```

Esta operación debería ser O(1) con un array directo, no O(1) amortizado con hash.

### 4.4 Timeout Hardcodeado en Event Loop

**Archivo:** `src/main.cpp:317`

```cpp
const auto timeout = std::chrono::seconds(180);
```

El timeout de 3 minutos está hardcodeado. Scripts que terminan normalmente esperan innecesariamente si hay algún thread colgado, y scripts legítimos de larga duración se cortan arbitrariamente.

### 4.5 `console.log` No Formatea Objetos

**Archivo:** `src/console.cpp:23-28`

```cpp
} else if (JS_IsObject(val)) {
    const char* str = JS_ToCString(ctx, val);
    if (str) {
        out << str;
```

`JS_ToCString` en un objeto arbitrario devuelve `[object Object]` en lugar de una representación JSON. Node.js realiza una inspección profunda del objeto. Esto hace que `console.log({a: 1})` muestre `[object Object]` en lugar de `{ a: 1 }`.

---

## 5. Análisis de Rendimiento

### 5.1 Benchmarks vs Node.js (V8 JIT)

| Benchmark      | protoJS (ms) | Node.js (ms) | Factor         |
|----------------|-------------|--------------|----------------|
| array_literal  | 6           | 3            | Node 2.00x     |
| control_flow   | 51          | 9            | Node 5.67x     |
| function_calls | 73          | 2            | **Node 36.50x** |
| numeric_loop   | 37          | 1            | **Node 37.00x** |
| object_property| 88          | 34           | Node 2.59x     |
| parallel_cpu   | 22          | 41           | **protoJS 1.86x** |
| string_concat  | 5           | 1            | Node 5.00x     |

**Media geométrica:** Node.js 5.22x más rápido (single-thread).

### 5.2 Benchmarks vs QuickJS (intérprete vs intérprete)

| Benchmark      | protoJS (ms) | QuickJS (ms) | Factor          |
|----------------|-------------|--------------|-----------------|
| function_calls | 71          | 79           | protoJS 1.11x   |
| parallel_cpu   | 22          | 630          | **protoJS 28.64x** |
| numeric_loop   | 37          | 33           | QuickJS 1.12x   |
| object_property| 101         | 64           | QuickJS 1.58x   |

**Media geométrica:** QuickJS 1.41x más rápido (single-thread). protoJS gana en `function_calls` y `parallel_cpu`.

### 5.3 Interpretación

- El **gap single-thread** (5x vs Node.js, 1.4x vs QuickJS) es esperado para un intérprete sin JIT frente a V8, pero el overhead adicional vs QuickJS se debe al costo de traducción bytecode → protoCore primitives.
- La **victoria en `parallel_cpu`** es la demostración más fuerte del valor del proyecto: paralelismo real a través de ProtoThreads sin GIL.
- El gap en `function_calls` (36x vs Node.js) sugiere overhead elevado en `call`/`return` del intérprete — prioridad de optimización.

---

## 6. Cobertura de Tests

### 6.1 Tests C++ (Catch2)

Los tests unitarios en `tests/unit/` son muy escasos para la envergadura del proyecto:

| Archivo de test               | Estado            |
|-------------------------------|-------------------|
| `test_main.cpp`               | Solo entrypoint, sin casos|
| `test_event_loop.cpp`         | Existe            |
| `test_io_thread_pool.cpp`     | Existe            |
| `test_thread_pools.cpp`       | Existe            |
| `test_semver.cpp`             | Existe            |
| `test_benchmark_runner.cpp`   | Existe            |
| `test_npm_registry.cpp`       | Existe            |
| `test_nodejs_test_runner.cpp` | Existe            |

**Ausentes:** `TypeBridge`, `GCBridge`, `ProtoInterpreter`, `Deferred`, `ExecutionEngine` — todos los componentes críticos del runtime no tienen tests unitarios directos.

### 6.2 Tests de Integración (JavaScript)

Los tests en `tests/integration/` cubren módulos básicos pero no son ejecutables de forma automatizada ni verifican condiciones de error sistemáticamente.

### 6.3 Test262 (Conformidad ECMAScript)

El runner de Test262 existe y está operativo. Hay una skip-list activa para 66 tests que fallan. No hay un número definitivo de tests pasando publicado en el código auditado.

**Recomendación:** Ejecutar el suite completo de Test262 para `language/` y `built-ins/` y publicar la tasa de conformidad como métrica de calidad.

### 6.4 Ausencia de CI/CD

No se detectó ningún archivo de configuración de integración continua (`.github/workflows/`, `.gitlab-ci.yml`, `Jenkinsfile`, etc.). Todo el testing es manual.

**Riesgo:** Regresiones no detectadas entre commits. El historial de commits muestra fixes frecuentes a problemas que debería capturar CI automáticamente.

---

## 7. Seguridad

### 7.1 Módulo crypto con OpenSSL

El módulo crypto linkea contra `libssl` y `libcrypto` de OpenSSL. El enlace es correcto. Se recomienda verificar que no se usen APIs deprecadas (`MD5`, `SHA1` sin propósito de compatibilidad) y que los errores de OpenSSL no se descarten silenciosamente.

### 7.2 Addons Nativos con `dlopen`

El cargador de addons nativos usa `dlopen` para cargar `.so` dinámicamente y exporta todos los símbolos de QuickJS/protoCore con `-rdynamic`. Esto es correcto pero implica:
- No hay sandboxing del addon nativo — código nativo tiene acceso completo al proceso.
- Esto es equivalente al modelo de Node.js y es una limitación de diseño conocida.

### 7.3 Inyección en Process

El módulo `ChildProcess` ejecuta comandos externos. Se debe verificar que los argumentos no se concatenen en strings para `system()` o `popen()` — revisar que la implementación usa `execve` con arrays de argumentos.

### 7.4 Paths de Usuario

El `ModuleResolver` maneja paths proporcionados por el usuario. Se recomienda verificar que no haya traversal de paths (`../`) no controlado fuera del directorio del proyecto.

---

## 8. Dependencias

| Dependencia | Versión       | Origen        | Notas |
|-------------|---------------|---------------|-------|
| QuickJS     | 2024-01-13    | Submódulo git | Motor de parsing/compilación |
| protoCore   | (externa)     | Sibling dir / prefix | Dependencia crítica; no está en el repo |
| Catch2      | v3.5.2        | FetchContent  | Framework de tests |
| OpenSSL     | Sistema       | `find_package`| Para módulo crypto |
| pthread     | Sistema       | Linkeo        | Thread support |

**Riesgo principal:** `protoCore` es una dependencia externa no incluida en el repositorio. El build falla si no está disponible en una ruta específica relativa (`../protoCore`). Esto dificulta la reproducibilidad del build para nuevos desarrolladores.

---

## 9. Build y Packaging

### 9.1 CMake

El `CMakeLists.txt` está bien estructurado. Puntos a mejorar:
- `option(CMAKE_BUILD_TYPE "Build type" Release)` en línea 219 es incorrecto — `CMAKE_BUILD_TYPE` no es un `option` de CMake sino una cache variable. Está definido dos veces y el orden puede causar confusión.
- No hay target `install` para las librerías de tests o módulos nativos.
- `set_source_files_properties` para QuickJS aplica `-DCONFIG_VERSION` correctamente.

### 9.2 Sanitizers

El proyecto admite `-DENABLE_COVERAGE=ON` pero no tiene soporte explícito para AddressSanitizer (ASan) o ThreadSanitizer (TSan) en CMakeLists. El directorio `build_asan/` en el git status sugiere que se construye manualmente.

**Recomendación:** Agregar opciones `ENABLE_ASAN` y `ENABLE_TSAN` en CMakeLists para facilitar builds de análisis.

### 9.3 Packaging

Existe infraestructura de packaging para `.deb`, `.rpm`, macOS `.pkg` y Windows `.msi` documentada en `packaging/PROCEDURES.md`. No se detectaron los scripts shell referenciados (`build_deb.sh`).

---

## 10. Documentación

La documentación es un punto **fuerte** del proyecto:

- 30+ archivos `.md` bien estructurados en `docs/`
- `EXECUTIVE_SUMMARY.md` — visión de alto nivel actualizada
- `BENCHMARK_STANDARD_RESULTS.md` — resultados honestos con análisis
- `TESTING_STRATEGY.md` — estrategia definida (pendiente de implementar plenamente)
- Completion reports por phase (Phase 2-6)
- `docs/API_REFERENCE.md` — referencia de API
- `docs/INSTALLATION.md` y `docs/TROUBLESHOOTING.md`

**Puntos de mejora:**
- `TESTING_STRATEGY.md` describe tests que no existen aún en el código (e.g., `TypeBridge/test_number.cpp`)
- Los completion reports de fases anteriores describen features como "implementadas" que en algunos casos son stubs

---

## 11. Análisis por Componente: Estado Real

| Componente             | Declarado   | Estado Observado               |
|------------------------|-------------|--------------------------------|
| TypeBridge             | Completo    | Funcional; BigInt trunca a 0   |
| GCBridge               | Completo    | Funcional; posible leak en JSValue* |
| EventLoop              | Completo    | Funcional y correcto           |
| ProtoInterpreter       | Completo    | Funcional; hot-path lento (allocs)|
| Deferred/ProtoThread   | Completo    | Funcional; wins en benchmarks MT|
| ProcessModule          | Completo    | Deshabilitado en script mode   |
| ChildProcessModule     | Completo    | Init doble en REPL             |
| console.log            | Completo    | No inspecciona objetos         |
| NPMRegistry            | Completo    | Implementación HTTP directa    |
| Profiler/MemoryAnalyzer| Completo    | Básico; no integrado con protoCore GC|
| IntegratedDebugger     | Completo    | Chrome DevTools Protocol declarado|
| Test262 Runner         | Completo    | Operativo; 66 tests en skip-list|

---

## 12. Recomendaciones por Prioridad

### Prioridad Alta (bloquea calidad)

1. **Corregir BUG-01:** Eliminar la segunda llamada a `ChildProcessModule::init` en el path REPL de `main.cpp`.

2. **Investigar y reparar BUG-02:** Restaurar `ProcessModule::init` en el path de script. El "hang" reportado debe diagnosticarse (probable deadlock en inicialización) y resolverse. Sin `process`, la compatibilidad Node.js es muy limitada.

3. **Configurar CI/CD básico:** Un pipeline en GitHub Actions con: build, `ctest`, smoke tests de integración (al menos `hello.js` y `arithmetic.js`) y una medición de Test262. Sin esto, la calidad no es verificable entre commits.

4. **Agregar tests unitarios para componentes críticos:** TypeBridge, GCBridge y ProtoInterpreter son el corazón del runtime y no tienen tests directos. Un fallo en cualquiera de ellos puede ser difícil de diagnosticar.

### Prioridad Media (mejora estabilidad)

5. **Auditar y reparar el ciclo de vida de `JSValue*` en GCBridge** (`src/GCBridge.cpp:80`). Confirmar que el finalizer siempre libera con `JS_FreeValue`.

6. **Mover los `std::cerr` de inicialización a `Logger`** condicionado a nivel de log. Reducir el ruido en modo normal.

7. **Corregir BUG-03:** BigInt grandes no deben silenciosamente ser 0. Al menos deben lanzar un error JavaScript descriptivo.

8. **Mejorar `console.log` para objetos:** Implementar inspección básica via `JSON.stringify` para objetos planos. Esto mejora drásticamente la experiencia de desarrollo.

### Prioridad Baja (optimización y pulido)

9. **Optimizar el stack del intérprete:** Considerar un buffer mutable local para el stack de evaluación (no compartido entre threads) en lugar de `ProtoList` inmutable. Esto podría cerrar parte del gap de rendimiento vs QuickJS.

10. **Cachear `slotKey()` por índice:** Un array de hashes precalculados para los primeros N índices locales eliminaría una alloc/hash por cada acceso a variable.

11. **Hacer configurable el timeout del event loop:** Exponer via `--event-loop-timeout N` en lugar de hardcodear 180s.

12. **Agregar targets CMake `ENABLE_ASAN` y `ENABLE_TSAN`** para facilitar análisis de memoria y threading.

---

## 13. Conclusión

protoJS es un proyecto técnicamente ambicioso con una propuesta de valor clara y diferenciada: paralelismo real sin GIL a través de protoCore. La arquitectura es coherente, la documentación es extensa y la victoria demostrada en benchmarks de CPU paralela (1.86x vs Node.js, 28.64x vs QuickJS) valida el concepto.

Los principales riesgos son la **deuda en testing**, la **ausencia de CI/CD** y dos bugs que limitan la compatibilidad Node.js real (`ProcessModule` deshabilitado, `ChildProcessModule` doblemente inicializado). Estos son resolubles con esfuerzo acotado y deberían ser la prioridad antes de promover el proyecto a un estado "production-ready".

El proyecto está en estado **alpha funcional** — válido para demostración del concepto y desarrollo activo, pero no listo para uso en producción sin las correcciones de alta prioridad.

---

*Auditoría realizada mediante análisis estático del código fuente y la documentación del proyecto. No se ejecutó el binario durante este análisis.*
