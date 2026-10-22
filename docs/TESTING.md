# Testing and acceptance

## Automated checks

`ctest --test-dir out/core -C Release --output-on-failure`:

* core_contracts: backend advertising, Unsupported errors, invalid shader inputs.
* copy_atomicity_gpu / copy_atomicity_validation: all exact hardware/WARP candidates,
  eight state/memory rejection cases repeated 64 times, unchanged accepted state/versions,
  dropped-wrapper resource statistics, independent source writes/transitions and empty submits.
  Accepted 4KiB buffer copies and complete padded RGBA8/RGBA16F texture payloads verify that
  shadow-state handling, retained native lifetimes and accepted-use stale rejection stay intact.
* swapchain_guards_gpu / swapchain_guards_validation: exact adapter selection on all hardware/WARP,
  2/3/4-buffer rings, current/expired-chain input rejection, stale submit before and after a full
  ring rotation, unchanged fence timeline on rejection, atomic attachment/copy rejection,
  retained-wrapper reuse when current again, resize, independent windows and loss/recovery.
  Each adapter verifies 15 complete clear/readback images. Invalid GPU work is never submitted.
* fence_wait_gpu / fence_wait_validation: all freshly enumerated hardware/WARP adapters,
  native two-gate queues with 4,096 zero polls and 64 timed retries per adapter, counted
  actual SetEventOnCompletion calls, earlier-target retries, stale wake, timeout-boundary
  loss and owned-event closure. Public copy readback, invalid completed timeout, finite
  maximum, future/foreign/thread rejection and controlled removal/recovery are also checked.
  Process-wide handle counts are observations; the exact owned handle/registration contracts
  are checked independently of validation/runtime handles. These are not long-soak acceptance.
* vertex_layout_contracts: portable input-layout limits, semantics, format, offset/stride and rate validation.
* binding_layout_contracts: portable graphics register/space/visibility limits and overlap rejection.
* render_pass_contracts: portable texture usage, output layout, attachment size/duplicate/load/clear contracts.
* memory_reclamation_gpu / memory_reclamation_validation / memory_reclamation_warp: short 2-second
  regressions for native resource accounting/deduplication, recorded ownership after wrapper release,
  bounded eight-frame submission churn, full MRT/depth/buffer references and zero retained resources
  after every drain. CPU/GPU retain/release positive controls and thread affinity are also exercised.
  These smokes do not satisfy the separate 900/300/300-second memory campaign gate.

The independent `memory_validation_probe` also tests cumulative unfiltered error accounting with
six intentionally injected errors across an 8-message storage limit, filters, text clearing, a worker
thread and multiple native-device owners. These expected errors run in their own process and are
separate from zero-error render workloads. Validation devices require native ID3D12InfoQueue1.
* render_pass_gpu / render_pass_validation / render_pass_warp: 61 complete color/depth images,
  eight compare functions and write masks, read-only/depth-only passes, mixed-format eight-MRT,
  load preservation, alpha blending, postprocess, D32 copies, atomic rejection and lifetime/version checks.
* device_recovery_gpu / device_recovery_validation / device_recovery_warp: four native RemoveDevice
  cycles each, eight full before/after images, opt-in/thread/loss reason contracts, retained stale
  buffers/textures/pipelines/commands/fences/swapchains, blocked re-creation retry and rebuilt hidden-window
  present/resize. Native/GBV checks run before deliberate removal and on each recovered device.
* resource_bindings_gpu / resource_bindings_validation / resource_bindings_warp: Texture2D/CBV arrays,
  VS/PS register reuse, nonzero spaces, immutable snapshots, point/linear clamp/repeat references,
  full image readback, malformed sets, shader compatibility, descriptor capacity, state/version/
  device/thread/lifetime checks. Native/GBV errors must remain zero after completion.
* vertex_input_gpu / vertex_input_validation / vertex_input_warp: eight complete 64×64 CPU image references,
  Upload/Device inputs, 16/32-bit indices, draw offsets, signed baseVertex, instance streams,
  combined vertex/index storage, all public formats, state/range/layout/device/thread rejection,
  stale Upload writes and destruction of input wrappers/commands before GPU completion.
* d3d12_hardware: real adapter upload/copy/readback, triangle output, timestamps,
  state scopes, device/thread mismatch, stale snapshot rejection, resource retention,
  sampled texture/reflection/descriptor exhaustion, hidden-window swapchain/present/resize.
* d3d12_validation: same correctness checks with D3D12 debug layer/GPU-based validation;
  ERROR/CORRUPTION count must be zero after GPU completion.
