# Phase 3: Logging and Monitoring System Design

**Priority:** High  
**Timeline:** Month 5, Week 3-4  
**Dependencies:** Error handling system

---

## Overview

The Logging and Monitoring System provides structured logging, performance metrics collection, and system monitoring for protoJS. It enables production monitoring and debugging capabilities.

---

## Architecture

### Design Principles

1. **Structured Logging:** JSON-formatted logs with context
2. **Performance Metrics:** Collection of performance data
3. **Low Overhead:** Minimal performance impact
4. **Extensibility:** Support for custom metrics and loggers

### Component Structure

```
Logging & Monitoring
├── Logger (structured logging)
├── Metrics (performance metrics)
└── Exporters (JSON, Prometheus, etc.)
```

---

## Logging System

### File Structure

```
src/logging/
├── Logger.h
├── Logger.cpp
├── LogLevel.h
├── LogFormatter.h
└── LogOutput.h
```

### Log Levels

**Levels:**
- `DEBUG`: Detailed debugging information
- `INFO`: General informational messages
- `WARN`: Warning messages
- `ERROR`: Error messages

### Logger API

```cpp
class Logger {
public:
    enum class Level {
        DEBUG,
        INFO,
        WARN,
        ERROR
    };
    
    // Logging methods
    static void debug(const std::string& message, const std::map<std::string, std::string>& context = {});
    static void info(const std::string& message, const std::map<std::string, std::string>& context = {});
    static void warn(const std::string& message, const std::map<std::string, std::string>& context = {});
    static void error(const std::string& message, const std::map<std::string, std::string>& context = {});
    
    // Configuration
    static void setLevel(Level level);
    static void setOutput(std::ostream* output);
    static void setFormatter(std::function<std::string(Level, const std::string&, const std::map<std::string, std::string>&)> formatter);
    
    // Context management
    static void pushContext(const std::string& key, const std::string& value);
    static void popContext(const std::string& key);
};
```

### Log Format

**Structured Format (JSON):**
```json
{
  "timestamp": "2026-01-24T10:30:45.123Z",
  "level": "INFO",
  "message": "Module loaded",
  "context": {
    "module": "fs",
    "file": "/path/to/module.js",
    "duration": "12.5ms"
  }
}
```

**Text Format:**
```
[2026-01-24 10:30:45.123] INFO Module loaded module=fs file=/path/to/module.js duration=12.5ms
```

### Log Output

**Output Targets:**
- Console (stdout/stderr)
- File (with rotation)
- Network (syslog, etc.)
- Custom output handlers

**Implementation:**
```cpp
class LogOutput {
public:
    virtual void write(const std::string& log) = 0;
};

class ConsoleLogOutput : public LogOutput {
    void write(const std::string& log) override {
        std::cout << log << std::endl;
    }
};

class FileLogOutput : public LogOutput {
    std::ofstream file;
    void write(const std::string& log) override {
        file << log << std::endl;
    }
};
```

---

## Monitoring System

### File Structure

```
src/monitoring/
├── Metrics.h
├── Metrics.cpp
├── Counter.h
├── Gauge.h
├── Histogram.h
└── Exporter.h
```

### Metrics Types

**Counter:**
- Incrementing metric
- Use for: request count, error count, etc.

**Gauge:**
- Value that can go up or down
- Use for: memory usage, active connections, etc.

**Histogram:**
- Distribution of values
- Use for: response times, request sizes, etc.

### Metrics API

```cpp
class Metrics {
public:
    // Counter
    static void incrementCounter(const std::string& name, double value = 1.0, const std::map<std::string, std::string>& labels = {});
    static double getCounter(const std::string& name, const std::map<std::string, std::string>& labels = {});
    
    // Gauge
    static void setGauge(const std::string& name, double value, const std::map<std::string, std::string>& labels = {});
    static void addGauge(const std::string& name, double value, const std::map<std::string, std::string>& labels = {});
    static double getGauge(const std::string& name, const std::map<std::string, std::string>& labels = {});
    
    // Histogram
    static void recordHistogram(const std::string& name, double value, const std::map<std::string, std::string>& labels = {});
    static HistogramStats getHistogram(const std::string& name, const std::map<std::string, std::string>& labels = {});
    
    // Snapshot
    struct MetricsSnapshot {
        std::map<std::string, double> counters;
        std::map<std::string, double> gauges;
        std::map<std::string, HistogramStats> histograms;
    };
    static MetricsSnapshot getSnapshot();
    
    // Export
    static std::string exportJSON();
    static std::string exportPrometheus();
};
```

### Built-in Metrics

**System Metrics:**
- `protojs.memory.used`: Memory usage in bytes
- `protojs.memory.allocated`: Total allocated memory
- `protojs.gc.cycles`: Number of GC cycles
- `protojs.gc.duration`: GC duration in milliseconds

**Thread Pool Metrics:**
- `protojs.threadpool.cpu.active`: Active CPU threads
- `protojs.threadpool.cpu.queue`: Queue size
- `protojs.threadpool.io.active`: Active I/O threads
- `protojs.threadpool.io.queue`: Queue size

**Module Metrics:**
- `protojs.modules.loaded`: Number of loaded modules
- `protojs.modules.load_time`: Module load time histogram
- `protojs.modules.cache_hits`: Module cache hits
- `protojs.modules.cache_misses`: Module cache misses

**Performance Metrics:**
- `protojs.performance.typebridge_conversions`: TypeBridge conversion count
- `protojs.performance.typebridge_time`: TypeBridge conversion time histogram
- `protojs.performance.eventloop_latency`: Event loop callback latency

