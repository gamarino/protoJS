# Próximos Pasos: protoJS

**Fecha:** 2026-01-24

## Resumen de Estado Actual

✅ **Completado:**
- Arquitectura de virtual threads implementada
- ThreadPoolExecutor, CPUThreadPool, IOThreadPool funcionando
- EventLoop implementado
- Módulo I/O básico funcionando
- Deferred con estructura completa (necesita mejoras)
- Compilación sin errores

⚠️ **Parcial:**
- Deferred: Ejecuta código pero no la función real del usuario (problema de JSValue entre contextos)

## Tareas Inmediatas

### 1. Completar Deferred (Prioridad Alta)

**Problema:** JSValue no se puede compartir entre contextos de QuickJS.

**Solución Recomendada (Opción B):**
- Función JS se ejecuta en thread principal
- Detecta trabajo CPU-intensivo (loops, cálculos)
- Delega trabajo pesado a protoCore en worker thread
- Retorna resultado a función JS

**Implementación:**
```cpp
// En executeTask:
// 1. Analizar función JS para detectar trabajo CPU-intensivo
// 2. Extraer trabajo a función protoCore
// 3. Ejecutar función protoCore en worker thread
// 4. Retornar resultado a función JS
```

**Archivos a modificar:**
- `src/Deferred.cpp` - Implementar Opción B
- `src/Deferred.h` - Actualizar estructura si necesario

### 2. Tests Unitarios (Prioridad Alta)

**Crear tests para:**
- ThreadPoolExecutor
- CPUThreadPool
- IOThreadPool  
- EventLoop
- Deferred (cuando esté completo)

**Framework:** Catch2 o Google Test

**Archivos:**
- `tests/unit/test_thread_pool_executor.cpp`
- `tests/unit/test_cpu_thread_pool.cpp`
- `tests/unit/test_io_thread_pool.cpp`
- `tests/unit/test_event_loop.cpp`
- `tests/unit/test_deferred.cpp`

### 3. Tests de Integración (Prioridad Media)

**Scripts JavaScript:**
- Test de múltiples Deferred concurrentes
- Test de separación CPU/I/O
- Test de escalabilidad (muchos Deferred)

**Archivos:**
- `tests/integration/test_concurrent_deferred.js`
- `tests/integration/test_io_separation.js`
- `tests/integration/test_scalability.js`

### 4. Mejorar Manejo de Resultados (Prioridad Media)

**Soporte para:**
- Objetos complejos
- Arrays
- Funciones
- Serialización adecuada

**Archivos:**
- `src/Deferred.cpp` - Mejorar conversión de resultados

### 5. Documentación (Prioridad Media)

**Crear:**
- Ejemplos de uso de Deferred
- Guía de configuración de pools
- API reference
- Troubleshooting guide

**Archivos:**
- `docs/DEFERRED_USAGE.md`
- `docs/THREAD_POOLS.md`
- `docs/API_REFERENCE.md`

## Plan de Implementación Sugerido

### Semana 1: Completar Deferred
1. Implementar Opción B (ejecutar en main, trabajo en protoCore)
2. Tests básicos de Deferred
3. Documentación básica

### Semana 2: Tests y Mejoras
1. Tests unitarios completos
2. Tests de integración
3. Mejorar manejo de resultados

### Semana 3: Optimizaciones y Documentación
1. Optimizaciones de performance
2. Documentación completa
3. Ejemplos avanzados

## Notas Técnicas

### JSValue Sharing Issue

**Problema actual:**
- QuickJS no permite compartir JSValue entre contextos
- Necesitamos ejecutar función del usuario en worker thread

**Soluciones consideradas:**
1. Serialización a string (limitada)
2. Ejecutar en main, trabajo en protoCore (recomendada)
3. Bytecode serialization (compleja)

**Decisión:** Opción 2 para Fase 1, evaluar Opción 3 para fases futuras.

### Thread Safety

**Consideraciones:**
- QuickJS contexts no son thread-safe
- Cada worker thread necesita su propio JSContext
- Resultados deben serializarse para pasar entre threads
- Event loop centraliza callbacks en thread principal

## Recursos

- [PROGRESS_UPDATE.md](PROGRESS_UPDATE.md) - Estado detallado
- [IMPLEMENTATION_STATUS.md](IMPLEMENTATION_STATUS.md) - Estado de implementación
- [ARCHITECTURE.md](ARCHITECTURE.md) - Arquitectura técnica
- [PLAN.md](PLAN.md) - Plan completo del proyecto
