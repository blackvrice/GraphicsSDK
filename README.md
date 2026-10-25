# GraphicsSDK

Independent C++23 graphics SDK, namespace `gfx`, CMake target `GraphicsSDK::Core`.
**Initial development snapshot 0.0.1:** real Windows D3D12 implementation and an opt-in
[Vulkan transfers/color/depth passes/SPIR-V drawing, resource bindings, GPU timestamps and Win32 presentation](docs/VULKAN.md). Metal and later Vulkan features remain
unimplemented. This is not a completed cross-platform SDK release.

Vulkan validation now explicitly requests core/synchronization checks and has a
[pinned local runtime and native error/counter controls](docs/PHASE3_VULKAN_VALIDATION_REPORT.md).

The implementation includes buffers/textures and aligned copies, a small graphics pipeline,
command recording/submission, resource-state validation, fence-retained lifetimes, explicit texture/
constant-buffer binding layouts, static samplers, timestamps, headless rendering and a Win32 swapchain. It includes correctness
tests, installed-package consumer smoke and LLGL/SDL3/GraphicsSDK benchmark adapters,
plus optional pinned standalone Diligent Core, single-thread bgfx and The Forge D3D12 adapters.
Sandbox is a read-only source reference and is not a build dependency or integration target.

Public `Device::queryMemoryBudget` reports exact-adapter local/nonlocal OS process
usage, budgets and reservation values alongside the separate SDK ownership statistics.
See the [memory budget contract and validation](docs/PHASE2_MEMORY_BUDGET_REPORT.md).

```powershell
cmake --preset windows
cmake --build --preset windows-release
ctest --preset windows-release
./out/core/Release/gfx_triangle.exe out/triangle.ppm
```

```cmake
find_package(GraphicsSDK CONFIG REQUIRED)
target_link_libraries(MyApplication PRIVATE GraphicsSDK::Core)
```

See [build and benchmark commands](docs/BUILD.md), [actual measurements and limitations](docs/BENCHMARK_REPORT.md),
[API](docs/PUBLIC_API.md), [architecture](docs/ARCHITECTURE.md), [competitor analysis](docs/COMPETITOR_ANALYSIS.md),
[testing](docs/TESTING.md), [optimization log](docs/OPTIMIZATION_LOG.md), and [remaining roadmap](docs/ROADMAP.md).
All raw measurement files are actual observations; unavailable GPU measurements are null.
No general performance superiority claim is made.
The [Diligent comparison](docs/DILIGENT_BENCHMARK_REPORT.md) adds all eight existing scenarios
with the same shader binaries, readback checks and workload contracts.
The [bgfx comparison](docs/BGFX_BENCHMARK_REPORT.md) adds six matching workloads and separately
records pooled buffer allocations. Lazy bgfx program linking is excluded from the PSO workload.
The [The Forge comparison](docs/FORGE_BENCHMARK_REPORT.md) covers eight matching workloads
under a documented MSVC compatibility configuration with legacy NVAPI excluded.
The [GPU timestamp report](docs/GPU_TIMESTAMP_BENCHMARK_REPORT.md) compares matching draw
intervals for GraphicsSDK/LLGL/Forge and records Diligent's different resolve interval separately.
The [CPU/memory report](docs/CPU_MEMORY_BENCHMARK_REPORT.md) adds opt-in process CPU time,
lifetime CPU memory peaks and bounded GPU memory checkpoint maxima for the common Windows profile.
The [LLGL upload recovery report](docs/LLGL_UPLOAD_BENCHMARK_REPORT.md) validates a public
buffer-to-texture path and fresh repeated observations. The pinned WriteTexture bug and old-path
data remain historical; no vendor patch or native copy bypass is used.

The [Phase 1 extension report](docs/PHASE1_COMPLETION_REPORT.md) adds 22 LLGL cache/resource/
2D/3D/compute/video baselines with CPU references and fresh GPU/CPU observations. The additional
[SDL3 E-H comparison](docs/PHASE1_SDL_COMPARISON_REPORT.md) covers 14 public semantic workloads
with explicit shader binding differences, both GPU queries disabled and 84 independent processes.
**Phase 1 foundation acceptance is complete** for the documented Windows D3D12 scope:
the [GPU memory trace](docs/PHASE1_GPU_MEMORY_REPORT.md) reconciles five process lifetimes,
zero event loss and three transient controls. Other platforms, remaining SDK APIs and later phases
are still pending; this is not a completed SDK release.

