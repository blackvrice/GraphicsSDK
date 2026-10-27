# Current-product Vulkan comparison inputs (P3-BEN-01)

`contract.json` freezes the Windows x64 Release GraphicsSDK/SDL comparison scope:
upload (4 shapes), draw (3), pipeline creation (2), resource allocation (4).
Each profile keeps its existing workload and timing boundaries. CPU API creation
and public release times cannot be combined with completed upload/draw wall times
or interpreted as native allocation counts, GPU time, reclamation or an overall ranking.

Prepare on a Windows x64 MSVC host with the reviewed SDL and Vulkan-Headers
checkouts already available. Use an unused output directory and the exact NVIDIA
GPU name. No Intel investigation is part of this initial adapter profile.

```powershell
python tools/prepare_vulkan_comparison.py build --roots out/phase3-comparison-new --cmake C:/path/to/cmake.exe --gpu-name "NVIDIA GeForce GTX 1660 SUPER"
python tools/prepare_vulkan_comparison.py check --roots out/phase3-comparison-new --live
```

The tool builds a fresh installed dual-backend SDK, pinned static SDL and four
standalone consumers. It checks Release flags, actual compiler/linker inputs,
installed library/header bytes and embedded source fingerprints, then runs eight
metadata-only probes and eight absent-target rejection controls. The optional
expected-name probe argument rejects a different GPU before any workload begins.
Original three-argument probes remain supported by each consumer.

`evidence/` contains exact commands, build logs, compiler/linker traces, cache
settings, input hashes and raw probe output. `check` without `--live` checks the
saved receipt; `--live` additionally requires unchanged local build inputs.
Dependency pins and source bytes must remain unchanged throughout preparation.
Build/process elapsed seconds in the command receipt are diagnostics, not workload
measurements. Binary files are retained locally; archived reports use their hashes.

The NVIDIA UUID/driver UUID/LUID is attributed through a unique exact-name match
to native Vulkan and DXGI. SDL's native device handle has not been independently
verified. Full changing readback, required validation in separate processes,
interleaved repeated timings, warmup and raw P50/P95/P99/MAD acceptance belong to
P3-BEN-02 and later workload tasks. BEN-01 accepts no correctness or performance
result, reuses no historical timing and does not complete Linux GPU/display gates.

## Correctness and process/statistics qualification (P3-BEN-02)

`protocol.json` fixes a bounded protocol qualification, using the still-unchanged
BEN-01 executable hashes and installed SDK. Required core/sync validation covers
all 13 scenarios for both SDKs in 26 separate processes (1 warmup + 3 samples).
Eight missing-required-layer and eight absent-target controls must then fail
without results. The missing-layer controls exercise the shared SDK prerequisite
gate before each consumer's validation workload.

Only after those gates succeed, representative odd upload/draw1/pso1/buffer1 cases
run with 3 warmups, 12 samples and 3 independent repetitions per SDK. Seeded profile
blocks alternate adjacent SDK pairs across repetitions. All 24 timing processes
perform full changing readback outside their stated timing intervals. Validation
and warmup timings are excluded; raw samples and every process median are kept.
P50/P95/P99 use linear interpolation at `(N-1)*q`; MAD is the median absolute
deviation from P50. No sample trimming or combined profile ranking is performed.
These small representative statistics qualify the protocol; BEN-03..06 still
require their full workload performance campaigns and interpretation.

```powershell
python tools/run_vulkan_comparison.py run --prepared-roots out/phase3-comparison-20261027-3 --runtime-receipt out/vulkan-validation-runtime/receipt.json --output out/comparison-protocol-new
python tools/run_vulkan_comparison.py check --output out/comparison-protocol-new --live
```

The runtime must already match `tools/vulkan-validation.lock.json`. Its DLL and
manifest remain local. Validation uses explicit settings and loader traces; timing
processes receive an empty layer/settings directory and no inherited Vulkan/SDL/
GFX selection or validation settings. Commands, per-process environment overrides,
PIDs, serialized start/end intervals, stdout/stderr and input hashes are retained.
The offline `check` recomputes statistics from raw output, verifies complete order
and controls, and rejects altered outputs, missing repetitions or mixed modes.
