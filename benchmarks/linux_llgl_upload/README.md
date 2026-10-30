# Linux approved LLGL upload correctness preparation

This consumer is generated from the frozen Windows correctness source. Its
upload resources, Map/Unmap, copy, fence completion, full changing readback,
corrupt-readback and core/sync control bodies remain unchanged. Only platform
selection/modules/identity, diagnostic scope and SHA-256 implementation are
adapted. System OpenSSL Crypto replaces Windows BCrypt; it is a benchmark build
prerequisite and is not a new SDK dependency or redistributed binary.

```sh
python3 tools/linux_llgl_upload.py run --probe out/linux-llgl-evidence --output out/linux-llgl-upload-new
python3 tools/linux_llgl_upload.py check --output out/linux-llgl-upload-new --live
```

The preceding approved LLGL probe must be clean, software-qualified and built at
the same revision. Its existing audited shared libraries are reused unchanged;
the new consumer is compiled separately with GCC 14 and recorded GNU dependencies.
The original LLGL checkout and Windows consumer sources are never edited.

All four inputs (odd RGBA8, 1080p RGBA8, 4K RGBA8 and 4K RGBA16F) run plain and
with pinned required validation. Each process checks one warmup and three further
complete changing images against the frozen payload and independent CPU hash
reference. Eight normal processes verify 32 complete images. Two native core/sync
positive controls record no GPU control submission. Five rejections cover missing
required layer, wrong UUID, wrong kind, corrupt readback and disabled sync detection.

The runner retains all process outputs, source/adaptation snapshots, commands,
module hashes, actual verbose compiler commands, GNU depfiles and input/license
classification. This is a CPU/software ICD correctness preparation campaign:
performance samples are zero and no timing ranking is produced. Hardware GPU
qualification, three-SDK fresh Linux performance comparison and P3-LBEN-01 remain
pending. The old independent CPU hash constants are reference data, not reused
timing samples. Application API request is not intercepted.
