# Arquitectura Técnica: protoJS

**Versión:** 1.0  
**Fecha:** 2026-01-24

---

## Visión General de la Arquitectura

```
┌─────────────────────────────────────────────────────────────┐
│                    JavaScript Code                           │
│              (ES2020+ Modern JavaScript)                     │
└──────────────────────┬──────────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────────┐
│                    QuickJS Parser                            │
│              (quickjs.c - Solo parser/compiler)            │
└──────────────────────┬──────────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────────┐
│              protoJS Runtime Layer                           │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐    │
│  │ TypeBridge   │  │ Execution    │  │ GC Bridge    │    │
│  │              │  │ Engine       │  │              │    │
│  └──────────────┘  └──────────────┘  └──────────────┘    │
└──────────────────────┬──────────────────────────────────────┘
                       │
                       ▼
┌─────────────────────────────────────────────────────────────┐
│                    protoCore Runtime                        │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐    │
│  │ ProtoSpace   │  │ ProtoContext  │  │ ProtoThread  │    │
│  │ (GC, Memory)  │  │ (Execution)   │  │ (Concurrency)│    │
│  └──────────────┘  └──────────────┘  └──────────────┘    │
│  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐    │
│  │ ProtoObject  │  │ Collections   │  │ Immutability │    │
│  │ (Prototypes) │  │ (List, Set...)│  │ (Structural) │    │
│  └──────────────┘  └──────────────┘  └──────────────┘    │
└─────────────────────────────────────────────────────────────┘
```

---

## Componentes Principales

### 1. JSContextWrapper

**Responsabilidad:** Gestionar el ciclo de vida de QuickJS JSContext y su integración con protoCore.

**Estructura:**
```cpp
class JSContextWrapper {
    JSRuntime* rt;              // QuickJS runtime (solo para parser)
    JSContext* ctx;              // QuickJS context
    proto::ProtoSpace pSpace;    // protoCore space (GC, memoria)
    proto::ProtoContext* pContext; // protoCore execution context
};
```

**Decisiones de diseño:**
- Un `ProtoSpace` por `JSRuntime` (comparte GC)
- Un `ProtoContext` por `JSContext` (aislamiento de ejecución)
- QuickJS runtime se usa solo para parsing/compiling, no para ejecución real

### 2. TypeBridge

**Responsabilidad:** Conversión bidireccional entre tipos JavaScript (QuickJS) y tipos protoCore.

**Mapeo de Tipos:**

| JavaScript Type | protoCore Type | Notas |
|----------------|----------------|-------|
| `null` / `undefined` | `PROTO_NONE` | |
| `boolean` | `SmallInteger` (tagged) | Usa constantes PROTO_TRUE/PROTO_FALSE |
| `number` (entero pequeño) | `SmallInteger` | Si cabe en 54 bits |
| `number` (entero grande) | `LargeInteger` | |
| `number` (float) | `Double` | |
| `bigint` | `LargeInteger` | |
| `string` | `ProtoString` | UTF-8 ↔ UTF-16 conversion |
| `array` (denso) | `ProtoList` | Inmutable por defecto |
| `array` (sparse) | `ProtoSparseList` | Mutable, optimizado |
| `object` | `ProtoObject` | Con `mutable_ref` para mutabilidad |
| `function` | `ProtoMethod` | Wrapped en ProtoMethodCell |
| `date` | `Date` (protoCore) | |
| `regexp` | `ProtoObject` + libregexp | |
| `map` | `ProtoSparseList` | Con wrapper JS |
| `set` | `ProtoSet` | Con wrapper JS |
| `typedarray` | `ProtoByteBuffer` | |
| `arraybuffer` | `ProtoByteBuffer` | |
| `promise` | `Deferred` (custom) | Nuestra implementación |

**Conversión JS → protoCore:**

```cpp
const proto::ProtoObject* TypeBridge::fromJS(
    JSContext* ctx, 
    JSValue val, 
    proto::ProtoContext* pContext
) {
    // 1. Detectar tipo QuickJS
    // 2. Convertir a tipo protoCore equivalente
    // 3. Manejar mutabilidad según contexto
    // 4. Retornar ProtoObject*
}
```

