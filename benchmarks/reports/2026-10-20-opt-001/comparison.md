# Measurement comparison

Baseline: `benchmarks\results\2026-10-20-draw-before\summary.json` (graphicssdk). Candidate: `benchmarks\results\2026-10-20-draw-after\summary.json` (graphicssdk).

Cost delta = 100 * (candidate P50 / baseline P50 - 1). Negative is lower measured cost.
Advisory warnings use repeated run medians and MAD; they do not establish statistical significance.

| Scenario | Count | Metric | Baseline P50 ms | Candidate P50 ms | Change % | Advisory |
|---|---:|---|---:|---:|---:|---|
| draw | 10000 | frame_ms | 3.909050 | 0.507350 | -87.02 | inconclusive |
| draw | 10000 | record_ms | 3.328900 | 0.137100 | -95.88 | inconclusive |
| draw | 10000 | submit_ms | 0.103350 | 0.027200 | -73.68 | inconclusive |
| draw | 10000 | wait_ms | 0.394400 | 0.325750 | -17.41 | inconclusive |
