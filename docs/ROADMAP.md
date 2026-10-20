# GraphicsSDK roadmap and acceptance

This is an initial 0.0.1 implementation, not a completed v0.1 release.
Only measured/tested scope is checked. Follow the remaining phases in order.

## Phase 0 — research

- [x] Read-only Sandbox source/interface audit at a fixed SHA.
- [x] Seven competitor snapshots, feature/architecture/license comparison and source links.
- [x] Diligent commercial Metal, NVRHI absence of Metal, Forge Windows Vulkan policy distinguished.
- [x] No Sandbox modifications or integration.
- [x] Windows SDK/LLGL/SDL3 and added Diligent Core compiler-read dependency inventories, notices, source origin evidence and SDK package checks ([audit scope](DEPENDENCY_LICENSES.md)).

Future redistributed competitors/tools and other platform/configuration changes require a fresh
inventory and terms review; the checked item is limited to the current reviewed artifacts.

## Phase 1 — common benchmark foundation

- [x] Fixed LLGL/SDL3 D3D12 adapters; independent benchmark CMake project.
- [x] Same physical adapter/driver/compiler/validation/workload contract checks.
- [x] Common allocation workload and actual same-machine competitor data.
- [x] Draw 1k/10k/100k, instancing; pre/post image readback.
- [x] Persistent 1080p/4K RGBA8 and 4K RGBA16F upload; byte readback.
- [x] Repeated PSO creation from identical precompiled bytecode (driver cache uncontrolled).
- [x] Multiple processes, warmup, P50/P95/P99/MAD, JSON/CSV and comparison reports.
- [x] Standalone pinned Diligent Core D3D12 adapter: all eight existing workloads, native validation/readback, dependency inventory and same-machine repeated measurements ([report](DILIGENT_BENCHMARK_REPORT.md)).
- [ ] Remaining primary comparison adapters: bgfx and The Forge (overall competitor expansion remains incomplete).
- [ ] Low-level GPU timestamp comparisons where both SDKs expose matching functionality.
- [ ] CPU utilization, peak CPU/GPU memory, controlled shader cache cold/warm and PSO cache hit/miss.
- [ ] Multi-texture uploads, reuse/in-flight allocation scenarios, switching and binding workloads.
- [ ] E/F/G/H workloads and correctness references (2D/3D/compute/media stages).

The minimal same-environment competitor comparison criterion is met for the implemented
end-to-end subset. The full requested A–H benchmark suite is not complete.

## Phase 2 — D3D12 initial core

- [x] C++23/Result, independent static library and capability queries.
- [x] Device/Buffer/Texture/Pipeline/CommandList/Queue/Fence/render pass/swapchain.
- [x] Explicit transitions with stale-recording validation and retained native lifetimes.
- [x] HLSL SM5.1 bootstrap, basic reflection, one sampled binding, timestamp/debug marker.
- [x] Real triangle/readback, WARP, GPU-based validation and installed consumer checks.
- [x] Same suite adapter and measured narrow draw-recording optimization.
- [ ] Vertex/index buffers, generalized binding layouts, depth/MRT and broader correctness coverage.
- [ ] Device loss recovery/injection, long-running memory stress and interactive presentation acceptance.

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