**Conversión protoCore → JS:**

```cpp
JSValue TypeBridge::toJS(
    JSContext* ctx,
    const proto::ProtoObject* obj,
    proto::ProtoContext* pContext
) {
    // 1. Detectar tipo protoCore
    // 2. Convertir a JSValue equivalente
    // 3. Crear wrapper si es necesario (Set, Multiset, etc.)
    // 4. Retornar JSValue
}
```

**Consideraciones especiales:**

1. **Mutabilidad:**
   - Objetos JS normales → `mutable_ref > 0` en protoCore
   - Arrays densos pequeños → `ProtoList` (inmutable)
   - Arrays sparse o grandes → `ProtoSparseList` (mutable)
   - Usuario puede forzar con `protoCore.ImmutableObject()` o `protoCore.MutableObject()`

2. **Strings:**
   - QuickJS usa UTF-8 internamente
   - protoCore ProtoString es UTF-8 también
   - Conversión directa, pero manejar casos edge (surrogate pairs)

3. **Functions:**
   - QuickJS bytecode se mantiene
   - Ejecución se hace en contexto protoCore
   - Closures usan `closureLocals` de ProtoContext

### 3. Execution Engine

**Responsabilidad:** Ejecutar bytecode de QuickJS en contexto protoCore.

**Flujo de ejecución:**

```
1. QuickJS compila JS → bytecode
2. ExecutionEngine intercepta ejecución
3. Para cada operación:
   a. Convierte operandos JS → protoCore
   b. Ejecuta operación en protoCore
   c. Convierte resultado protoCore → JS
4. Retorna resultado a QuickJS (para compatibilidad)
```

**Operaciones interceptadas:**

- Creación de objetos
- Acceso a propiedades
- Llamadas a funciones
- Operaciones aritméticas
- Operaciones de colecciones

**Implementación:**

```cpp
class ExecutionEngine {
    // Interceptar operaciones QuickJS
    static JSValue op_get_property(JSContext* ctx, JSValue obj, JSAtom prop);
    static JSValue op_set_property(JSContext* ctx, JSValue obj, JSAtom prop, JSValue val);
    static JSValue op_call(JSContext* ctx, JSValue func, JSValue this_val, int argc, JSValueConst* argv);
    // ... más operaciones
};
```

### 4. Deferred Implementation (Virtual Threads Model)

**Responsabilidad:** Implementar Promises que ejecutan como tareas ligeras en el pool de CPU, similar a virtual threads de Java.

**Arquitectura:**

```
┌─────────────────────────────────────────┐
│         Main Thread (JSContext)          │
│  - Ejecuta código JS síncrono           │
│  - Event loop para callbacks            │
└──────────────┬──────────────────────────┘
               │
    ┌──────────┼──────────┐
    │          │          │
    ▼          ▼          ▼
┌─────────┐ ┌─────────┐ ┌─────────┐
│ CPU Pool│ │ I/O Pool│ │  GC     │
│         │ │         │ │ Thread  │
│ Thread1 │ │ Thread1 │ │         │
│ Thread2 │ │ Thread2 │ └─────────┘
│ ...     │ │ ...     │
│ ThreadN │ │ ThreadM │
│ (N=CPUs)│ │(M=3-4*N)│
└────┬────┘ └────┬────┘
     │          │
     │          │
     ▼          ▼
┌─────────────────────────┐
│   Tareas Ligeras         │
│  ┌─────────┐ ┌─────────┐│
│  │Deferred1│ │Deferred2││
│  │(Context)│ │(Context)││
│  └─────────┘ └─────────┘│
│  ┌─────────┐ ┌─────────┐│
│  │IO Task1 │ │IO Task2 ││
│  │(Context)│ │(Context)││
│  └─────────┘ └─────────┘│
└─────────────────────────┘
     │          │
     ▼          ▼
┌─────────────────────────┐
│      protoCore          │
│  - ProtoSpace (shared)   │
│  - Objetos inmutables    │
│  - GC concurrente        │
└─────────────────────────┘
```

