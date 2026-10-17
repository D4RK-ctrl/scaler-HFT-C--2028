# Low-Latency Order Book

| Team member | Roll number | SST email |
| --- | --- | --- |
| Daksh Shah | 24bcs10092 | shah.24bcs10092@sst.scaler.com |
| Tirth Bhalani | 24bcs10098 | tirth.24bcs10098@sst.scaler.com |

Stage 5: A/B/C are approved; seeded reference-model verification is added for review.
C++17, Release `-O2`, single instrument and single thread. No matching or trades.
A/B/C have deterministic and randomized correctness verification. No benchmark exists.

## Layout

| Path | Current purpose |
| --- | --- |
| order.hpp | Integer tick/quantity types, orders, aggregated levels, optional BBO sides and result enums |
| order_book.hpp | Abstract interface for the five required operations |
| impl_a/ | Working map/FIFO OrderBookA (.hpp and .cpp) |
| impl_b/ | Indexed binary-heap OrderBookB (.hpp and .cpp) |
| impl_c/ | Fixed tick-array/occupancy-bitmap OrderBookC (.hpp and .cpp) |
| main.cpp | Comment-only placeholder for a later demo; not built |
| verify.cpp | Original 78 deterministic cases plus seeded independent-reference random verification |
| bench.cpp | Comment-only placeholder for a later benchmark; not built |
| implementations.md | Proposed API contract, invariants, designs, complexity and review questions |
| benchmark.md | Placeholder and future measurement requirements; no results |
| screenshots/ | Only .gitkeep; no screenshots yet |

Shared headers and entry points are at the project root to keep navigation simple.
CMake builds orderbook_a, orderbook_b and orderbook_c static libraries and verify.
Demo/benchmark placeholders are not built.

## Build and verify A/B/C in Ubuntu WSL

From the repository root:

```sh
cmake -S Capstone/24bcs10092_24bcs10098 -B Capstone/24bcs10092_24bcs10098/build -DCMAKE_BUILD_TYPE=Release
cmake --build Capstone/24bcs10092_24bcs10098/build --verbose
ctest --test-dir Capstone/24bcs10092_24bcs10098/build --output-on-failure
./Capstone/24bcs10092_24bcs10098/build/verify
```

Requires GCC or Clang, CMake 3.16+, and Make (or another CMake generator).
The verify executable runs 78 named cases. CTest registers one executable,
implementations_abc. Release uses `-O2 -DNDEBUG -std=c++17`; runtime checks
remain active under NDEBUG.
Generated build directories are ignored within this submission.

## Public semantics and independent designs

The interface takes integer ticks (`100.01 = 10001`), uint64_t order IDs and
integer quantities. Bids are 5000..9999 ticks; asks are 10001..15000 ticks.
Depth returns aggregated price levels, bids descending and asks ascending.
BBO includes aggregate quantities; optional sides provide explicit presence
flags through `has_value()`. IDs cannot be reused, even after cancellation;
A, B and C retain accepted-ID history for the book lifetime. Rejected adds do not consume
IDs, ID zero is valid, and invalid inputs or quantity overflow leave state unchanged.
Depth <= 0 returns an empty snapshot; unknown cancellations return not_found. See
[implementations.md](implementations.md) for the complete contract.

- A (implemented): ordered std::map price levels, FIFO order lists and live-ID locators.
- B (implemented): indexed binary heaps of orders, ID positions and price aggregate hash tables;
  depth copies and sorts occupied levels. Additional synchronization and sorting
  costs are explicit design concerns.
- C (implemented): fixed tick arrays, occupancy bitmap, FIFO order lists and live-ID locators.

A/B/C use no external libraries and own independent storage and mutation logic.
The shared types and interface do not select a default implementation yet.

## Reference organization