---

## Implementation Details

### Logger Implementation

**Logging Flow:**
1. Log call with message and context
2. Check log level
3. Format log entry
4. Add timestamp and context
5. Write to output

**Thread Safety:**
- Thread-safe logging
- Lock-free where possible
- Minimal contention

### Metrics Implementation

**Metrics Storage:**
```cpp
struct MetricEntry {
    std::string name;
    std::map<std::string, std::string> labels;
    double value;
    std::chrono::time_point<std::chrono::steady_clock> timestamp;
};

class MetricsStore {
    std::unordered_map<std::string, std::vector<MetricEntry>> counters;
    std::unordered_map<std::string, std::vector<MetricEntry>> gauges;
    std::unordered_map<std::string, std::vector<HistogramEntry>> histograms;
    std::mutex mutex;
};
```

**Histogram Implementation:**
```cpp
struct HistogramEntry {
    std::vector<double> buckets;
    std::vector<size_t> counts;
    double sum;
    size_t count;
};

void Metrics::recordHistogram(const std::string& name, double value, const std::map<std::string, std::string>& labels) {
    // Find or create histogram
    // Add value to appropriate bucket
    // Update statistics
}
```

---

## Export Formats

### JSON Export

**Format:**
```json
{
  "counters": {
    "protojs.requests.total": 1234
  },
  "gauges": {
    "protojs.memory.used": 1048576
  },
  "histograms": {
    "protojs.response_time": {
      "buckets": [0.1, 0.5, 1.0, 5.0],
      "counts": [100, 50, 20, 10],
      "sum": 123.45,
      "count": 180
    }
  }
}
```

### Prometheus Export

**Format:**
```
# TYPE protojs_requests_total counter
protojs_requests_total 1234

# TYPE protojs_memory_used gauge
protojs_memory_used 1048576

# TYPE protojs_response_time histogram
protojs_response_time_bucket{le="0.1"} 100
protojs_response_time_bucket{le="0.5"} 150
protojs_response_time_bucket{le="1.0"} 170
protojs_response_time_bucket{le="5.0"} 180
protojs_response_time_sum 123.45
protojs_response_time_count 180
```

---

## Integration Points

### Module Integration

**FS Module:**
```cpp
void FSModule::readFile(JSContext* ctx, ...) {
    auto start = std::chrono::steady_clock::now();
    Metrics::incrementCounter("protojs.fs.operations", 1, {{"operation", "readFile"}});
    
    try {
        // File operation
        auto duration = std::chrono::steady_clock::now() - start;
        Metrics::recordHistogram("protojs.fs.duration", 
                                 std::chrono::duration<double, std::milli>(duration).count());
    } catch (...) {
        Metrics::incrementCounter("protojs.fs.errors", 1, {{"operation", "readFile"}});
        throw;
    }
}
```

### Event Loop Integration

**Event Loop Metrics:**
```cpp
void EventLoop::processCallbacks() {
    auto start = std::chrono::steady_clock::now();
    
    // Process callbacks
    
    auto duration = std::chrono::steady_clock::now() - start;
    Metrics::recordHistogram("protojs.eventloop.latency", 
                            std::chrono::duration<double, std::milli>(duration).count());
}
```

### Thread Pool Integration

**Thread Pool Metrics:**
```cpp
void ThreadPoolExecutor::submitTask(...) {
    Metrics::setGauge("protojs.threadpool.queue_size", getQueueSize());
    Metrics::setGauge("protojs.threadpool.active_threads", getActiveCount());
}
```

---

## Configuration

### Logger Configuration

**Environment Variables:**
- `PROTOJS_LOG_LEVEL`: Log level (DEBUG, INFO, WARN, ERROR)
- `PROTOJS_LOG_FORMAT`: Log format (json, text)
- `PROTOJS_LOG_FILE`: Log file path

**API Configuration:**
```javascript
// JavaScript API
process.env.PROTOJS_LOG_LEVEL = 'INFO';
process.env.PROTOJS_LOG_FORMAT = 'json';
```

### Metrics Configuration

**Environment Variables:**
- `PROTOJS_METRICS_ENABLED`: Enable metrics (true/false)
- `PROTOJS_METRICS_EXPORT_INTERVAL`: Export interval in seconds
- `PROTOJS_METRICS_EXPORT_FORMAT`: Export format (json, prometheus)

---

## Testing Strategy

### Unit Tests
- Logging at all levels
- Metrics collection
- Export formats
- Configuration

### Integration Tests
- Logging in modules
- Metrics in modules
- Export functionality
- Performance impact

---

## Dependencies

- **Error Handling:** Error logging
- **All Modules:** Logging and metrics integration

---

## Success Criteria

1. ✅ Structured logging at all levels
2. ✅ Performance metrics collection
3. ✅ Export to JSON and Prometheus
4. ✅ Low performance overhead (<2%)
5. ✅ Integration with all modules
6. ✅ Configurable log levels and formats

---

## Implementation Order

1. **Week 3:**
   - Logger implementation
   - Log levels and formatting
   - Log output handlers

2. **Week 4:**
   - Metrics implementation
   - Built-in metrics
   - Export formats
   - Integration with modules
   - Testing and optimization

---

## Notes

- Logging and monitoring essential for production
- Low overhead is critical
- Structured logging enables log analysis
- Metrics enable performance monitoring
- Export formats enable integration with monitoring systems
