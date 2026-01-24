# Plan de Implementación: protoJS

**Versión:** 1.0  
**Fecha:** 2026-01-24  
**Objetivo:** Runtime de JavaScript basado en protoCore, equivalente a Node.js

---

## Visión General

protoJS es un runtime de JavaScript que utiliza protoCore como base para la representación interna de objetos, memoria y concurrencia. Utiliza QuickJS como parser y motor de compilación, pero reemplaza completamente el runtime de QuickJS con protoCore.

### Principios Fundamentales

1. **Parser de biblioteca**: QuickJS proporciona el parser y compilador
2. **Compatibilidad Node.js**: Soporta los mismos parámetros y paquetes npm
3. **JavaScript moderno únicamente**: Sin compatibilidad hacia atrás
4. **Implementación por fases**: Primera fase como demostrador de protoCore
5. **Tipos básicos en protoCore**: Todos los tipos JS usan primitivas de protoCore
6. **Colecciones especiales**: ProtoSet, ProtoMultiset, ProtoSparseList como módulos nuevos
7. **Deferred con worker threads**: Implementación transparente usando todos los núcleos

---

## FASE 1: DEMOSTRADOR DE PROTOCORE

**Objetivo:** Demostrar las capacidades de protoCore como base para un runtime JavaScript moderno.

**Duración estimada:** 4-6 semanas

### 1.1 Arquitectura Base

#### 1.1.1 Integración QuickJS + protoCore

**Estado actual:** Parcialmente implementado

**Tareas:**
- [x] Estructura básica JSContextWrapper
- [ ] **Completar integración del runtime:**
  - Reemplazar el runtime de QuickJS con protoCore
  - Interceptar todas las operaciones de objetos JS para usar protoCore
  - Implementar custom allocator que use ProtoSpace
- [ ] **Gestión de contexto:**
  - Cada JSContext debe tener su propio ProtoContext
  - Sincronización entre múltiples JSContexts (si se necesitan)
  - Thread-local storage para ProtoContext

**Archivos a modificar/crear:**
- `src/JSContext.cpp` - Completar integración
- `src/RuntimeBridge.h/cpp` - Nuevo: Bridge entre QuickJS runtime y protoCore

#### 1.1.2 TypeBridge Completo

**Estado actual:** Parcialmente implementado, incompleto

**Tareas:**
- [ ] **Conversión JS → protoCore:**
  - [x] Primitivos básicos (null, undefined, boolean, number, string)
  - [ ] BigInt → LargeInteger
  - [ ] Symbol → Nuevo tipo en protoCore o mapeo especial
  - [ ] Array → ProtoList (inmutable) o ProtoSparseList (mutable/sparse)
  - [ ] Object → ProtoObject (con soporte mutable/inmutable)
  - [ ] Function → ProtoMethod
  - [ ] Date → protoCore Date
  - [ ] RegExp → Implementación usando libregexp de QuickJS
  - [ ] Map/Set → ProtoSparseList/ProtoSet (con wrapper JS)
  - [ ] TypedArray → ProtoByteBuffer
  - [ ] ArrayBuffer → ProtoByteBuffer
  - [ ] Promise → Deferred (nuestra implementación)

- [ ] **Conversión protoCore → JS:**
  - [x] Primitivos básicos
  - [ ] LargeInteger → BigInt
  - [ ] ProtoList → Array
  - [ ] ProtoSparseList → Array o Object (según uso)
  - [ ] ProtoObject → Object
  - [ ] ProtoMethod → Function
  - [ ] ProtoSet → Set (con wrapper)
  - [ ] ProtoMultiset → Nuevo módulo JS
  - [ ] ProtoTuple → Array (inmutable, read-only)
  - [ ] ProtoString → String (con conversión UTF-8 completa)
  - [ ] ProtoByteBuffer → ArrayBuffer/TypedArray

- [ ] **Manejo de mutabilidad:**
  - Detectar cuando un objeto JS debe ser mutable
  - Crear objetos protoCore con `mutable_ref` cuando sea necesario
  - Exponer API para que el usuario elija mutabilidad (sin romper compatibilidad JS)

