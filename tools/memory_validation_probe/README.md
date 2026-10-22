# Windows D3D12 lifetime probes

These tools diagnose the open Phase 2 GBV reclamation item. They do not change
SDK validation, act as an allocator workaround or accept the original long stress gate.
Use fresh output paths and run GPU tests serially.

## Exact-adapter resource-churn campaign

`tools/MemoryStress.cpp` now binds its DXGI observer to the public device's opaque
adapter ID using `EnumAdapterByLuid`; adapter names are descriptive metadata.
Schema 2 records the requested, SDK, observer and final device IDs. A missing ID or
software/hardware mismatch fails without falling back. Refresh IDs after reboot or
topology changes using the installed presentation example's `--list-adapters`.

```powershell
out/presentation/Release/gfx_presentation.exe --list-adapters
python tools/run_memory_stress.py --mode gpu --adapter <fresh-hardware-id> --diagnostic-seconds 3 --output out/memory-id-smoke-new
python tools/run_memory_stress.py --mode validation --adapter <fresh-hardware-id> --output out/memory-gbv-new
python tools/run_memory_stress.py --output out/memory-three-modes-new
```

An explicit adapter requires one mode; WARP uses its own ID with `--mode warp`.
Without `--diagnostic-seconds`, policy durations remain hardware 900 seconds,
full GBV 300 seconds and WARP 300 seconds, each after 30 seconds of warmup.
Diagnostic runs check references/identity and always record `accepted=false`.
One mode cannot accept the overall three-mode campaign.

The native tool checks process private-commit growth after each fully verified,
drained eight-frame round, including warmup. Default growth limit is 512 MiB;
`--max-private-growth-mib` accepts 1..4096. The baseline is after device/statistics
contracts, before positive controls, shader setup and warmup. This includes runtime
and driver retention and is not a pure SDK leak metric, a hard allocation ceiling,
a GPU budget or a continuous peak measurement. A round may overshoot the limit.
An observed breach writes `guard_aborted`, a final zero-ownership checkpoint and an
`aborted/private_growth_limit` footer, then exits 2. Native errors still fail.

The runner preserves process exit codes, logs, frozen source copies and
`acceptance.json` on native rejection, guard abort, timeout or plateau failure.
It runs remaining modes after a per-mode failure; input mutation or timeout stops
the campaign. A guard abort or incomplete run can never pass the unchanged long
memory policy. A failed runner exits 1; diagnostic completion exits 0 while memory
acceptance stays false. Historical schema 1 observations remain analyzable but do
not establish a newly requested exact ID.

For an unexplained native exit without a memory footer, use the separate
[own-child crash collector](../crash_capture/README.md). It preserves second-chance
exception/context/module/stack receipts and an optional local-only minidump. Its
debugger-instrumented workload remains diagnostic and cannot accept this memory gate.

`gbv_lifetime_probe` is a native reproduction: no GraphicsSDK header or library.
The dedicated CMake project needs `Lifetime.cpp`, `Runtime.cpp` and `native/CMakeLists.txt`,
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

## Runtime and adapter comparison

`gbv_runtime_probe` also has no SDK header/library dependency. It compares the global
API, inbox device factory and a pinned app-local Agility device factory. It queries
the actual device configuration, provider module and adapter LUID; it rejects a
fallback runtime or reduced GBV. Each process deliberately injects one ERROR after
overflowing native text storage, checks the unfiltered callback, and keeps that error
separate from unexpected errors. This is allocation/destruction diagnosis, without
rendering or a long-running memory acceptance claim.

Prepare the pinned runtime under a fresh local output directory. It uses Microsoft
DirectX binary license terms, separately from the MIT headers; package headers and
runtime DLLs are not added to the SDK distribution. `--package` can supply a local
copy matching the exact official package pin instead of downloading it.

```powershell
python tools/memory_validation_probe/prepare_runtime.py --output out/agility-runtime-new
cmake --build out/memory-validation-probe --config Release --parallel 1 --target gbv_runtime_probe
python tools/memory_validation_probe/runtime_compare.py --runtime out/agility-runtime-new --adapter-index 0 --iterations 512 --output out/runtime-adapter0-new
python tools/memory_validation_probe/runtime_compare.py --runtime out/agility-runtime-new --adapter-index 1 --iterations 512 --output out/runtime-adapter1-new
```

Hardware indices enumerate DXGI non-software adapters; an absent index fails rather
than selecting another GPU. WARP remains explicitly selected in each nine-process
profile. Do not assume a second hardware adapter exists on another host. The raw
output records both DXGI segments, private commit, counters and loaded module paths;
the runner checks the selected runtime pair against frozen package/system DLL hashes.
It does not use SetSDKVersion, ApplyToGlobalState, experimental features or change
Windows Developer Mode. See [the runtime comparison](../../docs/PHASE2_RUNTIME_COMPARISON_REPORT.md).
