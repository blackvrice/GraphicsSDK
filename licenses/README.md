# Reviewed dependency inventories

These inventories apply to the specific Windows x64 Release builds and artifact hashes
recorded in each JSON file. They are a source/notice inventory, not a legal redistribution
approval or a claim about every platform, every static-library member or future releases.

* [SDK inventory](generated/windows-sdk-release/inventory.json) and
  [SDK notices](generated/windows-sdk-release/THIRD_PARTY_NOTICES.txt).
* [Benchmark inventory](generated/windows-benchmarks-release/inventory.json) and
  [benchmark notices](generated/windows-benchmarks-release/THIRD_PARTY_NOTICES.txt).
* [Review policy](policy.json), [D3DX12 origin proof](provenance/d3dx12-origin.json),
  [Microsoft MIT text](provenance/d3dx12-upstream-LICENSE.txt),
  [Apache-2.0 text](texts/Apache-2.0.txt).
* [Diligent benchmark inventory](generated/windows-benchmarks-diligent-release/inventory.json),
  [notices](generated/windows-benchmarks-diligent-release/THIRD_PARTY_NOTICES.txt) and
  [additional review policy](diligent-policy.json). Use this profile for the optional Diligent build;
  the earlier benchmark snapshot records its earlier artifact hashes.
* [bgfx benchmark inventory](generated/windows-benchmarks-bgfx-final-release/inventory.json),
  [notices](generated/windows-benchmarks-bgfx-final-release/THIRD_PARTY_NOTICES.txt) and
  [additional policy](bgfx-policy.json) cover the optional `-Bgfx` profile at its recorded pins.
* [The Forge benchmark inventory](generated/windows-benchmarks-forge-release/inventory.json),
  [notices](generated/windows-benchmarks-forge-release/THIRD_PARTY_NOTICES.txt),
  [additional policy](forge-policy.json) and [terms provenance](provenance/forge-terms-origin.json)
  cover `-Forge`: MSVC compatibility config, no legacy NVAPI, actual AGS/DXC binaries and GPU data.
  PIX headers are conservatively inventoried with incomplete historical primary terms; no PIX
  library/DLL is linked/copied. Their redistribution requires separate review. This profile
  is an inventory of local build inputs, not clearance to redistribute Forge binaries/headers.
* Timestamp expansion inventories: [Forge](generated/windows-benchmarks-timestamps-forge-release/inventory.json)
  and [Diligent](generated/windows-benchmarks-timestamps-diligent-release/inventory.json), with
  their adjacent notices. These record the newer adapter binaries under the same pinned
  dependency policies. Historical inventories above remain unchanged.

Before redistributing the reviewed benchmark binaries, retain the full benchmark notice
file with them. BSD license and non-endorsement terms, MIT notices, Apache terms and upstream
source modification notices remain applicable as specified by each component. The explicit
HIDAPI choice is BSD-3-Clause; GPL is an offered alternative, not the selected license.
The standalone SDK package contains own SDK code only. Windows SDK/CRT/driver/loaders are
unbundled prerequisites; copying their DLLs would require a separate terms review.

## Reproduce offline

Build with the existing Windows presets/bootstrap first, then use a new output directory:

```powershell
python tools/audit_licenses.py --profile sdk --package out/GraphicsSDK-0.0.1-dev-windows-x64.zip --output out/sdk-license-audit
python tools/audit_licenses.py --profile benchmarks --output out/benchmark-license-audit
python tools/audit_licenses.py --profile benchmarks --output out/benchmark-license-audit --check
python tools/audit_licenses.py --profile benchmarks-diligent --output out/diligent-license-audit
python tools/audit_licenses.py --profile benchmarks-diligent --output out/diligent-license-audit --check
python tools/audit_licenses.py --profile benchmarks-bgfx --output out/bgfx-license-audit
python tools/audit_licenses.py --profile benchmarks-bgfx --output out/bgfx-license-audit --check
python tools/audit_licenses.py --profile benchmarks-forge --output out/forge-license-audit
python tools/audit_licenses.py --profile benchmarks-forge --output out/forge-license-audit --check
```