**Archivos:**
- `src/TypeBridge.cpp` - Completar todas las conversiones
- `src/TypeBridge.h` - Agregar helpers para mutabilidad

### 1.2 Implementación de Tipos Básicos JavaScript

#### 1.2.1 Números

**Tareas:**
- [ ] **Number:**
  - SmallInteger para enteros pequeños (usando tagged pointers)
  - LargeInteger para BigInt y enteros grandes
  - Double para números de punto flotante
  - Operaciones aritméticas delegadas a protoCore
  - Coerción automática según tamaño

- [ ] **BigInt:**
  - Mapeo directo a LargeInteger
  - Operaciones bitwise usando protoCore
  - Conversión a/desde Number cuando sea seguro

**Archivos:**
- `src/types/NumberBridge.h/cpp` - Nuevo

#### 1.2.2 Strings

**Tareas:**
- [ ] **String:**
  - Mapeo a ProtoString (inmutable por defecto)
  - Conversión UTF-8 ↔ UTF-16 (QuickJS usa UTF-8 internamente)
  - Operaciones de string usando métodos de protoCore
  - Template literals usando concatenación eficiente de protoCore

**Archivos:**
- `src/types/StringBridge.h/cpp` - Nuevo
- Mejorar `TypeBridge.cpp` para strings

#### 1.2.3 Arrays

**Tareas:**
- [ ] **Array:**
  - Por defecto: ProtoList (inmutable, eficiente)
  - Opción para ProtoSparseList si es sparse o muy grande
  - Métodos Array.* delegados a protoCore cuando sea posible
  - Iteradores usando ProtoListIterator
  - Sparse arrays detectados automáticamente

**Archivos:**
- `src/types/ArrayBridge.h/cpp` - Nuevo

#### 1.2.4 Objects

**Tareas:**
- [ ] **Object:**
  - Mapeo a ProtoObject
  - Atributos → ProtoSparseList
  - Prototype chain → ParentLink de protoCore
  - Property descriptors usando atributos especiales
  - Getters/setters como métodos protoCore
  - Object.freeze/seal → control de mutabilidad

**Archivos:**
- `src/types/ObjectBridge.h/cpp` - Nuevo

#### 1.2.5 Functions

**Tareas:**
- [ ] **Function:**
  - Compilación QuickJS → bytecode
  - Ejecución en contexto protoCore
  - Closure usando closureLocals de ProtoContext
  - `this` binding usando protoCore object model
  - Argumentos → ProtoList
  - Parámetros con nombre → ProtoSparseList (kwargs)

**Archivos:**
- `src/types/FunctionBridge.h/cpp` - Nuevo
- `src/ExecutionEngine.h/cpp` - Nuevo: Motor de ejecución

### 1.3 Deferred con Virtual Threads (Modelo Java)

**Estado actual:** Implementado con modelo de virtual threads

**Tareas:**
- [x] **ThreadPoolExecutor genérico:**
  - [x] Pool de threads reutilizable
  - [x] Queue de tareas thread-safe
  - [x] Shutdown graceful
  - [x] Métricas (activeCount, queueSize)

- [x] **CPUThreadPool:**
  - [x] Pool optimizado para CPU (tamaño = número de CPUs)
  - [x] Singleton pattern
  - [x] Auto-detección de CPUs

- [x] **IOThreadPool:**
  - [x] Pool optimizado para I/O (tamaño = 3-4x CPUs, configurable)
  - [x] Singleton pattern
  - [x] Factor configurable

- [x] **EventLoop:**
  - [x] Procesamiento de callbacks en thread principal
  - [x] Queue thread-safe
  - [x] Singleton pattern

- [x] **Implementación Deferred:**
  - [x] Tareas ligeras (no ProtoThread completo)
  - [x] Ejecución en CPUThreadPool
  - [x] ProtoContext aislado por tarea
  - [x] Callbacks vía EventLoop
  - [x] Sincronización thread-safe

- [x] **Módulo I/O:**
  - [x] API explícita (io.readFile, io.writeFile)
  - [x] Ejecución en IOThreadPool
  - [x] Operaciones bloqueantes en pool I/O

