# Adaptive Vulkan small-buffer reservation experiment

This is an isolated successor to the fixed 64KiB experiment. Production SDK sources,
public ABI, vendor trees and the prior experiment are unchanged. It is not adopted.

Eligible device-local buffers use separate VkBuffer objects bound at aligned,
nonoverlapping offsets. Classes remain power-of-two strides from 64 to 4096 bytes,
separated by memory type. A class starts with one slot. When all live blocks are
full, the next block doubles the largest live block's capacity, up to 64KiB.
For fresh 4096-byte buffers, capacities are 1, 2, 4, 8, 16, 16, ... .

This requests 4096 bytes for a fresh single 4KiB buffer. A fresh batch of 1000
buffers requests 4,124,672 bytes in 66 blocks (1007 slots). The fixed candidate
requests 65,536 bytes for one and 4,128,768 bytes in 63 blocks for 1000.
Native request bytes do not measure physical residency or driver allocation.

Existing holes are reused before growth. The native Allocation retains each slot
through ended recordings and pending GPU submissions. Empty blocks are freed
immediately; no empty-block cache, migration or compaction is introduced. A lone
survivor in a previously populated 64KiB block still retains that whole block.
After all leases end, a fresh class starts at one slot again. The growth rule is
based on currently live blocks, not a permanent high-water mark.

Upload/readback, images, buffers above the size/alignment threshold, and buffers
preferring or requiring dedicated allocation stay dedicated. Block allocation OOM
attempts exact-size fallback. Failure injection, mixed concurrent pressure, and
broader size/distribution performance acceptance remain open; this experiment
does not approve those paths or the deferred long memory/Intel AV investigation.

`BufferPoolRange.hpp` and CPU tests cover bounded capacity, growth, alignment,
nonoverlap, exhaustion and reuse. GPU tests cover sparse boundary batches,
partial release/replacement, retained large-block survivors, reset after empty,
distinct data in retained/new slots, mixed sizes and retired device generations.
The generator checks the original SDK fingerprint and unique patch anchors, and
only creates a new source tree under `out/`. `candidate.patch` is the review diff.

The study uses fresh baseline, fixed candidate, adaptive candidate and pinned SDL
processes. Validation/native tracing runs separately from CPU API timing. See
[the report](../../docs/PHASE3_VULKAN_ADAPTIVE_BUFFER_POOL_REPORT.md) and its linked
receipts, raw commands, statistical summaries and reproducible verification.
