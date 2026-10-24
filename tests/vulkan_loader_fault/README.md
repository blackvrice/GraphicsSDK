# Real Vulkan loader creation failure contracts

Windows x64/MSVC Release, independent `GraphicsSDK::Core` consumer and own explicit
`VK_LAYER_GFX_loader_fault`. No product code or public test switch is added. The
controller preloads/pins the absolute expected DLL before SDK initialization; a
native snapshot proves the loader used that same module. Missing manifests and
wrong DLL/GPU/CLI profiles fail without producing accepted result JSON.

The layer follows Khronos loader negotiation v2 and instance/device link chains.
It forwards successful creation unchanged and tracks instance, logical device,
debug messenger and Win32 surface creation/destruction. One-shot host/device OOM
results are synthetic: no real allocation exhaustion, device loss or TDR is induced.
Dispatchable failed outputs are null. Messenger/surface failures are tested with
both null and a deliberate non-null poison; unknown destroy calls are blocked
before lower validation/driver code. Handles are not wrapped on successful paths.

Each process runs three cycles while holding a live guard device and known buffer
data. Failed creation must retain the same live native owners and public resource
statistics. Full buffer readback checks prior contents without rewriting them;
successful retry devices also perform GPU copies. Surface retry acquires, clears,
copies and presents a hidden real 321x197 Win32 window; every pixel and readback
padding byte is checked. All retry owners are destroyed explicitly and final
native created/destroyed totals must match with zero live objects. These four
object counters do not measure buffer-memory allocations or physical reclamation.

Initialization runs on both exact hardware adapters, validation OFF/ON. Messenger
creation requires validation ON. Surface profiles run only where the product
advertises swapchain capability (NVIDIA in this recording); Intel's unsupported
surface matrix is recorded, never counted as a pass. The same test links the old
installed SDK for guarded messenger/surface regression controls.

`PlanTests.cpp` checks one-shot arming, wrong-call preservation, invalid profiles
and dispatchable poison rejection with 216 CPU assertions, no driver loading.
`tools/run_vulkan_loader_fault.py` runs sequential child processes in clean Vulkan
environments. `tools/report_vulkan_loader_fault.py` rejects incomplete matrices,
counter/identity drift, extra diagnostics and changes to raw commands/results.
The report and mutation tests keep this profile separate from performance,
long-memory acceptance and the deferred historical Intel crash investigation.

[Report](../../docs/VULKAN_LOADER_FAULT_REPORT.md) contains exact commands and evidence.
