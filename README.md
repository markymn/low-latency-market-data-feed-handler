# High-Performance NASDAQ ITCH 5.0 Market Data Engine

## Overview

This project is a high-frequency trading (HFT) market data engine designed to process **NASDAQ TotalView-ITCH 5.0** data with ultra-low latency. It is built in C++ and leverages modern hardware-aware optimization techniques to achieve high throughput and minimal jitter.

The engine parses the raw binary ITCH 5.0 protocol, maintains full-depth real-time order books, and allows for efficient market data queries and trade validation.

## Key Features

-   **Protocol Support**: Full implementation of the NASDAQ ITCH 5.0 specification.
-   **High Throughput**: Capable of processing millions of messages per second on standard hardware.
-   **Low Latency**: Optimized for single-digit microsecond latencies using lock-free patterns and cache-aware structures.
-   **Cross-Platform**: Compiles and runs on both **Windows** (MSVC) and **Linux** (GCC/Clang).

## Strategies for High-Speed Processing

The engine employs several advanced optimization strategies common in HFT systems to minimize CPU cycles and memory overhead:

### 1. Zero-Copy Message Parsing
Instead of copying bytes into intermediate structures, the engine defines message structs that exactly match the wire format using `#pragma pack`. It creates a "view" over the raw network buffer using `reinterpret_cast`, allowing for instant access to message fields without deserialization overhead.
-   **Implementation**: `include/message_types.hpp`

### 2. Lock-Free Object Pooling
Heap allocation (`new`/`malloc`) is too slow and non-deterministic for the hot path. We use a custom `ObjectPool` to pre-allocate `Order` objects.
-   **Benefits**: Zero allocation cost during trading, better cache locality, and no memory fragmentation.
-   **Implementation**: `include/order_book.hpp` (Class `ObjectPool`)

### 3. Huge Page Allocation
The `ObjectPool` attempts to allocate memory using **Huge Pages** (2MB or 1GB pages) instead of standard 4KB pages.
-   **Strategy**: Uses `VirtualAlloc` with `MEM_LARGE_PAGES` on Windows or `mmap` with `MAP_HUGETLB` on Linux.
-   **Benefit**: drastically reduces Translation Lookaside Buffer (TLB) misses, which is critical for traversing large order books.

### 4. Cache-Conscious Data Structures
-   **Linear Probing Hash Map**: `OrderMap` uses open addressing with linear probing instead of the linked-list chaining used by `std::unordered_map`. This reduces pointer chasing and keeps data in the CPU cache.
-   **Flat Sorted Vectors**: Price levels are stored in a simple `std::vector`, keeping price headers contiguous in memory for efficient scanning by the hardware prefetcher.
-   **Cache Alignment**: Critical structures like `Order` are aligned to 64-byte cache lines (`ITCH_CACHE_ALIGNED`) to prevent false sharing and ensure clean cache line loads.

### 5. Compile-Time Polymorphism (CRTP)
Virtual function calls (`vtable` lookups) incur a small runtime penalty and inhibit compiler inlining. The `TemplateParser` uses the **Curiously Recurring Template Pattern (CRTP)** to dispatch events to the `FeedHandler` at compile time, eliminating virtual call overhead.

### 6. Branch Prediction Hints
Hot paths are annotated with `ITCH_LIKELY()` and `ITCH_UNLIKELY()` macros (wrapping `__builtin_expect` or equivalent). This guides the compiler to optimize the instruction layout for the "happy path" (e.g., assuming an order lookup will succeed), reducing pipeline stalls.

### 7. CPU Core Pinning
The engine supports pinning the processing thread to a specific isolated CPU core using `set_thread_affinity`. This prevents the operating system from migrating the thread between cores, preserving the L1/L2 cache state (cache warming).

## Architecture

*   **`FeedHandler`**: The main controller. Integrates the parser and the order book manager.
*   **`TemplateParser`**: A high-performance template-based parser for the ITCH 5.0 binary stream.
*   **`OrderBookManager`**: Manages a collection of `OrderBook` instances (one per stock symbol).
*   **`OrderBook`**: Maintains the BBO (Best Bid/Offer) and detailed depth for a single instrument.

## Building and Running

### Prerequisites
-   **CMake** (3.10+)
-   **C++ Compiler** (C++17 compliant: MSVC, GCC, or Clang)

### Build
```bash
mkdir build
cd build
cmake ..
cmake --build . --config Release
```

_Note: Always build in `Release` mode for performance benchmarking. Debug builds will be significantly slower due to assertions and lack of inlining._

### Run
The main executable includes a built-in benchmark generator:
```bash
./lowlatencymarketdataengine
```
This will run a series of examples, including a synthetic benchmark processing millions of orders to measure throughput and latency.
