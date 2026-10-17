# Screenshot inventory

User-supplied PNGs were visually inspected during final Stage 8B readiness checks.
All six show relevant, readable real terminal output. No images were generated
or edited by the implementer. Terminal prompts identify the team project path.

| Existing PNG | Visible evidence | Review |
| --- | --- | --- |
| [verification.png](verification.png) | `./build/verify` piped through `tail -n 9`, final C/cross-check passes, total 78 cases | Readable; excerpt rather than complete test output or build evidence |
| [demo.png](demo.png) | `./build/demo`, A/B/C states, ID reuse rejection, final consistency PASS | Readable and complete |
| [perf-stat-A.png](perf-stat-A.png) | `cat profiling/stat-A.csv`, all nine counter means and running percentages | Readable archived Stage 7 evidence |
| [perf-stat-B.png](perf-stat-B.png) | `cat profiling/stat-B.csv`, all nine counter means and running percentages | Readable archived Stage 7 evidence |
| [perf-stat-C.png](perf-stat-C.png) | `cat profiling/stat-C.csv`, all nine counter means and running percentages | Readable archived Stage 7 evidence |
| [perf-report-B.png](perf-report-B.png) | `head -n 16 profiling/report-B-self.txt`, cycle event, zero lost samples, introsort/depth self overhead | Readable hotspot names and percentages; long symbols wrap or are clipped |

The archived counter images agree with the tracked CSV files and
[profiling.md](../profiling.md). B's hotspot percentages agree with the tracked
report excerpt. These screenshots document existing measurements, not fresh
perf collections. Verification's tail does not independently display every test;
the fresh Stage 8B CTest and full verify invocation both passed.

## Coverage and final selection

**Screenshot coverage is partial.** The team has selected exactly the six PNGs
listed above for submission. No additional screenshots are planned or required
by the team's final cleanup decision.

Release-build output, mandatory benchmark A/B/C results and perf-report A/C
hotspots are not covered by screenshots. Reproducible build/test commands remain
in [README.md](../README.md); recorded benchmark results and A/C profiling evidence
remain in [benchmark.md](../benchmark.md) and [profiling.md](../profiling.md).
The three perf-stat images show archived counters, not benchmark replay results.
This inventory links only to existing PNGs and makes no claim of complete visual
coverage.