- [x] **Configuración CLI:**
  - [x] --cpu-threads N
  - [x] --io-threads N
  - [x] --io-threads-factor F

**Archivos:**
- `src/ThreadPoolExecutor.h/cpp` - Pool genérico
- `src/CPUThreadPool.h/cpp` - Pool de CPU
- `src/IOThreadPool.h/cpp` - Pool de I/O
- `src/EventLoop.h/cpp` - Event loop
- `src/Deferred.h/cpp` - Refactorizado para tareas ligeras
- `src/modules/IOModule.h/cpp` - Módulo I/O
- `src/main.cpp` - Configuración CLI

### 1.4 Módulos Básicos

#### 1.4.1 Console

**Estado actual:** Implementación básica

**Tareas:**
- [x] console.log básico
- [ ] console.error, console.warn, console.info
- [ ] console.debug, console.trace
- [ ] console.table, console.group, console.time
- [ ] Formateo avanzado (objetos, arrays, etc.)
- [ ] Colores y estilos (opcional para fase 1)

**Archivos:**
- `src/console.cpp` - Completar

#### 1.4.2 Módulo protoCore (Nuevo)

**Tareas:**
- [ ] Exponer colecciones especiales de protoCore:
  - `protoCore.Set` - Wrapper de ProtoSet
  - `protoCore.Multiset` - Wrapper de ProtoMultiset  
  - `protoCore.SparseList` - Wrapper de ProtoSparseList
  - `protoCore.Tuple` - Wrapper de ProtoTuple (inmutable)
  - `protoCore.ImmutableObject` - Crear objetos inmutables explícitamente
  - `protoCore.MutableObject` - Crear objetos mutables explícitamente

- [ ] Utilidades:
  - `protoCore.isImmutable(obj)` - Verificar si es inmutable
  - `protoCore.makeImmutable(obj)` - Convertir a inmutable
  - `protoCore.makeMutable(obj)` - Convertir a mutable

**Archivos:**
- `src/modules/ProtoCoreModule.h/cpp` - Nuevo

#### 1.4.3 Módulo I/O (Nuevo - Virtual Threads)

**Estado actual:** Implementado básico

**Tareas:**
- [x] `io.readFile(path)` - Leer archivo usando IOThreadPool
- [x] `io.writeFile(path, content)` - Escribir archivo usando IOThreadPool
- [ ] `io.fetch(url)` - HTTP requests (futuro)
- [ ] Operaciones asíncronas con Promises (futuro)

**Arquitectura:**
- Todas las operaciones I/O usan explícitamente el IOThreadPool
- Operaciones bloqueantes no afectan el pool de CPU
- API explícita: usuario debe usar `io.readFile()` en lugar de detección automática

**Archivos:**
- `src/modules/IOModule.h/cpp` - Implementado

#### 1.4.4 Módulo process (Básico)

**Tareas:**
- [ ] `process.argv` - Argumentos de línea de comandos
- [ ] `process.env` - Variables de entorno
- [ ] `process.exit(code)` - Salir del proceso
- [ ] `process.cwd()` - Directorio actual
- [ ] `process.platform` - Plataforma (linux, darwin, win32)
- [ ] `process.arch` - Arquitectura

**Archivos:**
- `src/modules/ProcessModule.h/cpp` - Nuevo

### 1.5 Garbage Collector Integration

**Tareas:**
- [ ] **Integración GC:**
  - QuickJS objects deben ser rastreados por protoCore GC
  - JSValue → ProtoObject mapping para GC roots
  - Liberación automática cuando protoCore GC recolecta
  - Weak references usando protoCore mechanisms

- [ ] **Optimizaciones:**
  - Objetos JS de corta duración en young generation
  - Objetos compartidos entre threads en old generation
  - Minimizar pauses del GC

**Archivos:**
- `src/GCBridge.h/cpp` - Nuevo: Bridge para GC

### 1.6 Tests Exhaustivos

**Objetivo:** Demostrar potencial y validar ante desarrolladores

