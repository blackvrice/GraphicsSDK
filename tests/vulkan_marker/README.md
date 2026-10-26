# Actual Vulkan debug-label observation

This own diagnostic explicit layer forwards `vkCmdInsertDebugUtilsLabelEXT` and
records its command handle, exact UTF-8 label, zero color and forward count.
Both instance and device procedure lookups are intercepted: debug-utils is an
instance extension even though the label command receives a command buffer.
The controller pins the explicit absolute layer library and reads that module's
counters. Unloaded/unobserved layers, duplicated extensions, missing forwarding,
retained instances/devices or label truncation fail. The bounded observer holds
32 labels, each at most 255 bytes; overflow fails rather than silently truncating.

`--deny-proc` makes both observer lookups return null before SDK initialization.
It verifies the SDK's Unsupported path without inducing driver failure, device
loss, changing registration or installing a layer globally. This is synthetic
function unavailability, not proof of a hardware lacking the extension.

```powershell
cmake --preset windows -B out/marker-sdk -DGFX_ENABLE_VULKAN=ON -DGFX_VULKAN_HEADERS=D:/source/GraphicsSDK/.deps/Vulkan-Headers
cmake --build out/marker-sdk --config Release --parallel 1
cmake --install out/marker-sdk --config Release --prefix out/marker-install
cmake -S tests/vulkan_marker -B out/marker-observer -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=D:/source/GraphicsSDK/out/marker-install
cmake --build out/marker-observer --config Release --parallel 1
python tools/run_vulkan_markers.py --binary out/marker-observer/Release/gfx_marker_observe.exe --layer out/marker-observer/Release/VkLayer_gfx_marker.dll --manifest-dir out/marker-observer/Release --sdk-library out/marker-install/lib/GraphicsSDKCore.lib --output out/marker-evidence-new
```

Add `--validation-dir <reviewed-Khronos-manifest-directory>` to execute two further
core/sync validation cases. The wrapper changes only its child's Vulkan environment,
disables implicit layers and requires fresh output. Linux uses the same project,
an installed Vulkan-only SDK, `.so` observer and optional exact `--icd` manifest;
`--require-software` prevents hardware execution being relabelled software coverage.
The workflow demonstrates the single-config Ninja paths. No external binaries
are redistributed by this diagnostic's source or evidence records.
