#!/usr/bin/env python3
"""Local-only repeatable HTTP latency sample. No third-party packages."""
import argparse
import concurrent.futures
import json
import pathlib
import socket
import subprocess
import time
import urllib.request
import urllib.error


def percentile(sorted_samples, fraction):
    index = max(0, min(len(sorted_samples) - 1, int(len(sorted_samples) * fraction + 0.999999) - 1))
    return round(sorted_samples[index], 3)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--server', default='./build-release/event-horizon-server')
    parser.add_argument('--requests', type=int, default=200)
    parser.add_argument('--concurrency', type=int, default=4)
    args = parser.parse_args()
    if not 1 <= args.requests <= 2000 or not 1 <= args.concurrency <= 8:
        parser.error('requests must be 1..2000 and concurrency must be 1..8')
    root = pathlib.Path(__file__).resolve().parents[1]
    server = pathlib.Path(args.server).resolve()
    if not server.is_file():
        parser.error(f'build the server first: {server}')
    with socket.socket() as sock:
        sock.bind(('127.0.0.1', 0))
        port = sock.getsockname()[1]
    proc = subprocess.Popen([str(server), '--host', '127.0.0.1', '--port', str(port),
                             '--data-dir', str(root / 'data'), '--web-dir', str(root / 'web'),
                             '--workers', str(args.concurrency)],
                            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    base = f'http://127.0.0.1:{port}'
    csv = b'ACCOUNT,B,10000,0\nACCOUNT,S,0,1\nPLACE,s,S,SELL,1,5000\nPLACE,b,B,BUY,1,5000\nSETTLE\n'
    try:
        for attempt in range(100):
            try:
                with urllib.request.urlopen(base + '/api/health', timeout=2) as response:
                    assert json.load(response)['status'] == 'ok'
                break
            except (OSError, urllib.error.URLError):
                time.sleep(0.05)
        else:
            raise RuntimeError('local API did not start')

        def measure(mode):
            request = urllib.request.Request(base + ('/api/scenarios/baseline' if mode == 'get' else '/api/replay'),
                data=(None if mode == 'get' else csv),
                headers=({} if mode == 'get' else {'Content-Type': 'text/plain'}))
            start = time.perf_counter_ns()
            with urllib.request.urlopen(request, timeout=10) as response:
                data = json.load(response)
            ms = (time.perf_counter_ns() - start) / 1e6
            if data.get('metrics', {}).get('trades_settled') != 3 and mode == 'get':
                raise AssertionError('unexpected baseline result')
            if data.get('metrics', {}).get('trades_settled') != 1 and mode == 'post':
                raise AssertionError('unexpected custom replay result')
            return ms

        for mode in ('get', 'post'):
            with concurrent.futures.ThreadPoolExecutor(max_workers=args.concurrency) as pool:
                # Warm up outside the measurement window.
                for _ in range(10):
                    measure(mode)
                total_start = time.perf_counter()
                samples = sorted(pool.map(measure, [mode] * args.requests))
                wall = time.perf_counter() - total_start
            print(json.dumps({'mode': mode, 'requests': args.requests,
                'concurrency': args.concurrency, 'p50_ms': percentile(samples, 0.50),
                'p95_ms': percentile(samples, 0.95), 'p99_ms': percentile(samples, 0.99),
                'max_ms': round(samples[-1], 3),
                'completed_requests_per_second': round(args.requests / wall, 1)}))
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=5)


if __name__ == '__main__':
    main()
