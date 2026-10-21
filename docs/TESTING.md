# Testing and acceptance

## Automated checks

`ctest --test-dir out/core -C Release --output-on-failure`:

* core_contracts: backend advertising, Unsupported errors, invalid shader inputs.
* d3d12_hardware: real adapter upload/copy/readback, triangle output, timestamps,
  state scopes, device/thread mismatch, stale snapshot rejection, resource retention,
  sampled texture/reflection/descriptor exhaustion, hidden-window swapchain/present/resize.
* d3d12_validation: same correctness checks with D3D12 debug layer/GPU-based validation;
  ERROR/CORRUPTION count must be zero after GPU completion.
* d3d12_warp: same functional tests on explicitly selected software adapter. Its timings
  do not enter hardware performance comparisons.

GPU/debug-layer unavailable returns 77 and CTest reports SKIP. A skipped test is not verified
GPU acceptance. Compile/runtime failures are errors. Tests check a red triangle center,
blue clear corner, green sampled result, all uploaded bytes in odd-pitch texture rows and
buffer copies. Resource wrappers and command lists are deliberately destroyed before GPU completion.

The original validation run exposed OBJECT_DELETED_WHILE_STILL_IN_USE at swapchain resize.
The fix signals a fresh queue-tail fence after DXGI work. The regression also rejects resize
while an unsubmitted command retains a backbuffer tracker.

2026-10-20 Windows acceptance: 4/4 D3D12 configuration CTests, 1/1 backend-disabled
configuration CTest and 3/3 Python statistics tests passed. GPU validation completed
44 assertions, including ERROR/CORRUPTION count zero. Native Linux/macOS and remote CI
were not run. Actual CTest logs are retained in `benchmarks/reports/validation`.

Python `unittest` checks percentiles, repeated run statistics, null GPU observations,
incompatible device/validation/category rejection and noisy regression decisions.
An independent `tests/consumer` uses installed headers and package exports only.
`examples/Triangle.cpp` renders and writes an actual PPM readback.

The subsequent license-audit change passed 17 Python tests in total (the original 3 statistics
tests plus 14 audit/package tests). It checks stale pins, D3DX12 origin mismatches, nested/end
notices, missing compiler evidence, unknown linked libraries, altered/missing package payloads,
unsafe archive paths and snapshot drift. Both actual Windows inventory profiles and the SDK
ZIP passed offline validation. The Windows CI SDK audit command also passed locally; remote
CI was not run. Rendering implementation and historical performance measurements were unchanged.

The subsequent Diligent adapter change passed 20 Python tests in total, adding rejection of
wrong workload/sample/readback results, stale source/dependency fingerprints, and explicit
RSA-MD5/NVAPI/DXC license classification. All eight Diligent scenarios passed a separate
Release correctness run (`count=1`, warmup=1, samples=2) with the native D3D12 debug layer.
The adapter requires the native info queue to exist when validation is requested and fails
on ERROR/CORRUPTION messages after GPU completion; no such messages were found. This is
debug-layer validation, not Diligent GPU-based validation. Draw/PSO runs check the triangle
center and clear corner before/after timing; uploads compare every byte in both checks.
Actual logs and JSON are retained under `benchmarks/reports/validation/2026-10-20-diligent`.
Performance campaigns disable validation and are reported separately in the
[Diligent report](DILIGENT_BENCHMARK_REPORT.md). Core rendering code was unchanged.
The documented `bootstrap_competitors.ps1 -Diligent` command also passed on the pinned
checkouts, preserving the measured executable hash. Both the original SDK ZIP audit and
the additional Diligent inventory `--check` passed; core CTest passed 4/4 again.

2026-10-21 bgfx expansion: 25 Python tests passed, including pre-execution rejection of
unsupported PSO requests without creating an output directory, distinct pooled allocation
contracts, build-option/dependency fingerprint changes and vendored license exceptions.
`out/bench-bgfx` has one CPU CTest with 8 shader-container checks: valid payload preservation,
stage mismatch, empty/malformed DXBC, bound resources, vertex attributes and user varyings.
All 7 supported scenarios passed native debug-layer execution and the applicable pre/post
readback checks. A separate 10,000 draw/instance validation run also passed. Native info queue
errors/corruption fail the run after GPU completion and during teardown. These are debug-layer
checks, not GPU-based validation or prolonged leak-stress acceptance. bgfx's single-thread
draw stats additionally reject dropped submissions. Primitive stats are not an instance-count oracle.
See [bgfx measurement scope](BGFX_BENCHMARK_REPORT.md) and retained JSON/logs under
`benchmarks/reports/validation/2026-10-21-bgfx-final`. Old reports and source snapshots remain historical.

