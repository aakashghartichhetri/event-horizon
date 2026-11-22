# Benchmark evidence and reproduction

## Measurement environment

- Date: November 21, 2026.
- Hardware reported by Linux: AMD EPYC 9V74 80-Core Processor, **5 visible virtual CPUs**, x86-64, 5.8 GiB RAM, virtualized KVM environment.
- GCC 14.2.0, CMake 3.31.6, Ninja, Linux kernel 6.18.57.
- CMake Release builds (`-O3 -DNDEBUG`), no sanitizers for timing runs.
- The same `tests/benchmarks.cpp` harness was compiled against the archived baseline library and the modified library. Each measurement launched a separate process; **five independent samples**, medians shown. No CPU pinning, clock control, or production load shaping was used.
- Timed operation excludes account setup, final settlement, invariant checking and output formatting, except the `replay` mode, which includes scenario I/O and the engine's replay verification. `book` setup time is reported separately.

## Before and after (lower is better)

| Scenario | Archived baseline median | Modified median | Observation |
|---|---:|---:|---|
| 9,000 one-fill order pairs (matching only) | 8.677 ms | 9.195 ms | 6% slower |
| 4,000 resting asks swept by one incoming buy | 15.127 ms | 0.696 ms | ~21.7x faster |
| 7,000 resting asks swept by one incoming buy | 45.118 ms | 1.231 ms | ~36.7x faster |
| 300 baseline scenario replays | 5.787 ms | 5.428 ms | Slightly faster; close to noise |

The old matching algorithm repeatedly searched the entire resting book for each execution. Profiling the baseline (`gprof`, GCC `-pg`) on a 7,000-order sweep attributed **63.64% of sampled CPU time** to `Exchange::best_match` (sampling interval 10 ms, so percentages are approximate). The new implementation scans crossing orders once and sorts by price and sequence before filling. For `N` resting orders and `K` eligible orders, admission planning is `O(N + K log K)`, followed by `O(F)` execution for `F` fills. Book insertion, cancellation, and cleanup remain vector-based and are not production-grade low-latency data structures.

### Reproduce native benchmarks

```bash
cmake -S . -B build-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DEH_WARNINGS_AS_ERRORS=ON
cmake --build build-release -j 2
./build-release/event-horizon-bench single 9000
./build-release/event-horizon-bench book 4000
./build-release/event-horizon-bench book 7000
./build-release/event-horizon-bench replay 300
/usr/bin/time -v ./build-release/event-horizon-bench book 7000
```

Sample modified run: 7,000-order sweep `setup_ms=54.274`, `operation_ms=1.343`, 7,000 settlements; `/usr/bin/time -v` reported **5,016 KiB** peak resident memory on Linux. Peak RSS includes process initialization, data structures, and measurement overhead, and is not an allocation-profile breakdown. The archive's older core can be checked out and built separately to reproduce before/after comparisons; never compare Debug and Release timing.

### Local HTTP latency

```bash
python3 scripts/bench_http.py --server ./build-release/event-horizon-server \
    --requests 200 --concurrency 4
```

A local-only Boost.Beast service was started on a temporary loopback port with four workers. After warmup, **200 GET requests** for the baseline scenario had `p50=1.632 ms`, `p95=3.319 ms`, `p99=6.084 ms`. **200 POST requests** for a short custom settlement replay had `p50=1.335 ms`, `p95=2.239 ms`, `p99=2.840 ms`. All returned results were checked. This is client-observed response latency on one machine; no claims are made about internet deployment, scaling across hosts, or service-level objectives.

### Test and instrumentation limitations

- GCC Debug, Clang Release, Clang ASan+UBSan, and GCC TSan each passed the local CTest suite (11 groups).
- GCC TSan compilation of Boost.Asio emits a documented `-Wtsan` warning about `atomic_thread_fence`; use `-DEH_WARNINGS_AS_ERRORS=OFF` for that instrumentation build.
- Clang static analysis completed on all four engine/replay/report/resilience source files without findings. This is not a proof of correctness.
- Docker runtime, hosted GitHub Actions, full graphical browser accessibility, memory-leak behavior under production traffic, and public-network load testing were **not** verified here.
- Results fluctuate with hardware, scheduling, compiler versions, and load. Collect multiple rounds on your own machine before discussing optimizations in an interview.

## November 22, 2026: transaction staging and shutdown review

Same host, GCC 14.2.0, Release (`-O3 -DNDEBUG`), CMake 3.31.6, Ninja, Linux x86-64, **five visible virtual Intel Xeon Platinum 8573C CPUs**. The earlier hardened source ZIP and the updated source were independently built with the same compiler and benchmark harness. Each figure below is the median of five fresh process runs; operation time excludes book setup and final settlement unless it is the replay workload.

| Workload | Earlier hardened | This release | Interpretation |
| --- | ---: | ---: | --- |
| 7,000-order deep-book sweep | 1.672 ms | 2.186 ms | About 31% slower; pre-built trade records and capacity planning add work |
| 9,000 one-fill matches | 12.040 ms | 11.731 ms | Similar performance within measurement noise |
| 300 baseline scenario replays | 7.531 ms | 7.013 ms | Similar performance within measurement noise |

Some individual samples were outliers (the new sweep included a 27.500 ms sample); this VM was not CPU-isolated. The earlier 2026-11-21 deep-book speedup documented above still reflects the optimization from repeated best-match scans to sorting once; the safety upgrade trades away some of that speed. Do not compare across different CPUs, builds, compilers, or workloads as though the results were controlled production measurements.

The HTTP service now polls readiness instead of sleeping between accepts. A local sample of 100 concurrent GETs (four clients) showed `p50=2.552 ms, p95=4.206 ms`; 100 POSTs showed `p50=2.276 ms, p95=4.108 ms`. The CTest integration suite also verified 32 concurrent independent replays, stalled-connection interruption, and process exit on SIGTERM. These are loopback measurements, not Internet load tests.
