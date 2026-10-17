# Implementation architecture — Stage 3

Implementations A and B are implemented independently. C remains proposed only.
An initial deterministic A/B cross-check exists; no benchmark or profiling exists.

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

## Shared A/B contract

| Case | Actual A/B behavior |
| --- | --- |
| Valid add | Store the full resting order; return `added`. |
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
owned snapshots and dispatch through the abstract interface. The original 20 A cases are preserved as a shared templated suite and also run
against B. C has no tests. These are not random differential tests.

## B. Indexed binary heaps — implemented

`impl_b/orderbook_b.hpp` and `.cpp` define `OrderBookB`, independently of A.
Each side stores individual orders in a contiguous `vector<Entry>`. An entry
contains the shared `Order` and an internal uint64_t arrival sequence. Bids
prioritize higher prices, asks lower prices; price ties prioritize smaller
sequences. IDs do not determine priority. Copy/move are disabled for a simple
instance-lifetime policy consistent with A.

`live_` maps ID to side and vector position. `bid_totals_` and `ask_totals_`
are unordered price-to-quantity maps, never ordered maps. `used_ids_` retains
all accepted IDs permanently, independently of whether their orders are live.
The production B implementation does not depend on A or use std::map.

### Five operations and cancellation

- Add uses the same validation precedence and enum results as A, appends an
  entry, creates its index/history/aggregate records, and sifts upward.
- Cancel locates the order immediately, swaps it with the last entry, updates
  both position records, pops the vector, erases its live record, and subtracts
  its aggregate. A zero total removes that price record. If a replacement
  remains, it sifts upward when better than its parent, otherwise downward.
  There are no tombstones, lazy deletion or stale heap entries.
- Every heap swap updates both affected ID positions. Vector growth changes
  addresses but not indexed positions; no pointers into vector storage are kept.
- BBO reads each heap root's price and the corresponding aggregate total, or
  returns an absent optional side. No scan over same-price orders is needed.
- Bid/ask depth copies only occupied aggregate levels, sorts by price best-first,
  and truncates to depth. A nonpositive request returns before allocation.
  Even depth 1 copies/sorts all occupied levels; returned snapshots own their data.

### Allocation ordering and exception safety

All validation and sequence-exhaustion checks happen before mutation. An Entry
contains only trivial scalar fields, so vector push_back can grow with its strong
exception guarantee. If that succeeds, no heap movement occurs yet. The code then:

1. Creates/finds an aggregate record (a new one initially has total zero).
2. Inserts the accepted-ID record.
3. Inserts the live ID/position record.
4. Commits the aggregate quantity and increments the sequence, then sifts up.

If steps 1-3 throw, completed history/aggregate insertions are erased and the
provisional vector entry is popped. If vector growth itself throws, no index
has yet changed. Existing aggregate totals and next sequence are not modified
until all allocations complete. No further allocations occur during commit or
sifting: Entry swaps are trivial, integer arithmetic is prechecked and existing
position records use find rather than insertion. Fixed integral hash/equality
operations do not throw. Cancellation likewise needs no allocation.

This strategy is intended to preserve logical state on allocation failure;
vector capacity and hash bucket counts may remain larger. Allocation failure
has not been injected deterministically in the tests, so exception-path coverage
is a known limitation, not a fault-injection-verified guarantee. Query allocation
failure leaves the book unchanged. Internal logic_error checks indicate an index
invariant bug, not a normal user-input error.

Sequence values 0..UINT64_MAX-1 are available. Once next_seq_ reaches UINT64_MAX,
a business-valid new order throws overflow_error before mutation or ID consumption;
there is no wraparound or renumbering. Business-invalid adds still return their
normal validation result first. Sequence exhaustion is tested via friend access.
A uses list order and has no equivalent sequence limit.

### B invariants and deterministic tests

The B test-access struct is defined only in verify.cpp. It independently checks
heap ordering, unique arrival sequences, every ID-to-position locator, legal
order fields, permanent history membership, heap/live counts and recomputed
per-price totals against the aggregate hash tables. FIFO IDs are reconstructed
by sequence, and explicit root-draining tests verify tie priority itself.

There are 48 named cases in total: the original 20 A contract cases, the same
20 applied to B, seven B-specific cases and one deterministic A/B cross-check.
The B-specific cases cover root/downward repair, middle/upward repair,
middle/downward repair, last-element removal, FIFO root order, 256-order
repeated-swap/growth/locator consistency, and sequence exhaustion. Each applicable
repair fixture runs on both sides. Overflow tests retry the rejected ID and check
unchanged snapshots/history. All runtime checks remain enabled under NDEBUG.

The A/B cross-check compares add/cancel results, BBO presence/prices/quantities
and both depth snapshots at depths -1, 0, 1, 3 and INT64_MAX after every operation.
It covers invalid input, overflow/retry, cancelled-ID rejection, repeated prices,
unknown/repeated cancellation and complete draining. This is initial verification,
not an independent reference model or a benchmark; shared bugs remain possible.
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

| Operation | A: implemented | B: implemented | C: proposed |
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
are cancelled. B may also reallocate its heap vector and sort all occupied levels
on shallow depth requests. Each heap swap performs two locator hash lookups;
adversarial collisions can make a full repair O(N log(N+1)), and a history rehash
can cost O(U). Vector capacity is retained after cancellations. No performance numbers
are inferred from these bounds.

## Later authorized work and remaining decisions

C implementation, broader differential verification, demo and benchmark are
not part of Stage 3. Future verification should add an independent reference
model, generated traces and allocation-failure injection.

The benchmark must use exactly 1,000,000 primary operations: 700,000 adds,
200,000 cancellations of live orders and 100,000 BBO calls. Both depth queries
run every 100 primary operations. Use identical pre-generated traces, five runs
per implementation, medians and actual Linux perf analysis. Seed, depth,
warmup, timing boundaries and metrics still need review. Trace generation and
correctness checks must remain outside timing, with query results consumed.

Reviewer questions: approve B's implementation and aggregate/hash/sort
tradeoff, any timestamp requirement, B's sequence-exhaustion policy, and final
benchmark parameters. No benchmark results or screenshots exist at this stage.