**Flujo de ejecución:**

1. Usuario crea `new Deferred(fn)`
2. Se crea `DeferredTask` (tarea ligera, no un ProtoThread completo)
3. Se encola en `CPUThreadPool`
4. Thread del pool disponible ejecuta la tarea
5. Cada tarea crea su propio `ProtoContext` aislado pero comparte el thread del SO
6. Si `fn` accede a objetos inmutables, los comparte sin copia (mismo ProtoSpace)
7. Si `fn` accede a objetos mutables, usa sincronización vía `mutableRoot`
8. Cuando `fn` llama `resolve(value)`, se encola callback en EventLoop
9. EventLoop ejecuta callbacks en thread principal

**Implementación:**

```cpp
class Deferred {
    // Lightweight task structure (not a full ProtoThread)
    struct DeferredTask {
        JSValue func;
        JSValue resolve;
        JSValue reject;
        JSContext* jsContext;
        proto::ProtoSpace* space;
        JSContextWrapper* wrapper;
    };
    
    static void executeTask(std::shared_ptr<DeferredTask> task) {
        // Execute in CPU pool thread
        // Create isolated ProtoContext
        // Execute JS function
        // Schedule callback on event loop
    }
};
```

**Ventajas del modelo Virtual Threads:**

- **Eficiencia**: Múltiples Deferred pueden ejecutarse en el mismo thread del SO
- **Escalabilidad**: Puede manejar millones de Deferred sin crear millones de threads
- **Separación CPU/I/O**: Pool de CPU optimizado para trabajo computacional, pool de I/O para operaciones bloqueantes
- **Compartir objetos inmutables**: Múltiples tareas en el mismo thread pueden compartir objetos inmutables sin overhead

### 5. GC Bridge

**Responsabilidad:** Integrar el Garbage Collector de protoCore con objetos QuickJS.

**Problema:**
- QuickJS tiene su propio GC
- protoCore tiene su propio GC
- Necesitamos unificar para evitar memory leaks

**Solución:**

1. **JSValue como GC Root:**
   - Cada JSValue activo se registra como root en protoCore GC
   - Cuando JSValue se libera, se desregistra

2. **ProtoObject → JSValue mapping:**
   - Mantener mapa bidireccional
   - Cuando protoCore GC recolecta, liberar JSValue correspondiente

3. **Weak References:**
   - Usar mecanismos de protoCore para weak refs
   - Implementar WeakMap/WeakSet usando esto

**Implementación:**

```cpp
class GCBridge {
    // Mapa JSValue <-> ProtoObject
    static std::unordered_map<JSValue, const proto::ProtoObject*> jsToProto;
    static std::unordered_map<const proto::ProtoObject*, JSValue> protoToJS;
    
    // Registrar root para GC
    static void registerRoot(JSValue jsVal, const proto::ProtoObject* protoObj);
    
    // Desregistrar root
    static void unregisterRoot(JSValue jsVal);
    
    // Callback para GC de protoCore
    static void onProtoObjectCollected(const proto::ProtoObject* obj);
};
```

### 6. Módulo protoCore

**Responsabilidad:** Exponer características únicas de protoCore a JavaScript.

**API JavaScript:**

```javascript
// Colecciones especiales
const set = new protoCore.Set([1, 2, 3]);
const multiset = new protoCore.Multiset([1, 1, 2, 3]);
const sparseList = new protoCore.SparseList();
const tuple = protoCore.Tuple([1, 2, 3]); // Inmutable

// Control de mutabilidad
const immutable = protoCore.ImmutableObject({a: 1});
const mutable = protoCore.MutableObject({a: 1});

// Utilidades
protoCore.isImmutable(obj);
protoCore.makeImmutable(obj);
protoCore.makeMutable(obj);

// Información del runtime
protoCore.getThreadCount(); // Número de threads disponibles
protoCore.getGCMemory();    // Memoria usada
```

