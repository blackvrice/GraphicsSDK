# Phase 2: GBV allocation path and Intel crash investigation

2026-10-23. Investigated commit `639efa9a16e5293865a6dee3f7e32de2d5ab42c4`.
Production SDK source fingerprint:
`50fe75fea32a5444531868da9f1a225a05573d78ba12c52bb622a1ea80d35edf`.

The reproducible texture-churn growth includes allocations made by Microsoft's
GPU-based validation (GBV) resource-information controller and its internal command
buffers. It reproduces without GraphicsSDK on both hardware adapters. These
allocations remain while the device lives, including between spaced batches, and
much of the private commit is reclaimed after device destruction with a longer wait.
This establishes an allocation path and retention condition, not an unbounded leak
or a particular defect in Microsoft's unavailable source. The original memory gate
still fails. The earlier Intel access violation remains unresolved: five fresh long
runs completed without reproducing it. No production SDK fix is claimed.

## Environment and frozen scope

Windows build 26220; i7-14700K; BIOS F10a; registry microcode revision bytes decode to
`0x12F`. NVIDIA GTX 1660 SUPER driver `32.0.16.2101`; Intel UHD 770 driver
`32.0.101.6129`. The tested inbox D3D12Core/SDKLayers file versions are
`10.0.26100.9739`; exact hashes are recorded with the symbol identities. BIOS settings,
CPU/RAM stability and newer driver/runtime behavior were not validated or changed.

Experiments ran serially, with serial MSVC builds. The machine rebooted between the
initial three long runs/allocation-origin controls and later controls. Intel adapter
ID changed from `106766` to `107499`; NVIDIA changed from `96650` to `97383`.
The initial post-reboot attempt correctly rejected the stale ID before starting a
workload; that startup rejection is preserved separately and is not a crash.
Cross-run timing/performance comparisons are not claimed.

SDK sources, the core library, original memory policy, earlier reports/inventories
and all eleven SDK packages are preserved. No SDK package is regenerated. The
existing tests from the preceding unchanged-SDK snapshot are historical evidence;
the current work checks the investigation controls and rechecks the dependency
inventory/package instead of claiming a new complete regression run.

## SDK-free causal controls

Each native process creates/releases 1,536 textures: two batches of 256 iterations,
each creating a 64x64 RGBA8 texture, RGBA8 render target and D32 depth texture.
`ID3DDestructionNotifier` callbacks confirm all 1,536 resource destructions and one
device destruction in each of twelve main controls; validation ERROR/CORRUPTION
counts are zero. Their footer's `passed` is a lifetime/functional result, not memory
acceptance. Private bytes and native DXGI process local/nonlocal usage are separate
measurements. Mapped/image/private VirtualQuery regions are also recorded.

The six allocation-origin controls use `HeapOptimizeResources` after release.
Private commit (bytes), sampled before texture creation and after the second batch:

| Adapter | Validation | Baseline | Second batch released |
|---|---|---:|---:|
| NVIDIA | CPU debug | 77,627,392 | 77,045,760 |
| NVIDIA | Full GBV | 137,211,904 | 488,411,136 |
| NVIDIA | GBV, state tracking disabled | 114,491,392 | 112,062,464 |
| Intel | CPU debug | 35,237,888 | 39,428,096 |
| Intel | Full GBV | 68,345,856 | 219,484,160 |
| Intel | GBV, state tracking disabled | 59,490,304 | 63,692,800 |

Successful heap optimization removes little of the full-GBV increase. Four further
controls omit heap optimization: full GBV, with/without an empty application command
list submission and real fence drain after each batch, followed by 20 seconds of
idle with the device alive. Large private commit remains in all four: NVIDIA
494,411,776 / 467,599,360 and Intel 219,402,240 / 221,929,472 bytes at the final idle
sample. Draining the application queue does not establish that GBV's private queue
and allocators have been reset or retired.

