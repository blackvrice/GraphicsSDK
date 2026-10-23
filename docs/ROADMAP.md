# GraphicsSDK roadmap and acceptance

This is an initial 0.0.1 implementation, not a completed v0.1 release.
Only measured/tested scope is checked. Follow the remaining phases in order.

2026-10-23 user direction: defer the unresolved long-running memory acceptance and
historical Intel AV investigation, and proceed with Phase 3 implementation. Neither
item is accepted or attributed conclusively to hardware. Historical policy, failed
observations and evidence stay unchanged. Interactive/physical Phase 2 acceptance
also remains open; it is not inferred from advancing implementation.

## Phase 0 — research

- [x] Read-only Sandbox source/interface audit at a fixed SHA.
- [x] Seven competitor snapshots, feature/architecture/license comparison and source links.
- [x] Diligent commercial Metal, NVRHI absence of Metal, Forge Windows Vulkan policy distinguished.
- [x] No Sandbox modifications or integration.
- [x] Windows SDK/LLGL/SDL3, Diligent Core and bgfx compiler-read dependency inventories, notices, source origin evidence and SDK package checks ([audit scope](DEPENDENCY_LICENSES.md)).

Future redistributed competitors/tools and other platform/configuration changes require a fresh
inventory and terms review; the checked item is limited to the current reviewed artifacts.

## Phase 1 — common benchmark foundation

- [x] Fixed LLGL/SDL3 D3D12 adapters; independent benchmark CMake project.
- [x] Same physical adapter/driver/compiler/validation/workload contract checks.
- [x] Common allocation workload and actual same-machine competitor data.
- [x] Draw 1k/10k/100k, instancing; pre/post image readback.
- [x] Persistent 1080p/4K RGBA8 and 4K RGBA16F upload; byte readback. LLGL uses a validated public buffer-to-texture path after the pinned WriteTexture state failure; historical old-path numbers remain excluded ([fresh measurements](LLGL_UPLOAD_BENCHMARK_REPORT.md)).
- [x] Repeated PSO creation from identical precompiled bytecode (driver cache uncontrolled).
- [x] Multiple processes, warmup, P50/P95/P99/MAD, JSON/CSV and comparison reports.
- [x] Standalone pinned Diligent Core D3D12 adapter: all eight existing workloads, native validation/readback, dependency inventory and same-machine repeated measurements ([report](DILIGENT_BENCHMARK_REPORT.md)).
- [x] Pinned bgfx D3D12 adapter: six matching workloads, pooled buffers under a separate contract, native queue-completion/readback validation, explicit unsupported lazy PSO, dependency audit and repeated measurements ([report](BGFX_BENCHMARK_REPORT.md)).
- [x] Pinned The Forge D3D12 adapter: eight matching workloads, native validation/readback, no-NVAPI dependency inventory and repeated measurements; explicit MSVC compatibility and untimed native readback limitations ([report](FORGE_BENCHMARK_REPORT.md)).
- [x] Matching draw/instanced GPU timestamp intervals at 1k/10k/100k for GraphicsSDK/LLGL/Forge; Diligent's start-query resolve interval kept separate, SDL/bgfx matching scopes rejected ([report](GPU_TIMESTAMP_BENCHMARK_REPORT.md)).
- [x] Opt-in Windows process-batch CPU utilization, process-lifetime CPU memory peaks and explicitly bounded GPU memory checkpoint maxima for GraphicsSDK/LLGL/SDL3; matching contract, native checks and repeated observations ([report](CPU_MEMORY_BENCHMARK_REPORT.md)).
- [x] Public LLGL upload/readback state regression recovery, padded-row/changing-payload native checks and fresh same-binary upload measurements with telemetry ON/OFF ([report](LLGL_UPLOAD_BENCHMARK_REPORT.md)).
- [x] Event-based continuous/transient GPU memory peak acceptance for the documented Windows D3D12 / LLGL process-lifetime scope: loss-free DxgKrnl usage/commitment changes, 1,274 paired allocation lifetimes and three transient controls missed by checkpoints ([report](PHASE1_GPU_MEMORY_REPORT.md)).
- [x] LLGL application shader-cache cold/warm and PSO handle-cache hit/miss workloads with actual counters and reference validation; driver/OS cache remains uncontrolled ([report](PHASE1_COMPLETION_REPORT.md)).
- [x] LLGL four-texture uploads, persistent reuse/four-fence retained allocation and pipeline/texture/scissor switching baselines with readback references ([report](PHASE1_COMPLETION_REPORT.md)).
- [x] Independent LLGL E/F/G/H baselines: sprites/atlas/blend/clip, mesh depth/MRT/postprocess, four compute kernels plus graphics transition, NV12/P010/two-video composition/filter and CPU references ([report](PHASE1_COMPLETION_REPORT.md)).
- [x] Additional SDL3 E/F/G/H public-workload adapter and same-environment LLGL/SDL3 CPU completion comparisons: 14 workloads, explicit binding ABI differences, both GPU queries disabled, full CPU references and three process repetitions ([report](PHASE1_SDL_COMPARISON_REPORT.md)). GraphicsSDK depth/MRT prerequisites are now implemented below; compute and additional advanced adapters/comparisons remain unimplemented.

