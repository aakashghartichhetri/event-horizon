# Engineering notes

## System boundaries

The web UI is a static set of files served by the C++ HTTP process. No frontend framework or dependency installation is needed. The service runs one new `Simulation` per request. `run_text` parses and replays ordered commands; `Exchange` owns accounts, resting orders, trades, clearing escrow, and facility loans. `report.cpp` serializes the resulting immutable snapshot and per-event trace.

```text
HTTP request → bounded routing → replay parser → Exchange
                                   │              ├─ match orders
                                   │              ├─ reserve cash / shares
                                   │              ├─ settlement FIFO / escrow
                                   │              └─ verify invariants
                                   └─ event log → JSON response → browser
```

The server uses four workers by default with a bounded accept queue. Each experiment owns its mutable financial state and does not share balances with concurrent requests. The browser tracks only presentation state: active view, playback step, selected participant, draft script, and selected comparisons. The `/api/replay` endpoint never modifies on-disk scenarios.

## Accounting rules

- All cash amounts and prices are **signed 64-bit integer cents**; quantities are positive whole shares. There is one simulated asset and one currency.
- A resting BUY reserves its remaining quantity times limit price. A resting SELL reserves its remaining shares. Only available assets may be used by subsequent orders.
- A match uses the **resting order's price**, selected by best price, then FIFO sequence. The buyer pays the executed notional into clearing; the seller's shares enter clearing. The seller does not receive the money, and the buyer does not receive the shares, until settlement.
- `SETTLE` delivers the cash to the seller and shares to the buyer for pending fills in FIFO order. `SETTLE,n` caps the count. `PAYMENTS_DOWN` blocks settlement but does not block order matching.
- `CANCEL` releases only unfilled reservations. Trades already executed remain obligations. The `FACILITY` can lend only its existing cash and can receive repayments; this does not mint money.
- After every valid event, the engine checks `Σ(account cash) + clearing cash = initial cash`, the corresponding share equation, exact reservation totals, pending trade / escrow reconciliation, trade counters, and loan consistency.

This is deliberately **not** a CCP margin, netting, credit, or insolvency model. The price-band check uses a fixed configured reference rather than dynamic market-wide halts.

## Scenario language

One comma-separated command per line. Leading and trailing spaces are trimmed; blank lines and `#` comments are allowed. Quoted CSV fields are not supported. Only printable ASCII and newlines are permitted; numeric inputs are integers.

```csv
ACCOUNT,BUYER,10000,0
ACCOUNT,SELLER,0,2
PLACE,s1,SELLER,SELL,2,2000
PLACE,b1,BUYER,BUY,2,2500
SETTLE
```

| Command | Arguments | Behavior |
| --- | --- | --- |
| `ACCOUNT` | `name,cash_cents,shares` | Add a participant during initial setup |
| `PLACE` | `order_id,owner,BUY/SELL,quantity,limit_cents` | Submit a limit order |
| `CANCEL` | `owner,order_id` | Remove an unfilled order remainder |
| `SETTLE` | optional `max_trades` | Deliver pending trades FIFO |
| `PAYMENTS_DOWN` / `PAYMENTS_UP` | none | Disable / restore settlement |
| `EXCHANGE_DOWN` / `EXCHANGE_UP` | none | Disable / restore new order acceptance |
| `LEND` / `REPAY` | `borrower,cash_cents` | Transfer funds to / from funded `FACILITY` |
| `MAX_NOTIONAL` | `max_cash_cents` | Limit incoming order notional |
| `PRICE_BAND` | `reference_cents,deviation_bps` | Restrict price of crossed matches; `0,0` disables |
| `AGENT` | `id,owner,BUY/SELL,qty,price_cents` | Register a deterministic one-shot strategy |
| `TICK` | none | Let funded registered agents submit in registration order |

A business rejection (e.g., insufficient cash) is recorded and replay continues. Malformed input ends that replay request with an error carrying a line number.

## Controlled recovery experiments

The resilience matrix runs a fixed opening market with three intended trades: NOVA buys
from BEACON ($400), BEACON buys from CIRRUS ($360), and CIRRUS buys from DELTA
($300). A payment outage starts immediately after the first execution.

The two deliberately varied dimensions are:

- `recovery=0` restores before BEACON's next purchase; `1` before CIRRUS's;
  `2` after both downstream orders; `3` never restores during the run.
- `funding=0` offers no loans; `1` transfers $310 to BEACON from the existing
  FACILITY account; `2` also transfers $270 to CIRRUS if needed before the
  third purchase. Loans are repaid after settlement when payment recovery occurs.

