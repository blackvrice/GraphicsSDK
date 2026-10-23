# Vulkan transfers, color passes and SPIR-V vertex/index drawing

The opt-in Phase 3 implementation provides Vulkan 1.1 adapter enumeration, exact
selection, device creation, upload/device/readback buffers, explicit copy states,
RGBA8/RGBA16F 2D textures, color Clear/Load/Store passes, command recording/submission,
queue fences, graphics pipelines/vertex/index drawing and resource statistics. The D3D12
backend remains the default. `backendCompiled(Vulkan)` reports this limited
backend when enabled; depth/descriptor bindings/timestamps/presentation remain unimplemented.

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
  transitions surround copies. Upload buffers also accept vertex/index read states;
  readback stays CopyDestination. Each buffer
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
  `VK_EXT_debug_utils`, `VK_EXT_validation_features`, `VK_EXT_layer_settings`.
  It requests synchronization validation (including submit-time checking), a zero
  duplicate-message limit and an empty VUID filter. Missing support fails explicitly.
  The callback counts delivered ERROR messages atomically. Environment/settings
  files can override layer API settings; controlled acceptance clears those inputs.
  A zero count without a validation device is not validation-layer acceptance.

Depth textures, compute pipelines, binding layouts, timestamps,
debug markers and swapchains return `Unsupported`; their capability flags are
false. HLSL compilation remains the existing D3D bootstrap and supplies no SPIR-V.
These APIs, Linux execution and matching Vulkan competitor measurements remain
ordered follow-up work. See the [implementation report](PHASE3_VULKAN_TRANSFER_REPORT.md).

## Color-texture transfers

`createTexture` accepts non-attachment `RGBA8UNorm` and `RGBA16Float` 2D images.
Descriptions use the common 1..16384 extent contract and also check the selected
adapter's optimal-tiling image format/extent support. Each image has a dedicated
device-local allocation, one mip, one layer and one sample. Color render-target usage
is also accepted; depth remains `Unsupported`. Sampling is not implemented.

`footprint()` reports `rowBytes = width * texelBytes`, a 256-byte-aligned `rowPitch`,
and `size = rowPitch * height`, including final-row padding. Copy the full image at
buffer offset zero with a buffer at least `size` bytes long. Native row length is in
texels (`rowPitch / texelBytes`); padding is neither copied into the image nor written
by readback. This footprint is a staging-buffer layout, not native image allocation size.
Upload sources may be Upload or Device buffers; download destinations may be
Readback or Device buffers. The opposing host memory kinds are rejected.

Images begin logically in `Common` with an uninitialized native layout. The first
accepted transition uses `UNDEFINED`; later transitions preserve the submitted layout.
Explicit `CopyDestination` and `CopySource` are required around copies; `Common`
maps to `GENERAL`. Same-layout repeated copies also carry memory dependencies.
Images share buffer version/atomic-rejection/retained-allocation/retirement rules.
No partial regions, mip chains, arrays, image-to-image API or allocator pooling are exposed.

`gfx_vulkan_texture_tests [--validation]` exercises odd widths, padded rows, 1080p/4K,
both formats, repeated copies, device-buffer staging, changing-payload reuse,
version rejection and wrapper/device lifetimes on each enumerated adapter.
The installed `vulkan_texture_consumer` repeats this public suite.
The isolated build/install/consumer roots are `out/vulkan-textures`,
`out/vulkan-textures-install` and `out/vulkan-textures-consumer`; enable
`GFX_VULKAN_VALIDATION_TESTS=ON` for required validation tests without skips.
See [texture evidence](PHASE3_VULKAN_TEXTURE_REPORT.md).