The optional `--package` checks the existing ZIP against the actual core library, all public
headers, installed CMake exports and own MIT license. Unreviewed files, Windows name aliases,
unsafe archive paths and altered/missing required contents fail verification. No ZIP is
extracted and no existing package or snapshot is overwritten.

The benchmark profile reads MSBuild compiler-read logs from core, adapters, LLGL,
LLGL_DXCommon, LLGL_Direct3D12 and SDL. This conservatively includes headers and translation
units whose bodies may be preprocessed out or unused after linking. Generated PCH/config
inputs and system header counts are separate. It does not infer runtime loader usage from
header inclusion. Linker-read logs verify the actual static/import libraries, including inputs
introduced through linker directives. Unreviewed libraries fail even if no foreign header was
read by the compiler. System link inputs and artifact hashes are also recorded.

Changed SDK pins require an explicit policy review. Tracked competitor modifications,
untracked compiler-read sources, unknown local/external inputs, stale core build fingerprints
or stale LLGL DLL runtime copies fail the audit. These checks do not constitute a general
license detector for arbitrary newly copied code. Root-license fallback is scoped to the
exact reviewed competitor commits and the reviewed exception rules. The additional Diligent
profile records the public Core, configured xxHash/Abseil, actual NVAPI linker library, NCSA DXC
headers and the AMD checksum derived from the RSA Data Security, Inc. MD5 Message Digest
Algorithm. Retain its RSA attribution and derivative-work terms with the MIT AMD notice.

`--check` detects drift in both the inventory and collected notices, without rewriting either.
Snapshots contain build-specific evidence; another machine/build should produce a new
snapshot. The Windows CI job creates a fresh SDK inventory after installation. Remote CI
execution has not been verified here.

The Windows CPU/memory observer has a separate build snapshot at
[windows-benchmarks-telemetry-release](generated/windows-benchmarks-telemetry-release/inventory.json)
with adjacent notices (968 compiler-read project inputs, 22 components). It covers the common
LLGL/SDL3/GraphicsSDK benchmark binary. PSAPI/DXGI remain system prerequisites; optional
competitor rebuilds and redistribution clearance are outside this snapshot. Historical inventories
and the SDK package remain unchanged.

The subsequent public LLGL buffer-to-texture upload path has its own
[windows-benchmarks-llgl-upload-release](generated/windows-benchmarks-llgl-upload-release/inventory.json)
snapshot and adjacent notices (969 compiler-read project inputs, 22 components). It retains the
same competitor pins, system prerequisites and redistribution limitations; old snapshots remain historical.

The Phase 1 advanced LLGL baseline has the separate `benchmarks-phase1` audit profile and
[final inventory](generated/windows-benchmarks-phase1-final-release/inventory.json) with adjacent
notices (970 project inputs, 22 components). This conservative closure includes both common and
extension targets. The first exploratory inventory remains historical. Neither snapshot is a
redistribution clearance or evidence that all Phase 1 acceptance gates passed.

The graphics binding expansion has fresh
[SDK](generated/windows-sdk-bindings-release/inventory.json) and
[common benchmark](generated/windows-benchmarks-bindings-release/inventory.json) inventories,
with adjacent notices: 17 own compiler-read inputs / 1 component and 975 inputs / 22 components.
The new binding SDK ZIP and both snapshots passed `--check`. Original packages and prior
inventories remain historical; no new external dependency or competitor modification was added.

The Depth/MRT extension has separate
[SDK](generated/windows-sdk-depth-mrt-release/inventory.json) and
[common benchmark](generated/windows-benchmarks-depth-mrt-release/inventory.json) inventories,
with adjacent notices: 19 own compiler-read inputs / 1 component and 977 inputs / 22 components.
The fresh SDK ZIP and both snapshots passed `--check`. All earlier packages, source archives,
measurements and inventories remain preserved; no external dependency or competitor code changed.
