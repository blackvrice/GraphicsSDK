# Linux LLGL software probe preparation

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

This is native CPU/software preparation only. It emits no timing samples or
performance statistics, and does not accept a physical GPU. Its minimal buffer
smoke is not the upload/draw/PSO/allocation workload suite. The approved image
barrier patch is compiled exactly but its full Linux workload behavior remains
to be tested. Core callback delivery does not demonstrate every sync-validation
error control or GPU-assisted validation. Application instance API request is not
intercepted; queried device/loader API support is not a measured request value.
