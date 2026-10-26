# Vulkan generation recovery controls

This own process-local explicit layer returns synthetic `VK_ERROR_DEVICE_LOST`
from one QueueSubmit, GetFenceStatus or CreateFence call. It drains the actual
device before returning the synthetic loss, so real pending resources are safe
to retire. It does not crash/reset a driver or physically lose a device.

Each case rejects old operations without observed native work, forces two failed
recreations (device OOM, then device lost), retries successfully and rebuilds real
GPU buffer/image/pipeline resources from retained CPU inputs. Full pixel, padding
and buffer references are compared. Old/fresh handles and fence generations stay
separate. The automatic-selection case reverses actual physical-device enumeration
on multi-adapter hosts to verify exact UUID retention. Native creation/destruction
counters verify retirement ordering and complete cleanup.

On supported Windows devices the test also keeps the old swapchain/backbuffer
wrappers alive while recreating the same hidden window's swapchain, then verifies
the new rendered image before presenting. This is not human monitor acceptance.
Linux runs offscreen; software ICD success is not physical GPU acceptance.

Build an installed Vulkan SDK, then configure this directory with its prefix:

```powershell
cmake -S tests/vulkan_recovery -B out/recovery-observer -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH=D:/source/GraphicsSDK/out/recovery-install
cmake --build out/recovery-observer --config Release --parallel 1
python tools/run_vulkan_recovery.py --binary out/recovery-observer/Release/gfx_recovery_observe.exe --layer out/recovery-observer/Release/VkLayer_gfx_recovery.dll --manifest-dir out/recovery-observer/Release --sdk-library out/recovery-install/lib/GraphicsSDKCore.lib --output out/recovery-evidence-new
```

`--validation-dir` adds required core/sync validation cases with a reviewed VVL
directory. The runner uses a fresh empty layer-settings directory and freezes
own source/binary/library plus selected validation runtime hashes. Child-only
environment variables activate the layer; no global layer registration is written.
`DeviceDesc::enableDeviceLossTesting` and `removeForTesting()` remain unsupported
for Vulkan; this test layer does not implement public loss injection (P3-CAP-03).
