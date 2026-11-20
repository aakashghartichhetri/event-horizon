# EVENT HORIZON — Financial System Instability Lab

A C++20 financial-systems laboratory that replays exchange, clearing, and payment failures, traces how liquidity shortages spread between firms, and tests whether recovery actions change the outcome.

## Overview

A trade can match even when its cash has not arrived. If a payment service fails during that gap, a firm may be unable to fund its next trade. The interruption can travel through several firms although the initial failure happened in one place.

EVENT HORIZON makes that chain observable. Run a fictional trading day, pause settlement, inspect the affected balances, and try a different intervention. The dashboard shows actual results from the C++ engine—not hard-coded charts or predictions.

The system includes a REST API, a responsive incident dashboard, historical position replay, a 12-case resilience matrix, scenario editing, and automated tests. All participants, cash, orders, and incidents are simulated.

## Tech Stack

- **Frontend:** HTML, CSS, JavaScript modules (no frontend dependencies)
- **Backend:** C++20, Boost.Beast / Boost.Asio 1.83+
- **Build:** CMake, Ninja, GCC / Clang
- **Testing:** CTest, C++ tests, Python API integration, Node.js built-in test runner
- **Infrastructure:** Multi-stage Docker, Docker Compose, GitHub Actions

No database, third-party API, account credentials, or cloud service is required. Every experiment starts from an isolated engine and can be reproduced from its CSV input.

## Key Features

- **Exchange and Settlement:** Limit orders, price-time priority, partial fills, reservations, escrow, and delivery-versus-payment settlement.
- **Failure and Recovery:** Nine built-in incidents involving payment outages, exchange downtime, capacity limits, liquidity shocks, price controls, and autonomous rule-based strategies.
- **Historical Replay:** Step through the event trace, see available cash at each moment, and inspect the actual ledger and participants.
- **Resilience Lab:** Explore 12 controlled counterfactuals that vary recovery timing and pre-funded liquidity support while holding initial balances and intended orders fixed.
- **Experiment Workbench:** Edit and validate CSV scenarios, run them through the C++ API, save drafts in your browser, and export results.
- **Operational Checks:** Bounded HTTP input and worker queue, service health and request counters, accounting invariants, CI, and sanitizer builds.

## Getting Started

**Prerequisite:** Docker Desktop with Docker Compose.

```bash
# Start the dashboard and C++ API
docker compose up --build
```

Open **http://localhost:8081**. Port 8081 is the default host port to avoid conflicts with other local development services; the container listens on port 8080 internally. To choose a different host port:

```bash
EH_HOST_PORT=9090 docker compose up --build
```

**Native macOS (Homebrew):**

```bash
brew install boost cmake ninja
./scripts/start.sh
```

**Native Ubuntu 24.04 / WSL2:**

```bash
sudo apt update
sudo apt install -y g++ cmake ninja-build libboost1.83-dev python3
./scripts/start.sh
```

Native builds listen at **http://127.0.0.1:8080** unless `EH_PORT=9090` (or another port) is set. The startup script uses Homebrew's Boost prefix automatically if available. The macOS path has not been verified in this build environment.

## Testing

```bash
# Native C++ tests, scenario checks, benchmark smoke, and HTTP integration
./scripts/test.sh

# Deterministic recovery matrix in JSON
./build/event-horizon resilience-json

# Benchmark: synthetic matching + settlement (not an exchange SLA)
./build/event-horizon benchmark 10000

# Stop Docker services
docker compose down
```

If Node.js is installed, CMake also runs dashboard analysis and JavaScript syntax tests. The GitHub Actions workflow builds GCC/Clang configurations, runs sanitizers, exercises the HTTP API, and checks the container.

## Reference Scenarios

| Scenario | What happens |
| --- | --- |
| Healthy network | Three matched trades settle and their proceeds fund the next participant. |
| Payments outage | $400 is trapped in clearing and two downstream buyers lack cash. |
| Early recovery | Restoring settlement in time lets all three trades complete. |
| Liquidity backstop | A pre-funded facility supports participants until settlement resumes. |
| Settlement capacity | Three trades match but only two settle under a cycle limit. |
| Resilience Lab | Twelve cases compare four restoration times and three support levels. |

The remaining built-in scenarios explore exchange downtime, cash reservation shocks, fixed price bands, and autonomous strategies.

## Architecture

```text
Responsive browser dashboard
   │  Same-origin REST API
   ▼
C++ HTTP service (Boost.Beast, bounded worker queue)
   │  Fresh state per request
   ├── Scenario replay / strict CSV validation
   │      ├── Limit-order exchange + risk checks
   │      ├── Clearing escrow + settlement
   │      ├── Existing-cash liquidity facility
   │      └── Accounting invariants after every event
   │
   ├── Resilience experiment generator (12 controlled cases)
   │      └── Same replay engine, no separate financial logic
   ▼
JSON reports + historical account positions + trade ledger
   │
   └── Dashboard playback, intervention grid, exports
```

```text
include/event_horizon/    C++ interfaces
src/                      Exchange, replay, reports, resilience, API and CLI
web/                      Responsive dashboard
data/                     Nine reproducible CSV experiments
tests/                    Engine and HTTP integration checks
scripts/                  Native start and test commands
.github/workflows/        CI
Dockerfile, compose.yaml  Local container runtime
CMakeLists.txt             Native build configuration
docs/ENGINEERING.md       Accounting rules, API, security and tradeoffs
```

## Engineering Considerations

- **Conservation:** Money is represented as integer cents. Initial cash and shares must equal account assets plus clearing assets after every event.
- **Isolation:** Each HTTP request builds its own exchange state. Concurrent experiments cannot share mutable account balances.
- **Cause and effect:** A matched order is not a settled trade. The event trace records funding and pending obligations at each step.
- **Controlled comparisons:** Resilience cases share the same initial balances and intended trades. Funding comes from existing facility cash. The grid is a deterministic simulation, **not** a probability or financial forecast.
- **Bounded resource use:** Requests, accounts, agents, events, and the worker queue have explicit limits. The server binds to localhost by default.
- **Understandable tradeoffs:** Best-order selection uses linear scanning rather than a production low-latency order book. No PostgreSQL is needed because scenarios are reproducible inputs, not user-owned transaction records.

**Limitations:** EVENT HORIZON is an educational, single-instrument, local simulation—not a production trading venue, market-data service, regulatory model, or validated systemic-risk forecast. Internet deployment would require authentication, authorization, TLS termination, request-level abuse controls, and operational hardening. Browser-saved scripts are local only.

For CLI commands, endpoint examples, the CSV format, exact invariants, and failure modes, see [Engineering notes](docs/ENGINEERING.md).

MIT licensed. See [LICENSE](LICENSE).
