# Windows D3D12 lifetime probes

These tools diagnose the open Phase 2 GBV reclamation item. They do not change
SDK validation, act as an allocator workaround or accept the original long stress gate.
Use fresh output paths and run GPU tests serially.

`gbv_lifetime_probe` is a native reproduction: no GraphicsSDK header or library.
The dedicated CMake project needs only `Lifetime.cpp` and `native/CMakeLists.txt`,
preserving that relative layout; it can be built outside this repository:

```powershell
cmake -S tools/memory_validation_probe/native -B out/gbv-lifetime-native -G "Visual Studio 17 2022" -A x64
cmake --build out/gbv-lifetime-native --config Release --parallel 1
out/gbv-lifetime-native/Release/gbv_lifetime_probe.exe hardware-gbv 8 256 out/native-new.jsonl
```

The native modes are `hardware-cpu`, `hardware-gbv`, and `warp-gbv`. Each cycle
creates a fresh native device, checks last-alias resource destruction, creates/releases
three textures per iteration, then destroys the device. Actual resource/device
destruction callbacks and loaded runtime module paths are recorded. Full-GBV modes
explicitly retain default state tracking. The CPU mode is a diagnostic comparison.
The native process stops if post-device private memory exceeds its initial value by
512MiB. Short observations are not a memory plateau gate.

`sdk_lifetime_probe` needs the existing SDK Release library. It repeats the public
MRT/depth/buffer readback workload, drains resources, drops the Device while retaining
one child buffer, then drops both aliases of that child. A native destruction observer
confirms the last generation owner releases the native device. Native access records
that receipt and does not perform the SDK workload. SDK statistics become unavailable
after dropping Device; no zero statistics are synthesized. ERROR/CORRUPTION counts
cover the active SDK generation before teardown, not callbacks after unregister.

```powershell
cmake -S tools/memory_validation_probe -B out/memory-validation-probe -G "Visual Studio 17 2022" -A x64
cmake --build out/memory-validation-probe --config Release --parallel 1 --target sdk_lifetime_probe
out/memory-validation-probe/Release/sdk_lifetime_probe.exe hardware 8 8 out/sdk-new.jsonl
out/memory-validation-probe/Release/sdk_lifetime_probe.exe warp 8 8 out/sdk-warp-new.jsonl
python tools/memory_validation_probe/lifetime.py --cycles 8 --iterations 256 --batches 8 --output out/lifetime-new
```

The serial Python runner executes all five modes. It snapshots source and hashes
executables, SDK inputs and four system runtime DLLs before execution; it verifies
those hashes afterward. It parses callback/alias/order/reference/error contracts and
emits observations with `memory_acceptance_claim=false`. Native modules loaded by
each device must match the pre-hashed System32 files. Unsupported hardware memory
queries fail; unsupported WARP queries stay explicit. SDK/core and all external
runtime binaries remain outside the source archive.

Existing `memory_validation_probe` and `run.py` keep their factor/isolation/bounded
reuse profiles. The fixed-set 300-second gate is analyzed by
`tools/analyze_bounded_gbv.py`; none of the lifetime probes can satisfy it or the
original resource-churn gate. See [the lifetime report](../../docs/PHASE2_DEVICE_LIFETIME_REPORT.md).
