# EVENT HORIZON

A deterministic C++20 simulation for studying how exchange disruptions and delayed payments can interrupt trading across connected firms.

## The problem

A trade is not the same as a payment. Imagine NOVA buying shares from BEACON just before payment processing stops. The exchange records the trade, but BEACON cannot spend the proceeds until settlement. If BEACON planned to buy from CIRRUS, that next order may fail; CIRRUS can then face the same shortage. EVENT HORIZON makes this chain visible without real money, market feeds, or brokerage connections.

The application runs fictional incidents through a single simulation engine. You can replay events, inspect the trade ledger, compare recovery choices, and edit a scenario. Its results are deterministic illustrations, **not** forecasts or measured financial risk.

## Capabilities

- **Exchange:** Limit orders with resting-price execution, price-time priority, partial fills, cancellations, and self-trade prevention.
- **Clearing:** Reserved funds and shares, escrowed obligations, FIFO settlement, payment outages, and capacity limits.
- **Recovery:** A pre-funded lending facility, price bands, rule-based participants, nine scenarios, and twelve controlled recovery comparisons.
- **Investigation:** A responsive dashboard, historical participant balances, a trade ledger, custom CSV replay, and JSON exports.
- **Verification:** Accounting invariants after each event, seeded differential matching, HTTP concurrency and timeout tests, sanitizers, and repeatable benchmarks.

## Architecture

```text
Browser dashboard (HTML, CSS, JavaScript)
              |
        Same-origin REST
              |
Boost.Beast HTTP server -- bounded queue, timeouts, metrics
              |
      One isolated run per request
              |
CSV validation -> replay -> exchange -> clearing/settlement
                                  |
                     accounting invariants
                                  |
                    JSON timeline and ledger
```

The C++20 engine owns balances and orders. The dashboard displays server-generated results; it does not maintain another financial model. There is no database because scenarios are reproducible inputs, not persistent customer transactions.

## Technology

C++20, CMake, Ninja, GCC/Clang, Boost.Beast/Asio 1.83+, Python 3 standard library, optional Node.js for frontend tests, Docker Compose, and GitHub Actions. No npm dependencies or financial data providers are needed.

## Run locally

**Docker Desktop:**

```bash
docker compose up --build
```

Open **http://localhost:8081**. The container listens internally on 8080. Stop with `docker compose down`.

**Ubuntu 24.04 / WSL2:**

```bash
sudo apt update
sudo apt install -y g++ cmake ninja-build libboost1.83-dev python3
./scripts/start.sh
```

**macOS:** `brew install boost cmake ninja`, then `./scripts/start.sh`. Native startup uses **http://127.0.0.1:8080** by default. Linux native builds were verified; Docker and macOS startup require verification on the target machine.

## Tests and benchmarks

```bash
./scripts/test.sh
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build-release -j 2
ctest --test-dir build-release --output-on-failure
./build-release/event-horizon-bench book 7000
./build-release/event-horizon-bench single 9000
./build-release/event-horizon-bench replay 300
python3 scripts/bench_http.py --server ./build-release/event-horizon-server
```

The matching engine plans eligible fills once rather than repeatedly rescanning the book. On a five-vCPU Intel Xeon VM, median 7,000-order sweeps measured **2.186 ms** here versus **1.672 ms** in the previous hardened release (five runs each). Preparing all trade records before balance changes costs some performance, while improving the failure-safety design. This is a synthetic measurement, not an SLA. [Benchmark details](docs/BENCHMARKS.md) include other workloads and the earlier optimization history;

## Other operational decisions and limits

Cash uses signed 64-bit integer cents, not floating point. A buyer reserves at the order's limit price; trades execute at the resting price. Matching and settlement are distinct operations. The engine checks that account assets plus clearing escrow equal the initial totals. Before changing balances, it plans executable fills and validates the actual path through the book.

The order book scans and sorts crossing quotes once. Multi-fill trades and queue storage are prepared before balances change; the underlying vectors grow geometrically rather than on every trade. Requests have bounded inputs and isolated state. The HTTP server closes excess connections without blocking its listener, interrupts stalled reads, and joins workers on shutdown.

This is a single-instrument local laboratory. It has no authentication, distributed clearing, margin model, public-market data, or regulatory validation. Monetary values exceeding JavaScript's exact integer range are serialized as decimal strings and formatted with `BigInt` in the dashboard; smaller values remain numeric for compatibility. Do not expose the service to the public internet without additional security controls. Passing tests and sanitizers does not prove memory safety.

MIT license; see [LICENSE](LICENSE).
