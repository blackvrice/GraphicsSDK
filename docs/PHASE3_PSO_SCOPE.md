# P3-BEN-05 pipeline creation/public release scope

This task executes the frozen BEN-01 PSO contract on the current Windows NVIDIA
Vulkan product and pinned SDL. It accepts CPU API intervals only: public pipeline
creation (including shader object creation/release within each implementation)
and public pipeline-handle release. Descriptor/payload preparation, GPU use,
completed fences/idle waits, full-image readback and teardown are outside timers.

The two cases create 1 or 8 pipelines per iteration using the same SPIR-V fixtures
and 65x37 RGBA8 target. Every created pipeline draws a distinct scissored region;
the full changing red/green regions and blue/cyan clear are checked each iteration.
This proves public pipeline use, not native creation counts or physical memory
reclamation. Driver caches are uncontrolled across and within processes: neither
cold-cache creation nor cache hits/misses are classified or inferred.

Four required core/synchronization-validation processes and four missing-layer/
wrong-target controls precede twenty independent timing processes. Each case/SDK
has five repetitions, five excluded warmups and thirty retained samples per
process (600 paired intervals, 150 per group). Adjacent SDK pairs alternate in
seeded case blocks; all raw samples, outliers and process medians are retained.
Earlier BEN-02 qualification timings are never pooled into these measurements.

Preparation/qualification may be reused only while their frozen inputs and exact
GPU/driver/LUID remain unchanged. The campaign records its own source revision
separately from the preparation revision. New validation and negative controls
still run for this campaign. Linux measurements, GPU timestamps, cache-controlled
comparisons and overall SDK rankings are outside this acceptance.
