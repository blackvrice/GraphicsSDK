# GraphicsSDK

Independent C++23 graphics SDK, namespace `gfx`, CMake target `GraphicsSDK::Core`.
**Initial development snapshot 0.0.1:** real Windows D3D12 implementation; Vulkan and Metal
are not implemented yet. This is not a completed cross-platform SDK release.

The implementation includes buffers/textures and aligned copies, a small graphics pipeline,
command recording/submission, resource-state validation, fence-retained lifetimes, one sampled
texture binding, timestamps, headless rendering and a Win32 swapchain. It includes correctness
tests, installed-package consumer smoke and LLGL/SDL3/GraphicsSDK benchmark adapters.
Sandbox is a read-only source reference and is not a build dependency or integration target.

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

Own SDK and adapter code is MIT. Competitor SDKs and Windows tooling retain their own
licenses; see the [license audit](docs/COMPETITOR_ANALYSIS.md#기능과-라이선스).
