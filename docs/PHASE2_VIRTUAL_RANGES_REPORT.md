# Phase 2: observed virtual-memory range reconstruction

2026-10-23. Follow-up to the
[allocation-path investigation](PHASE2_FAULT_INVESTIGATION_REPORT.md), based on
commit `d6b31743f50dea2f50e104a80153dbaa0d3ee69e`.

`tools/analyze_virtual_ranges.py` now reconstructs observed committed address ranges
and their retirement from the four existing loss-free target-process exports. This
removes the earlier ambiguity between commit-call byte sums and simultaneous
observed commitments. No new GPU workload or system trace is run by this tool.

## Results on the frozen original exports

| Adapter / GBV mode | Events | All observed live ranges, maximum bytes | GBV resource-info command-buffer ranges, maximum bytes | Observed live bytes at export end |
|---|---:|---:|---:|---:|
| NVIDIA / full | 1,136 | 327,995,392 | 320,028,672 | 0 |
| NVIDIA / tracking disabled | 657 | 2,850,816 | 0 | 0 |
| Intel / full | 16,125 | 156,868,608 | 111,587,328 | 0 |
| Intel / tracking disabled | 286 | 5,787,648 | 0 | 0 |

These 18,204 events come from the previous investigation. The command-buffer tag
requires both `NDebug::CGBVDataController::AddResourceInfo` and
`NDXGI::CDevice::AllocateCB_0022` in the same commit stack, using exact module-relative
offsets from the previously matched public-PDB table. Other AddResourceInfo stacks
and all remaining allocations have separate categories.

For these four exports, overlap with an already-observed live range is **zero** for
every commit. In particular, the 320,028,672 / 111,587,328 command-buffer byte sums
were not inflated by overlapping recommit: all of those tagged ranges coexist at
their respective observed maximum. Every observed commit range is later retired
by an observed DECOMMIT or RELEASE before its export ends; the analyzer does not
clear outstanding ranges implicitly at process exit. The earlier live-device
retention and delayed post-device reclamation observations remain separate native
measurements. This replay does not synchronize ETW microseconds to native phase
timestamps or assign an exact device-destruction time to a range.

The replay is not a complete process commitment census. Collection begins after
process startup; frees outside previously observed live ranges are counted
explicitly (including release of reserved/uncommitted spans), without inventing a
missing allocation, negative balance or initial committed state. The totals do
not represent process private bytes, VRAM, resident physical memory, a whole-process
peak, CPU object lifetimes or a leak proof. A commit newly seen by the analyzer may
have existed before collection; the report calls it an introduced *observed* range.
The original memory acceptance remains false and the historical Intel AV unresolved.

## Accounting and rejection contracts

The implementation stores disjoint half-open intervals rather than a record per
page. First observed commit attribution survives overlapping recommits. Partial
decommit/release splits an interval; a new commit in a retired hole starts a fresh
observed lifetime. Reserve-only events do not add committed bytes. RESERVE COMMIT
is handled as a commit. Source attribution comes from commit stacks; free stacks
are unnecessary and need not have resolved symbols.

The reviewed schema contains actual nonzero, page-aligned ETW ranges. It is not a
raw VirtualFree-argument decoder: a zero-size API RELEASE cannot supply its extent
and is refused. Unsupported flags (including reset/placeholder operations), foreign
PIDs, reversed times, malformed ranges, duplicate JSON fields, truncated/oversized
exports, changed hashes, nonzero/missing loss receipts, conflicting symbol identities
and inconsistent event/call counters are rejected. Input hashes are checked again
after replay. Existing output directories are refused before analysis/writes.

Microsecond conversion can tie distinct events. Overlapping ranges at the same
timestamp have ambiguous order and are rejected. Disjoint ranges commute for final
state but can still have different instantaneous peaks if frees and commits swap
order. Timeline rows therefore represent timestamp boundaries. The peak lower bound
uses those boundaries; the upper bound permits all same-time commits before frees.
In the four actual exports the resulting global and per-category peak bounds agree.
Agreement applies only within the observed export/model, not to unobserved memory.

[Microsoft's ETW event contract](https://learn.microsoft.com/en-us/windows/win32/etw/pagefault-virtualalloc)
defines owner PID, base address, region size and flags.
[VirtualAlloc](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualalloc)
permits recommit of already committed pages; reserve alone does not add commitment.
[VirtualFree](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualfree)
distinguishes decommit from release, which also decommits the released region.

## Reproduction and verification

From the repository root (standard Python only):

```powershell
python -m unittest discover -s tests -p test_virtual_ranges.py
python tools/analyze_virtual_ranges.py `
  --trace-manifest benchmarks/reports/validation/2026-10-22-phase2-fault-investigation/virtual-call-paths.json `
  --symbols benchmarks/reports/validation/2026-10-22-phase2-fault-investigation/resolved-virtual-call-paths.json `
  --output out/virtual-ranges-new
```

`--root` optionally changes the root for manifest-relative input paths. Output
contains one boundary timeline per case, its hash, and a summary with origin
balances, recommit overlap, unmatched-free bytes, peak bounds and surviving ranges.
All output is explicitly diagnostic with `memory_acceptance=false`. PDB download,
privilege elevation and native binary rebuilding are unnecessary.

Thirteen new tests cover partial release/decommit, attribution across overlapping
commits, hole reuse, sparse 64-bit ranges, reservation semantics, same-time peak
ambiguity, malformed evidence and overwrite refusal. A 700-operation deterministic
page oracle independently checks the interval implementation after each operation.
All **182 Python tests** pass without skips. The four real exports replay under
frozen inputs; the prior SDK-crash compiler/linker inventory still passes `--check`.
Production SDK fingerprint, core library, original policy, prior evidence and all
eleven SDK ZIPs are unchanged. No complete native regression, fresh soak, driver
comparison, performance or new acceptance result is claimed for this Python change.

[Summary](../benchmarks/reports/2026-10-23-phase2-virtual-ranges/summary.json) and
[evidence manifest](../benchmarks/reports/2026-10-23-phase2-virtual-ranges/evidence.json)
record the source archive and exact input/result hashes. The old reports/exports
are preserved; no ETL, unfiltered CSV, PDB or minidump is redistributed.
