# Vulkan transfers, drawing, resource bindings, depth, timestamps and Win32 presentation

The opt-in Phase 3 implementation provides Vulkan 1.1 adapter enumeration, exact
selection, device creation, upload/device/readback buffers, explicit copy states,
RGBA8/RGBA16F 2D textures, color Clear/Load/Store passes, command recording/submission,
queue fences, graphics pipelines/vertex/index drawing, resource bindings, D32 depth, GPU timestamps and resource statistics. The D3D12
backend remains the default. `backendCompiled(Vulkan)` reports this limited
backend when enabled; Win32 presentation requires the completion-fence capabilities below.

The [D3D12/Vulkan support matrix](PHASE3_SCOPE.md) separates current capability/API
contracts, required gaps and conditional features. Vulkan debug markers remain a
required Phase 3 task; Linux native execution and WSI acceptance remain pending.
The [Windows GPU/driver support policy](PHASE3_WINDOWS_SUPPORT.md) records the
tested NVIDIA presentation and Intel unsupported combinations, plus fallback decisions.

## Upload CPU diagnostics

The [upload memory study](PHASE3_VULKAN_UPLOAD_MEMORY_REPORT.md) adds a standalone
CPU API stage profiler and fresh baseline/candidate/SDL comparisons. Persistent
mapping alone had no clear benefit; host-cached memory preference increased median
end-to-end cost in five of six tested cells. Both candidates were rejected.
Production buffer memory selection, map/unmap, public API and ABI remain unchanged.
Candidate policy tests and full source/build evidence are archived separately;
their 96 selected CTests describe the rejected candidate, not a new product release.
CPU API intervals are separate diagnostics, not GPU timestamps or driver profiling.

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
The [Linux environment inventory](PHASE3_LINUX_ENVIRONMENT.md) records the current
Windows host's unavailable WSL path and the prerequisites for a native receipt.

## Contract

- Candidates require Vulkan 1.1 and a graphics queue. CPU device type denotes
  software. Exact IDs hash all 16 `deviceUUID` bytes with FNV-1a-64; zero UUIDs,
  zero IDs and collisions are rejected. IDs are backend scoped and are never
  enumeration indices. Exact-ID/software-kind mismatch fails without fallback.
- Adapter memory fields sum local/nonlocal heap capacities. They are not OS
  usage/budget measurements; `queryMemoryBudget` returns `Unsupported`.
- Device buffers start in `Common`. Explicit `CopyDestination`/`CopySource`
  transitions surround copies. Upload buffers also accept vertex/index/constant read states;
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

Compute pipelines,
debug markers return `Unsupported`; their capability flags are
false. HLSL compilation remains the existing D3D bootstrap and supplies no SPIR-V.
These APIs, Linux execution and matching Vulkan competitor measurements remain
ordered follow-up work. See the [implementation report](PHASE3_VULKAN_TRANSFER_REPORT.md).

## Color-texture transfers

`createTexture` accepts non-attachment `RGBA8UNorm` and `RGBA16Float` 2D images.
Descriptions use the common 1..16384 extent contract and also check the selected
adapter's optimal-tiling image format/extent support. Each image has a dedicated
device-local allocation, one mip, one layer and one sample. Color render-target usage
is also accepted. D32 depth images follow the separate depth contract below. Color images support sampling as described below.

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
push constants and other builtins are outside this path. Descriptors are supported below. Standalone
float32 scalar/vector vertex attributes and matching float varyings are now supported below.

The SDK performs bounded header/instruction framing and interface inspection, **not
full semantic validation**. Supply trusted compiler output already checked by a
SPIR-V validator for the Vulkan environment. The repository fixture tool assembles
and validates its own shaders using a pinned local SPIRV-Tools library; that library
is not linked into or distributed with the SDK. `compileHlsl` remains DXBC only;
`reflectShader` explicitly returns Unsupported for SPIRV. General DXC and SPIR-V
reflection remain Phase 5 work.

Pipelines use triangle lists, no culling/MSAA, optional D32 depth and 0..8 floating
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
otherwise UInt32 binding returns Unsupported. Both input capability flags are true.

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

## Resource binding layouts

