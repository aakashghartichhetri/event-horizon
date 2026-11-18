# EVENT HORIZON

**A deterministic laboratory for financial system instability — C++20 exchange, clearing, liquidity shocks, REST API, and interactive dashboard.**

> **Portfolio project, not a trading venue.** Every order, balance, currency amount, participant, and outage is simulated. There is no connection to real exchanges, accounts, APIs, or market data.

EVENT HORIZON answers a concrete systems question: **When a payment or exchange component stops working, how can a small failure propagate through other participants?** Load a scenario, inject an outage, inspect the escrow ledger, then compare what happens when infrastructure is restored or a funded liquidity facility is made available.

### Highlights

- **C++20 exchange and clearing engine:** limit orders, price/time FIFO matching, resting-price fills, partial executions, cancellations, per-account reservations, delivery-versus-payment settlement.
- **Failure and mitigation laboratory:** payments/exchange outages, settlement capacity limits, cash reservation shocks, risk notional limits, circuit breakers, and funded/repaid liquidity loans.
- **Deterministic autonomous agents:** registered one-shot trading strategies submit only when sufficiently funded, using reproducible event ticks.
- **Interactive local web application:** scenario navigator, escrow timeline, participant network, accounts and trade ledger, comparative results, editable CSV experiments and JSON export.
- **Testable engineering:** conservation assertions after *every* event, bounded inputs, deterministic JSON reports, native and HTTP tests, concurrent replay tests, reproducible benchmark, sanitizer targets, Docker and GitHub Actions.

No database, frontend package manager, cloud account, or API key is required. Scenarios are pure inputs; **each HTTP request runs its own isolated simulation**.

## Quick start (Ubuntu 24.04 / WSL2)

Install once:

```bash
sudo apt update
sudo apt install -y g++ cmake ninja-build libboost1.83-dev python3
```

From the project root, run:

```bash
./scripts/start.sh
```

Open **http://127.0.0.1:8080** in your browser. The script configures CMake, builds the CLI + server, and starts the web app. Press `Ctrl+C` to stop.

Or use **Docker Compose** as an alternative:

```bash
docker compose up --build
```

Open the same URL. The container publishes only on host loopback (`127.0.0.1`), uses a non-root user, and has a read-only filesystem. Stop with `Ctrl+C` or `docker compose down`.

**macOS:** Install CMake, Ninja, a C++20 compiler and Boost >= 1.83 (e.g., `brew install cmake ninja boost`), then run `./scripts/start.sh`. This path is not verified by CI. **Windows:** WSL2 with Ubuntu 24.04 is the supported path. If port 8080 is occupied, run `EH_PORT=9090 ./scripts/start.sh`.

`.env.example` only documents optional shell variables. There is no database or secret configuration. You do **not** need to copy `.env.example`.

## What you will see

The dashboard loads nine built-in experiments and can run an edited CSV script. Try **Healthy network**, then **Payments outage**, then **Early recovery** and **Liquidity backstop**. The numbers below are the deterministic outputs of the included scenarios:

| Scenario | Trades executed | Settled | Rejected | Pending | Cash in clearing |
|---|---:|---:|---:|---:|---:|
| Healthy network | 3 | 3 | 0 | 0 | $0 |
| Payments outage | 1 | 0 | 2 | 1 | $400 |
| Early recovery | 3 | 3 | 0 | 0 | $0 |
| Liquidity backstop | 3 | 3 | 0 | 0 | $0 |
| Cash reservation shock | 0 | 0 | 3 | 0 | $0 |
| Price-band circuit breaker | 1 | 1 | 2 | 0 | $0 |
| Exchange outage | 1 | 1 | 1 | 0 | $0 |
| Settlement capacity | 3 | 2 | 0 | 1 | $70 |
| Autonomous strategies | 3 | 3 | 0 | 0 | $0 |

**Model interpretation:** In the outage scenario, NOVA purchases $400 of shares from BEACON, but settlement fails. BEACON cannot spend those $400 and so cannot buy from CIRRUS, who in turn cannot buy from DELTA. The failed scenarios cancel unfilled sell orders where necessary to avoid confusing this *cash shortage* with self-trade prevention. An early payment restart or pre-funded loan facility prevents the cascade. The backstop uses $660 of **existing** facility cash and repays it after settlement. Some other experiments intentionally have different starting participants and are not direct counterfactuals.

## Build and test manually

```bash
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j 2
ctest --test-dir build --output-on-failure
```

