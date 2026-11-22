"""End-to-end API smoke and concurrency tests using only the Python standard library."""
import concurrent.futures
import http.client
import json
import re
import signal
from html.parser import HTMLParser
import pathlib
import socket
import subprocess
import sys
import time
import urllib.error
import urllib.request

server = pathlib.Path(sys.argv[1])
root = pathlib.Path(sys.argv[2])

with socket.socket() as probe:
    probe.bind(("127.0.0.1", 0))
    port = probe.getsockname()[1]

process = subprocess.Popen(
    [str(server), "--host", "127.0.0.1", "--port", str(port),
     "--data-dir", str(root / "data"), "--web-dir", str(root / "web"), "--workers", "4"],
    stdout=subprocess.DEVNULL, stderr=subprocess.PIPE,
)
base = f"http://127.0.0.1:{port}"


def reject_duplicate_fields(pairs):
    output = {}
    for key, value in pairs:
        if key in output:
            raise AssertionError(f"Duplicate JSON property: {key}")
        output[key] = value
    return output


def strict_json(data):
    return json.loads(data, object_pairs_hook=reject_duplicate_fields)


def get(path):
    with urllib.request.urlopen(base + path, timeout=8) as response:
        return response.status, response.headers, response.read()


def post(csv, content_type="text/plain"):
    request = urllib.request.Request(base + "/api/replay", method="POST",
        data=csv.encode("utf-8"), headers={"Content-Type": content_type})
    try:
        with urllib.request.urlopen(request, timeout=8) as response:
            return response.status, strict_json(response.read())
    except urllib.error.HTTPError as error:
        return error.code, strict_json(error.read())


