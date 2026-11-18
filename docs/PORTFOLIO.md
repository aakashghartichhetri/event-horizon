# Mid-level SWE interview guide

## A useful 90-second project explanation

> EVENT HORIZON is a deterministic C++20 system for exploring how exchange and payment infrastructure failures propagate. It matches limit orders using price-time priority, reserves funds, moves executed cash and shares into clearing escrow, and settles them using a delivery-versus-payment model. A stopped payment rail traps proceeds, so downstream participants cannot finance subsequent trades. I built an HTTP API and browser interface for scenario comparison, funded facility loans, circuit breakers, and autonomous strategy ticks. To make behavior testable I use integer cents, conservation invariants after every event, reproducible audit traces, property-style testing, and an isolated engine per concurrent request.

## Interview questions to practice

**Why signed 64-bit integer cents?** Decimal money has precise cents while IEEE-754 binary floating point cannot exactly represent many decimal fractions. Keeping cents as bounded integers prevents rounding error and allows exact conservation comparisons. Bounded quantity × price multiplication must be checked *before* arithmetic or constrained so overflow is impossible.

**Why escrow between execution and settlement?** An exchange match is an obligation, not instantly available funds at the other participant. Cash/share escrow makes a payment outage affect actual participant liquidity, not merely a dashboard status flag. It also models why a system can have matched trades and still fail to settle.

**What's the complexity of matching?** O(N) per selected best resting match, with O(N) cleanup in a vector. This favors simple, verifiable behavior for hundreds of events. For millions of orders I would measure first, then implement sorted price levels, FIFO order queues per price, and an ID-to-order index, with correctness tests preserving priority and partial-fill behavior.

**How is it thread-safe?** The API's workers share only immutable configuration and fixed files. Each request builds a fresh `Simulation` and `Exchange` on its worker. There are no shared mutable account balances, so no locks are needed around the financial core. The HTTP accept queue is protected by a mutex/condition variable and limited in size. For a shared live market, I would use partitioned ownership or a single sequencer per instrument, not simply add mutexes around every method.

**How do you know the accounting is correct?** Every replay command checks cash/share conservation across participants and escrow; reservations equal open order remainders; pending trade inventory equals escrow; and trade settlement counters match the immutable fill records. Tests include FIFO selection, price improvement, partial fills, outages, repayments, randomized operations and parallel independent replays. An invariant alone does not prove all requirements, so the scenario acceptance tests independently assert expected outcomes.

**Why aren't all application errors fatal?** Syntactically invalid input makes the replay unusable and is rejected with a line number. Insufficient cash or exchange downtime is an expected business outcome, recorded as a `REJECTED` event so the user can study the failure cascade.

**How does the loan facility work?** Only an explicitly initialized `FACILITY` account can lend. Loan principal moves from the facility's available cash to a borrower, is tracked as debt, and can be repaid only from unreserved borrower cash. Therefore the sum of system cash is unchanged. Real collateral/margin/credit-loss dynamics are intentionally out of scope.

**What would you improve under pressure?** First instrument performance profiles and p95/p99 request latency, use larger property/fuzz workloads, optimize price-level indexing, and add bounded request execution time. For multi-user mutable state add a durable append-only event log with idempotency tokens, deterministic replay, crash recovery, and per-market single-writer sequencing. Those are meaningful changes driven by requirements, not resume buzzwords.

## Suggested demo (3–4 minutes)

1. Show the healthy dashboard (three settled trades) and explain the cash/shares conservation invariant.
2. Click Payments outage: one matched trade, $400 trapped and two downstream orders rejected for insufficient cash.
3. Click Early recovery, then Liquidity backstop: explain that one fixes payments while the other temporarily funds the participants using *existing* money.
4. Open Experiment Editor, change a `SETTLE` timing and replay. Show the event log, then download JSON.
5. Open tests and CI. Discuss why the core is deterministic and how you bound inputs and isolate concurrent requests.

## Accurate resume bullets (once you have published and verified CI)

- Built **EVENT HORIZON**, a C++20 deterministic financial-market failure simulator with limit-order matching, escrow-based settlement, liquidity backstops and event replay exposed through an HTTP API and interactive web dashboard.
- Designed exact integer accounting and replay-time conservation checks for simulated cash/share ledgers; tested with scenario regression cases, randomized operation sequences, and concurrent HTTP integration tests.
- Added CMake, container build, multi-compiler CI, sanitizer testing, deterministic JSON reporting and synthetic throughput benchmarking; documented tradeoffs and security boundaries.

Do **not** claim this is a deployed financial exchange, real HFT performance system, actual systemic-risk prediction platform, or proof of FAANG hiring readiness. Interview readiness also depends on the ability to explain and modify the system yourself.