Every run begins with the same initial account balances and shares and uses the same
prices, sizes, and intended counterparties. A canceled, unfunded ask is excluded from
later matching to avoid confusing liquidity rejections with self-trade prevention.
There is no random sampling, credit creation, or statistically calibrated risk score.
These are controlled *model* outcomes, not estimates of actual market behavior.

```bash
./build/event-horizon resilience-json
./build/event-horizon resilience-case 2 2
curl -s http://127.0.0.1:8080/api/resilience
curl -s 'http://127.0.0.1:8080/api/resilience/case?recovery=2&funding=2'
```

## API

The API listens on `127.0.0.1:8080` for native runs. Docker uses host port 8081 by default, bound to loopback (overridable with `EH_HOST_PORT`).

| Method | Endpoint | Result |
| --- | --- | --- |
| GET | `/api/health` | Service status |
| GET | `/api/scenarios` | Built-in catalog |
| GET | `/api/resilience` | Controlled 4 × 3 intervention matrix |
| GET | `/api/resilience/case?recovery=2&funding=2` | Full event replay for one matrix cell; recovery 0..3, funding 0..2 |
| GET | `/api/metrics` | Uptime and accepted-request counters (local diagnostics) |
| GET | `/api/scenarios/{id}` | Full replay JSON for the selected scenario |
| GET | `/api/compare` | Summary metrics for all nine built-in scenarios |
| POST | `/api/replay` | Run raw CSV (`Content-Type: text/plain`) in a clean simulation |

```bash
curl -s http://127.0.0.1:8080/api/health
curl -s http://127.0.0.1:8080/api/scenarios/payments_outage
curl -s http://127.0.0.1:8080/api/compare
curl -sS -X POST http://127.0.0.1:8080/api/replay \
  -H 'Content-Type: text/plain' --data-binary @data/baseline.csv
```

Full results include `metrics`, `risk`, `invariants`, `accounts`, `trades`, `open_orders`, `loans`, and `timeline`. Each event in `timeline` includes the original command, outcome, index, clearing balance, and cumulative executed/settled counts. Each timeline event also includes an `accounts` array with the actual post-event cash, reservations, and shares. The network and trade ledger separately show end-of-run state. Request counters are in-process, reset on restart, and are not durable monitoring. Error responses are `{"error":"message"}` with HTTP 400 for invalid scripts, 404 for unknown scenarios, 413 for oversized bodies, and 415 for unsupported content types.

## Tests and reliability

- `./scripts/test.sh` compiles native targets and runs core unit/property-style tests, scenario smoke tests, a synthetic benchmark check, and HTTP integration tests (including concurrent requests).
- `node --test web/insights.test.mjs` exercises frontend event-step derivation, accounting chart input, classification, and CSV export. Node 20+ is optional for local use; native test runs without it.
- `CXX=clang++ cmake -S . -B build-san -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_SUPPRESS_REGENERATION=ON -DEH_ENABLE_SANITIZERS=ON && cmake --build build-san -j 2 && ctest --test-dir build-san --output-on-failure` runs the AddressSanitizer and UndefinedBehaviorSanitizer configuration.
- `./build/event-horizon benchmark 10000` measures synthetic matching/settlement on this computer. It is not a production-throughput claim.

Source scripts are validated as bounded payloads: 65,536-byte bodies, 8,192-byte headers, 4,096-character lines, at most 500 events, 100 accounts, 16 agents, and 10,000 live orders/trades. Multiplication and total ranges are checked before adding positions.

## Security and deployment boundary

The server is intended for local, untrusted-input experimentation. Routes are explicitly allowlisted; static file serving never accepts an arbitrary path. The API returns JSON-escaped strings. UI content is inserted through DOM `textContent`, not `innerHTML`. Responses set CSP, frame protection, MIME sniffing prevention, and a no-referrer policy. The Docker runtime uses a non-root user, read-only filesystem, and localhost-only published port.

There is **no** authentication, authorization, TLS termination, identity-based rate limiting, or durable audit database. Do not expose this app to the public internet without a separate deployment and security design.

## Design tradeoffs

The order book uses linear scanning for best price/time matching. It is easy to inspect but not a production low-latency order book. The HTTP service uses Boost.Beast (packaged as Boost 1.83 on Ubuntu 24.04) rather than a network-fetched Crow dependency, keeping the build reproducible offline. The frontend uses no framework: it is a standalone, same-origin interface to a deterministic engine. Browser saves use `localStorage` only so repeatable CSV inputs can still be checked into Git and shared without a server database.
