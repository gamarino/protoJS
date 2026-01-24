# Thread Pool Configuration

## Architecture

protoJS uses two separate thread pools:

- **CPU Thread Pool**: For CPU-intensive work (Deferred, calculations)
- **I/O Thread Pool**: For blocking I/O operations (files, network)

## Default Configuration

- **CPU Threads**: Number of system CPUs (auto-detected)
- **I/O Threads**: 3-4x number of CPUs (configurable)

## Manual Configuration

### Command Line

```bash
# Specify number of CPU threads
protojs --cpu-threads 8 script.js

# Specify number of I/O threads
protojs --io-threads 24 script.js

# Specify factor for I/O threads
protojs --io-threads-factor 4.0 script.js

# Combine options
protojs --cpu-threads 8 --io-threads-factor 3.5 script.js
```

### Automatic Detection

If no options are specified:
- CPU threads = `std::thread::hardware_concurrency()`
- I/O threads = CPU threads × 3.0

## Examples

### System with 4 CPUs

```bash
# Default:
# - CPU pool: 4 threads
# - I/O pool: 12 threads (4 × 3)

# Custom:
protojs --cpu-threads 8 --io-threads 16 script.js
```

### System with 16 CPUs

```bash
# Default:
# - CPU pool: 16 threads
# - I/O pool: 48 threads (16 × 3)

# For I/O-intensive applications:
protojs --io-threads-factor 5.0 script.js
# I/O pool: 80 threads (16 × 5)
```

## Considerations

- **CPU Pool**: More threads is not always better (context switching overhead)
- **I/O Pool**: More threads helps with many concurrent I/O operations
- **I/O Factor**: Adjust according to application type (I/O intensive vs CPU intensive)

## Monitoring

Future versions will include:
- Thread usage metrics
- Queue size statistics
- Configuration recommendations