Or run all of that with `./scripts/test.sh`. The native CTest suite includes core matching/accounting cases, randomized conservation checks, independent parallel replays, scenario smoke checks, and end-to-end HTTP tests. The HTTP test starts its own server on a temporary port, exercises the API, and shuts it down.

To test with **Clang and sanitizers**:

```bash
CXX=clang++ cmake -S . -B build-san -G Ninja -DCMAKE_BUILD_TYPE=Debug -DEH_ENABLE_SANITIZERS=ON
cmake --build build-san -j 2
ctest --test-dir build-san --output-on-failure
```

To run additional **GoogleTest** cases (this step needs a network connection to download a *pinned commit of GoogleTest 1.17.0*):

```bash
cmake -S . -B build-gtest -G Ninja -DEH_WITH_GTEST=ON
cmake --build build-gtest -j 2
ctest --test-dir build-gtest --output-on-failure
```

GoogleTest is deliberately optional: the entire core test suite works without any network downloads.

## CLI commands

Run these from the project root after building:

```bash
./build/event-horizon demo
./build/event-horizon list
./build/event-horizon scenario payments_outage
./build/event-horizon scenario liquidity_bridge
./build/event-horizon replay data/agents.csv
./build/event-horizon scenario-json payments_outage > result.json
./build/event-horizon compare-json > comparison.json
./build/event-horizon benchmark 10000
```

The benchmark measures *synthetic, single-process* matching and settlement and checks invariants. It is **not** an exchange-performance or SLA claim. Results depend on the machine, compiler, and build type; use `-DCMAKE_BUILD_TYPE=Release` for meaningful comparison. Limit: 10,000 test trades.

## REST API

By default the API listens on **127.0.0.1:8080**; Docker binds to `0.0.0.0` *inside* the container and publishes only the host loopback port. Use the same origin as the dashboard. The API is stateless; it never executes real trades.

| Method | Route | Description |
|---|---|---|
| `GET` | `/api/health` | Health + version |
| `GET` | `/api/scenarios` | Scenario IDs, titles, descriptions |
| `GET` | `/api/scenarios/{id}` | Full reproducible simulation, trade ledger, timeline, balances |
| `GET` | `/api/compare` | Results for all nine built-in experiments |
| `POST` | `/api/replay` | Run a caller-provided raw CSV scenario (`Content-Type: text/plain`) |
| `GET` | `/`, `/app.js`, `/style.css`, `/favicon.svg` | Web dashboard |

Example requests (with the server running):

```bash
curl -s http://127.0.0.1:8080/api/health
curl -s http://127.0.0.1:8080/api/scenarios/payments_outage
curl -s http://127.0.0.1:8080/api/compare
curl -sS -X POST http://127.0.0.1:8080/api/replay \
  -H 'Content-Type: text/plain' \
  --data-binary @data/agents.csv
```

Successful simulation responses have `scenario`, `source`, `metrics`, `invariants`, `risk`, `accounts`, `open_orders`, `trades`, `loans`, and `timeline`. The `timeline` shows the command, outcome, escrow cash and pending trades after every event. HTTP errors use `{"error":"description"}` with `400` for malformed scenarios, `404` for unknown scenarios, `413` for oversized requests, and `415` for unsupported content types. See [docs/API.md](docs/API.md) for example JSON and error semantics.

## How to test every feature manually

1. **Order execution / price priority:** `./build/event-horizon replay data/baseline.csv` reports 3 executed and 3 settled. Additional price/FIFO/partial-fill/cancel cases are in `tests/core_tests.cpp`.
2. **Failure propagation:** `./build/event-horizon scenario payments_outage` reports 1 trade executed, 0 settled, $400 escrow, and 2 rejected downstream orders.
3. **Mitigation:** Compare `recovery` and `liquidity_bridge` with the outage; both complete 3 settlements, and facility debt is fully repaid.
4. **Risk controls and capacity:** Run `circuit_breaker`, `liquidity_shock`, and `settlement_throttle`; inspect rejected orders, outstanding reservations and pending settlement counts.
5. **Autonomous agents:** Run `agents`; three funded, single-shot strategies execute in sequence without margin borrowing.
6. **HTTP / dashboard:** Start `./scripts/start.sh`, open the browser, click different scenarios and **Compare scenarios**. Edit a command in the Experiment Editor and press **Run custom scenario**; compare the event log and download JSON.
7. **Validation:** In the editor, change `BUY` to `LONG`; the API should return a line-numbered error rather than silently accepting invalid input.

## Scenario language

