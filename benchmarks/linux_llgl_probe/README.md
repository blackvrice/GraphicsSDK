# Linux LLGL minimal probe preparation

Build pinned LLGL with the existing user-approved
`llgl-fixed-pin-transfer-src-v1` ten-line overlay. The original `.deps/LLGL`
checkout stays clean. No other renderer, SPIR-V reflection, GPU-assisted
validation, debug wrapper, XR, example or test target is selected. LLGL core and
the Vulkan backend are shared libraries; the GNU Release configuration uses
`-O2 -DNDEBUG`, no LTO, no unity build and no CPU-specific tuning.

Use a clean Linux checkout, GCC 14, Ninja, installed Vulkan loader/software ICD,
X11/Xrandr development headers, pinned Vulkan-Headers/LLGL checkouts, and the
same-revision BEN-07 software qualification with the pinned VVL runtime:

```sh
python3 tools/linux_llgl_probe.py run --qualification out/linux-comparison-evidence --output out/linux-llgl-new
python3 tools/linux_llgl_probe.py check --output out/linux-llgl-new --live
```

Four normal native processes run identity and a minimal buffer smoke in plain
and required-validation modes. Each smoke writes/copies/fence-waits/reads a
changing 4096-byte buffer three times and checks every byte. The LLGL renderer's
actual physical device, logical device and graphics queue are queried; UUID,
driver and environment must match the prior SDK/SDL qualification. Linux LUID is
null. The actual core/backend/loader/ICD and validation shared objects are checked.

A host-only invalid `VkPhysicalDeviceProperties2.sType` control must deliver its
specific VUID through the actual debug callback. It records no GPU command.
Missing-layer, wrong-UUID and wrong-kind controls must fail with their intended
errors and no success JSON. Eight native probe/control processes are retained.

The runner retains verbose compiler commands, Ninja commands, raw GNU depfiles,
input/license classifications, immutable hashes, source ZIP, per-process raw
output, environments, PIDs and serialized intervals. Unknown compiler/runtime
inputs or license branches fail. External shared libraries and system/driver
files are never included in the saved evidence payload.

The default remains native CPU/software preparation. It emits no timing samples
or performance statistics. Its minimal buffer smoke is separate from the
upload/draw/PSO/allocation workload suite. Core callback delivery does not demonstrate every sync-validation
error control or GPU-assisted validation. Application instance API request is not
intercepted; queried device/loader API support is not a measured request value.

## Physical profile

A clean Linux GPU host must first pass `linux_comparison.py run --mode physical`
with an explicit installed ICD, exact UUID, corroborating PCI vendor/device and
bound driver, and the pinned VVL runtime. Then use the same revision:

```sh
python3 tools/linux_llgl_probe.py run --mode physical --qualification out/linux-comparison-physical --output out/linux-llgl-physical
python3 tools/linux_llgl_probe.py check --output out/linux-llgl-physical --live
```

The separate `GFX_LLGL_PHYSICAL_PROFILE=ON` build binds `physical` into its
fingerprint, rejects software mode, and requires native integrated/discrete
device type before loading LLGL. The actual LLGL native device/driver UUID and
all qualified properties must match. LLGL's default adapter is not silently
replaced or retried: a mismatch fails. This matters for multi-GPU hosts.
Eight normal/control processes use the same minimal buffer and callback checks.
Only successful physical execution sets `physical_probe_qualified=true`;
`physical_GPU_accepted`, `P3_BEN_08_completed`, workload and performance acceptance
remain false. This is a minimal probe, not the complete comparison suite.

Hosted software CI additionally builds that same physical profile in a fresh
build tree and proves four intended rejections:

```sh
python3 tools/linux_llgl_probe.py guard --mode physical --qualification out/linux-comparison-evidence --output out/linux-llgl-physical-guard
python3 tools/linux_llgl_probe.py check --output out/linux-llgl-physical-guard --live
```

Native CPU device, wrong UUID, software-mode fallback and missing validation
layer must fail with specific errors and no success JSON. These checks stop
before LLGL renderer creation, produce zero readback/performance samples, and
never set physical qualification. The physical manual workflow includes the
LLGL checkout/build/probe; the four workload consumers remain software-only.
