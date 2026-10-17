# Implementation architecture — Stage 2

Implementation A is implemented. B and C remain independent proposed designs;
no B/C code, cross-implementation verification, benchmark or profiling exists yet.

## Scope and common API

C++17, Release `-O2`, one instrument and one thread. No matching or trades.
Prices are integer ticks: bid 5000..9999 and ask 10001..15000 inclusive,
corresponding to tick size 0.01 and exactly 5,000 legal levels per side.

`OrderId = uint64_t`, `PriceTicks = int32_t`, `Quantity = int64_t`, and
`Depth = int64_t`. Individual orders and aggregate quantities use the same signed
quantity type. Input prices are already ticks; no floating-point conversion occurs.

```cpp
AddResult add_order(const Order& order);
CancelResult cancel_order(OrderId id);
Bbo get_bbo() const;
DepthSnapshot get_bids(Depth depth) const;
DepthSnapshot get_asks(Depth depth) const;
```

`Order` holds ID, side, price and quantity. `PriceLevel` holds price and total
live quantity. Each BBO side is an independent `optional<PriceLevel>`; its
`has_value()` is the presence flag. Depth returns owned vectors of aggregated
levels, bids descending and asks ascending. Depth counts distinct prices, not
orders. Queries do not mutate the book. Old BBO/depth snapshots remain valid and
unchanged after book mutations; modifying a snapshot cannot modify the book.

## Implemented A contract

| Case | Actual A behavior |
| --- | --- |
| Valid add | Append the full resting order; return `added`. |
| Invalid enum side | Return `invalid_side`. |
| Price outside the inclusive side range | Return `invalid_price`. |
| Quantity <= 0 | Return `invalid_quantity`. |
| ID accepted previously, live or cancelled, on either side | Return `duplicate_id`. |
| Aggregate would exceed `INT64_MAX` | Return `quantity_overflow`. |
| Unknown/already-cancelled cancellation | Return `not_found`; no mutation. |
| Known cancellation | Remove the entire order; return `cancelled`. |
| Last order removed at a price | Erase that price level. |
| Empty BBO side | `nullopt`, independently of the other side. |
| Empty side or depth <= 0 | Empty depth vector. |
| Depth above available levels | All occupied levels, best-first. |
| ID zero or UINT64_MAX | Valid; subject to permanent uniqueness. |
| Rejected add | No logical state change; ID is not consumed. |

Validation precedence is side, price, quantity, accepted-ID history, aggregate
overflow. All business-input failures return an enum and leave logical state
unchanged. The overflow check uses `qty > max - total` before adding, avoiding
signed overflow. There is no book-wide quantity sum, only per-level totals.

IDs cannot be reused for the lifetime of a book instance. `used_ids_` records
successful adds permanently; cancel only removes the corresponding `live_`
entry. Invalid adds and unknown cancellations consume no ID. There is no reset
API. Separate new book instances have independent ID histories.

A stores FIFO order through list insertion order, so no timestamp or arrival
counter is needed. Orders are never sorted by ID. A has no sequence-exhaustion
condition. Whether future public input must contain timestamps remains a reviewer
question; current FIFO follows successful call order.

## Actual A storage and five operations

`impl_a/orderbook_a.hpp` declares the final `OrderBookA` class. Its source owns:

- Two ascending `std::map<PriceTicks, Level>` containers, one per side.
- Each occupied level holds a cached total and a `std::list<Order>` in FIFO order.
- `unordered_map<OrderId, Locator> live_` holds side, map iterator and list iterator.
- `unordered_set<OrderId> used_ids_` permanently remembers accepted IDs.

Both maps use the same ascending map type, keeping cancellation locators simple.
Bid queries use reverse iterators, while ask queries use forward iterators.

1. `add_order`: validate, look up the price, check aggregate overflow, create a
   level if needed, append the list node and insert both ID records. Commit the
   total only after all insertions succeed.
2. `cancel_order`: find the live locator, subtract its node quantity, unlink the
   node and erase the live record; erase the map level if its list becomes empty.
3. `get_bbo`: copy the bid reverse endpoint and ask forward endpoint, including
   cached totals; leave absent sides empty.
4. `get_bids`: copy up to depth reverse-map levels into an owned vector.
5. `get_asks`: copy up to depth forward-map levels into an owned vector.

Add rolls back a newly created level, appended node and accepted-ID insertion if
a later allocation fails. `std::bad_alloc` propagates; there is no noexcept promise.
Logical contents are restored, although a hash table may retain increased bucket
capacity. Deterministic allocation-failure injection is not part of the current
test suite; the rollback paths are code-reviewed, not fault-injection verified.
Snapshot allocation can throw without altering book state.

