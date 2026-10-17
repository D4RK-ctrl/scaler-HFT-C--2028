# Low-Latency Order Book

| Team member | Roll number | SST email |
| --- | --- | --- |
| Daksh Shah | 24bcs10092 | shah.24bcs10092@sst.scaler.com |
| Tirth Bhalani | 24bcs10098 | tirth.24bcs10098@sst.scaler.com |

Stage 3: A is approved; B is implemented for review. C remains proposed only.
C++17, Release `-O2`, single instrument and single thread. No matching or trades.
A/B have deterministic functional verification and an initial cross-check. No benchmark exists.

## Layout

| Path | Current purpose |
| --- | --- |
| order.hpp | Integer tick/quantity types, orders, aggregated levels, optional BBO sides and result enums |
| order_book.hpp | Abstract interface for the five required operations |
| impl_a/ | Working map/FIFO OrderBookA (.hpp and .cpp) |
| impl_b/ | Indexed binary-heap OrderBookB (.hpp and .cpp) |
| impl_c/ | Reserved directory; only .gitkeep |
| main.cpp | Comment-only placeholder for a later demo; not built |
| verify.cpp | 48 cases: 20 A, 20 B contract, 7 B heap/sequence, 1 A/B cross-check |
| bench.cpp | Comment-only placeholder for a later benchmark; not built |
| implementations.md | Proposed API contract, invariants, designs, complexity and review questions |
| benchmark.md | Placeholder and future measurement requirements; no results |
| screenshots/ | Only .gitkeep; no screenshots yet |

Shared headers and entry points are at the project root to keep navigation simple.
CMake builds orderbook_a and orderbook_b static libraries and the verify executable.
Demo/benchmark placeholders are not built. C has no implementation or target.

## Build and verify A/B in Ubuntu WSL

From the repository root:

```sh
cmake -S Capstone/24bcs10092_24bcs10098 -B Capstone/24bcs10092_24bcs10098/build -DCMAKE_BUILD_TYPE=Release
cmake --build Capstone/24bcs10092_24bcs10098/build --verbose
ctest --test-dir Capstone/24bcs10092_24bcs10098/build --output-on-failure
./Capstone/24bcs10092_24bcs10098/build/verify
```

Requires GCC or Clang, CMake 3.16+, and Make (or another CMake generator).
The verify executable runs 48 named cases. CTest registers one executable,
implementations_ab. Release uses `-O2 -DNDEBUG -std=c++17`; runtime checks
remain active under NDEBUG. C is not implemented or tested.
Generated build directories are ignored within this submission.

## Public semantics and independent designs

The interface takes integer ticks (`100.01 = 10001`), uint64_t order IDs and
integer quantities. Bids are 5000..9999 ticks; asks are 10001..15000 ticks.
Depth returns aggregated price levels, bids descending and asks ascending.
BBO includes aggregate quantities; optional sides provide explicit presence
flags through `has_value()`. IDs cannot be reused, even after cancellation;
A and B retain accepted-ID history for the book lifetime. Rejected adds do not consume
IDs, ID zero is valid, and invalid inputs or quantity overflow leave state unchanged.
Depth <= 0 returns an empty snapshot; unknown cancellations return not_found. See
[implementations.md](implementations.md) for the complete contract.

- A (implemented): ordered std::map price levels, FIFO order lists and live-ID locators.
- B (implemented): indexed binary heaps of orders, ID positions and price aggregate hash tables;
  depth copies and sorts occupied levels. Additional synchronization and sorting
  costs are explicit design concerns.
- C (proposed): fixed tick arrays, occupancy bitmap, FIFO order lists and live-ID locators.

A/B use no external libraries and own independent storage and mutation logic.
The shared types and interface do not select a default implementation yet.

## Reference organization

[PR #159](https://github.com/KnightKnight27/scaler-HFT-C--2028/pull/159) was
inspected at head `20cbbca2fe0694c63482f5794f3cf2912fd95b2a`: changed paths,
README and CMake only. Its root shared headers/entry points, implementation
subdirectories and report locations informed this layout. Its CMake has separate
implementation libraries and demo/verify/bench executables; A/B libraries and their shared verification target exist here. We retain C++17 and our map/indexed-heap/tick-array designs.
No source implementation, reports, measurements or screenshots were copied.
The future workload remains 1,000,000 primary operations; see benchmark.md.

B completes allocations before heap movements and rolls back provisional entries on
failure. Allocation-failure injection remains future work. Arrival-sequence exhaustion
throws before mutation. See implementations.md for the strategy and limitations.
