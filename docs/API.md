# API reference

The API is a synchronous local simulation service. Defaults: `http://127.0.0.1:8080`, 4 workers, no authentication because it is **not intended for public internet exposure**. Responses are `application/json; charset=utf-8` unless a static asset is requested.

## Endpoints

### `GET /api/health`

```json
{"status":"ok","service":"event-horizon","version":"1.0.0"}
```

### `GET /api/scenarios`

Response has `default` and `scenarios`, an array of `{id,title,description}`. IDs are an allowlist, not arbitrary filesystem paths.

### `GET /api/scenarios/{id}`

Runs a full replay using a bundled scenario script. The response contains these top-level keys:

| Field | Description |
|---|---|
| `scenario`, `source` | Scenario ID and original CSV text |
| `exchange_online`, `payments_online` | Final infrastructure status |
| `metrics` | accepted/rejected/canceled orders, executed/settled trades, settlement blocks, loan events, traded notional, peak clearing, pending trades, open orders, escrow |
| `invariants` | `passed`, total initial cash and shares |
| `risk` | Max order notional and optional price-band reference and bps |
| `accounts` | Each participant's cash, cash reservation, available cash, shares and available shares |
| `open_orders` | Remaining limit orders and FIFO sequence |
| `trades` | Fill ID, order IDs, buyer/seller, execution price, quantity, integer notional, settled flag |
| `loans` | Borrower and outstanding facility principal |
| `timeline` | Every replayed command's sequence, line, status, message, escrow, and trade counters |

All monetary amounts are **integer cents**. This avoids floating-point decimal problems and also avoids losing precision in the C++ internal accounting. Browser display divides by 100 only for formatting.

### `GET /api/compare`

Runs all built-in scenarios separately. Returns `{"comparison":[{"id":...,"title":...,"executed":...,"settled":...,"rejected":...,"pending":...,"escrow_cents":...,"risk_rejections":...,"cash_initial_cents":...},...]}`.

### `POST /api/replay`

Body: raw unquoted CSV with `Content-Type: text/plain`, max 65,536 bytes and 500 events. Returns the same schema as the scenario endpoint, with `"scenario":"custom"`.

```bash
curl -i -X POST http://127.0.0.1:8080/api/replay \
  -H 'Content-Type: text/plain' \
  --data-binary $'ACCOUNT,B,10000,0\nACCOUNT,S,0,1\nPLACE,s,S,SELL,1,5000\nPLACE,b,B,BUY,1,5000\nSETTLE\n'
```

Expected: `HTTP 200`, one trade executed and settled, buyer B with 5,000 cents cash and one share, seller S with 5,000 cents cash. `"invariants":{"passed":true,...}`.

## Errors

| Status | Example | Meaning |
|---|---|---|
| 400 | `{"error":"line 3: side must be BUY or SELL"}` | Invalid scenario syntax |
| 404 | `{"error":"unknown scenario: example"}` | Unknown allowlisted ID |
| 405 | `{"error":"method not allowed"}` | Unsupported verb or route method |
| 413 | `{"error":"request limit exceeded"}` | Body/header beyond parser cap |
| 415 | `{"error":"use Content-Type: text/plain"}` | Unsupported body content type |
| 500 | `{"error":"internal server error"}` | File or internal server error; detail logged server-side |
| 503 | `{"error":"server is busy"}` | Bounded queue exceeded |

There is no CORS wildcard, websocket connection, user session or API key. Connections close after one request. Server request parser timeouts are enforced and no arbitrary filesystem paths are served.
