# Verification record — November 18, 2026

The following statements are based on commands executed for this exact source version in the provided Linux build environment. They are not predictions about other operating systems or unexecuted CI jobs.

| Build / check | Toolchain | Result |
|---|---|---|
| Native Debug | GCC 14.2.0, CMake, Ninja, Boost 1.83 | `ctest`: **6/6 test groups pass** |
| Native Release | Clang 17, CMake, Ninja, Boost 1.83 | `ctest`: **6/6 test groups pass** |
| Sanitized Debug | Clang 17, AddressSanitizer + UBSan | `ctest`: **6/6 test groups pass** |
| C++ core test executable | GCC 14.2.0, Debug | **24/24 individual cases pass** |
| HTTP integration | Python stdlib + compiled Beast server | Nine scenarios, dashboard assets, bad requests, size limit and 32 concurrent requests pass |
| JavaScript syntax | Node.js 22 | `node --check web/app.js` passes |
| Synthetic benchmark | Clang 17 Release | 10,000 one-share trades / 20,000 orders completed in **0.0150 seconds** on this runner, including settlement and a final invariant check |

The benchmark is a machine-specific synthetic test, not a reproducible throughput guarantee. In particular it mostly matches one resting order per incoming order, rather than keeping an expensive multi-thousand-order book open.

**Not executed here:** Docker build / Docker Compose (Docker unavailable), GoogleTest network fetch (internet access unavailable), hosted GitHub Actions workflows (repository has not been published), and visual browser UI interaction (headless Chromium could not initialize in the sandbox; HTTP asset serving and JavaScript syntax were verified separately). These must not be represented as passing until run independently.

A source ZIP deliberately contains no executables, build trees, compiled libraries, dependency downloads, `.env` secrets or logs. The user can reproduce native verification via:

```bash
./scripts/test.sh
```

and sanitizer testing via the commands in the README.