#### 1.6.1 Tests Unitarios

**Tareas:**
- [ ] **TypeBridge:**
  - Tests para cada tipo de conversión JS ↔ protoCore
  - Tests de mutabilidad
  - Tests de edge cases (null, undefined, NaN, Infinity)

- [ ] **Tipos básicos:**
  - Number: operaciones aritméticas, BigInt, coerción
  - String: operaciones, UTF-8, template literals
  - Array: métodos, iteradores, sparse arrays
  - Object: prototype chain, property descriptors
  - Function: closures, this binding, arguments

- [ ] **Deferred:**
  - Ejecución en worker thread
  - Resolve/reject
  - Múltiples deferreds concurrentes
  - Compartir datos inmutables entre threads
  - Performance: comparación con Promise estándar

- [ ] **Módulos:**
  - Console: todos los métodos
  - protoCore: todas las colecciones
  - process: todas las propiedades

#### 1.6.2 Tests de Integración

**Tareas:**
- [ ] Scripts de demostración:
  - Hello World básico
  - Manipulación de arrays grandes (demostrar inmutabilidad)
  - Cálculos paralelos con Deferred
  - Uso de colecciones protoCore
  - Performance benchmarks

- [ ] Tests de compatibilidad:
  - Sintaxis ES2020+
  - Características modernas (async/await, optional chaining, etc.)

#### 1.6.3 Benchmarks

**Tareas:**
- [ ] Comparación con Node.js:
  - Operaciones con arrays grandes
  - String manipulation
  - Object creation/access
  - Concurrent operations (Deferred vs Promise)

- [ ] Demostrar ventajas de protoCore:
  - Memoria compartida entre threads (inmutables)
  - GC más eficiente
  - Menor overhead en operaciones concurrentes

**Archivos:**
- `tests/unit/` - Tests unitarios
- `tests/integration/` - Tests de integración
- `tests/benchmarks/` - Benchmarks
- `tests/demos/` - Scripts de demostración

### 1.7 Build System y Documentación

**Tareas:**
- [ ] **CMake:**
  - [x] Estructura básica
  - [ ] Tests integrados (CTest)
  - [ ] Opciones de build (debug/release)
  - [ ] Dependencias automáticas

- [ ] **Documentación:**
  - [ ] README.md con ejemplos
  - [ ] API documentation (doxygen o similar)
  - [ ] Guía de uso de Deferred
  - [ ] Guía de módulo protoCore
  - [ ] Arquitectura interna (para desarrolladores)

**Archivos:**
- `CMakeLists.txt` - Completar
- `README.md` - Crear/actualizar
- `docs/` - Documentación

---

## FASE 2: COMPATIBILIDAD BÁSICA CON NODE.JS

**Objetivo:** Ser un sustituto básico de Node.js para aplicaciones simples.

**Duración estimada:** 8-12 semanas

### 2.1 Módulos Core de Node.js

- [ ] `fs` - Sistema de archivos (básico)
- [ ] `path` - Manipulación de rutas
- [ ] `url` - URLs
- [ ] `http` - Servidor HTTP básico
- [ ] `events` - EventEmitter
- [ ] `stream` - Streams básicos
- [ ] `util` - Utilidades
- [ ] `crypto` - Criptografía básica

### 2.2 Sistema de Módulos

- [ ] CommonJS (`require`, `module.exports`)
- [ ] ES Modules (`import`/`export`)
- [ ] Resolución de módulos (node_modules)
- [ ] Ciclo de dependencias

### 2.3 npm Support Básico

- [ ] Instalación de paquetes
- [ ] Resolución de dependencias
- [ ] Ejecución de scripts de package.json

### 2.4 CLI Compatible

- [ ] Mismos flags que Node.js
- [ ] REPL básico
- [ ] --eval, --print, etc.

---

## FASE 3: SUSTITUTO COMPLETO DE NODE.JS

**Objetivo:** Reemplazo completo de Node.js para la mayoría de casos de uso.

**Duración estimada:** 6-12 meses

### 3.1 Módulos Avanzados

