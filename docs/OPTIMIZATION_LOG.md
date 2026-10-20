# Optimization log

## OPT-001 — per-draw driver status query

Workload: same 10,000 vertex-ID triangle draws, 64×64 RGBA8, D3D12/GTX 1660 SUPER,
validation off, Release, three independent processes ×240 observations, warmup 30.
Original baseline: `benchmarks/results/2026-10-20-draw-before`.
Remeasurement: `benchmarks/results/2026-10-20-draw-after`.

Inspection found `DXCommands::ready` called `State::check`, which called
`ID3D12Device::GetDeviceRemovedReason`, for each recorded draw. This driver status check
does not submit or execute GPU commands. The change checks the creating thread and
recording/pass scope locally; device removal checks remain at begin/submit/wait and native
API failures still propagate. No hot-path validation was globally disabled.

Observed recording P50: 3.328900 → 0.137100 ms (-95.88%).
Observed total batch wall P50: 3.909050 → 0.507350 ms (-87.02%).
The cost delta formula is `100*(after_P50/before_P50-1)`. These are workload-specific
observations, not an overall SDK advantage. Frame-time tails and background variation remain.
The comparison tool's `inconclusive` field means no repeated **regression warning** was
established; it is not a statistical significance test for improvement.

Buffer/texture readback, early wrapper destruction, stale command snapshots, foreign-device
and thread rejection, sampled descriptors, timestamp and swapchain tests were rerun, including
GPU-based validation. See [comparison](../benchmarks/reports/2026-10-20-opt-001/comparison.md)
and [benchmark report](BENCHMARK_REPORT.md).

## Correctness fix — wait for Present queue work

GPU validation found `OBJECT_DELETED_WHILE_STILL_IN_USE` when backbuffers were released at
resize. Waiting only for the previous rendering fence did not cover DXGI Present's later
queue work. waitIdle/resize/destruction now signal and wait for a fresh queue-tail fence.
Unsubmitted command references also prevent resize. This is a correctness fix; no performance
improvement is claimed and no fabricated before/after timing is attached.

## Deferred optimizations

Root signature/PSO caches, memory suballocation, upload ring, descriptor cache and resource
pooling require dedicated baseline contracts and memory/cold-warm evidence before implementation.
