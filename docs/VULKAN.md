# Vulkan buffer-transfer backend

The opt-in Phase 3 implementation provides Vulkan 1.1 adapter enumeration, exact
selection, device creation, upload/device/readback buffers, explicit copy states,
command recording/submission, queue fences and resource statistics. The D3D12
backend remains the default. `backendCompiled(Vulkan)` reports this limited
backend when enabled; it does not promise rendering support.

## Build

Use the reviewed, unmodified [Vulkan-Headers v1.3.290](https://github.com/KhronosGroup/Vulkan-Headers/tree/v1.3.290)
commit `b379292b2ab6df5771ba9870d53cf8b2c9295daf`:

```powershell
git clone --depth 1 --branch v1.3.290 https://github.com/KhronosGroup/Vulkan-Headers.git .deps/Vulkan-Headers
cmake -S . -B out/vulkan-transfer -G "Visual Studio 17 2022" -A x64 -DGFX_ENABLE_VULKAN=ON
cmake --build out/vulkan-transfer --config Release --parallel 1
ctest --test-dir out/vulkan-transfer -C Release --output-on-failure -j 1
./out/vulkan-transfer/Release/gfx_vulkan_tests.exe --validation
cmake --install out/vulkan-transfer --config Release --prefix out/vulkan-install
```

`GFX_VULKAN_HEADERS` can select another checkout. A changed dependency requires a
new compiler-input/license audit before distribution. Public/installed SDK headers
do not include Vulkan headers, and consumers need no Vulkan SDK or import library.
Windows loads the system `vulkan-1.dll`; Linux code loads `libvulkan.so.1` and links
`${CMAKE_DL_LIBS}`. The loader, ICD, driver and validation layer are external
prerequisites and are never bundled. Linux native compilation/runtime is still pending.

## Contract

- Candidates require Vulkan 1.1 and a graphics queue. CPU device type denotes
  software. Exact IDs hash all 16 `deviceUUID` bytes with FNV-1a-64; zero UUIDs,
  zero IDs and collisions are rejected. IDs are backend scoped and are never
  enumeration indices. Exact-ID/software-kind mismatch fails without fallback.
- Adapter memory fields sum local/nonlocal heap capacities. They are not OS
  usage/budget measurements; `queryMemoryBudget` returns `Unsupported`.
- Device buffers start in `Common`. Explicit `CopyDestination`/`CopySource`
  transitions surround copies. Upload/readback states remain fixed. Each buffer
  has a dedicated Vulkan allocation; this initial implementation has no allocator
  pooling or performance claim.
- Copy ranges start at zero and accept any positive byte count within both
  descriptions, including odd lengths. Host reads/writes accept bounded offsets.
  Noncoherent host memory uses whole-allocation mapping and flush/invalidate.
  Memory barriers cover transfer dependencies and GPU/host visibility.
- CPU writes and successful submissions invalidate recorded resource versions.
  Rejected transitions/copies retain no new resource references or versions.
  An ended recording submits once; `begin` replaces a completed recording.
  Recordings and pending submissions retain native resources, not public wrappers.
- Queue fences are generation scoped and monotonically numbered. Zero timeout
  polls; finite range is 0..4294967294ms even for completed targets. A timeout
  retains pending work and permits retry. Submission allocates its retention node
  before native submission. Draining releases pending references.
- Native operations require the creating thread. Destroying the public device
  drains and retires its native generation, including still-held children. Child
  descriptions remain available; native operations return `DeviceLost`.
  Observed native device loss is cached, but recovery and loss injection are not
  implemented. This is not unexpected-device-loss acceptance.
- `DeviceDesc.validation=true` requires `VK_LAYER_KHRONOS_validation` and
  `VK_EXT_debug_utils`. Missing support fails explicitly. The callback counts
  ERROR messages atomically; a zero count without a validation device does not
  constitute validation-layer acceptance.

Textures, graphics/compute pipelines, rendering, binding layouts, timestamps,
debug markers and swapchains return `Unsupported`; their capability flags are
false. HLSL compilation remains the existing D3D bootstrap and supplies no SPIR-V.
These APIs, Linux execution and matching Vulkan competitor measurements remain
ordered follow-up work. See the [implementation report](PHASE3_VULKAN_TRANSFER_REPORT.md).

The synchronization and lifetime rules follow the primary
[copy](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdCopyBuffer.html),
[submission](https://docs.vulkan.org/refpages/latest/refpages/source/vkQueueSubmit.html),
[mapping](https://docs.vulkan.org/refpages/latest/refpages/source/vkMapMemory.html),
[UUID](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceIDProperties.html)
and [fence wait](https://docs.vulkan.org/refpages/latest/refpages/source/vkWaitForFences.html)
specifications.