- [ ] `fs` completo (async, sync, streams)
- [ ] `net` - Networking
- [ ] `dgram` - UDP
- [ ] `child_process` - Procesos hijos
- [ ] `cluster` - Clustering
- [ ] `worker_threads` - Worker threads (usando nuestro sistema)
- [ ] `buffer` - Buffers
- [ ] `crypto` completo

### 3.2 Performance y Optimizaciones

- [ ] JIT compilation (opcional, usando QuickJS optimizations)
- [ ] Profiling tools
- [ ] Memory leak detection
- [ ] Performance monitoring

### 3.3 Compatibilidad Completa

- [ ] Test suite de Node.js (subset relevante)
- [ ] Compatibilidad con paquetes npm populares
- [ ] Debugging support (inspector protocol)

### 3.4 Características Avanzadas

- [ ] Hot reload
- [ ] Source maps
- [ ] TypeScript support (opcional)
- [ ] WebAssembly support

---

## FASE 4: OPTIMIZACIONES Y CARACTERÍSTICAS ÚNICAS

**Objetivo:** Diferenciadores avanzados y optimizaciones específicas.

### 4.1 Deferred Avanzado

- [ ] Auto-paralelización de loops
- [ ] Detección automática de trabajo paralelizable
- [ ] Scheduling inteligente
- [ ] Load balancing entre threads

### 4.2 Integración Profunda con protoCore

- [ ] Persistencia de objetos
- [ ] Serialización eficiente
- [ ] Sharing de memoria entre procesos
- [ ] Distributed computing support

### 4.3 Herramientas de Desarrollo

- [ ] Debugger integrado
- [ ] Profiler visual
- [ ] Memory analyzer
- [ ] Performance profiler

---

## Consideraciones Técnicas

### Mutabilidad en JavaScript

**Problema:** JavaScript asume objetos mutables por defecto, pero protoCore es inmutable por defecto.

**Solución:**
1. Objetos JS normales → ProtoObjects mutables (`mutable_ref > 0`)
2. Exponer API opcional para crear objetos inmutables:
   ```javascript
   const immutable = protoCore.ImmutableObject({a: 1});
   const mutable = protoCore.MutableObject({a: 1});
   ```
3. Internamente, detectar cuando un objeto debe ser mutable basado en uso
4. Arrays pueden ser ProtoList (inmutable) o ProtoSparseList (mutable) según necesidad

### Colecciones sin Equivalencia Directa

- **ProtoSet**: Similar a JS Set, pero con características especiales (inmutabilidad, hash-based)
- **ProtoMultiset**: No existe en JS → Nuevo módulo `protoCore.Multiset`
- **ProtoSparseList**: Similar a Array pero optimizado para sparse → `protoCore.SparseList`
- **ProtoTuple**: Array inmutable → `protoCore.Tuple`

### Worker Threads y Deferred

- Cada Deferred crea un ProtoThread
- Los objetos inmutables se comparten sin copia
- Los objetos mutables requieren sincronización
- El pool de threads se inicializa con un thread por núcleo CPU
- Scheduling inteligente para balancear carga

---

## Métricas de Éxito - Fase 1

1. ✅ Todos los tipos básicos JS funcionan usando protoCore
2. ✅ Deferred ejecuta código en worker threads transparentemente
3. ✅ Tests unitarios con >90% coverage
4. ✅ Benchmarks demuestran ventajas en operaciones concurrentes
5. ✅ Documentación completa y ejemplos funcionando
6. ✅ Build system robusto y fácil de usar

---

## Próximos Pasos Inmediatos

1. Completar TypeBridge (prioridad alta)
2. Implementar Deferred funcional (prioridad alta)
3. Crear estructura de tests
4. Implementar módulo protoCore
5. Escribir documentación básica

---

## Notas de Implementación

- **QuickJS**: Usar solo el parser y compilador. El runtime será completamente protoCore.
- **Compatibilidad**: No intentar compatibilidad hacia atrás. Solo ES2020+.
- **Performance**: Priorizar demostrar ventajas de protoCore sobre compatibilidad 100%.
- **Testing**: Tests exhaustivos son críticos para validar el concepto.
