# GraphicsSDK

Independent C++23 graphics SDK, namespace `gfx`, CMake target `GraphicsSDK::Core`.
**Initial development snapshot 0.0.1:** real Windows D3D12 implementation; Vulkan and Metal
are not implemented yet. This is not a completed cross-platform SDK release.

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

Own SDK and adapter code is MIT. Competitor SDKs and Windows tooling retain their own
licenses; see the [dependency and package audit](docs/DEPENDENCY_LICENSES.md),
[retained notices](licenses/README.md), and [competitor license comparison](docs/COMPETITOR_ANALYSIS.md#기능과-라이선스).