Scenarios are a deliberately strict, simple CSV format: **no quoted CSV fields**, no commas inside values, printable ASCII and line breaks only, blank lines and `#` comments are allowed. All prices and transfers are **integer cents**, all share quantities are positive integers, and commands are executed from top to bottom.

```csv
ACCOUNT,BUYER,10000,0
ACCOUNT,SELLER,0,2
PLACE,s1,SELLER,SELL,2,2000
PLACE,b1,BUYER,BUY,2,2500
SETTLE
```

| Command | Arguments | Meaning |
|---|---|---|
| `ACCOUNT` | `name,cash_cents,shares` | Create participant during initial setup only |
| `PLACE` | `order_id,owner,BUY/SELL,quantity,price_cents` | Limit order; reserves funds/shares |
| `CANCEL` | `owner,order_id` | Cancel unfilled remainder |
| `SETTLE` | optional `max_trades` | Settle all or a bounded number of pending fills FIFO |
| `PAYMENTS_DOWN` / `PAYMENTS_UP` | none | Stop/restart settlement |
| `EXCHANGE_DOWN` / `EXCHANGE_UP` | none | Stop/restart order acceptance |
| `LEND` / `REPAY` | `borrower,cash_cents` | Transfer cash from/to the pre-funded `FACILITY` account |
| `MAX_NOTIONAL` | `max_cash_cents` | Cap each incoming order's limit notional |
| `PRICE_BAND` | `reference_cents,deviation_bps` | Reject executions outside reference ± basis points; `0,0` disables |
| `AGENT` | `id,owner,BUY/SELL,quantity,price_cents` | Register an independent one-shot strategy |
| `TICK` | none | Each funded, unsubmitted agent submits its limit order in registration order |

**Important:** `PLACE` can be accepted without executing (a resting limit order). `CANCEL` works even while the exchange is offline. `PAYMENTS_DOWN` prevents clearing delivery, **not** execution. Facility lending uses pre-existing balances, so conserved cash does not increase. An invalid CSV command is fatal to that *request*; normal business rejections are recorded as events and execution continues. Every request uses a new engine, so malformed custom replays do not mutate anyone else's state.

**Input limits:** 65,536-byte request bodies; 8,192-byte headers; 4,096 characters per CSV line; 500 events per scenario; 100 accounts; 16 agents; and 10,000 live orders/trades. Account/order numeric ranges are validated; reserve multiplication uses bounded signed 64-bit arithmetic. These limits are intentionally conservative to keep the local lab fast and predictable.

## Repository structure

```text
event-horizon/
├── include/event_horizon/     Public C++ domain and replay headers
├── src/
│   ├── exchange.cpp           Price/time matching, escrow, lending, risk, invariants
│   ├── replay.cpp             Strict CSV parser and deterministic strategy ticks
│   ├── report.cpp             Scenario catalog and JSON serialization
│   ├── main.cpp               Command-line interface and benchmark
│   └── server.cpp             Bounded, multithreaded local HTTP API
├── data/                      Nine realistic, fictional scenarios
├── web/                       Self-contained dashboard (no npm install)
├── tests/                     Core, property-style and HTTP integration tests
├── scripts/                   One-command start and test scripts
├── docs/                      API, architecture and portfolio interview notes
├── .github/workflows/ci.yml   GCC/Clang, ASan/UBSan, GoogleTest, Docker
├── CMakeLists.txt
├── Dockerfile
├── compose.yaml
├── .env.example
├── .gitignore
└── LICENSE
```

## Engineering model and tradeoffs

**Accounting:** `Σ account.cash + clearing.cash = initial.cash`, and likewise for shares. Each resting bid reserves `price × remaining_quantity`; each ask reserves `remaining_quantity` shares. A fill deducts exactly `resting_price × filled_quantity` from the buyer, transfers the seller's shares into escrow, and leaves the seller's cash and buyer's shares **unchanged until settlement**. Price improvement automatically becomes available cash because the buyer's reservation is released at their own bid limit. Every replay event verifies these conservation, reservation, loan, and trade-counter invariants.

**Concurrency:** The web server has four workers by default and a bounded backlog. Each request constructs an isolated exchange and event trace, eliminating shared mutable financial state and nondeterministic interleaving. This is a deliberate, understandable boundary rather than a multi-instrument HFT engine. The matching algorithm scans resting orders in linear time to select the best; that is acceptable for this lab's bounded inputs and leaves a clear measured optimization path.