[PR #159](https://github.com/KnightKnight27/scaler-HFT-C--2028/pull/159) was
inspected at head `20cbbca2fe0694c63482f5794f3cf2912fd95b2a`: changed paths,
README and CMake only. Its root shared headers/entry points, implementation
subdirectories and report locations informed this layout. Its CMake has separate
implementation libraries and demo/verify/bench executables; A/B/C libraries and their shared verification target exist here. We retain C++17 and our map/indexed-heap/tick-array designs.
No source implementation, reports, measurements or screenshots were copied.
The future workload remains 1,000,000 primary operations; see benchmark.md.

B completes allocations before heap movements and rolls back provisional entries on
failure. Allocation-failure injection remains future work. Arrival-sequence exhaustion
throws before mutation. See implementations.md for the strategy and limitations.

C uses 5,000 fixed levels and 79 bitmap words per side. Its last word has eight
valid bits; queries mask unused bits. Cached totals and FIFO-node locators avoid
order scans for depth/cancellation. Its list/index allocations finish before
committing totals and occupancy; allocation-failure injection remains future work.

## Seeded randomized correctness verification

Run from the project directory in Ubuntu WSL after the Release build:

```sh
./build/verify
./build/verify --random --ops 100000 --seed 42
./build/verify --random --ops 100000 --seed 7
./build/verify --random --ops 100000 --seed 2026
```

Every invocation runs the existing 78 deterministic cases first. `--random`
defaults to 100000 operations and seed 42; `--help` shows usage. Numeric options
are unsigned decimal integers. `--ops` and `--seed` require `--random`.

The generator uses mt19937_64 and explicit rejection sampling: approximately
70% adds, 20% cancellations from live IDs only, and 10% BBO operations. An empty
cancellation choice becomes an add. Add side is sampled 50/50, quantities are
1..100 and tick prices are uniform across each side's 5000 valid ticks. IDs are
monotonic and never reused. The printed exact counts need not equal the nominal
percentages. Additional BBO/depth comparisons are verification work outside
those primary operation counts.

The independent model uses append-only order records and sorted aggregate
vectors, with no production map, heap, bitmap or locator code. Every operation
compares results, BBO and both top-ten snapshots. Full-depth comparisons and
internal invariant checks run initially, every 1000 operations and at the final
partial checkpoint. The model also rebuilds aggregates from live records then.

Failures report seed, one-based operation index (0 for initial state), operation
details, implementation, check, expected and observed values; exit is nonzero.
Progress is printed every 10000 operations. Runtime is correctness-verification
wall time, not order-book benchmark latency or throughput. Logs under build/
are ignored. No benchmark or perf measurements are performed.

Sanitizer commands, also from the project directory:

```sh
g++ -std=c++17 -O2 -g -fsanitize=address,undefined \
  -fno-omit-frame-pointer -Wall -Wextra -Wpedantic -I . \
  impl_a/orderbook_a.cpp impl_b/orderbook_b.cpp impl_c/orderbook_c.cpp \
  verify.cpp -o build/verify_sanitized
./build/verify_sanitized --random --ops 100000 --seed 42
```

### Observed Stage 5 Release results

All original 78 deterministic cases passed. The following reference-model runs
also passed with zero mismatches; each completed 101 full-depth/invariant checks.

| Seed | Primary ops | Adds | Live-ID cancels | BBO ops | Bid adds | Ask adds | Final live | Verification seconds |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 42 | 100000 | 70017 | 19906 | 10077 | 35098 | 34919 | 50111 | 126.965 |
| 7 | 100000 | 69882 | 20155 | 9963 | 34910 | 34972 | 49727 | 152.467 |
| 2026 | 100000 | 69966 | 20164 | 9870 | 35047 | 34919 | 49802 | 153.103 |

Durations include verification overhead and concurrent test-run load. They are
not benchmark measurements and should not be used to compare implementations.
Five malformed CLI cases were rejected: negative count, uint64 overflow, seed
without --random, missing count, and unknown flag. A zero-operation run passed
with default seed 42 and one initial full/invariant checkpoint.

Repeatability check: two seed-42 runs of 1234 operations produced identical output
excluding elapsed time (842 adds, 277 cancels, 115 BBO operations, three checkpoints).
ASan/UBSan also passed all 78 deterministic cases and 100000 random operations
with seed 42, exit 0, with no sanitizer diagnostics. Counts matched Release:
70017 adds, 19906 cancels, 10077 BBO operations and 101 full/invariant checkpoints.
Observed sanitizer verification wall time: 309.457 seconds; this is checking
runtime, not a benchmark. Logs are retained only in ignored build artifacts.
