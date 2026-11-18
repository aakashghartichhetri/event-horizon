# EVENT HORIZON 

A full-stack simulation application that explores how exchange outages, payment failures, and liquidity shocks propagate through a financial network.

## Overview

EVENT HORIZON models a fictional financial system with trading participants, an exchange, and a clearing and settlement process. It replays deterministic events to show how a local failure can prevent downstream trades and compares ways to contain the disruption.

The system includes a REST API, an interactive dashboard, scenario replay, simulated trading strategies, and automated tests. All money, shares, and participants are simulated; no real trading or financial accounts are involved.

## Tech Stack

- **Frontend:** HTML, CSS, JavaScript
- **Backend:** C++20, Boost.Beast 1.83+
- **Build:** CMake, Ninja, GCC/Clang
- **Testing:** CTest, C++ tests, Python HTTP integration tests, optional GoogleTest
- **Infrastructure:** Docker, Docker Compose, GitHub Actions

## Key Features

- **Exchange and Clearing:** Matches limit orders using price-time priority and settles trades through an escrow-based accounting system.
- **Failure Propagation:** Simulates exchange outages, payment failures, settlement bottlenecks, and liquidity shocks.
- **Mitigation Experiments:** Compares infrastructure recovery, price-band controls, and pre-funded liquidity support.
- **Autonomous Participants:** Runs deterministic, rule-based trading strategies within reproducible scenarios.
- **Interactive Dashboard:** Displays network activity, balances, executed trades, event timelines, and scenario comparisons; supports custom CSV replay and JSON export.

## Getting Started

**Prerequisite:** Docker Desktop with Docker Compose.

```bash
# Start the application
docker compose up --build
```

Open **http://localhost:8080** to access the dashboard.

For a native Ubuntu 24.04 / WSL2 setup, install `g++`, `cmake`, `ninja-build`, `libboost1.83-dev`, and `python3`, then run `./scripts/start.sh`.

## Testing

The small Docker runtime image does not include the test suite. Run automated tests natively on Ubuntu 24.04 / WSL2 after installing the prerequisites above.

```bash
# Build and run automated C++ and HTTP integration tests
./scripts/test.sh

# Stop the Docker application
docker compose down
```

For Clang sanitizer tests, optional GoogleTest, and CLI commands, see [the detailed guide](docs/DETAILED_GUIDE.md).

## Reference Scenarios

| Scenario | Test Scenario |
| --- | --- |
| Healthy Network | Executes and settles three trades without disruptions. |
| Payments Outage | Leaves $400 in clearing and blocks two downstream orders. |
| Early Recovery | Restores settlement before a liquidity shortage propagates. |
| Liquidity Backstop | Uses a pre-funded, repaid loan to keep downstream trading active. |

The application includes nine deterministic scenarios, including circuit breakers, limited settlement capacity, and autonomous strategies.

## Architecture

```text
include/event_horizon/   # Public C++ engine, replay, and reporting interfaces
src/                     # Exchange, clearing, scenarios, CLI, and HTTP server
web/                     # Browser dashboard
data/                    # Nine deterministic CSV scenarios
tests/                   # C++ tests and HTTP integration checks
docs/                    # Architecture, API, security, and portfolio documentation
scripts/                 # Start and test scripts
.github/workflows/       # CI pipeline
```

## Engineering Considerations

- Uses integer cents and share quantities to avoid floating-point accounting errors.
- Checks cash/share conservation and order reservations after every replay event.
- Isolates each HTTP simulation request to preserve reproducibility and avoid shared financial state.
- Validates inputs and limits event counts, request sizes, and simulation resources.
- Keeps order matching straightforward for a bounded laboratory, rather than claiming high-frequency exchange throughput.
- Tests the engine and HTTP API with deterministic scenarios, concurrent requests, and sanitizer builds.

**Limitations:** This is a single-instrument, fictional simulation, not a production trading venue or a calibrated financial-risk forecast. The local HTTP server has no authentication or TLS and must not be exposed publicly without additional security controls. Docker execution, GitHub Actions, and visual browser testing remain pending independent verification; native test results and their scope are recorded in [docs/VERIFICATION.md](docs/VERIFICATION.md).
