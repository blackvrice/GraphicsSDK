# Phase 2 — adapter discovery, exact selection and recovery

2026-10-22, Windows x64 Release. The public SDK now supports fresh candidate metadata,
backend-scoped IDs and exact device selection/recovery. NVIDIA, Intel and WARP pass
native identity proof, image tests and installed consumers. Long GBV memory, manual
display/DPI and unexpected TDR/hot-unplug acceptance remain open.

## Contract

`enumerateAdapters()` reads a fresh DXGI candidate snapshot: hardware in OS high-
performance preference order, then WARP when available. `AdapterInfo` includes UTF-8
name, software flag, vendor/device IDs and dedicated-video/shared-system capacities.
Capacity is not current usage, budget, residency or peak. Enumeration creates/probes
no native devices and changes no debug/GBV settings; `createDevice` separately checks
feature level 11.0. Uncompiled backend enumeration returns Unsupported.

`AdapterId` combines backend and opaque UInt64; D3D12 uses native LUID bits. It is not
an enumeration index or a persistent serial. Refresh after reboot/topology changes.
`DeviceDesc::adapter` performs exact EnumAdapterByLuid selection. Missing IDs return
Unsupported without fallback; backend/software-kind mismatch returns InvalidArgument.
Zero is an explicit ID. Omitting the option retains previous hardware/WARP policy.
`caps().adapterId` comes from actual native GetAdapterLuid and is checked against the
selected metadata/request. Recovery retains the descriptor and ID; an unavailable
target cannot silently select another GPU. An external owner of a removed native
singleton blocks recovery with DeviceLost until released, then the same ID is retried.
No-ID recovery reapplies the prior selection policy. Consumers rebuild with matching
headers/static library because public structures changed; 0.0.1 has no frozen ABI.

Primary contracts: [preference enumeration](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_6/nf-dxgi1_6-idxgifactory6-enumadapterbygpupreference),
[LUID lookup/actual device identity](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_4/nf-dxgi1_4-idxgifactory4-enumadapterbyluid),
[device creation/singleton semantics](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-d3d12createdevice).

The installed window example adds `--list-adapters` (no window) and `--adapter ID`.
Hardware uses `--mode gpu`/`validation`, WARP uses `--mode warp`. Each recovery checks
the requested ID. [API details](PUBLIC_API.md#어댑터-조회와-명시적-선택) and
[example instructions](../examples/presentation/README.md) document selection.

## Actual validation

Host: Intel Core i7-14700K, 68,473,667,584 physical RAM bytes, Windows build 26220,
MSVC 19.44.35228, Windows SDK 10.0.26100.0, Release `/O2 /MD`. NVIDIA GTX 1660 SUPER
driver 32.0.16.2101 and Intel UHD 770 driver 32.0.101.6129 are recorded separately
from WARP. Final snapshot IDs are 96576 / 106703 / 106596 respectively, as session
observations only. DXGI shared-system capacity is preserved exactly, without inferring
physical RAM or an allocation from it.

[AdapterTests.cpp](../tests/AdapterTests.cpp) selects all three in each of two fresh
processes (validation off / full GBV). Public metadata matches independent DXGI
descriptors. Each renders a complete 31×17 image, independently obtains the requested
adapter's native D3D12 singleton and removes it; the SDK must observe the identical
loss reason. This proves actual selection beyond echoing a requested ID. Recovery
must first fail while the independent native owner remains alive, then recreate the
same ID and render a different reference after owner release. All **12 full images /
6 native recoveries** pass; full-GBV devices report zero errors. CPU/native negatives
cover cross-backend IDs, unavailable backends, software-kind mismatch and missing IDs.
Zero counts are healthy-device snapshots before removal/after rebuilding; the public
counter returns DeviceLost for a lost generation, whose count is not reported as zero.

The std-only installed `adapter_consumer` independently passes another **12 full
images / 6 recoveries** across three adapters in ordinary and full-GBV processes.
Existing install/render/recovery consumers also pass after rebuild.

The [explicit window campaign](../benchmarks/reports/validation/2026-10-22-phase2-adapters-final/explicit-campaign/summary.json)
uses NVIDIA GBV, Intel GBV and WARP without validation: **147 full images / 22,464,513
RGBA pixels**, 27 minimize/restore cycles and six controlled recoveries. Initial,
recovered and final IDs match. Both hardware GBV error counts are zero; WARP count
is unavailable/null. Eight real CLI refusal controls reject missing/low IDs, wrong
kind, malformed/overflow/duplicate IDs and mixed list commands without a successful
frame. Ten new Python tests reject incomplete/stale metadata, wrong IDs and lost
recovery/validation evidence, including confusing matching names with identity.

The six default [presentation cases](../benchmarks/reports/validation/2026-10-22-phase2-adapters-final/presentation-campaign/summary.json)
pass another 294 images / 44,929,026 pixels, pacing caps and owned-window wake/close.
Together the window campaigns verify **441 images / 67,393,539 pixels**. Registered
presentation CTest is a separate repeat, excluded from those totals.

## Build, audit and preservation

Fresh checks pass: core **25/25**, D3D12-OFF **5/5**, common benchmark correctness
**72/72**, presentation **7/7**, Python **124/124** and installed consumers. Two serial
CL.exe attempts end with access violation `-1073741819`: initial core code generation
and unchanged BindingTests.cpp in D3D12-OFF. Original failures remain intact; unchanged-
source retries succeed. Compiler failure cause remains unresolved and a reliable
clean build on every attempt is not claimed. Optimizations/validation and system/
driver settings are unchanged.

New package: `out/GraphicsSDK-0.0.1-dev-windows-x64-adapters.zip`, with matching current
headers/static library/CMake exports/MIT notices. Source fingerprint:
`85190ccf45a1c6c57473c25c8bef5c69e5387a5631a742d5561aef8b0e659970`.
Fresh SDK/common/adapters/stress inventories and package checks replace reuse of old
SDK acceptance. The adapter profile explicitly covers the native test, installed
consumer and both presentation executables. DXGI/D3D12 are existing Windows system
prerequisites; no external package or runtime redistribution is introduced.
Compiler-read input/component counts are SDK 19/1, common 977/22, adapters 37/1 and
stress tools 25/1, with actual linker inputs and adjacent notices retained in
[the new inventories](DEPENDENCY_LICENSES.md).

[Final logs/commands](../benchmarks/reports/validation/2026-10-22-phase2-adapters-final/),
[first build failure](../benchmarks/reports/validation/2026-10-22-phase2-adapters/) and
[summary/frozen-source evidence](../benchmarks/reports/2026-10-22-phase2-adapters/)
are retained separately. Previous six SDK ZIPs and all historical raw/source evidence
are hash-preserved. Older timing/stress results keep their original fingerprints.
No new performance rank, long memory plateau, physical unplug/unexpected TDR,
compositor/human display, physical DPI, Linux/Vulkan or native Metal acceptance is
inferred from these automated tests.