Phase 1 foundation acceptance is met for the documented Windows D3D12 end-to-end subset.
This does not complete every SDK/platform combination, external media import, presentation
acceptance or later SDK feature milestones.

## Phase 2 — D3D12 initial core

- [x] C++23/Result, independent static library and capability queries.
- [x] Device/Buffer/Texture/Pipeline/CommandList/Queue/Fence/render pass/swapchain.
- [x] Explicit transitions with stale-recording validation and retained native lifetimes.
  Transition/copy validation now finishes before retaining new resources or version snapshots; repeated rejection, wrapper reclamation and accepted shadow-state/lifetime controls are verified ([report](PHASE2_COPY_ATOMICITY_REPORT.md)). This does not accept the separate long memory gate.
- [x] HLSL SM5.1 bootstrap, basic reflection, one sampled binding, timestamp/debug marker.
- [x] Real triangle/readback, WARP, GPU-based validation and installed consumer checks.
- [x] Same suite adapter and measured narrow draw-recording optimization.
- [x] Vertex/index input layouts, UInt16/UInt32 indexed draws, per-instance streams and bounded buffer views; full-image hardware/GBV/WARP tests, state/version/lifetime rejection and installed-consumer checks ([report](PHASE2_VERTEX_INPUT_REPORT.md)).
- [x] Explicit graphics binding layouts: bounded Texture2D/constant-buffer arrays, register spaces/stage visibility, static point/linear clamp/repeat samplers, shader compatibility and full-image/state/version/lifetime validation ([report](PHASE2_BINDINGS_REPORT.md)). Storage/UAV/compute, dynamic sampler arrays and unbounded bindings remain outside this graphics gate.
- [x] D32 depth attachments, eight compare functions/write masks, read-only/depth-only passes, up to eight mixed-format color attachments, Clear/Load and pipeline/pass compatibility; full color/depth references, MRT postprocess, atomic rejection and retained lifetimes on hardware/GBV/WARP ([report](PHASE2_DEPTH_MRT_REPORT.md)). Stencil/MSAA/resolve and fragment depth export are outside this initial gate.
- [x] Observed-loss status/reason caching, native opt-in RemoveDevice injection and explicit generation retirement/re-creation; old handle/fence rejection, blocked-recovery retry and rebuilt full images/swapchains on hardware/GBV/WARP ([report](PHASE2_RECOVERY_REPORT.md)).
- [x] Fresh adapter metadata, backend-scoped identities and explicit same-ID selection/recovery; independent native removal proves selected device identity on NVIDIA/Intel/WARP, with missing-target/software-kind rejection and rebuilt installed consumers ([report](PHASE2_ADAPTER_REPORT.md)). Candidate enumeration does not guarantee device creation or persistent identity across reboot/topology changes; unexpected hot-unplug/TDR acceptance stays below.
- [x] Public exact-adapter local/nonlocal OS memory budget snapshots, distinct from SDK ownership statistics; independent native field mapping, 64MiB retain/release controls, thread/segment/loss/retirement contracts and installed-consumer recovery on NVIDIA/Intel/WARP ([report](PHASE2_MEMORY_BUDGET_REPORT.md)). These are dynamic process/node-0 observations, not total/free VRAM, continuous peaks or long memory acceptance.
- [x] Completion-independent timeout validation, event-free zero polls and bounded reusable native fence-wait registrations; gated native hardware/GBV/WARP controls, earlier-target/stale-wake/deadline-loss checks and installed consumer reproduction ([report](PHASE2_FENCE_WAIT_REPORT.md)). This repairs queue wait/retry behavior and does not accept the separate texture-churn memory gate below.
- [ ] Long-running memory stress and measured reclamation acceptance. Diagnostic API/tooling, 15-minute hardware and 5-minute WARP observations are implemented; the 5-minute GBV campaign fails memory growth limits. Native queue overflow also exposed an error-counting gap, now repaired with unfiltered callbacks. The failed observations and post-repair factor probes remain separate ([report](PHASE2_MEMORY_STRESS_REPORT.md)); no completed stress gate is inferred from zero SDK-owned resources.
  Native-only controls now reproduce texture-creation growth with GBV state tracking, while CPU validation and the reduced-tracking diagnostic remain stable. A separate fixed-resource, full-GBV 30-second warmup + 300-second campaign passes the unchanged memory limits and complete readback checks ([reclamation investigation](PHASE2_GBV_RECLAMATION_REPORT.md)). This bounded reuse profile does not accept the original resource-churn gate or complete this item.
  Native destruction callbacks now confirm all 18,456 test textures and 40 device lifetimes; full-GBV public SDK hardware/WARP generation teardown also passes 1,024 frames and last-child alias checks. Hardware nonlocal usage returns after native device destruction, while process private commit retains growth; these short lifetime observations do not accept a memory plateau ([device lifetime report](PHASE2_DEVICE_LIFETIME_REPORT.md)).
  The SDK-free runtime/adapter probe now validates actual SDK version, provider DLL, full GBV, LUID and injected-error coverage. Eighteen fixed-work processes on NVIDIA/Intel with inbox 616 and pinned Agility 1.619.6/619 still reproduce hardware GBV growth; a runtime substitution alone does not resolve the gate ([runtime comparison](PHASE2_RUNTIME_COMPARISON_REPORT.md)).
  Resource-churn campaigns now select/observe exact public adapter IDs, preserve rejection/timeout/crash receipts and apply an explicit process-private growth guard. Default-limit NVIDIA GBV abort and GPU→GBV-abort→WARP continuation are verified. An Intel default-limit run ends with access violation `0xc0000005` and no footer; Windows reports an unknown faulting module. The cause remains unresolved, and neither short guarded runs nor the aborted/crashed run accept this memory gate ([campaign report](PHASE2_MEMORY_CAMPAIGN_REPORT.md)).
  An own-child x64 debugger now captures second-chance exceptions, module ranges, context/stack and optional local-only minidumps, with actual null-call/handled/breakpoint/timeout/quoting/text-budget controls. A separate symbol-bearing Intel full-GBV diagnostic completes 30s warmup + 300s and 53,088 reference images without reproducing the earlier access violation. This debugger-instrumented run does not replace normal memory acceptance or resolve the original crash ([capture report](PHASE2_CRASH_CAPTURE_REPORT.md)).
  The collector now prioritizes the fault thread and snapshots up to 16 live threads with 64 frames each under the same debug stop. Schema 2 records omitted threads and unavailable contexts explicitly; real worker-fault and oversized-thread controls validate the bounded collection ([thread snapshot report](PHASE2_THREAD_SNAPSHOT_REPORT.md)). This improves future crash evidence and does not resolve the earlier Intel AV or accept the memory gate.
  Loss-free target-filtered virtual allocation stacks and matching Microsoft public PDBs now identify SDK-free GBV resource-info registration and internal command-buffer allocations. Twelve lifetime controls distinguish live-device accumulation from delayed reclamation after device destruction. Five fresh long Intel runs verify 161,808 full images without reproducing the AV; normal/extended memory checks still fail ([allocation-path investigation](PHASE2_FAULT_INVESTIGATION_REPORT.md)). The memory gate and historical AV cause stay open.
  The reusable [virtual-range analyzer](PHASE2_VIRTUAL_RANGES_REPORT.md) now deduplicates overlapping commits, splits partial frees and bounds same-timestamp peaks. Replay of 18,204 frozen events confirms simultaneous tagged command-buffer commitments and later retirement of all observed ranges; this is bounded trace accounting, not whole-process memory acceptance or an Intel crash fix.
  A [normal-run environment wrapper](PHASE2_MEMORY_ENVIRONMENT_REPORT.md) now binds fresh hardware identity, driver/runtime/OS/CPU/BIOS metadata, frozen workload inputs and native receipts before comparing cases. Two short same-environment Intel controls pass 1,416 full images; driver-only comparison correctly requires an actual driver change and unchanged other recorded controls. A real changed-driver/host comparison and Intel AV cause remain outstanding; recorded metadata does not certify hardware stability.
  A separate [fixed-work diagnostic](PHASE2_FIXED_WORK_REPORT.md) now fixes warmup/measured round counts and compares equal-work checkpoints. Five fresh NVIDIA/Intel/WARP controls verify 720 frames/2,160 full images, with distinct guard/deadline prefix controls. Its separate schema and reference-only validation cannot accept the original timed gate; actual changed-driver investigation and Intel AV cause remain open.
  Resource-churn schema 3 now records the public memory-budget API beside independent native before/after queries at every checkpoint. Stable four-field mapping, explicit unavailable values, adapter/segment identity and SDK retain/release controls are checked even for short and guard-aborted runs; historical observations remain separate. This integrates API telemetry without changing the original memory policy or accepting the open long-running gate ([budget campaign report](PHASE2_BUDGET_CAMPAIGN_REPORT.md)).