`gfx_debug_capture <absolute-path-to-gfx_tests.exe> --validation` captures Windows debug output
for otherwise abrupt native validation termination. Use it for diagnostics, outside performance runs.

## Remaining acceptance

Vulkan/Metal, Linux/macOS native build/runtime, real device removal injection, prolonged
memory-leak stress, multi-queue/parallel recording, occlusion/minimize/DPI/interactive resize,
external GPU import, and PIX/RenderDoc vendor profiler captures are unverified.
Hidden-window presentation does not establish interactive window-system acceptance.
Validation error count does not by itself prove absence of every leak or data race.
CI configuration is provided separately; creating the file does not mean a remote run passed.

The final bgfx upload path references the immutable campaign source with makeRef. The original
copy-based exploratory campaigns are retained and excluded from final comparisons. All seven
correctness scenarios and 10,000 draw/instance validation passed again on the final build.
The final bootstrap reproduced the measured executable hash; SDK ZIP and final bgfx inventory
checks passed. Core CTest remained 4/4 and the optional CPU CTest passed all 8 checks. Remote CI
and a separate render-thread bgfx profile were not run.

2026-10-21 The Forge expansion: `bootstrap_competitors.ps1 -Forge` passed the pinned
no-NVAPI Release build and all 10 native CTests (eight workloads plus 10,000 draw/instance).
The native debug layer rejects ERROR/CORRUPTION through GPU completion and teardown;
device reset/removal invalidates a run. Core CTest remained 4/4. All 29 Python tests passed,
including changed external-config fingerprints, runtime/GPU-config copy tampering and legacy
NVAPI input rejection. The Forge inventory and unchanged own SDK ZIP checks passed.

Draw center/corner and full upload byte readback are checked before and after each campaign.
The pinned loader's transfer-queue copyResource readback fails current native layout validation;
the untimed correctness probe therefore uses native CopyTextureRegion on a Forge direct command
list with Forge barriers/submission/fence. This does not validate that public copyResource path.
Forge GPU-based validation, long-running stress and official VS 2019 sample acceptance remain
unperformed. See [Forge report](FORGE_BENCHMARK_REPORT.md) and the retained native CTest log
under `benchmarks/reports/validation/2026-10-21-forge`.

2026-10-21 timestamp expansion: Forge profile CTest 17/17, Diligent profile CTest 7/7
and core CTest 4/4 passed. The CPU conversion executable checks 12 conditions, including
nanosecond/tick conversion, same-tick zero, near-UINT64_MAX subtraction, reversed counters
and zero/negative/non-finite frequencies. All 34 Python tests passed, including missing or
invalid GPU results, unsupported requests before output creation, scope/category separation,
and rejection of Diligent's distinct start-query resolve contract.

LLGL, GraphicsSDK, Diligent and Forge timestamp CTests run 10,000 draw/instances with
native validation and multiple completed query reuses. Native ERROR/CORRUPTION is checked
through resource teardown; own core GPU-based validation remains a separate test.
Each measured process verifies the triangle before and after collection. Query-read CPU
cost is separate from GPU duration, and regular draw tests keep gpu_ms=null.

One initial GraphicsSDK root-signature creation failure was not reproduced by the rerun,
the final complete suites, ten additional ordinary/timestamp draw initializations or the
debugger capture. Its cause remains unresolved; native-code diagnostics were added without
claiming a fix. The initial log is preserved. The runner never silently retries a failure.
Initial Diligent data whose contract mislabeled its start resolve are preserved as excluded
exploration; corrected metadata came from new processes, not rewritten observations.
See [timestamp report](GPU_TIMESTAMP_BENCHMARK_REPORT.md) and validation `final-summary.json`
under `benchmarks/reports/validation/2026-10-21-timestamps`.