* d3d12_warp: same functional tests on explicitly selected software adapter. Its timings
  do not enter hardware performance comparisons.

2026-10-21 graphics binding expansion: core CTest 12/12, Windows D3D12-OFF CTest 3/3,
common benchmark CTest 72/72, Python 56/56 and the updated installed consumer passed.
The three GPU modes each verified eight complete binding images (9,216 pixels per mode);
debug+GBV completed with zero ERROR/CORRUPTION messages. See the
[binding report](PHASE2_BINDINGS_REPORT.md) for contracts and raw evidence.

The existing functional GPU/recovery tests return 77 when GPU/debug-layer support is unavailable
and CTest reports SKIP. The memory-stress executable requires its requested mode and fails if
unavailable; it never converts missing observations into passing acceptance. A skipped test is not
verified GPU acceptance. Compile/runtime failures are errors. Tests check a red triangle center,
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

2026-10-21 device recovery expansion: SDK CTest 19/19, Windows backend-disabled 4/4,
common benchmark 72/72, Python 56/56 and both installed consumer targets passed.
Hardware/debug+GBV/WARP each passed four native removal/recovery cycles and eight complete
images; the installed recovery consumer verified 2,048 pixels per hardware/WARP run.
Unexpected native/GBV errors were zero before deliberate removal and on fresh devices.
Intentional removal diagnostics on the lost device are a separate expected outcome. See the
[recovery report](PHASE2_RECOVERY_REPORT.md) for exact contracts and remaining acceptance.

2026-10-21 Depth/MRT expansion: SDK CTest 16/16, Windows backend-disabled CTest 4/4,
common benchmark CTest 72/72, Python 56/56 and the installed consumer passed without skips.
The new render tests each verified 61 complete color/depth images on hardware, debug+GBV and WARP;
debug+GBV ERROR/CORRUPTION remained zero. The installed package consumer verified 1,024 rendered
pixels. [Depth/MRT report](PHASE2_DEPTH_MRT_REPORT.md) records exact scope and limitations.

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

2026-10-21 CPU/memory observer: common profile CTest 17/17 and core CTest 4/4 passed.
The CPU observer arithmetic test covers 9 conditions, including >100% process-thread sums,
same counter zero, near-UINT64_MAX subtraction and invalid wall/counter intervals. Python
tests passed 41/41, covering observer opt-in, GPU LUID/node, finite consistent CPU values,
checkpoint order/completeness, live-allocation observations, lifetime peak monotonicity,
process-level aggregation and refusal to mix instrumented/uninstrumented contracts.

25 native process runs validated all supported common observer paths, GPU timestamp
coexistence and warmup=0. Three additional uninstrumented draw processes preserved the
default telemetry=null contract. Performance evidence is separate: 63 processes / 5,760
workload samples, 3 runs per SDK/scenario; allocation uses 20 samples after 3 warmups,
other workloads use 120 after 20. CPU observations are process-batch values, not per-frame
utilization samples. Pipeline batches were too short for reliable CPU accounting (raw zeros
are preserved and reported as inconclusive). GPU memory maxima are checkpoint bounds,
not continuously observed transient peaks or leak-stress acceptance.

The initial LLGL upload native test failed with COPY_SOURCE used as COPY_DEST. It reproduced
with the observer disabled. The pinned public full-region WriteTexture path lacks a transition
after ReadTexture; all three LLGL uploads are now rejected before runner output/execution
and by the direct CLI. Failed logs are preserved and excluded from performance evidence.
Historical LLGL upload observations are not current validation acceptance. Current upload
measurements cover SDL3/GraphicsSDK. Vendor sources, core SDK and SDK ZIP were unchanged.
Optional Diligent/bgfx/Forge observer profiles, continuous GPU peaks, cache control and remote
CI were not executed. See [CPU/memory report](CPU_MEMORY_BENCHMARK_REPORT.md).

2026-10-21 LLGL public upload recovery: common CTest 22/22 and existing core CTest 4/4
passed. The dedicated native test performs 13,156 checks over 8 RGBA8/RGBA16F extent cases,
including unaligned rows, 72 public uploads and 96 complete readbacks with changing payloads.
Short payload rejection preserves the previous texture contents; zero/overflow layout rejection
is checked before GPU mutation. Three repeated upload CTests cover 1080p/4K/4K16F after
pre-readback. Native ERROR/CORRUPTION checks remain active through teardown.

