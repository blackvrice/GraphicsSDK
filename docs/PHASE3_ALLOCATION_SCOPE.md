# P3-BEN-06 resource creation/public release scope

Execute the frozen BEN-01 resource contract on the current Windows NVIDIA Vulkan
product and pinned SDL. Measure two separate CPU API intervals: public device
resource creation and public resource-handle release. Descriptor/vector setup,
staging resource creation/release, changing payload generation/write, GPU copies,
fence/idle waits, full readback, reference comparison and teardown are untimed.

The four cases create 1 or 1,000 buffers (4,096 bytes each), or 1 or 1,000 textures
(65x37 RGBA8 each). Every resource receives a payload varying with iteration,
resource index and byte offset. The entire resource must round-trip through the
GPU and match. Padding is excluded from the logical verified texture bytes.

Both use their default public device-resource allocation policy. The SDK general
device buffer and SDL vertex-usage buffer differ in public usage semantics; both
are verified by the same full copy workload. Textures use sampled RGBA8 2D usage.
No native allocation counts, allocator/cache state, physical memory reclamation,
GPU timestamps or overall SDK ranking are inferred from public API timings.
Public release may defer native destruction. This is a bounded workload campaign;
long-duration memory/Intel crash investigations remain outside this task.

Eight core/synchronization-validation processes and four absent-layer/target
controls precede forty timing processes. Each case/SDK has five repetitions of
five excluded warmups and thirty retained paired intervals (1,200 pairs, 150 per
group). Seeded adjacent SDK pairs alternate across repetitions. All outliers and
process medians remain. Historical BEN-02 timings are never pooled into results.

Reuse preparation/qualification only if frozen source/build/runtime inputs and
exact GPU/driver/LUID remain unchanged. Record campaign and preparation revisions
separately; run fresh validation, controls and measurements for this task.
