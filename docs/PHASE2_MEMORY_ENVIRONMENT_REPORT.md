# Phase 2: normal-run environment comparison controls

2026-10-23. Base commit `a588fbc0abf4f1df9d645d82e9976f215e247624`.

`tools/memory_environment.py` records normal full-GBV workload runs with before/after
environment snapshots and compares two recorded cases. This implements the next
controlled-environment step from the
[Intel fault investigation](PHASE2_FAULT_INVESTIGATION_REPORT.md), while preserving
the normal executable and original memory policy. It does not install drivers or
change BIOS, registry values, policies, adapter settings or existing trace sessions.

## Recorded controls and comparison rules

The wrapper uses the existing normal `gfx_memory_stress.exe` and
`run_memory_stress.py` without a debugger. It records exact file paths/SHA-256 for
the normal executable, adapter enumerator, core library, policy, native workload,
runner, analyzer, this environment tool and the audit helper. Fresh public adapter
enumeration must match the current SDK fingerprint. Only an explicit hardware ID
is allowed. A unique PCI vendor/device/name match in CIM supplies the driver
package version/date. Multiple identical adapter models or ambiguous CIM matches
are refused. The PnP identity is hashed; raw PnP data is not published.

Read-only Windows snapshots include OS version/build and boot time, CPU topology,
BIOS version/date, microcode revision bytes, other GPU driver identities and
disk-file versions/hashes for d3d12, D3D12Core, SDKLayers, WARP and DXGI. No machine
name, SID, system UUID or hardware serial is recorded. DLL hashes describe disk
files; they do not prove which driver/runtime modules were loaded in the child.

`compare --change none` requires all recorded controls, selected driver and workload
settings to match. `--change selected-driver` requires an actual selected-driver
version/date change and rejects differences in any other recorded control. The
comparison includes binary and source hashes, runtime files, policy, duration,
warmup, interval, growth guard and instrumentation. It never relaxes the original
memory policy to make a comparison pass.

Adapter IDs are ephemeral. A different ID is allowed between cases only when a
different boot time is also recorded and stable selected-adapter controls match.
Within a run, both boot and adapter-ID changes are rejected. Observed time is not
a control. Before/after environment stability is recomputed when loading a case.
Native commands, frozen input hashes, adapter/SDK identity, references and settings
are validated against the recorded workload; case summaries cannot override them.
All recorded artifact hashes are checked, including again at comparison end.
Existing case directories/comparison outputs and paths escaping a case are refused.

This is **recorded-control agreement**, not certification that only one physical
variable changed. BIOS settings, CPU/RAM stability, temperature, power state,
background work, driver package contents beyond the recorded metadata and loaded
module identities remain unverified. Matching cases do not establish a crash cause,
statistical confidence, a leak fix, performance superiority or memory acceptance.

## Actual same-environment controls

Two serial Intel UHD 770 normal-executable runs use full GBV/state tracking,
3 measured seconds + 1 second warmup, 1-second checkpoints and the unchanged
512MiB emergency guard. The driver remains `32.0.101.6129` throughout.

| Case | Native exit | Frames | Full images | Verified buffer bytes |
|---|---:|---:|---:|---:|
| Baseline | 0 | 232 | 696 | 60,817,408 |
| Same environment | 0 | 240 | 720 | 62,914,560 |
| Total | | 472 | 1,416 | 123,731,968 |

Both complete with zero validation errors and complete native reference checks.
Before/after recorded controls match. Cross-case `--change none` succeeds;
`--change selected-driver` correctly rejects the same cases because the driver did
not change. Real overwrite and missing-adapter controls also reject without
altering existing results or creating a failed-preflight case directory.

These are short wrapper/identity/reference controls. They are not a new long soak,
a real changed-driver comparison or an Intel crash reproduction. Actual driver
changes and the historical AV cause remain outstanding. The original GBV memory
gate stays open, independent of these functional results.

An initial read-only preflight failed before starting any workload because the
default Windows PowerShell could not load `Get-FileHash`. The final implementation
uses the .NET SHA-256 API instead and reports query stderr on failure. The initial
script/tool versions and rejected attempt remain separate from the successful
`final/` campaign. No reduced validation or system policy workaround was used.

## Commands and result interpretation

From the root, freshly enumerate IDs and choose the intended hardware adapter:

```powershell
out/presentation/Release/gfx_presentation.exe --list-adapters
python tools/memory_environment.py record --adapter <fresh-id> --output out/env-baseline
# After a separately managed environment change, enumerate IDs again.
python tools/memory_environment.py record --adapter <fresh-id> --output out/env-candidate
python tools/memory_environment.py compare `
  --before out/env-baseline/case.json --after out/env-candidate/case.json `
  --change selected-driver --output out/env-comparison.json
```

Default runs retain the policy's 300s measurement/30s warmup profile. Optional
`--diagnostic-seconds 1..120` uses the existing short-reference path, which cannot
accept memory. Guard selection remains bounded to 1..4096MiB and must match between
cases. `record` returns zero for a valid recorded outcome, including native failure,
guard abort or timeout; callers must inspect `outcome`, `native_exit_code`, references
and the underlying runner acceptance/errors. Incomplete/inconsistent receipts or
environment drift return one and retain `status=rejected`. `compare` returns zero
only for recorded-control agreement. Every outer case/comparison explicitly has
`memory_acceptance=false` and `crash_cause_resolved=false` where applicable.

The normal-run wrapper does not collect exception contexts or dumps. Use the
existing [own-child crash collector](../tools/crash_capture/README.md) separately
when an instrumented recurrence is needed; it is a different instrumentation
contract. Installing/changing a driver or BIOS is not part of this script.

Fourteen new synthetic contracts cover driver-only versus confounded changes,
reboot/ID handling, ambiguous hardware mapping, malformed controls, command/policy/
reference binding, crash receipts, environment drift and overwrite refusal. All
**196 Python tests** pass without skips. Existing SDK-crash inventory `--check`,
SDK/source/library/policy hashes and all eleven SDK ZIPs remain unchanged. No new
native compilation, whole native regression or SDK repack is claimed for this
Python-only change. Only standard Python and read-only Windows/CIM/.NET APIs are
used; no new redistributed runtime dependency is added.

[Summary](../benchmarks/reports/2026-10-23-phase2-memory-environment/summary.json) and
[evidence](../benchmarks/reports/2026-10-23-phase2-memory-environment/evidence.json)
bind the final commands, raw cases, rejected attempt, source archive and prior
evidence. Source archives and Git do not include PDBs, executables, dumps, ETLs or
unfiltered system-event exports.