Eighteen native runner processes validate all 3 upload scenarios for LLGL/SDL3/GraphicsSDK,
with telemetry ON/OFF and warmup=0 for the observer path. All 43 Python tests passed,
including the restored public model, missing/obsolete model rejection, LLGL pin/source/harness
fingerprint changes and separation from old upload contracts and uninstrumented data.
Fresh performance data uses one binary: 54 processes / 6,480 samples across two separate
observer profiles. No failed old-path observation is relabeled as passing.

The pinned LLGL WriteTexture implementation is unchanged. The adapter uses WriteBuffer
and CopyTextureFromBuffer, including its extra default-buffer copy/wait cost. Common new
inventory and unchanged SDK ZIP checks passed. Optional competitor profiles, LLGL GBV,
continuous GPU memory peaks, cache hit/miss control, prolonged stress and remote CI were
not run. See [public upload report](LLGL_UPLOAD_BENCHMARK_REPORT.md).

Phase 1 extensions (2026-10-21): final common CTest 44/44, core CTest 4/4 and Python
49/49 passed. The extension adds 22 native LLGL scenarios with bytecode or full GPU readback
CPU references, real completed timestamp queries, strict work-byte/draw/dispatch counts,
actual application cache counters, and process CPU/memory observations. A separate native
runner validates all 22 final scenarios. Final performance evidence is 66 processes / 7,920
samples (3 independent runs × 120 samples, warmup 3 each).

Final video_layers uploads two distinct NV12 planesets and composites them on the GPU.
The initial single-video/solid-overlay campaign and metadata that did not distinguish CPU
shader validation from readback are retained as excluded exploration. No original was rewritten.

Native validation checks ERROR/CORRUPTION through teardown. A parallel MSVC compiler crash
was retained and final serial builds passed; its cause is unresolved. New inventory and unchanged
SDK package checks passed. Other advanced SDK adapters, continuous/transient GPU peak analysis,
LLGL GBV and long stress remain unperformed. WPR GPU capture failed profiling policy; elevation
was canceled and before/after status showed no recording. The completion gate deliberately
returns exit 1 for missing advanced cross-SDK comparisons and event-based peak evidence.
See [Phase 1 status](PHASE1_COMPLETION_REPORT.md).

Phase 1 final GPU memory acceptance (2026-10-21): after the user's fresh approval, a private
DxgKrnl file session captured five fresh LLGL process lifetimes. Header/consumer event loss and
buffer loss are zero. TDH decoded the installed OS schema. All 1,274 global allocation lifetimes
pair completely, including 675 destructions in System/other process contexts. Local/nonlocal
usage and commitment old/new chains begin at zero and end at zero with no gaps.
Three positive intervals contain two overlapping 32MiB GPU buffers, verified by full readback;
change events detect 63.992188/64/64MiB beyond the all-checkpoint local maximum.

The new positive-control CTest 1/1 and current Python tests 56/56 passed. Prior common CTest
72/72 and SDL/LLGL timing evidence were hash-verified, not rerun for this tools-only addition.
New compiler-read inventory covers 974 inputs/22 components; the unchanged SDK ZIP check passes.
Target-process events/native outputs are versioned; system ETL stays local and is hash-bound.
The final completion gate now returns exit 0. Scope is process-lifetime VidMm event-based usage
in the documented Windows D3D12 profile, not frame-only physical VRAM occupancy or other
platform/stress/device-loss acceptance. See [final GPU memory report](PHASE1_GPU_MEMORY_REPORT.md).

Phase 2 vertex/index input (2026-10-21): current core CTest 8/8, Windows D3D12-OFF CTest 2/2,
common benchmark correctness CTest 72/72 and Python 56/56 passed. Native ERROR/CORRUPTION count is
zero in the debug/GBV case. The installed consumer uses the new exported layout validator and
vertex binding API. All 65,536 pixels of the indexed-instancing example match a separate CPU rectangle
reference. New SDK package and SDK/common compiler-read inventories pass. Native Linux/macOS,
broader resource binding, depth/MRT and new performance comparisons were not run/completed.
See [vertex input report](PHASE2_VERTEX_INPUT_REPORT.md).

2026-10-22 memory diagnostics: current core CTest 22/22, Windows D3D12-OFF 4/4,
common benchmark correctness 72/72, Python 72/72 and installed/render/recovery consumers passed.
Three current 30-second GBV factor probes passed full references with unfiltered callback error
counts zero; a separate positive counter probe counted six injected errors despite native queue
overflow, filters, clearing and multiple thread/owner lifetimes. Fresh SDK/common/stress inventories
and the new package passed; five older packages remain unchanged.