Phase 2 now includes [vertex/index input and instanced indexed draws](docs/PHASE2_VERTEX_INPUT_REPORT.md),
with portable layout validation, hardware/GBV/WARP image checks and the `gfx_indexed_quads` example.
It also supports [graphics binding layouts](docs/PHASE2_BINDINGS_REPORT.md), bounded texture/constant-buffer
arrays, register spaces and stage visibility with immutable binding snapshots.
[Depth/MRT](docs/PHASE2_DEPTH_MRT_REPORT.md) adds D32 depth testing, read-only/depth-only passes,
up to eight mixed-format color targets, Clear/Load and full-image validation.
[Device recovery](docs/PHASE2_RECOVERY_REPORT.md) retires lost generations and recreates devices,
with native removal tests and rebuilt image/swapchain checks.
[Memory diagnostics](docs/PHASE2_MEMORY_STRESS_REPORT.md) add resource ownership snapshots,
full-reference stress campaigns and unfiltered cumulative validation error callbacks. The observed
hardware/WARP memory gates passed, but GBV memory growth failed; long-running acceptance remains
open. Interactive presentation and unexpected driver TDR/hot-unplug acceptance also remain in Phase 2.

The [windowed presentation example](examples/presentation/README.md) uses the installed SDK
for Win32 resize, minimize/restore, close and observed-loss recovery. Six automated
hardware/GBV/WARP and VSync cases pass full backbuffer references; human display and
unexpected driver-loss acceptance remain separate ([report](docs/PHASE2_PRESENTATION_REPORT.md)).
The [small-window/pacing follow-up](docs/PHASE2_PRESENTATION_PACING_REPORT.md) fixes
zero-sized scissors at one-pixel heights and adds a configurable application frame cap
with a private high-resolution timer and responsive window-message waits.
The [adapter API](docs/PHASE2_ADAPTER_REPORT.md) adds fresh candidate metadata,
backend-scoped identities and exact GPU selection/recovery. The window example can
list/select adapters; missing explicit targets fail without switching GPUs.
The [memory-budget campaign](docs/PHASE2_BUDGET_CAMPAIGN_REPORT.md) records public
local/nonlocal snapshots alongside independent native queries during resource churn,
with strict receipt validation. The original long-running GBV memory gate remains open.
The [fence-wait fix](docs/PHASE2_FENCE_WAIT_REPORT.md) validates timeouts even after
completion, makes zero-timeout polling event-free and reuses bounded native wait
registrations across retries while preserving resource ownership and loss handling.
The [swapchain guard fix](docs/PHASE2_SWAP_GUARD_REPORT.md) rejects noncurrent or
destroyed-chain backbuffers during recording/submission and invalidates recordings
across presentation, including after a full buffer-ring rotation.
The [copy rejection fix](docs/PHASE2_COPY_ATOMICITY_REPORT.md) prevents rejected
copies/transitions from retaining new resources or poisoning later submissions.
The [bounded thread snapshot collector](docs/PHASE2_THREAD_SNAPSHOT_REPORT.md) records
fault and other live thread contexts/stacks, with explicit truncation receipts for crash diagnosis.
The [allocation-path investigation](docs/PHASE2_FAULT_INVESTIGATION_REPORT.md) traces
SDK-free growth through GBV resource-info registration and internal command buffers,
with delayed post-device reclamation. Five fresh long Intel runs do not reproduce
the historical AV; its cause and the original memory acceptance remain open.
The [virtual-range analyzer](docs/PHASE2_VIRTUAL_RANGES_REPORT.md) reconstructs observed
commit/free lifetimes from those frozen traces, with recommit deduplication and
timestamp peak bounds. The tagged command-buffer ranges coexist and are later retired.
The [normal-run environment wrapper](docs/PHASE2_MEMORY_ENVIRONMENT_REPORT.md) records
before/after driver/runtime/workload identities and rejects confounded driver
comparisons. Its short same-environment controls do not close the memory or AV gates.
The [fixed-work diagnostic](docs/PHASE2_FIXED_WORK_REPORT.md) fixes warmup/measurement
round counts and compares matching work checkpoints, with full readback references
and separate guard/deadline abort receipts. Timed acceptance remains unchanged.