The image paths follow Khronos [buffer/image addressing](https://docs.vulkan.org/refpages/latest/refpages/source/VkBufferImageCopy.html),
[image format queries](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetPhysicalDeviceImageFormatProperties.html)
and [image barriers](https://docs.vulkan.org/refpages/latest/refpages/source/VkImageMemoryBarrier.html).

## Color attachment passes

Create `TextureDesc.renderTarget=true`, transition explicitly to `RenderTarget`, then
use either `beginRenderPass(texture, clear)` or `beginRenderPass(RenderPassDesc)`.
The latter accepts 1..min(8, adapter maxColorAttachments) color attachments with
matching dimensions, distinct resources and mixed RGBA8/RGBA16F formats. Each uses
Clear or Load and always Store, over the full image. `multipleRenderTargets` reflects
whether the selected adapter supports at least two color attachments. This flag
does not advertise shaders, draw calls or depth support.

Native image layout and initialized contents are independent. A new image has no
initialized contents even after a transition to RenderTarget. Clear or a full accepted
upload initializes them; Load requires initialization in the current recording or
an earlier successful submission. Rejected multi-attachment passes neither clear
earlier attachments nor capture new versions/references. Clear values must be finite;
Load ignores clear values. Attachment load/store uses explicit external dependencies,
including transfer stages; adjacent passes with the same layout remain synchronized.

Nested passes, ending/submitting a recording with a pass open, and buffer/image
transitions/copies inside a pass return `InvalidState` and preserve the open pass.
`endRenderPass` requires an open pass. Framebuffers/render passes are retained by
their recording, destroyed before attachment views, and included in pending reference
statistics. No render-pass cache or allocation optimization is claimed.

`gfx_vulkan_render_tests [--validation]` and installed `vulkan_render_consumer`
compare full clear/load images, uploaded-content Load, untouched row padding,
1..8 mixed-format attachments, rejected-pass atomicity and submitted/unsubmitted
native lifetimes. The current isolated roots are `out/vulkan-color-pass`,
`out/vulkan-color-pass-install` and `out/vulkan-color-pass-consumer`.
See [color-pass evidence](PHASE3_VULKAN_COLOR_PASS_REPORT.md).

## Initial graphics pipelines and drawing

Use `ShaderBinary{stage, bytes, ShaderFormat::SPIRV}` for both stages. The default
format remains DXBC for source compatibility with existing HLSL compilation; rebuild
all consumers because the ShaderBinary layout has changed. Vulkan requires externally
validated **SPIR-V 1.3 for Vulkan 1.1**, one `main` entry, Shader capability, Logical
GLSL450 memory model, standalone float4 vertex Position and matching float4 fragment
outputs at locations 0..N-1. Fragment mode is OriginUpperLeft. VertexIndex/InstanceIndex
inputs are allowed. Extensions, imported instruction sets, member-decorated interfaces,
descriptors, push constants and other builtins are outside this initial path. Standalone
float32 scalar/vector vertex attributes and matching float varyings are now supported below.

The SDK performs bounded header/instruction framing and interface inspection, **not
full semantic validation**. Supply trusted compiler output already checked by a
SPIR-V validator for the Vulkan environment. The repository fixture tool assembles
and validates its own shaders using a pinned local SPIRV-Tools library; that library
is not linked into or distributed with the SDK. `compileHlsl` remains DXBC only;
`reflectShader` explicitly returns Unsupported for SPIRV. General DXC and SPIR-V
reflection remain Phase 5 work.

Pipelines use triangle lists, no culling/depth/MSAA, and 1..8 floating
color outputs bounded by the adapter limit. Layout/format count must match the active
pass. Both RGBA8/RGBA16F support and optional blending features are queried first.
Alpha blending matches D3D12: RGB = src.rgb*src.a + dst.rgb*(1-src.a), alpha = src.a.
No pipeline/driver cache control or performance improvement is claimed.

`setPipeline` requires an active pass and retains native pipelines, not wrappers.
Each new pass resets the binding and starts with full viewport/scissor. A negative
Vulkan viewport height maps clip-space +Y to the top, matching the existing D3D12
orientation. `setScissor` accepts a nonempty in-bounds rectangle. `draw` requires a
bound compatible pipeline, positive vertex/instance counts and nonoverflowing
firstVertex/firstInstance ranges. Rejection preserves prior binding/scissor. Pipeline/module/layout/render-pass
lifetimes are independent of caller wrappers and retire with the native device.

`gfx_vulkan_draw_tests [--validation]` / installed `vulkan_draw_consumer` verify full
images for oversized/ordinary triangles, base vertex/instance IDs, instancing,
scissor/pipeline switches, Load+blend and mixed-format 2/8 outputs on real adapters.
`spirv_interface_contracts` exercises inspection without a GPU. Isolated current roots:
`out/vulkan-draw`, `out/vulkan-draw-install`, `out/vulkan-draw-consumer`.
See [draw evidence](PHASE3_VULKAN_DRAW_REPORT.md).

## Vertex and index input

`VertexLayout.attributes[i]` maps to SPIR-V location `i`. Attribute semantic/name/index
continue to serve D3D12 mapping and portable structure checks; Vulkan does not derive
locations from their names. Float/Float2/Float3/Float4 map to float32 scalar/vectors;
UNorm8x4 maps to a normalized four-component float input. Layout and shader input
count/types must match exactly. Standalone default-interpolated float scalar/vector
vertex outputs and fragment inputs must have the same location/component maps.
Interpolation decorations, arrays/struct interfaces and integer attributes are unsupported.

Binding slots, stride, attribute offsets and format features are checked against
the selected adapter limits after common layout validation. Buffers carry vertex/index
usage alongside transfer usage. Device and Upload memory accept VertexBuffer,
IndexBuffer and VertexAndIndexBuffer states; Upload also accepts CopySource. Readback
memory cannot be vertex/index input. Transitions still occur outside render passes.

`bindVertexBuffer`/`bindIndexBuffer` require recording, may occur inside/outside passes,
and persist within a recording across passes. size=0 selects remaining bytes. Views
must contain 1..UINT32_MAX bytes. Index format is UInt16/UInt32, with element-aligned
offset/size. Full UInt32 index range is enabled only when the adapter supports it;
otherwise UInt32 binding returns Unsupported. Both input capability flags are true,
while descriptor bindings remain false.

At draw, each required vertex binding must have matching stride/read state and
layout-aligned offset, including all attributes of the final element. Nonindexed
firstVertex/count and per-instance firstInstance/count are bounded by their views.
Indexed firstIndex/count is bounded by the index view, and positive baseVertex is
checked against the vertex view. The caller must ensure every actual index value +
baseVertex addresses valid vertex data within the view; the SDK does not map/scan
device index contents. Negative baseVertex is permitted under that existing API contract.
Primitive restart and UInt8 indices are not enabled. Rejected binds/draws preserve prior
bindings and capture no new resources/versions. Accepted bindings retain native allocations,
and CPU writes/other submissions invalidate their observed versions.

`gfx_vulkan_vertex_tests [--validation]` and installed `vulkan_vertex_consumer` check
all five formats, color interpolation, sparse slots/per-instance streams, both memory
kinds, UInt16/UInt32, +/- baseVertex, firstVertex/firstIndex/firstInstance, combined
vertex/index allocations and 1080p/4K indexed images. `spirv_vertex_contracts` checks
input/varying inspection on CPU. Current isolated roots: `out/vulkan-vertex`,
`out/vulkan-vertex-install`, `out/vulkan-vertex-consumer`.
See [vertex/index evidence](PHASE3_VULKAN_VERTEX_REPORT.md).

The synchronization and lifetime rules follow the primary
[copy](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdCopyBuffer.html),
[submission](https://docs.vulkan.org/refpages/latest/refpages/source/vkQueueSubmit.html),
[mapping](https://docs.vulkan.org/refpages/latest/refpages/source/vkMapMemory.html),
[UUID](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceIDProperties.html)
and [fence wait](https://docs.vulkan.org/refpages/latest/refpages/source/vkWaitForFences.html)
specifications.

## Process-local validation

```powershell
python tools/prepare_vulkan_validation.py
cmake -S . -B out/vulkan-validation -G "Visual Studio 17 2022" -A x64 -DGFX_ENABLE_VULKAN=ON -DGFX_VULKAN_VALIDATION_TESTS=ON -DGFX_BUILD_EXAMPLES=OFF
cmake --build out/vulkan-validation --config Release --parallel 1
```

The preparation tool verifies `tools/vulkan-validation.lock.json` (VVL v1.3.290
and five exact dependency commits), refuses dirty/mismatched checkouts, builds
serially and appends `/MP1` for upstream MSVC projects. The local diagnostic build
disables mimalloc, upstream unit tests and SPIR-V executables. The layer uses
`/Od /Ob0` after the optimized build hit MSVC C1001;
SDK Release optimization is unchanged and no validation-performance claim is made.
It leaves the registry, drivers and persistent environment unchanged. Runtime hashes and full
commands are written below `out/vulkan-validation-runtime`.

Run validation children with `VK_LAYER_PATH` set to the directory containing the
recorded layer JSON/DLL. For controlled runs clear inherited Vulkan/VVL settings,
point `VK_LAYER_SETTINGS_PATH` at an empty local settings file, and disable implicit
layers using the process-local loader setting. The frozen runner in the
[validation report](PHASE3_VULKAN_VALIDATION_REPORT.md) records these exact inputs.
`GFX_VULKAN_VALIDATION_TESTS=ON` registers required tests; absent/broken layers fail,
and no skip or driver-only fallback is accepted. The native positive-control test
contains intentional errors and runs separately from zero-error SDK work.