The preserved pre-repair 900/300/300-second campaign is **not accepted**: hardware/WARP passed
their bounded memory criteria, but GBV private/nonlocal/handle growth failed. Its old zero-error
snapshot is not full native error coverage because factor probes exposed native text queue loss.
The repaired counter and current short probes are separate evidence, not a replacement long soak.
See the [memory report](PHASE2_MEMORY_STRESS_REPORT.md) and its accepted=false/exit-1 gate.

2026-10-22 GBV reclamation investigation: native-only controls isolate texture-creation growth
with full GBV state tracking; disabling that tracking is confined to a reduced-validation diagnostic.
The SDK default remains full GBV. A public-API reusable frame-slot helper passes a separate
30-second warmup + 300-second full-GBV profile: 70,800 frames, 212,400 full images and
18,559,795,200 verified buffer bytes, zero native errors and final SDK ownership zero.
Its 61 drained checkpoints pass the original private/handle/DXGI plateau limits. This does not
accept the original repeated-creation campaign or finish Phase 2 memory reclamation.

Current core CTest 22/22, Windows D3D12-OFF 4/4, Python 82/82 (10 new gate-negative tests)
and all 20 final isolation/counter cases pass their respective functional contracts. New sdk-stress
inventory covers 22 compiler-read inputs/1 component; SDK/common inventory checks and six old SDK
packages are unchanged. Common benchmark correctness/installed consumers are previous same-SDK
evidence with verified hashes, not newly executed. See the [investigation](PHASE2_GBV_RECLAMATION_REPORT.md).

2026-10-22 device lifetime: 5 fresh processes × 8 normal native device lifetimes pass
destruction/alias contracts. The native-only reproduction receives all 18,456 resource destruction
callbacks; the public SDK hardware/WARP cases verify 1,024 frames, 3,072 full images and
256MiB buffer bytes with full GBV before teardown. SDK statistics are explicitly unavailable after
dropping Device; real native destruction receipts are separate evidence. Core CTest 22/22,
D3D12-OFF 4/4 and Python 91/91 (9 added rejection tests) pass. New compiler/linker inventory
covers 24 own inputs/1 component and confirms no SDK library in the native-only executable.
These short device lifecycle observations do not accept the original GBV memory gate; CPU private
commit still grows between some settled cycles. See the [lifetime report](PHASE2_DEVICE_LIFETIME_REPORT.md).

2026-10-22 runtime selection: final source runs 18 fresh native processes across NVIDIA/Intel,
global/inbox-factory/Agility-factory paths and CPU-debug/hardware-GBV/WARP-GBV modes. Actual
provider DLL pairs, SDKVersion 616/619, validation flags and device LUIDs match their selections.
All 27,666 resource and 18 device destruction callbacks pass; 18 intentional overflowed ERRORs
are observed separately from zero unexpected errors. Three real bad-selection runs fail closed.
A separate SDK-free build verifies 193 resources/one device on Intel Agility GBV. Core 22/22,
D3D12-OFF 4/4 and Python 102/102 (11 added tests) pass; new own-source inventory is 25/1.
Hardware GBV growth remains on both GPUs/runtimes. This is no replacement for the rejected
long-running gate. See the [runtime report](PHASE2_RUNTIME_COMPARISON_REPORT.md).

2026-10-22 presentation: a standalone installed-package Win32 consumer passes six
fresh hardware/GBV/WARP × VSync cases: 246 full backbuffer images / 44,892,078 RGBA
pixels, 54 minimize/restore cycles, 12 controlled native recoveries, coalesced resize,
hide/show and close with HWND alive through GPU shutdown. Two GBV cases observe zero
native ERROR/CORRUPTION; validation availability in other modes remains null.
Registered presentation CTest 6/6 separately repeats those contracts; a 60-frame normal
loop smoke also passes. Core 22/22, D3D12-OFF 4/4 and Python 110/110 pass, including
eight new evidence/audit tests; four actual native argument/output rejections pass.
New compiler/linker/package inventory is 33 inputs/1 own component. The SDK and six
old SDK ZIPs remain unchanged. Common benchmark/other consumer evidence is hash-checked,
not rerun. Actual compositor output, human input, physical DPI transitions and unexpected
TDR/hot-unplug remain unaccepted; the failed GBV memory gate stays open.
See the [presentation report](PHASE2_PRESENTATION_REPORT.md).