- [ ] Interactive presentation acceptance and unexpected driver TDR/hot-unplug recovery acceptance. Native RemoveDevice and hidden-window tests have separate, narrower evidence.
  Backbuffer recording/submission now rejects noncurrent and destroyed-chain textures, with per-present version invalidation even after a full ring rotation. Exact-adapter 2/3/4-buffer, full-image, independent-window and recovery tests cover the public lifetime contract ([guard report](PHASE2_SWAP_GUARD_REPORT.md)); these automated checks do not replace the remaining interactive/physical acceptance.
  An installed-package Win32 application now implements deferred resize, minimized/hidden pause, safe close and observed-loss rebuild. Six fresh hardware/GBV/WARP × VSync executions pass 246 full backbuffer images, 54 minimize/restore cycles and 12 controlled recoveries; six registered CTests also pass ([presentation report](PHASE2_PRESENTATION_REPORT.md)). Backbuffer readback and programmatic window events do not certify compositor/monitor output, physical cross-monitor DPI changes, human interaction or unexpected TDR/hot-unplug, so this acceptance item stays open.
  A subsequent [small-window/pacing fix](PHASE2_PRESENTATION_PACING_REPORT.md) reproduces and repairs one-pixel-height failure, validates 294 full images and adds a configurable private-timer frame cap. Actual 30/60fps cap checks and owned-window wake/close pass; a seventh CPU CTest covers 2,278 clock/geometry assertions. These remain automated evidence and do not close the outstanding acceptance item.

