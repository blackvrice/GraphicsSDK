# GraphicsSDK roadmap and acceptance

This is an initial 0.0.1 implementation, not a completed v0.1 release.
Only measured/tested scope is checked. Follow the remaining phases in order.

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
- [x] HLSL SM5.1 bootstrap, basic reflection, one sampled binding, timestamp/debug marker.
- [x] Real triangle/readback, WARP, GPU-based validation and installed consumer checks.
- [x] Same suite adapter and measured narrow draw-recording optimization.
- [x] Vertex/index input layouts, UInt16/UInt32 indexed draws, per-instance streams and bounded buffer views; full-image hardware/GBV/WARP tests, state/version/lifetime rejection and installed-consumer checks ([report](PHASE2_VERTEX_INPUT_REPORT.md)).
- [x] Explicit graphics binding layouts: bounded Texture2D/constant-buffer arrays, register spaces/stage visibility, static point/linear clamp/repeat samplers, shader compatibility and full-image/state/version/lifetime validation ([report](PHASE2_BINDINGS_REPORT.md)). Storage/UAV/compute, dynamic sampler arrays and unbounded bindings remain outside this graphics gate.
- [x] D32 depth attachments, eight compare functions/write masks, read-only/depth-only passes, up to eight mixed-format color attachments, Clear/Load and pipeline/pass compatibility; full color/depth references, MRT postprocess, atomic rejection and retained lifetimes on hardware/GBV/WARP ([report](PHASE2_DEPTH_MRT_REPORT.md)). Stencil/MSAA/resolve and fragment depth export are outside this initial gate.
- [x] Observed-loss status/reason caching, native opt-in RemoveDevice injection and explicit generation retirement/re-creation; old handle/fence rejection, blocked-recovery retry and rebuilt full images/swapchains on hardware/GBV/WARP ([report](PHASE2_RECOVERY_REPORT.md)).
- [ ] Long-running memory stress and measured reclamation acceptance. Diagnostic API/tooling, 15-minute hardware and 5-minute WARP observations are implemented; the 5-minute GBV campaign fails memory growth limits. Native queue overflow also exposed an error-counting gap, now repaired with unfiltered callbacks. The failed observations and post-repair factor probes remain separate ([report](PHASE2_MEMORY_STRESS_REPORT.md)); no completed stress gate is inferred from zero SDK-owned resources.
  Native-only controls now reproduce texture-creation growth with GBV state tracking, while CPU validation and the reduced-tracking diagnostic remain stable. A separate fixed-resource, full-GBV 30-second warmup + 300-second campaign passes the unchanged memory limits and complete readback checks ([reclamation investigation](PHASE2_GBV_RECLAMATION_REPORT.md)). This bounded reuse profile does not accept the original resource-churn gate or complete this item.
- [ ] Interactive presentation acceptance and unexpected driver TDR/hot-unplug recovery acceptance. Native RemoveDevice and hidden-window tests have separate, narrower evidence.

## Remaining ordered implementation gates

1. Complete the outstanding Phase 1/2 contracts and measurements above.
2. Phase 3: Vulkan Windows/Linux backend, same readback/lifetime tests, same Vulkan competitors.
3. Phase 4: Metal implementation and actual Mac compile/runtime tests; keep unavailable evidence separate.
4. Phase 5: DXC DXIL/SPIR-V, SPIRV-Cross MSL, binding reflection and compiler/PSO caches.
5. Phase 6: measured allocator/upload ring/frame slots/deferred reclamation/cache optimizations,
   each with before/after data and regression validation.
6. Phase 7: complete cross-platform SDK distribution and supported-platform CI; Windows install
   and find_package smoke are already implemented as an early packaging check.

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