Two additional normal native processes omit the debugger, ETW and heap optimization
and wait 20 seconds **between** batches. End-of-wait private commit grows from
328,519,680 to 491,929,600 bytes on NVIDIA and from 143,175,680 to 219,828,224 bytes on
Intel. Thus this finite accumulation is not explained solely by immediate batch
overlap. It does not prove that growth would continue indefinitely.

After actual device destruction DXGI local/nonlocal usage becomes zero. Private
commit is still high after one second but drops substantially by 15 seconds. For
the spaced controls, NVIDIA goes from 398,688,256 to 10,563,584 bytes and Intel from
138,485,760 to 5,902,336 bytes. Earlier 200ms/short post-teardown observations remain
valid for their original interval; they cannot establish permanent CPU retention.

## Allocation stack evidence

Six own-PID heap traces show that ordinary heap allocations alone do not explain
the growth. Four additional private system-logger ETW sessions collect virtual
allocation/free stacks for full GBV and disabled state tracking on both adapters.
Each session starts, enables stacks/flags and stops successfully; controller and
xperf exports report zero event/buffer loss. Only the selected child's virtual
events and module-relative frames are redistributed. Whole ETLs/CSVs remain local
because they contain system metadata. No global NT Kernel Logger/WPR session,
registry, group or persistent policy was changed.

Official Microsoft public PDBs were fetched for the exact DLL CodeView GUID+age.
Offline DbgHelp confirms PDB symbol type, matching GUID/age and no PDB mismatch.
The PDBs are local-only. A recurring resolved call path is:

```text
NDebug::CDevice::CreateCommittedResource
  -> CDevice::CreateCommittedResource_Worker / CreateTestResourceAndHeap_Worker
  -> NDebug::CResource::FinalConstruct
  -> NDebug::CGBVDataController::AddResourceInfo
  -> NDebug::CGraphicsCommandList::ResetImpl / Reset / CopyBufferRegion
  -> [NVIDIA: CCommandList<...>::VersionedResetCommandList]
  -> NDXGI::CDevice::AllocateCB_0022
  -> CallAndLogImpl<...D3DKMT_CREATEALLOCATION...>
```

This is a readable combination of observed stacks; exact ordered frames, addresses,
displacements and symbol sizes/tags are retained in JSON. `AllocateCB_0022` resolves
within its 1,393-byte function. `AddResourceInfo` is a public symbol with no reported
function size, so its displacement is retained rather than asserting a source line.

| Adapter/mode | Commit-call byte sum with AddResourceInfo | Same stack also includes AllocateCB |
|---|---:|---:|
| NVIDIA, full GBV | 321,318,912 | 320,028,672 |
| Intel, full GBV | 149,860,352 | 111,587,328 |
| NVIDIA/Intel, tracking disabled | 0 | 0 |

These are sums of traced commit **calls**, not net unique committed pages, live
allocation totals or private-byte peaks; repeated/recommitted ranges can overlap.
The full raw filtered events permit independent recalculation. No missing allocator
reset or internal Microsoft source-code fix is inferred from a public symbol name.
Native live-object reports also identify an internal GBV resource-info buffer,
sparse buffer, GBV command queue and internal command allocators.