2026-10-22 small-window/pacing follow-up: parent application code reproduces
`Scissor outside target` at native client height 1. The fix passes 294 full images /
44,929,026 RGBA pixels over six modes, including 48 tiny-window images. Nine normal
and lifecycle campaign processes cover 30/60fps caps, 40 owned-window message wakes,
31ms close while waiting at 1fps, and the prior lifecycle contracts. A separate
60-frame normal GBV smoke passes. Presentation CTest 7/7 includes 2,278 CPU clock/
geometry assertions; core 22/22, D3D12-OFF 4/4 and Python 114/114 pass. Four Python
tests and four native FPS refusal controls are added. Final compiler/linker inventory
is 35/1. SDK code, six SDK ZIPs and previous raw/source evidence remain unchanged;
the example executable is rebuilt. Early coarse-timer observations are excluded from
final-source results. Manual display/DPI/unexpected-loss and original GBV memory
acceptance remain open. See the [follow-up report](PHASE2_PRESENTATION_PACING_REPORT.md).

2026-10-22 adapter API: core 25/25, D3D12-OFF 5/5, common correctness 72/72,
presentation 7/7 and Python 124/124 pass. Native selection tests independently remove
the requested NVIDIA/Intel/WARP singleton, verify exact loss and blocked-recovery
retry, then rebuild full images on the same ID. Both modes of an installed consumer
pass all three selections/recoveries. Explicit window cases pass 147 images /
22,464,513 pixels; default cases pass 294 / 44,929,026 plus pacing/wake/close. Ten
Python tests and eight real CLI selection refusals are added. Two preserved serial
CL.exe access violations pass on unchanged-source retries; their cause is unresolved.
New SDK package/inventories bind the current fingerprint. Long GBV memory, physical
hot-unplug/TDR, manual display and DPI remain open. See [adapter report](PHASE2_ADAPTER_REPORT.md).

2026-10-22 exact-adapter memory campaigns: core 25/25 and Python 139/139 pass.
Fifteen new Python tests cover ID mixing, signed LUID reconstruction, explicit zero,
GBV/observer mismatches, guard/exit consistency, source mutation, timeout cleanup and
native crash receipts. Fresh NVIDIA/Intel/WARP short resource-churn checks pass full
references. Guarded GBV runs abort with explicit zero-ownership receipts; a default
512MiB three-mode diagnostic records native exits 0/2/0 and runner exit 1. Eleven
native CLI/output rejections and a real missing-ID runner rejection pass. Native
CPU observer and SDK hardware/WARP destruction smoke tests pass after rebuilding
the shared helper. An earlier Intel 512MiB run crashes at 112.875 seconds with
0xc0000005 and no final footer; it remains rejected with cause unresolved. This is
tooling validation, not the long memory acceptance gate. SDK source/library/ZIPs,
common and presentation evidence remain unchanged and are hash-checked, not newly
executed acceptance. See [campaign report](PHASE2_MEMORY_CAMPAIGN_REPORT.md).

2026-10-22 crash capture: the standalone own-child x64 debugger passes real handled
exceptions, exact Unicode/empty/quoted/trailing-slash arguments, exit 7, null-PC
access violation with a local minidump and labelled caller candidate, unhandled
application breakpoint, timeout/reap, 1,000-message text-budget and overwrite controls.
Eight added synthetic parser tests reject incomplete contexts/exits, swallowed
breakpoints, altered scope, missing counters, failed dumps and timeout disagreements.
An Intel `/O2 /MD /Zi /DEBUG` public-workload diagnostic completes 30s + 300s,
17,696 frames / 53,088 full images / 4,638,900,224 buffer bytes, with SDK validation
errors zero and no second-chance exception. Earlier AV is not reproduced or resolved.
Debugger instrumentation and a different executable remain separate from the normal
memory gate. Early debug-text byte-length failure stays rejected and local-only;
an initial compiler AV passes on an unchanged-source retry. See
[capture report](PHASE2_CRASH_CAPTURE_REPORT.md) for final regressions and scope.

2026-10-22 public memory budgets: core 28/28, Windows D3D12-OFF 6/6, common
correctness 72/72, presentation 7/7 and Python 147/147 pass on the new SDK.
Two dedicated native processes compare 48 local/nonlocal snapshots against stable
independent DXGI brackets, including valid zero fields. Four hardware 64MiB
retain/release controls pass; 393,216 readback bytes are verified across six adapter
runs. Thread/invalid-segment/observed-loss/retired-generation/same-ID recovery and
unchanged SDK resource ownership are checked. Both installed budget consumer modes
pass 24 total snapshots and six recoveries without native headers. Existing window
campaigns pass 441 images separately from registered CTests. All consumers and
SDK-bearing diagnostics are rebuilt; five current audits/new SDK ZIP pass and seven
old SDK ZIPs remain unchanged. The original GBV memory gate and Intel AV cause
stay unresolved; no new long soak is claimed. See [budget report](PHASE2_MEMORY_BUDGET_REPORT.md).
