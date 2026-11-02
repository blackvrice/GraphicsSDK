# Linux LLGL draw / instancing correctness

Builds a separate Linux consumer of the approved `llgl-fixed-pin-transfer-src-v1`
shared libraries. Frozen Windows render/control bodies and the pass-through
Vulkan draw observer remain unchanged; only platform, crypto, identity and layer
exports are translated. The SDK library gains no dependency.

After a **same-revision clean tracked checkout** has completed
`tools/linux_llgl_probe.py run`:

```sh
python3 tools/linux_llgl_draw.py run --probe out/linux-llgl-evidence --output out/linux-llgl-draw-evidence
python3 tools/linux_llgl_draw.py check --output out/linux-llgl-draw-evidence --live
```

Requires native Linux x86_64, GCC 14, Ninja, Python, shared system OpenSSL Crypto
development files and the probe's isolated pinned VVL/explicit software ICD.
The workflow `.github/workflows/linux-comparison-probe.yml` prepares these inputs.

Runs three cases in plain/required-validation modes, two core/sync positive error
controls and nine intentional rejections: 17 serial processes, 24 full changing
65x37 RGBA8 images, 230,880 bytes. Verifies native forwarded draw calls, instances,
vertices, arguments, exact device/queue and retired native objects. The selection
query creates one additional observed instance; it creates no device or draws.
Controls record diagnostics without submitting invalid commands to the GPU.

Retains raw logs, source ZIP, deterministic translation, actual two-unit GNU
commands/depfiles and input/license/runtime hashes. Offline `check` executes no
archived source or native code. Software correctness supplies **zero performance
samples** and does not complete physical-GPU `P3-LBEN-02` acceptance.

## Physical profile

The default remains software-only. The explicit `--mode physical` runner requires
a same-revision successful physical LLGL probe and qualifies every native device
and runtime binding before the unchanged workload/reference checks. See
[physical workload preparation](../../docs/PHASE3_LINUX_LLGL_PHYSICAL_WORKLOAD_REPORT.md)
for the run/guard commands, compiled mode fingerprint, validation controls and
remaining real-GPU/performance acceptance.