**Implementación:**

Cada colección especial tiene un wrapper que:
1. Mantiene referencia al objeto protoCore interno
2. Expone métodos JavaScript equivalentes
3. Convierte entre JS y protoCore en cada operación

---

## Flujo de Ejecución Completo

### Ejecución de un Script Simple

```
1. Usuario ejecuta: protojs script.js

2. main.cpp:
   - Crea JSContextWrapper
   - Lee script.js
   - Llama wrapper.eval(code, "script.js")

3. JSContextWrapper::eval():
   - QuickJS parsea y compila código → bytecode
   - ExecutionEngine intercepta ejecución

4. Para cada operación en bytecode:
   a. ExecutionEngine convierte operandos JS → protoCore
   b. Ejecuta operación usando protoCore
   c. Convierte resultado protoCore → JS
   d. Retorna a QuickJS

5. Cuando script termina:
   - GC de protoCore puede recolectar objetos no usados
   - JSContextWrapper se destruye
   - Todo se limpia
```

### Ejecución con Deferred

```
1. Usuario crea: new Deferred(fn)

2. Deferred::constructor():
   - Analiza fn para determinar si es paralelizable
   - Crea DeferredData
   - Encola en WorkerPool

3. WorkerPool:
   - Asigna a worker thread disponible
   - Crea nuevo ProtoThread
   - Crea nuevo ProtoContext (aislado)

4. Worker thread ejecuta fn:
   - Convierte parámetros JS → protoCore
   - Ejecuta fn en contexto aislado
   - Comparte objetos inmutables sin copia
   - Sincroniza acceso a objetos mutables

5. Cuando fn llama resolve(value):
   - Convierte value protoCore → JS
   - Notifica thread principal
   - Ejecuta callbacks .then() en thread principal

6. Thread principal:
   - Recibe notificación
   - Ejecuta callbacks en su contexto
   - Limpia recursos del worker thread
```

---

## Decisiones de Diseño Clave

### 1. ¿Por qué mantener QuickJS runtime?

**Decisión:** Mantener QuickJS solo para parsing/compiling, no para ejecución.

**Razón:**
- QuickJS tiene un parser excelente y bien probado
- No queremos reimplementar el parser
- Pero queremos usar protoCore para todo lo demás (objetos, memoria, GC)

**Implementación:**
- Interceptar todas las operaciones de QuickJS
- Redirigir a protoCore
- QuickJS runtime se usa como "shell" pero no ejecuta realmente

### 2. ¿Cómo manejar mutabilidad?

**Decisión:** Objetos JS normales son mutables en protoCore, pero exponer API para inmutables.

**Razón:**
- Compatibilidad con JavaScript (objetos son mutables por defecto)
- Pero queremos aprovechar inmutabilidad de protoCore cuando sea posible
- Usuario puede elegir explícitamente

**Implementación:**
- Objetos JS → `mutable_ref > 0` en protoCore
- Arrays pequeños/densos → `ProtoList` (inmutable, eficiente)
- Arrays sparse/grandes → `ProtoSparseList` (mutable)
- API `protoCore.ImmutableObject()` para forzar inmutabilidad

### 3. ¿Cómo compartir objetos entre threads?

**Decisión:** Objetos inmutables se comparten sin copia, mutables requieren sincronización.

**Razón:**
- Ventaja clave de protoCore: inmutabilidad permite sharing seguro
- Objetos mutables necesitan locks o atomic operations

**Implementación:**
- Deferred detecta qué objetos son inmutables
- Comparte punteros directamente (seguro porque son inmutables)
- Objetos mutables usan `mutableRoot` de protoCore (thread-safe)

### 4. ¿Cómo manejar el GC?

**Decisión:** Unificar GC usando protoCore, registrar JSValues como roots.

**Razón:**
- Evitar dos GCs compitiendo
- Aprovechar GC eficiente de protoCore
- Simplificar gestión de memoria

**Implementación:**
- GCBridge mantiene mapa JSValue ↔ ProtoObject
- JSValues activos son roots en protoCore GC
- Cuando protoCore GC recolecta, libera JSValue correspondiente

