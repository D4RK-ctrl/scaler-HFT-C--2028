# Benchmark and profiling — placeholder

No order-book benchmarks or profiling have been performed. No measurements or
screenshots are supplied at this stage.

Future authorized work must replay 1,000,000 deterministic primary operations:
700,000 adds, 200,000 cancellations targeting live orders, and 100,000 BBO queries.
Both depth queries run every 100 primary operations (10,000 queries per side).
Use five runs per implementation, report medians, and collect actual Linux perf
evidence. Depth, seed, warmup and timing boundaries await reviewer approval.

Stage 0B tested only tool availability and counter access; its smoke counts are
not order-book performance results. See implementations.md for design costs.