**Persistence:** There is no PostgreSQL because every experiment is replayable from its source CSV and deterministic engine. That avoids unnecessary data migrations and risk of storing inconsistent states. Export JSON for reproducibility and version the scenario input in Git. There is no live account database or durable API mutation.

**HTTP dependency decision:** The requested stack included Crow. This release uses **Boost.Beast (minimum Boost 1.83)** for HTTP instead, as it is packaged on Ubuntu 24.04 and could be compiled and integration-tested *offline* here. It avoids a network-dependent Crow fetch or unverified custom HTTP parser. All matching and accounting code remains standard C++20. The dashboard uses plain browser JavaScript rather than React because it needs no frontend framework or state-management build chain. This is a deliberate, documented deviation, not a claim that Crow is included.

For system boundaries, invariants, failure modes and performance considerations, see [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) and [docs/PORTFOLIO.md](docs/PORTFOLIO.md).

## Security and known limits

The HTTP service is intended for **local, untrusted-input demo use only**, not internet exposure. There is no authentication, user authorization, TLS, customer money or real brokerage integration. By default it binds to `127.0.0.1`; the Docker Compose host port is also loopback-only. The HTTP layer enforces body/header limits, bounded worker queue and timeouts, allowlisted routes, no CORS override, JSON escaping, and restrictive browser security headers. Users should **not** expose it publicly without adding authentication, TLS termination, authorization and traffic controls. See [docs/SECURITY.md](docs/SECURITY.md).

This is a simplified single-instrument, fixed-currency, no-credit/no-short-selling model. It omits external counterparties, CCP margin waterfalls, netting, exchange fees, market data, real time, distributed consensus, bankruptcy proceedings and regulatory rules. The price band uses a configured *fixed* reference, not a dynamic market-wide halt. Loans are principal-only and must be pre-funded. Event ordering is deterministic and serialized inside each run; the system is a causal demonstration, **not an empirically calibrated financial risk forecast**. A finished portfolio demo can be solid without falsely claiming a production financial exchange.

## Troubleshooting

| Symptom | Fix |
|---|---|
| `BoostConfig.cmake` not found | Install `libboost1.83-dev` on Ubuntu 24.04, then remove `build/` and reconfigure. |
| `cmake` / `ninja` not found | Install the prerequisites listed in Quick start. |
| Compiler says C++20 unavailable | Use GCC 12+ or modern Clang; CI uses current Ubuntu 24.04 toolchains. |
| Port 8080 already in use | `EH_PORT=9090 ./scripts/start.sh` then visit port 9090. |
| `cannot open replay file` | Run CLI from the repository root, not from inside `build/`. |
| `line N: ...` parser error | Check integer cents, argument count and CSV commands; quoted CSV is unsupported. |
| Browser says API unavailable | Confirm the server is still running in the terminal and you opened the correct `http://127.0.0.1:<port>`. |
| GoogleTest download fails offline | Reconfigure with `-DEH_WITH_GTEST=OFF` (default); offline core tests still run. |
| Docker build/network error | Docker needs internet once to obtain Ubuntu packages; use native build instead. |

## GitHub publishing

Create an **empty public GitHub repository** named `event-horizon` (do not pre-add a README, license or `.gitignore`), then from this folder:

```bash
git init
git branch -M main
git add .
git commit -m "Build complete Event Horizon financial instability laboratory"
git remote add origin https://github.com/<your-github-username>/event-horizon.git
git push -u origin main
```

After the push, open the **Actions** tab to check builds and the **README** to confirm the documentation is visible. CI runs native Debug/Release with GCC/Clang, sanitizers, optional pinned GoogleTest, and a Docker build. Results are only official once those workflows execute in your repository.

## License

[MIT](LICENSE) — educational and research demonstration use; no financial advice.

For an explicit acceptance-criteria checklist, see [docs/ACCEPTANCE.md](docs/ACCEPTANCE.md). For the exact checks actually run in this build environment, see [docs/VERIFICATION.md](docs/VERIFICATION.md).


### Docker: Ninja repeatedly regenerates CMake

If the Docker build reports `ninja: error: manifest 'build.ninja' still dirty after 100 tries`, it failed during build-system regeneration, before compiling project code. The Dockerfile explicitly sets `CMAKE_SUPPRESS_REGENERATION=ON` for its **fresh, isolated image build** to avoid that loop; this does not change local developer builds, which keep normal regeneration. Rebuild the updated image with `docker compose build --no-cache` followed by `docker compose up`. If you still see timestamp warnings, restart Docker Desktop and verify the host/VM clocks are synchronized.