`resourceBinding` and `bindingLayouts` are true. Vulkan maps layout entry `i` to
`DescriptorSet 0, Binding i` and static sampler `i` to binding `32+i`. Register slot/space
retain the D3D12 shader mapping; they do not select Vulkan sets. Array element counts
must exactly match fixed shader descriptor arrays. Visibility must include every
declaring shader stage. Common layout checks and native per-stage/set descriptor and
fragment attachment limits apply before native pipeline creation.

Supported descriptors are separate float32 sampled 2D images, scalar immutable
samplers, and Uniform Block constant buffers. Blocks contain consecutive float4
members at offsets 0,16,32,..., up to 64KiB; minimum bytes are inspected in both stages.
Other uniform member shapes/layouts, combined image/samplers, storage, push constants,
runtime arrays and dynamic/nonuniform descriptor indexing are outside this slice.
Supply externally validated Vulkan 1.1 SPIR-V 1.3 modules. Inspection remains bounded
interface checking, not full semantic validation or public general SPIR-V reflection.

Static samplers use Point/Linear and Clamp/Repeat with one mip and no anisotropy or
comparison. All color textures carry sampled usage and views. Transition initialized
uploaded or rendered images to `ShaderRead`; native layout is SHADER_READ_ONLY_OPTIMAL.
Binding an uninitialized image or active render target fails. Linear sampling checks
native filter support; conservatively every texture in a layout with a linear sampler
must support it. Device and Upload buffers accept `ConstantBuffer`; Readback cannot
bind. Constant views require 256-byte-aligned offset/size, size 256..65536, complete
shader block coverage and adapter uniform alignment/range limits.

`setPipeline` and each new pass reset the selected resource snapshot. `bindResources`
requires a selected pipeline and exactly one texture or constant view for every layout
array element. Order is arbitrary. Complete validation precedes native allocation,
resource retention and version capture. Rejection preserves the prior set and budget.
Every accepted set has its own immutable descriptor set/pool: replacing it never changes
an earlier draw. A recording permits 256 resource descriptors in total and at most 256
snapshots; exhaustion returns InvalidState. Pools/sets, immutable samplers, views and
allocations survive caller wrapper destruction and retire in native dependency order.
Descriptor allocation is a correctness foundation; pooling/performance optimization is
still pending. Accepted Upload writes before submit cause stale-version rejection.

`sampledTexture=true` is the existing single fragment texture/sampler shorthand using
binding 0 and sampler binding 32 (Linear/Clamp). It cannot combine with explicit layout.
Use `bindTexture` only with this shorthand; explicit layouts use `bindResources`.