try:
    for _ in range(100):
        try:
            status, _, _ = get("/api/health")
            if status == 200:
                break
        except (OSError, urllib.error.URLError):
            time.sleep(0.05)
    else:
        raise AssertionError("server not ready")

    code, headers, html = get("/")
    assert code == 200 and b"EVENT HORIZON" in html
    assert headers["Content-Security-Policy"] and headers["X-Content-Type-Options"] == "nosniff"
    for path in ("/app.js", "/insights.mjs", "/style.css", "/favicon.svg"):
        assert get(path)[0] == 200

    js = get("/app.js")[2].decode("utf-8")
    assert "./insights.mjs" in js
    assert b"EVENT REPLAY" in html and b"Trade ledger" in html
    assert b"Resilience lab" in html and b"resilience-grid" in html
    css = get("/style.css")[2]
    assert b"@media" in css and b"max-width: 480px" in css

    class IdCollector(HTMLParser):
        def __init__(self):
            super().__init__()
            self.ids = []
        def handle_starttag(self, tag, attrs):
            self.ids.extend(value for key, value in attrs if key == "id")

    collector = IdCollector()
    collector.feed(html.decode("utf-8"))
    assert len(collector.ids) == len(set(collector.ids)), "duplicate HTML ids"
    referenced = set(re.findall(r"\$\(['\"]([A-Za-z0-9_-]+)['\"]\)", js))
    assert referenced <= set(collector.ids), f"missing UI elements: {referenced - set(collector.ids)}"
    catalog = strict_json(get("/api/scenarios")[2])
    assert len(catalog["scenarios"]) == 9
    comparison = strict_json(get("/api/compare")[2])
    assert len(comparison["comparison"]) == 9
    matrix = strict_json(get("/api/resilience")[2])
    assert len(matrix["cases"]) == 12
    assert len(matrix["recovery_labels"]) == 4
    assert len(matrix["funding_labels"]) == 3
    assert all(row["invariants_passed"] for row in matrix["cases"])
    assert len(set((row["recovery"], row["funding"]) for row in matrix["cases"])) == 12
    assert len(set(row["settled"] for row in matrix["cases"])) >= 3
    case = strict_json(get("/api/resilience/case?recovery=2&funding=2")[2])
    assert case["metrics"]["trades_settled"] == 3
    assert case["metrics"]["rejected_orders"] == 0
    assert case["timeline"][-1]["accounts"] == [
        {key: account[key] for key in ("name", "cash_cents", "available_cash_cents",
          "reserved_cash_cents", "shares", "available_shares")}
        for account in case["accounts"]
    ]
    try:
        get("/api/resilience/case?recovery=6&funding=2")
        raise AssertionError("invalid matrix coordinate accepted")
    except urllib.error.HTTPError as error:
        assert error.code == 400
    metrics = strict_json(get("/api/metrics")[2])
    assert metrics["requests_total"] > 0 and metrics["in_flight"] >= 1

    scenarios = {}
    for entry in catalog["scenarios"]:
        result = strict_json(get("/api/scenarios/" + entry["id"])[2])
        assert result["invariants"]["passed"]
        assert result["metrics"]["trades_executed"] == len(result["trades"])
        assert result["metrics"]["pending_trades"] == len([x for x in result["trades"] if not x["settled"]])
        assert len(result["timeline"]) > 0
        assert result["timeline"][-1]["rejected_orders"] == result["metrics"]["rejected_orders"]
        assert result["timeline"][-1]["exchange_online"] == result["exchange_online"]
        assert result["timeline"][-1]["payments_online"] == result["payments_online"]
        scenarios[entry["id"]] = result

    assert scenarios["baseline"]["metrics"]["trades_settled"] == 3
    assert scenarios["payments_outage"]["metrics"]["trades_executed"] == 1
    assert scenarios["liquidity_bridge"]["metrics"]["loan_repayments"] == 2
    assert scenarios["agents"]["metrics"]["trades_settled"] == 3

    code, custom = post("ACCOUNT,BUYER,10000,0\nACCOUNT,SELLER,0,1\n"
        "PLACE,s,SELLER,SELL,1,6000\nPLACE,b,BUYER,BUY,1,6000\nSETTLE\n")
    assert code == 200 and custom["metrics"]["trades_settled"] == 1
    assert custom["accounts"][0]["cash_cents"] == 4000
    code, body = post("ACCOUNT,X,1,0\nBAD\n")
    assert code == 400 and "line 2" in body["error"]
    assert post("ACCOUNT,X,1,0\n", "application/json")[0] == 415
    assert post("X" * 65537)[0] == 413
    try:
        get("/api/scenarios/../../etc/passwd")
    except urllib.error.HTTPError as error:
        assert error.code in (404, 405)

    def concurrent_health(_):
        return strict_json(get("/api/scenarios/baseline")[2])["metrics"]["trades_settled"]

    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
        assert all(count == 3 for count in pool.map(concurrent_health, range(32)))

    # Vary balances and settlement availability across simultaneous POST requests.
    # Every response must reflect its own input rather than another request's state.
    def independent_replay(index):
        payments = "PAYMENTS_DOWN\n" if index % 2 else ""
        settle = "SETTLE\n"
        script = (f"ACCOUNT,B,{10000 + index},0\nACCOUNT,S,0,1\n"
                  f"PLACE,ask,S,SELL,1,4000\nPLACE,bid,B,BUY,1,4000\n"
                  + payments + settle)
        code, result = post(script)
        assert code == 200
        assert result["invariants"]["cash_initial_cents"] == 10000 + index
        assert result["metrics"]["trades_executed"] == 1
        assert result["metrics"]["trades_settled"] == (0 if index % 2 else 1)
        assert result["metrics"]["pending_trades"] == (1 if index % 2 else 0)
        return True

    with concurrent.futures.ThreadPoolExecutor(max_workers=8) as pool:
        assert all(pool.map(independent_replay, range(32)))

    # API must preserve totals beyond IEEE-754's exact integer range.
    high_values = "".join(f"ACCOUNT,F{i},100000000000000,0\n" for i in range(96))
    code, precise = post(high_values)
    assert code == 200
    assert precise["invariants"]["cash_initial_cents"] == "9600000000000000"
    assert sum(account["cash_cents"] for account in precise["accounts"]) == 9600000000000000

    # Parser limits include headers, not only body bytes.
    with socket.create_connection(("127.0.0.1", port), timeout=5) as sock:
        sock.sendall(b"GET /api/health HTTP/1.1\r\nHost: localhost\r\n"
                     + b"X-Fill: " + b"a" * 9000 + b"\r\n\r\n")
        raw = sock.recv(4096)
        assert b"413" in raw, raw[:200]

    # A stalled partial header must not occupy a worker indefinitely, even
    # without a termination signal or a client-side socket close.
    stalled = socket.create_connection(("127.0.0.1", port), timeout=5)
    stalled.settimeout(8)
    stalled.sendall(b"GET /api/health HTTP/1.1\r\nHost: localhost\r\n")
    start = time.monotonic()
    try:
        assert stalled.recv(1) == b"", "timed-out client connection should close"
        assert time.monotonic() - start < 7, "stalled client exceeded timeout budget"
    finally:
        stalled.close()
    assert get("/api/health")[0] == 200, "healthy requests continue after timeout"

    # SIGTERM must stop the listener, close queued sockets and join workers.
    # Keep one request incomplete to exercise the active-read timeout path.
    idle = socket.create_connection(("127.0.0.1", port), timeout=5)
    idle.sendall(b"GET /api/health HTTP/1.1\r\nHost: localhost\r\n")
    time.sleep(0.05)
    process.terminate()
    try:
        assert process.wait(timeout=9) == 0, "server should exit cleanly on SIGTERM"
    finally:
        idle.close()

    print("PASS: dashboard assets, strict JSON, large cents, 9 scenarios, "
          "12 recovery cases, concurrent isolation, malformed input, "
          "resource bounds and graceful shutdown")
finally:
    if process.poll() is None:
        process.terminate()
    try:
        process.communicate(timeout=5)
    except subprocess.TimeoutExpired:
        process.kill()
        process.communicate(timeout=5)
