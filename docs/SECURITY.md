# Security model

**Scope:** local, educational simulation of fictional accounts. This is not a public-facing authenticated service and must not be deployed to an internet-reachable address without further work.

- Default listener: `127.0.0.1:8080`; Compose maps host loopback only. Do not use `--host 0.0.0.0` outside a trusted Docker or protected network environment.
- HTTP request bodies max 65,536 bytes, headers max 8,192 bytes, each CSV line max 4,096 chars, 500 events, 100 accounts, 16 agents, bounded order and trade counts. Bad numeric ranges and malformed commands reject immediately.
- `GET /api/scenarios/{id}` validates against a fixed ID allowlist before opening a file; static files are fixed server paths, so URL paths cannot read arbitrary files.
- Output strings are JSON-escaped by C++, and the dashboard inserts all scenario-controlled strings using `textContent`. Browser CSP disallows inline JS and off-origin scripts.
- Four workers by default (1..8 allowed) with a bounded 64-connection work queue. A best-effort five-second HTTP read timeout and ten-second write timeout apply. This is *not* a comprehensive DDoS solution.
- Server writes no account data, cookies or user data to a persistent database. Each replay has an isolated engine, so one user's request cannot alter another user's balances.
- Container runs as non-root (`10001:10001`) with capability drop, no-new-privileges, and read-only filesystem in Compose.
- There is no authentication, TLS, authorization, audit-log persistence, per-identity rate limiting, distributed consistency guarantee, financial regulatory compliance or handling of real assets. Add these before any exposure to untrusted public users.

## Reporting vulnerabilities

For a portfolio project, open a private message to the repository owner rather than publishing exploit details immediately. Do not place passwords, account credentials, personal finance records, or secrets in a scenario CSV.