Own SDK and adapter code is MIT. Competitor SDKs and Windows tooling retain their own
licenses; see the [dependency and package audit](docs/DEPENDENCY_LICENSES.md),
[retained notices](licenses/README.md), and [competitor license comparison](docs/COMPETITOR_ANALYSIS.md#기능과-라이선스).

The [Vulkan Win32 presentation slice](docs/PHASE3_VULKAN_PRESENTATION_REPORT.md) uses
explicit presentation resource-completion fences. It is verified on the NVIDIA adapter;
the current Intel driver lacks the required maintenance1 feature and returns Unsupported.
Existing offscreen Vulkan suites still run on both adapters. Linux WSI and interactive
display acceptance remain open.

The [Vulkan upload memory study](docs/PHASE3_VULKAN_UPLOAD_MEMORY_REPORT.md) compares
fresh baseline/candidate SDK and SDL runs with separate CPU API stage diagnostics.
Persistent mapping and cached-memory candidates were rejected after measurement;
production SDK sources and public API remain unchanged.

The [same-Vulkan draw comparison](docs/PHASE3_VULKAN_DRAW_BENCHMARK_REPORT.md) adds
full-image references, draw/instancing workloads and CPU recording/submission/wait
partitions. SDL explicitly requests Vulkan 1.1 for the same validated SPIR-V 1.3
shaders. This comparison reuses the unchanged production SDK.

The [Vulkan pipeline comparison](docs/PHASE3_VULKAN_PSO_BENCHMARK_REPORT.md) measures
repeated shader/pipeline creation and public handle release as separate CPU API
intervals, with each pipeline used in full-image-verified drawing. Release latency
is not actual GPU memory reclamation; production SDK sources remain unchanged.

The [Vulkan resource creation/public-release comparison](docs/PHASE3_VULKAN_ALLOCATION_BENCHMARK_REPORT.md)
covers batches of one/1,000 device buffers and sampled textures, with every resource
verified by GPU copy and full readback on NVIDIA/Intel. It measures separate CPU
API intervals under each library's default allocation policy; physical reclamation
and native allocation counts are not inferred. SDK production sources are unchanged.

The [native Vulkan allocation trace](docs/PHASE3_VULKAN_ALLOCATION_TRACE_REPORT.md)
adds an own process-local layer to count native resource/memory calls by workload phase,
with full readback and object/byte conservation checks on NVIDIA/Intel. These diagnostic
records explain API policy differences; physical reclamation and profiler timing are not inferred.

The [small Vulkan buffer-pool candidate](docs/PHASE3_VULKAN_BUFFER_POOL_REPORT.md)
adds a guarded isolated SDK implementation and fresh baseline/candidate/SDL trials.
It cuts 1,000-buffer native memory calls to 63 but increases a single buffer's
requested reservation and creation latency. It remains an experiment; product SDK
sources and defaults are unchanged.

The [adaptive Vulkan buffer reservation study](docs/PHASE3_VULKAN_ADAPTIVE_BUFFER_POOL_REPORT.md)
adds an isolated 1-slot-first growth candidate and fresh baseline/fixed/adaptive/SDL
comparison. It removes the fresh single-buffer 64KiB request while retaining batch
allocation savings. Production adoption, failure injection and broader churn acceptance remain open.

The [Vulkan allocator failure study](docs/PHASE3_VULKAN_BUFFER_POOL_FAULT_REPORT.md)
adds guarded synthetic OOM/bind errors and an isolated buffer output-ownership fix.
It reproduces 24 invalid frees in the old candidate and none after the fix, with 60
clean GPU failure/retry processes. Product-wide ownership and broader churn/performance
acceptance remain open; product sources are unchanged.

Product Vulkan [creation-output ownership](docs/VULKAN_CREATION_OWNERSHIP_REPORT.md)
is now hardened at 29 creation sites. Actual-implementation PFN tests reproduce 21
invalid cleanup attempts before the fix and zero afterward; separate NVIDIA/Intel
buffer error/readback tests also pass. This is a product correctness fix. Buffer
pooling remains experimental, and no new timing or physical-memory claim is made.

The [real-loader failure profile](docs/VULKAN_LOADER_FAULT_REPORT.md) now covers
instance/device/messenger failures on NVIDIA/Intel and Win32 surface failures on
the supported NVIDIA adapter. It passes 96 synthetic error/retry cycles with zero
invalid cleanup; the old SDK reproduces 24 guarded invalid cleanup calls. Product
sources remain unchanged; this adds correctness evidence without a timing claim.

The [mixed-size pool study](docs/VULKAN_POOL_CHURN_REPORT.md) rebases the adaptive
candidate onto the hardened product and tests mixed churn, sparse occupancy and
retained recordings on both GPUs. It passes 224 injected bind errors and 35,392
full readbacks. Sparse reservation reaches 6.2x live native requirements; pooling
remains experimental and the production dedicated policy is unchanged.

The [rebased allocation-failure stage](docs/VULKAN_POOL_REBASE_FAULT_REPORT.md)
freshly reruns the full fault matrix and adds mixed-context fallback/recording cases.
It passes 88 processes, 324 injected errors and 1,200 full readbacks on the unchanged
candidate. Exact fallback and rollback are verified; the sparse reservation policy
and fresh performance acceptance remain before any product promotion.