---

## Estructura de Directorios Propuesta

```
protoJS/
├── src/
│   ├── main.cpp                 # Entry point
│   ├── JSContext.h/cpp           # Wrapper QuickJS + protoCore
│   ├── TypeBridge.h/cpp          # Conversión de tipos
│   ├── ExecutionEngine.h/cpp     # Motor de ejecución
│   ├── GCBridge.h/cpp            # Integración GC
│   ├── Deferred.h/cpp            # Deferred implementation
│   ├── WorkerPool.h/cpp          # Pool de worker threads
│   ├── ThreadManager.h/cpp      # Gestión de threads
│   ├── console.h/cpp             # Módulo console
│   ├── types/                    # Bridges para tipos específicos
│   │   ├── NumberBridge.h/cpp
│   │   ├── StringBridge.h/cpp
│   │   ├── ArrayBridge.h/cpp
│   │   ├── ObjectBridge.h/cpp
│   │   └── FunctionBridge.h/cpp
│   └── modules/                  # Módulos JavaScript
│       ├── ProtoCoreModule.h/cpp
│       └── ProcessModule.h/cpp
├── tests/
│   ├── unit/                     # Tests unitarios
│   ├── integration/              # Tests de integración
│   ├── benchmarks/               # Benchmarks
│   └── demos/                    # Scripts de demostración
├── deps/
│   └── quickjs/                  # QuickJS (submodule o copia)
├── docs/                         # Documentación
├── CMakeLists.txt
├── README.md
├── PLAN.md                       # Este documento
└── ARCHITECTURE.md               # Este documento
```

---

## Consideraciones de Performance

### Optimizaciones Clave

1. **Tagged Pointers:**
   - Números pequeños y booleans no se alocan
   - Conversión directa sin overhead

2. **Structural Sharing:**
   - Operaciones en arrays/strings no copian datos
   - Compartir entre threads es gratis para inmutables

3. **Per-Thread Allocation:**
   - Cada thread aloca desde su arena local
   - Sin locks en la mayoría de casos

4. **Inline Caching:**
   - Cache de lookups de propiedades
   - Reducir traversals de prototype chain

### Benchmarks Objetivo (Fase 1)

- **Array operations:** 2-5x más rápido que Node.js para operaciones inmutables
- **String operations:** Similar o mejor que Node.js
- **Concurrent operations:** 5-10x más rápido con Deferred vs Promise (en multi-core)
- **Memory usage:** Similar o mejor que Node.js (gracias a structural sharing)

---

## Riesgos y Mitigaciones

### Riesgo 1: Complejidad de integración QuickJS + protoCore

**Mitigación:**
- Empezar simple, solo interceptar operaciones básicas
- Agregar complejidad gradualmente
- Tests exhaustivos en cada paso

### Riesgo 2: Performance peor que Node.js inicialmente

**Mitigación:**
- Esperado en Fase 1 (demostrador)
- Enfocarse en casos donde protoCore brilla (concurrencia, inmutabilidad)
- Optimizar iterativamente

### Riesgo 3: Bugs en conversión de tipos

**Mitigación:**
- Tests exhaustivos para cada tipo
- Edge cases documentados
- Fuzzing para casos raros

### Riesgo 4: GC pauses largos

**Mitigación:**
- Usar GC concurrente de protoCore
- Tuning de parámetros GC
- Monitoring y profiling

---

## Próximos Pasos Técnicos

1. **Completar TypeBridge:**
   - Implementar todas las conversiones
   - Tests para cada tipo
   - Edge cases

2. **Implementar ExecutionEngine:**
   - Interceptar operaciones básicas
   - Delegar a protoCore
   - Tests de ejecución

3. **Completar Deferred:**
   - Worker pool
   - Thread management
   - Tests de concurrencia

4. **Integrar GC:**
   - GCBridge completo
   - Tests de memory management
   - Leak detection

5. **Módulos básicos:**
   - protoCore module
   - process module
   - Tests de módulos
