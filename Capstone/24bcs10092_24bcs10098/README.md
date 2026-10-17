# Low-Latency Order Book

| Team member | Roll number | SST email |
| --- | --- | --- |
| Daksh Shah | 24bcs10092 | shah.24bcs10092@sst.scaler.com |
| Tirth Bhalani | 24bcs10098 | tirth.24bcs10098@sst.scaler.com |

A C++17, single-instrument, single-threaded resting order book implemented three
ways. There is no matching or trade generation. No external order-book libraries
are required. Code, tests, demo, measured reports and six inspected, user-captured
PNG screenshots are available for review. Screenshot coverage is partial; the
team has selected these six images for the final submission.

## Contract and architecture

The five operations are `add_order`, `cancel_order`, `get_bbo`, `get_bids`, and
`get_asks`. Prices use integer ticks (100.01 = 10001): bids 5000..9999 and asks
10001..15000 inclusive. IDs are uint64_t; quantities and aggregates are positive
int64_t. BBO sides are independent optionals. Depth owns its aggregated price
levels, bids descending and asks ascending. Nonpositive depth returns empty.

Accepted IDs cannot be reused, even after cancellation. Rejected adds consume
no ID; ID zero is valid. Invalid side/price/quantity, duplicate IDs and aggregate
overflow return explicit results without logical mutation. Unknown cancellation
returns not_found. FIFO follows successful insertion order.

| Implementation | Primary price ordering | Cancellation and depth |
| --- | --- | --- |
| [A](impl_a/orderbook_a.hpp) | std::map levels with FIFO lists | ID locators unlink orders; ordered level traversal |
| [B](impl_b/orderbook_b.hpp) | Indexed vector-backed binary heaps of orders | ID positions repair swaps; aggregate hash levels copied/sorted for depth |
| [C](impl_c/orderbook_c.hpp) | Fixed 5000-level arrays and 79-word bitmaps per side | ID/list locators; best-first occupied-bit traversal |

All three own independent storage and permanent accepted-ID history. See
[implementations.md](implementations.md) for exact behavior, invariants,
complexities, allocation rollback and limitations.

## Directory guide

| Path | Purpose |
| --- | --- |
| [order.hpp](order.hpp), [order_book.hpp](order_book.hpp) | Shared types and five-operation interface |
| [impl_a/](impl_a/), [impl_b/](impl_b/), [impl_c/](impl_c/) | Independent implementations |
| [main.cpp](main.cpp) | Genuine deterministic demo |
| [verify.cpp](verify.cpp) | 78 deterministic cases and seeded reference-model verification |
| [bench.cpp](bench.cpp) | Exact-count benchmark and isolated profiling mode |
| [analysis.md](analysis.md) | Concise final interpretation and workload-specific recommendation |
| [benchmark.md](benchmark.md) | Original five-run measurements, commands, raw output and limitations |
| [profiling.md](profiling.md), [profiling/](profiling/) | Actual perf counters, ratios and call-graph evidence |
| [scripts/profile.sh](scripts/profile.sh) | Reproducible perf collection |
| [screenshots/README.md](screenshots/README.md) | Six selected screenshots and partial-coverage disclosure |

## Build in Ubuntu WSL

Requires GCC or Clang with C++17, CMake 3.16+, and Make or another CMake generator.
From the repository root in Ubuntu:

```sh
cd Capstone/24bcs10092_24bcs10098
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --verbose
```

This builds libraries orderbook_a/b/c and executables demo, verify and bench.
Release explicitly uses **-std=c++17 -O2 -DNDEBUG**; language extensions are off.
Generated build directories, objects, executables and local profiling binaries
are ignored. No committed build cache or binary is needed.

## Run the demo

```sh
./build/demo
```

Each implementation shows an empty book, five additions including same-price
aggregation, cancellation within a level, removal of the best bid/ask levels,
and rejection of a cancelled ID. All five APIs are exercised. Final quotes are
bid 98.50 x 7 and ask 102.00 x 8. Exit 0 and `DEMO CONSISTENCY PASS` require all
three to match the same explicit expected states; no trades are produced.

## Correctness verification

```sh
ctest --test-dir build --output-on-failure
./build/verify
./build/verify --random --ops 100000 --seed 42
./build/verify --random --ops 100000 --seed 7
./build/verify --random --ops 100000 --seed 2026
```

CTest registers one executable containing 78 named cases: 20 contracts per
implementation, seven B heap/sequence cases, nine C bitmap cases and two cross-
checks. Runtime checks remain active under NDEBUG. Random invocations first run
those cases, then use mt19937_64 with configurable seed (default 42) and operation
count (default 100000). Cancels choose live IDs only; IDs never repeat.

The independent oracle uses append-only records and sorted aggregate vectors.
It compares operation results, BBO and both top-ten depths after every operation;
full depth, internal invariants and an independently rebuilt oracle are checked
every 1000 operations and at final partial checkpoints. Failures print seed,
operation, implementation and expected/observed values, and exit nonzero.

