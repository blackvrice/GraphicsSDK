# Linux LLGL resource lifecycle correctness

Native Linux consumer of the approved `llgl-fixed-pin-transfer-src-v1` shared
libraries. Preserves frozen Windows allocation/copy/public-release and validation
control bodies, original payload generator and native resource shape/uniqueness
checks. Uses the existing pass-through observer for instance/device lifetime
and absence of draw calls; it does not count native allocations.

After a clean tracked same-revision Linux LLGL probe:

```sh
python3 tools/linux_llgl_allocation.py run --probe out/linux-llgl-evidence --output out/linux-llgl-allocation-evidence
python3 tools/linux_llgl_allocation.py check --output out/linux-llgl-allocation-evidence --live
```

Requires Linux x86_64, GCC 14, Ninja, Python, shared system OpenSSL Crypto development
files and the probe's explicit software ICD/isolated pinned VVL. The workflow
`.github/workflows/linux-comparison-probe.yml` prepares these prerequisites.

Runs `buffer1`, `buffer1000`, `texture1`, `texture1000` in plain/required-validation
modes. Each creates distinct live public/native handles, copies changing data
for every resource through separate completed upload/download submissions,
compares full readback, then releases public handles. Verifies 8,008 buffers,
8,008 images, 109,837,728 bytes and 16,016 resource create/release pairs.
Frozen independent CPU SHA-256 constants are reference data, not reused GPU
results or timing samples.

Twenty-three serial processes include eight normal cases, two core/sync controls
and thirteen intended rejections: missing layer/UUID/kind, resource omission,
alias, release omission, wrong resource payload, stale iteration, skipped copy,
corrupt/short readback, missing observer, sync off. Invalid native control commands
are never submitted to the GPU.

Retains source/translation, two actual GNU commands/depfiles, input/license/runtime
hashes, raw process logs and reference data. Offline `check` executes no archived
code. Performance samples remain zero; physical-GPU `P3-LBEN-04`, internal native
allocation counts, allocator cache state and GPU memory reclamation remain
unverified. Adds no SDK dependency or vendor patch.