Copy and move are explicitly disabled because locators refer to the instance's
nodes. No custom allocator, pool, threads or external library is used.

## Invariants and verification

- Every live order has legal side/price, positive quantity and one unique ID.
- Every live ID is in accepted-ID history; cancelled IDs remain in that history.
- Each map level is nonempty and its cached total equals its FIFO-node sum.
- Every locator identifies the correct side, map level and order node.
- Live-node count equals live-index size; there are no unindexed or extra nodes.
- FIFO preserves insertion order after first/middle/last cancellations.
- BBO prices are map endpoints and BBO quantities equal level totals.
- Rejected business operations do not change snapshots, totals or ID history.

`verify.cpp` defines 20 deterministic named A test cases with explicit expected
prices, totals and FIFO IDs. Helpers check internal invariants after each wrapped
add/cancel. A friend test-access struct is defined only in verification code; it
does not add operations to the public API. Tests use runtime checks rather than
assert, so Release `-DNDEBUG` does not disable them. They cover both sides, empty
books, endpoints, invalid inputs, all cancellation positions, history, overflow,
owned snapshots and dispatch through the abstract interface. They are not B/C or
random differential tests.

## B. Indexed binary heaps — proposed only

Separate vector-backed order heaps prioritize best price then arrival sequence.
An ID hash index stores side and heap position; swaps repair both affected
positions. Cancellation replaces the target with the last element and repairs
heap order. A separate aggregate hash per side caches price totals, so BBO reads
the root price then its aggregate. Depth copies all occupied aggregate levels,
sorts best-first and truncates. A permanent accepted-ID set enforces ID lifetime.

This differs from a map ordering structure. Concerns are synchronizing heap,
positions, totals and history, plus O(L log L) depth even for a shallow request.
Without cached aggregates BBO quantity would require scanning orders. No B
implementation or sequence-exhaustion policy is finalized at Stage 2.

## C. Tick arrays and occupancy bitmap — proposed only

Each side has 5,000 fixed levels, FIFO order lists and totals. Tick offsets are
`price - 5000` for bids and `price - 10001` for asks. An occupancy bitmap uses
79 64-bit words per side, masking unused bits. BBO searches from the best end;
depth visits set bits best-first. ID locators allow node unlinking; cancellation
clears a bit when a level empties. A separate accepted-ID set retains history.

This differs from tree and heap ordering. Costs include fixed empty-level storage,
bitmap maintenance and ordinary list-node allocations. No pool or hierarchical
bitmap is currently proposed. C++17 bit helpers must handle zero words safely.

## Expected complexity and limitations

N = live orders; U = lifetime accepted IDs; L = occupied levels on the relevant
side; K = min(positive depth, L); P = 5,000; W = ceil(P/64) = 79 per side.
Hash costs below are expected amortized, not hard latency guarantees.

| Operation | A: implemented | B: proposed | C: proposed |
| --- | --- | --- | --- |
| Add | O(log(L+1)) | O(log(N+1)) | O(1) |
| Cancel | O(1) amortized | O(log(N+1)) | O(1) |
| BBO | O(1) | O(1) expected | O(W) |
| Positive depth including output | O(K) | O(L log(L+1)) | O(W + K) |
| Nonpositive depth | O(1) | O(1) | O(1) |
| Persistent storage | O(U + N + L_bid + L_ask) | O(U + N + L_bid + L_ask) | O(U + N + P) |
| Additional depth storage | O(K) | O(L) | O(K) |

A's map erase by iterator is amortized constant, not a guarantee for every call.
Its list and map nodes allocate separately and cause pointer chasing. Hash
collisions can make live lookup O(N) and history lookup O(U); rehashing and
allocation cause latency spikes. Permanent history memory grows even as orders
are cancelled. B may also reallocate its heap vector. No performance numbers
are inferred from these bounds.

## Later authorized work and remaining decisions

B/C implementation, cross-implementation verification, demo and benchmark are
not part of Stage 2. Future verification should add an independent reference
model, generated traces and allocation-failure injection.

The benchmark must use exactly 1,000,000 primary operations: 700,000 adds,
200,000 cancellations of live orders and 100,000 BBO calls. Both depth queries
run every 100 primary operations. Use identical pre-generated traces, five runs
per implementation, medians and actual Linux perf analysis. Seed, depth,
warmup, timing boundaries and metrics still need review. Trace generation and
correctness checks must remain outside timing, with query results consumed.

Reviewer questions: approve current A behavior, future B's aggregate/hash/sort
tradeoff, any timestamp requirement, sequence exhaustion for B, and final
benchmark parameters. No benchmark results or screenshots exist at this stage.