Previously observed Release runs of 100000 operations for seeds 42, 7 and 2026
passed. ASan/UBSan also passed 100000 seed-42 operations in the verification stage.
Final readiness checks passed all 78 cases and a fresh 10000-operation seed-42
sample: 6989 adds, 1990 cancels, 1021 BBO, 11 full/invariant checkpoints.
Finite seeds and these results do not prove exhaustive correctness.

Optional sanitizer build, from this project directory:

```sh
g++ -std=c++17 -O2 -g -fsanitize=address,undefined \
  -fno-omit-frame-pointer -Wall -Wextra -Wpedantic -I . \
  impl_a/orderbook_a.cpp impl_b/orderbook_b.cpp impl_c/orderbook_c.cpp \
  verify.cpp -o build/verify_sanitized
./build/verify_sanitized --random --ops 100000 --seed 42
```

## Mandatory benchmark

```sh
./build/bench --help
./build/bench --impl A --ops 1000000 --runs 5 --seed 42
./build/bench --impl B --ops 1000000 --runs 5 --seed 42
./build/bench --impl C --ops 1000000 --runs 5 --seed 42
```

Run sequentially. Each invocation generates one trace, audits it and validates
A/B/C outcomes outside timing, then replays the same trace five times on newly
constructed empty books. Counts are exactly 700000 adds, 200000 live cancels,
100000 BBO, plus 10000 calls per depth side. Seed-42 fingerprint is
1002121172284863879; checksum 12750139682901687748; final BBO is bid 9999:2988,
ask 10001:2344 (ticks:quantity).

CLI bounds: ops=10..2000000, runs=1..25, seed=0..UINT64_MAX; --impl is required.
Custom counts use floor(7*N/10) adds, floor(2*N/10) cancels, BBO=remainder and
floor(N/100) queries per depth side. Defaults are 1000000/5/42.

Original five-run overall medians: **A 429.377 ms, B 6737.518 ms, C 327.892 ms**.
Depth and instrumentation are included. These are preliminary WSL measurements,
not pure operation latencies. RSS includes trace and untimed validation, not
selected-book-only memory. [benchmark.md](benchmark.md) retains the exact original
results; later smoke checks do not replace those five-run measurements.

## Linux perf profiling

Use --profile-only when wrapping bench with perf: standard mode executes other
implementations' validation passes. Profile-only executes only the selected
implementation and validates the approved fixture; it intentionally supports
only ops=1000000, seed=42, runs=1 (default in this mode).

From the project directory:

```sh
cmake -S . -B build-profile -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_FLAGS="-g -fno-omit-frame-pointer"
cmake --build build-profile
./build-profile/bench --impl A --ops 1000000 --runs 1 --seed 42 --profile-only
sh scripts/profile.sh
```

The script collects five-repeat counters and grouped rate denominators, then
cycles:u call graphs for each implementation. It clears DEBUGINFOD_URLS only
locally because remote symbol lookup stalled. Kernel security settings remain
unchanged. All requested events were available at 100% running time, and the
recorded profiles reported zero lost samples. Perf includes setup, the selected
replay, validation and destruction, not just the chrono replay window.

[profiling.md](profiling.md) preserves exact commands, counter means, denominators,
hotspots and WSL limitations. Raw small text evidence is tracked under profiling/;
perf.data and full local reports are ignored under build-profile/.

## Screenshots and submission readiness

Six user-captured screenshots were visually inspected for readability and relevance:

- [Deterministic verification](screenshots/verification.png): the final verification output and 78-case count.
- [A/B/C demo](screenshots/demo.png): all expected states and final consistency PASS.
- Archived perf counters: [A](screenshots/perf-stat-A.png), [B](screenshots/perf-stat-B.png), [C](screenshots/perf-stat-C.png).
- [B hotspots](screenshots/perf-report-B.png): introsort 43.66% and depth 43.57% self overhead.

The perf images show terminal views of previously collected evidence files,
not new measurements. Long report symbols are clipped, but the relevant names
and percentages remain readable. See the [inventory](screenshots/README.md).
**Screenshot coverage is partial.** The team has chosen to submit these six
existing images. Release-build, mandatory benchmark A/B/C and perf-report A/C
output is not covered by screenshots; build/test instructions and measured text
evidence remain available in the documentation. No further captures are planned.

[analysis.md](analysis.md) recommends C for this bounded-price workload while
explaining B's measured sorting bottleneck, memory/history costs and measurement
limits. Allocation-failure injection, exhaustive traces, isolated cold-start/tail-
latency measurements and controlled native Linux benchmarking remain unperformed.
The code is an educational submission, not a production-readiness certification.

The directory organization was informed by the changed paths, README and CMake
of [PR #159](https://github.com/KnightKnight27/scaler-HFT-C--2028/pull/159) at head
20cbbca2fe0694c63482f5794f3cf2912fd95b2a. No implementation source, report prose,
measurements or screenshots were copied from that reference.
