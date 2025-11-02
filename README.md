# Low-Latency Market Data Feed Handler

High-performance ITCH 5.0 feed handler built in C++ for processing NASDAQ market data.

## Features (In Progress)

- [ ] ITCH 5.0 protocol parser
- [ ] Lock-free order book
- [ ] Sub-microsecond latency
- [ ] Historical replay mode
- [ ] Live UDP multicast support

## Building
```bash
mkdir build
cd build
cmake ..
make
```

## Usage
```bash
./feed_handler --file data/nasdaq_data.itch
```

## Performance Targets

- P99 latency: <500ns
- Throughput: 4M+ messages/sec

## Status

🚧 **Under active development**