[Microsoft's GBV documentation](https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-d3d12-debug-layer-gpu-based-validation)
describes validation's injected GPU work. Disabling
[GBV state tracking](https://learn.microsoft.com/en-us/windows/win32/api/d3d12sdklayers/ne-d3d12sdklayers-d3d12_gpu_based_validation_flags)
reduces validation coverage; it is a diagnostic ablation, not a replacement for the
full-GBV gate. Fixed-resource reuse previously passed its narrower profile; neither
reuse nor periodic device destruction silently changes the original churn contract.

## Intel access violation: outcome and limits

The historical normal Intel run failed after 112.875 seconds, last recording 4,792
verified frames. Application Error 1000 reports `c0000005`, unknown module and zero
offset. No original fault context/stack/dump is available. The unknown-module/zero
offset receipt does not identify a driver, null call, SDK defect or hardware fault.

| Fresh run | Measured seconds + warmup | Frames | Full images | Original numeric analyzer |
|---|---:|---:|---:|---|
| Normal Release under debugger 1 | 300 + 30 | 7,408 | 22,224 | Rejected: checkpoint gap |
| Normal Release under debugger 2 | 300 + 30 | 10,360 | 31,080 | Rejected: private bytes |
| Symbol-bearing Release under debugger | 300 + 30 | 11,320 | 33,960 | Passed individual numeric analysis |
| Normal Release, no debugger | 300 + 30 | 10,456 | 31,368 | Rejected: private bytes |
| Normal Release under debugger, extended | 600 + 30 | 14,392 | 43,176 | Rejected: private bytes |

All five native processes exited zero, with zero validation errors and complete
image/buffer references: 53,936 frames, 161,808 full images and 14,138,998,784 verified
buffer bytes. The four debugger captures have no second-chance exceptions or thread
snapshots. The extended diagnostic uses a 1,024MiB emergency growth guard; the
original policy file is unchanged. The unmonitored runner exits one for its failed
memory gate even though its native workload exits zero. The individually passing
symbol-bearing run is retained honestly; instrumentation and run variability prevent
it from accepting the failed original normal campaign.

Other targeted Application Error records show four independent MSVC `CL.exe`
access violations in `c2.dll`/`ucrtbase.dll`. They justify keeping machine/toolchain
instability as an alternative hypothesis, but do not prove a common cause. A
read-only 72-hour WHEA query found no matching events; absence of WHEA events does
not validate CPU/RAM stability. No BIOS/driver/settings changes or destructive
hardware stress were performed.

The next discriminating Intel evidence is a recurrence with fault context/minidump,
or a controlled environment comparison (same frozen workload, then one changed
driver/validated host at a time). Existing capture tooling will preserve up to 16
live thread contexts and bounded stacks on recurrence. The GBV retention path above
is established independently; no causal connection to the historical AV is proven.

## Reproduction, preservation and audit

[Machine-readable summary and evidence](../benchmarks/reports/2026-10-23-phase2-fault-investigation/summary.json)
link the current source archive, raw hashes, prior evidence and preserved SDK ZIPs.
[Verified observations](../benchmarks/reports/validation/2026-10-22-phase2-fault-investigation/verified-observations.json)
retain all twelve controls and five long-run receipts. Trace failures are preserved:
the first heap export omitted a required symbol option; the first xperf attempt at
a private virtual session rejected invalid flags before starting. Successful v2
sessions are identified separately. Initial WHEA/registry query limitations likewise
remain separate from their corrected explicit read-only query receipts.

The source archive preserves SDK/root sources plus four temporary native projects
and exact orchestration/analysis scripts. Restore members to their recorded paths
relative to the repository root, including `out/`, to preserve relative includes.
Build each project separately with `cmake -S out/<project> -B out/<project>/build`
and `cmake --build out/<project>/build --config Release --parallel 1`.
Run only one GPU process at a time, freshly enumerate adapters and use an empty
output folder. Scripts contain frozen paths/IDs and exclusive-create guards; they
are exact investigation receipts, not a portable one-command acceptance suite.
Elevated trace collection requires the Windows performance tools and access to
private ETW sessions. Offline evidence validation does not require elevation.

Fresh compiler/linker-read evidence covers the four native helper projects: five
unique own compiler inputs, Windows SDK/MSVC and two explicit OS locale/timezone
inputs; no competitor dependency. This is a conservative build-input inventory,
not legal redistribution approval or symbol reachability. Only own sources and
derived diagnosis are redistributed. Runtime/compiler/driver binaries, PDBs,
minidumps, whole ETLs and unfiltered CSVs remain local. Microsoft public symbols
are subject to the
[symbol server terms](https://learn.microsoft.com/en-us/legal/windows-sdk/microsoft-symbol-server-license-terms).
Raw Application Error XML is retained locally; published fields omit computer/SID.

The original long-memory acceptance and Intel AV cause remain open in the roadmap.