`gfx_vulkan_binding_tests [--validation]`, installed `vulkan_binding_consumer`, and
CPU `spirv_binding_contracts` cover arrays, VS constants/FS textures and constants,
minimum block sizes, two immutable samplers, view/version/ownership rejections and
full CPU sampling references. Isolated roots are `out/vulkan-bindings`,
`out/vulkan-bindings-install`, `out/vulkan-bindings-consumer`.
See [binding evidence](PHASE3_VULKAN_BINDINGS_REPORT.md). Native contracts follow
[descriptor layouts](https://docs.vulkan.org/refpages/latest/refpages/source/VkDescriptorSetLayoutBinding.html),
[descriptor binding](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdBindDescriptorSets.html)
and [adapter limits](https://docs.vulkan.org/refpages/latest/refpages/source/VkPhysicalDeviceLimits.html).

## D32 depth attachments and transfers

`TextureDesc{width,height,Format::D32Float,false,true}` creates a single-mip/layer/sample
depth image with a depth view and transfer usage. `depthAttachment` reports whether
the actual adapter supports this D32 attachment/transfer usage combination. Creation
checks native extent/sample/usage support. `footprint` uses four bytes per float32
texel, a 256-byte-aligned row pitch and full final-row padding. Full-image Upload or
Device buffer uploads and Readback or Device downloads use the depth aspect. Buffer
payloads are copied as supplied; callers provide valid depth data for rendering.

DepthWrite maps to DEPTH_STENCIL_ATTACHMENT_OPTIMAL and depth read/write access;
DepthRead maps to DEPTH_STENCIL_READ_ONLY_OPTIMAL and depth read access. Transitions
occur outside render passes. Color images reject depth states; depth images reject
RenderTarget and depth sampling via ShaderRead is Unsupported.

Passes support optional D32 depth alongside 0..8 mixed color attachments. A depth-only
pass is valid. Attachment extents must match and at least one attachment is required.
Depth Clear accepts a finite 0..1 value; Load requires an uploaded or cleared depth
image. Read-only depth requires Load and DepthRead; writable depth requires DepthWrite.
All attachments Store. Validation completes before any clear or resource/version
capture, including a late depth failure after otherwise valid colors.

`RenderTargetLayout.depth` must be D32Float. DepthState supports all eight CompareOp
values and test/write masks. Write requires test enabled and depth state requires a
depth layout. A depth-only pipeline can omit fragment bytes; a color layout requires
a fragment shader. Provided fragment modules must match the output layout as usual.
Pipeline/pass depth presence and color formats/counts must agree. Selecting a writing
pipeline in a read-only pass is allowed, but draw/drawIndexed returns InvalidState.
Depth bounds, stencil, depth sampling, MSAA and fragment depth export are unsupported.

External dependencies cover early/late fragment tests and depth attachment accesses
as well as color/transfer work. Pass/framebuffer/view/pipeline/recording lifetimes and
resource versions follow the existing retained-native model, including wrapper release
before completion and device retirement. No native cache or performance claim is made.

`gfx_vulkan_depth_tests [--validation]` and installed `vulkan_depth_consumer` compare
all D32/color pixels and sentinel padding for eight compare functions, write masks,
depth-disabled pipelines, 2/8 mixed MRT, order independence, Load/read-only passes,
depth-only fragment omission, full D32 transfer and 1080p/4K output. Rejection,
version/thread/ownership and retirement controls are separate from valid GPU work.
Current roots: `out/vulkan-depth`, `out/vulkan-depth-install`, `out/vulkan-depth-consumer`.
See [depth evidence](PHASE3_VULKAN_DEPTH_REPORT.md). Native contracts follow
[depth state](https://docs.vulkan.org/refpages/latest/refpages/source/VkPipelineDepthStencilStateCreateInfo.html),
[attachment load/store](https://docs.vulkan.org/refpages/latest/refpages/source/VkAttachmentDescription.html)
and [buffer/image copies](https://docs.vulkan.org/refpages/latest/refpages/source/VkBufferImageCopy.html).

## GPU timestamps

`timestampQueries` is true when the selected graphics queue reports 36..64 valid
timestamp bits and a finite positive `timestampPeriod`. Unsupported metadata returns
Unsupported from timestamp operations. No CPU timing is substituted for a GPU result.
Each supported `begin()` creates a two-slot timestamp query pool and records its reset
outside any render pass. A new recording discards the old result and retains its own
pool through submission/completion; device retirement destroys pools before the device.

Write slot 0 once for the start (TOP_OF_PIPE), and slot 1 once for the end
(BOTTOM_OF_PIPE). Writes may be inside/outside passes. Invalid indices, duplicate
writes and writes after resolve return InvalidArgument without changing the pair.
Call `resolveTimestamps()` once after both writes, outside a pass. This authorizes a
host query read; Vulkan does not need an intermediate readback buffer or query-copy
command. A reversed 1-then-0 pair may be recorded/resolved but result retrieval returns
InvalidState, independently of counter equality/wrap.

`elapsedGpuMilliseconds()` polls completion on the creating thread. It requires
resolve, successful submission and the matching completed fence. It reads two 64-bit
results with availability and without WAIT/PARTIAL flags. Unavailable/unsubmitted/
unresolved results return InvalidState; the function does not wait or manufacture zero.
An immediate post-submit read can succeed if the GPU has already completed. Repeated
successful reads return the cached result. Empty intervals may legitimately be zero.

Conversion masks valid bits and uses a modular tick difference multiplied by
timestampPeriod/1,000,000. A steady-clock interval from just before native submission
bounds the first read: if it reaches one complete counter cycle, retrieval conservatively
returns InvalidState because multiple wraps cannot be distinguished. This host bound
does not supply the reported duration. Read promptly; previously cached valid results
remain readable later. No calibrated timestamps, CPU/GPU clock correlation or device-wide
absolute time is exposed. The native scope of the recorded timestamp commands defines
the interval; it is not CPU frame time or a performance baseline.

Query pools add one retained item to pendingObjectReferences and are not Buffer/Image
allocations in uniqueResources/resourceAllocationBytes. The correctness implementation
creates a pool per supported recording; query-pool reuse/optimization remains future work.

`vulkan_timestamp_math` checks conversion, units, masking, wrap and ambiguous-order/cycle
controls on CPU. `gfx_vulkan_timestamp_tests [--validation]` and installed
`vulkan_timestamp_consumer` verify public readiness/reuse, positive timed copies,
independent recordings, dropped wrappers, mixed MRT+D32 images and retirement on both
actual adapters. Current roots: `out/vulkan-timestamps`, `out/vulkan-timestamps-install`,
`out/vulkan-timestamps-consumer`. Existing shader fixtures/tools are hash-checked and
reused; no new shader compilation/validation campaign is claimed.
See [timestamp evidence](PHASE3_VULKAN_TIMESTAMP_REPORT.md). Native contracts follow
[timestamp writes](https://docs.vulkan.org/refpages/latest/refpages/source/vkCmdWriteTimestamp.html),
[query results](https://docs.vulkan.org/refpages/latest/refpages/source/vkGetQueryPoolResults.html)
and [valid bits](https://docs.vulkan.org/refpages/latest/refpages/source/VkQueueFamilyProperties.html).

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

## Win32 presentation

`caps.swapChain` requires instance surface/win32_surface/get_surface_capabilities2/
surface_maintenance1, device swapchain/swapchain_maintenance1 and its feature bit.
The selected graphics queue must also support the actual surface. A caller-owned HWND
must belong to the process and creating thread. There is no adapter fallback.

Surface format is RGBA8 UNORM / SRGB nonlinear with color-attachment and both transfer
usages. Requested 2..4 buffers are adjusted to the native minimum; actual image count
and acquisition order are native. VSync uses FIFO; otherwise IMMEDIATE then MAILBOX,
or Unsupported if neither exists. Fixed native client extent must exactly match the
requested dimensions. Resize the HWND first and release acquired wrappers/recordings
before `resize`. Rejected size/reference checks preserve the existing chain; after
native recreation starts, a failure retires the old chain and requires resize retry.

Acquisition has a one-second native timeout, followed by a host fence wait on success.
Repeated acquire returns the current wrapper. First-use layout initialization permits
present without rendering but defines no texel values. Explicit RenderTarget/copy/
Present states apply. Offscreen Present is InvalidArgument; backbuffer ShaderRead is
Unsupported. Present requires the submitted Present state, invalidates resource
versions, and releases current acquisition. Both recording and submit reject noncurrent,
destroyed-chain/window and stale backbuffers. Retained descriptions remain readable.

An ordered semaphore signal submission connects prior graphics work to native present.
Per-image semaphores and maintenance1 present fences guarantee safe resource reuse and
teardown. `present` waits for resource completion; this does not prove monitor display
completion. Queue/device idle alone is insufficient for presentation resource lifetime;
there is no legacy teardown fallback. This initial host-synchronized implementation
has no frame-overlap or throughput claim. External swapchain images have views but no
SDK-owned memory allocation: statistics count the images and add zero fabricated bytes.
Views/framebuffers are released before swapchains and the device during retirement.

OUT_OF_DATE/SURFACE_LOST returns InvalidState with the native code; recreate as needed.
SUBOPTIMAL succeeds. Thread/generation rules and DeviceLost handling remain unchanged.
NVIDIA GTX 1660 SUPER supports the required feature; this Intel UHD 770 driver does not.
Its explicit Unsupported result is tested while all earlier offscreen GPU suites still
run on both adapters. This feature result does not explain the historical Intel AV.

Current roots are `out/vulkan-presentation`, `-install`, `-consumer`, `-only`, `-portable`.
The frozen runner excludes the three deferred `^memory_reclamation_` tests from the
integrated run. See the [implementation and evidence report](PHASE3_VULKAN_PRESENTATION_REPORT.md).
Linux WSI, interactive/physical display acceptance, unexpected loss recovery and
matching competitor measurements remain separate work.

The standalone [draw/instancing benchmark](PHASE3_VULKAN_DRAW_BENCHMARK_REPORT.md)
uses the existing installed Vulkan SDK. SDL explicitly requests Vulkan 1.1 via
its public options, allowing identical externally validated SPIR-V 1.3 shaders.
Default SDL Vulkan 1.0 rejects those shader versions; this is recorded as an
excluded compatibility attempt. SDK support remains SPIR-V 1.3; production
sources, public API and ABI are unchanged.

The standalone [pipeline creation/public-release profile](PHASE3_VULKAN_PSO_BENCHMARK_REPORT.md)
uses the unchanged installed SDK and SDL. Identical SPIR-V 1.3 pipelines are
created in batches of one/eight, used for striped full-image checks, then released
after untimed fence/idle completion. CPU creation and public release intervals
are separate; native destruction, actual memory reclamation and cache hit/miss
classification are outside its claims.

The standalone [resource creation/public-release profile](PHASE3_VULKAN_ALLOCATION_BENCHMARK_REPORT.md)
compares one/1,000 device buffers or sampled RGBA8 textures with GPU copy/full readback.
It uses public defaults: SDK dedicated allocation and broad buffer usage versus SDL's
internal allocation policy and vertex-buffer usage. These are semantic API workloads,
not identical VkMemory allocation flags/counts or actual memory reclamation timings.
The production implementation is unchanged.

The [process-local native allocation layer](PHASE3_VULKAN_ALLOCATION_TRACE_REPORT.md)
observes eight resource/memory commands without wrapping or replacing Vulkan objects.
It binds phases to explicit diagnostic workload markers, tracks device-scoped handles
and validates memory/object conservation through teardown. VkDeviceMemory request bytes
are not OS commitment, residency, VRAM peaks or driver-internal allocation counts.
SDK production code and API are unchanged.

The [isolated small-device-buffer candidate](PHASE3_VULKAN_BUFFER_POOL_REPORT.md)
uses 64KiB blocks with memory-type/stride-separated slots and native-lifetime leases.
It honors native alignment/dedicated preferences and counts shared reserved bytes once.
Host buffers and images stay dedicated; empty blocks return immediately. It remains
outside the product because fixed reservation penalizes sparse use. Product behavior,
public API and the current dedicated allocation path are unchanged.

An [adaptive small-buffer candidate](PHASE3_VULKAN_ADAPTIVE_BUFFER_POOL_REPORT.md)
uses one-slot-first geometric growth per live memory type/stride, capped at 64KiB.
It is applied only to a copied SDK. Public ABI and product sources remain unchanged.
Empty blocks are freed, existing slots retain native lifetimes, and occupied large
blocks do not compact automatically. Request-byte accounting is not physical residency.

The [buffer failure study](PHASE3_VULKAN_BUFFER_POOL_FAULT_REPORT.md)
changes a separate adaptive candidate to acquire buffer memory output ownership only
after allocation succeeds. Guarded poison-output controls reproduce the prior invalid
free without forwarding fake handles to VVL/the driver. Slot rollback, native lifetime,
existing data and retry are verified. Product creation paths still need their own
output-ownership audit/fix; this experiment does not modify the product SDK.

The [product creation-output fix](VULKAN_CREATION_OWNERSHIP_REPORT.md) uses
private `acquireOutput` to take ownership only after successful creation. Graphics
pipeline arrays retain their defined null/partial-success cleanup. Public ABI,
host-memory preference and dedicated resource allocation remain unchanged; no pool
policy is promoted. Historical product inputs are resolved through a verified
pre-fix snapshot by the new audit, without changing old inventories/reports.

The [real-loader initialization contracts](VULKAN_LOADER_FAULT_REPORT.md) test
synthetic host/device OOM in instance, debug messenger, device and supported Win32
surface creation. The current 29-site ownership fix needs no further product change.
Dispatchable failures use null; non-dispatchable null/poison paths have guarded
cleanup controls. Successful retries preserve live guard data and zero VVL errors.

The [current-product pool rebase](VULKAN_POOL_CHURN_REPORT.md) keeps all 29 product
creation guards and adds one for block allocation in an isolated candidate. Pool
policy headers retain their reviewed bytes. Mixed churn and retained recordings
pass exact binding/data checks, but one survivor per block retains 6.2x the live
native requirements in the recorded sparse case. No product promotion is made.

The [mixed-context exact fallback contracts](VULKAN_POOL_REBASE_FAULT_REPORT.md)
verify that free-slot reuse leaves an allocation fault pending, the next expansion
consumes it, and successful fallback binds exactly 4KiB of new memory. Repeated
OOM, unknown errors and fallback bind failure preserve existing bindings/data and
growth retry. No allocator source or product policy changes in this stage.