## Remaining ordered implementation gates

1. Phase 1 foundation is accepted for its documented scope. Preserve the deferred
   Phase 2 memory/Intel issues and outstanding interactive/physical acceptance above.
2. Phase 3: Vulkan Windows/Linux backend, same readback/lifetime tests, same Vulkan competitors.
3. Phase 4: Metal implementation and actual Mac compile/runtime tests; keep unavailable evidence separate.
4. Phase 5: DXC DXIL/SPIR-V, SPIRV-Cross MSL, binding reflection and compiler/PSO caches.
5. Phase 6: measured allocator/upload ring/frame slots/deferred reclamation/cache optimizations,
   each with before/after data and regression validation.
6. Phase 7: complete cross-platform SDK distribution and supported-platform CI; Windows install
   and find_package smoke are already implemented as an early packaging check.

## Phase 3 — Vulkan implementation

- [x] Opt-in Vulkan 1.1 device/exact-adapter selection and buffer transfer slice:
  upload/device/readback, barriers, version/rejection contracts, retained native
  lifetimes and finite queue waits on Windows NVIDIA/Intel ([report](PHASE3_VULKAN_TRANSFER_REPORT.md)).
- [ ] Vulkan validation-layer execution (missing on this machine); explicit missing-layer rejection is separate.
- [ ] Textures/render passes/graphics pipelines, SPIR-V, bindings, timestamps and presentation.
- [ ] Linux native compile/runtime and equivalent readback/lifetime tests.
- [ ] Matching Vulkan competitor workloads and comparable measurements.

## Release milestones

| Release | Scope | Required evidence |
|---|---|---|
| v0.1 | D3D12/Vulkan/Metal common RHI | images, lifetime/validation, A–D, native platform builds |
| v0.2 | Compute + render graph | CPU reference kernels, barriers, scheduling/lifetime correctness |
| v0.3 | GPU memory/synchronization | measured allocation/upload/frame-time stability, stress |
| v0.4 | Graphics2D | identical sprite/batching/clip references and E |
| v0.5 | Graphics3D | mesh/instancing/depth/MRT/postprocess and F |
| v0.6 | Media graphics | NV12/P010/HDR/color/filter/external-resource contracts and H |
| v0.7 | UI rendering | canvas/clip/text integration contracts and image tests |
| v1.0 | Stable API/ABI and release | frozen contracts, multi-platform stability, real consumer validation |

There are no numerical superiority targets before comparable baseline evidence. A whole-SDK
performance ranking is not a release gate; correctness and maintenance remain mandatory.

Phase 1 foundation completion now passes the [machine-readable gate](../benchmarks/reports/2026-10-21-phase1-gpu-memory-final/acceptance.json).
The user approved a new elevated, private GPU ETW capture after the earlier WPR failure/cancellation.
No system policy/group changes or unrelated WPR session control were used. The LLGL/SDL3 public
semantic-workload CPU comparison and event-based process memory evidence retain their explicit scope.
Checkpoint maxima remain lower bounds; the new acceptance uses change events and reconstructed lifetimes.
