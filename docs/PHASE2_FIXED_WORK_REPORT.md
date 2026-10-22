# Phase 2: fixed-work memory diagnostics

2026-10-23; base `924b2a6577c218cf7290f7c5d68d424361740341`.

A separate `gfx_memory_fixed_work` executable now fixes warmup and measured work
by verified rounds. Each round processes eight frames using the unchanged
`MemoryStress.cpp` submit, full-image/buffer readback and ownership checks. The
existing timed executable, SDK core, source fingerprint and acceptance policy are
unchanged. This is a diagnostic profile for fairer future environment comparisons;
it does not replace the original long-running churn gate.

## Contract

`--warmup-rounds` and `--rounds` count completed, verified and drained batches. One
pipeline is created per round; each frame uses the original two color targets,
depth target, sampled texture, constant/copy buffers and timestamp/copy commands.
Four public textures are created per frame. Each frame checks three complete
images and 262,144 copied bytes. Warmup is fixed work too, so a faster GPU cannot
silently perform more warmup churn. Checkpoints use measured round coordinates,
including the last round when it is not divisible by the checkpoint interval.

The native tool retains CPU 32MiB and GPU 64MiB retain/release positive controls,
independent native/public budget brackets, explicit exact-adapter selection and
full GBV/state tracking in validation mode. A process-private growth guard is
checked after every verified round. A wall-time limit is checked between rounds;
the Python wrapper also bounds and reaps its own child if it stalls. These are
finite watchdogs, not hard realtime, memory-cap or continuous-peak guarantees.

Native exit 0 means the exact requested fixed work completed; 2 means a private
growth abort; 3 means the wall-time limit. Both aborts preserve only the actually
verified prefix with separate warmup/measured counts, final drained ownership and
an observed stop threshold. Unexpected native errors/incomplete output are
rejected. Exclusive output creation keeps an open write handle and refuses
existing files before device work. Argument ranges and duplicate options are
checked before creating output.

Raw schema `fixed_work_v1` is deliberately distinct from the timed schema. Its
parser checks exact work counts, phase/checkpoint order, native exit/footer
agreement, dimensions, references, aborts and zero ownership after drain. A
read-only header projection reuses **only** existing schema-3 identity/public-budget
field validation. It does not rewrite the raw file or project fixed work into timed
acceptance. The original analyzer rejects this schema. Every result retains
`memory_acceptance=false`.

The Python wrapper snapshots the existing recorded environment before/after,
freezes the new executable/PDB/helper sources/core library and saves the original
policy bytes. Offline validation binds those policy bytes to their recorded hash;
it does not silently use a later changed policy. Completed cases also bind the
native command to the header and adapter. Only completed equal-work cases can
produce checkpoint comparisons. Workload, fixed-input or environment differences
reject the comparison; aborts are not promoted to completed fixed work.

`compare` joins private-byte observations at the same measured round. It reports
absolute observed differences without statistical significance, slope acceptance
or driver-cause claims. Recorded controls do not certify temperature, power,
CPU/RAM stability, all BIOS settings or loaded-module identities. Driver changes
still require an actual change in the selected driver metadata and matching other
recorded controls.

## Fresh serial native verification

All five completed controls use 2 warmup + 16 measured rounds, checkpoint interval
4, 60-second watchdog and 512MiB growth guard:

| Case | Verified frames | Full images | Verified buffer bytes |
|---|---:|---:|---:|
| Intel full GBV A | 144 | 432 | 37,748,736 |
| Intel full GBV B | 144 | 432 | 37,748,736 |
| NVIDIA normal GPU | 144 | 432 | 37,748,736 |
| NVIDIA full GBV | 144 | 432 | 37,748,736 |
| WARP | 144 | 432 | 37,748,736 |
| Completed total | 720 | 2,160 | 188,743,680 |

Each completes exactly 16 measured rounds (128 measured frames) and verifies zero
validation errors where enabled. The two Intel recorded environments match and
their checkpoints align at 4/8/12/16 rounds (32/64/96/128 measured frames). Their
private-byte differences vary despite matching work and environment metadata;
this is not evidence of a changed driver or an improvement.

A separate 1MiB-guard Intel control exits 2 after one verified round (8 frames,
24 images, 2,097,152 buffer bytes). A 1-second-deadline Intel control exits 3 after
11 verified rounds (88 frames, 264 images, 23,068,672 bytes). Neither completes its
requested target. Native overwrite, duplicate/zero/out-of-bounds options, comparison
overwrite and unchanged-driver rejection controls pass. No driver/settings changes
or Intel crash reproduction are claimed. This short fixed-work profile does not
close the timed memory gate or resolve the historical AV.

## Build, run and audit

```powershell
cmake -S tools/memory_fixed_work -B out/memory-fixed-work -G "Visual Studio 17 2022" -A x64
cmake --build out/memory-fixed-work --config Release --parallel 1
out/presentation/Release/gfx_presentation.exe --list-adapters
python tools/run_fixed_memory.py run --adapter <fresh-hardware-id> `
  --rounds 512 --warmup-rounds 8 --checkpoint-rounds 8 --output out/fixed-a
python tools/run_fixed_memory.py compare --before out/fixed-a/case.json `
  --after out/fixed-b/case.json --change none --output out/fixed-comparison.json
```

Use a new output directory and one GPU process at a time. Default watchdog/guard
are 120 seconds/512MiB. The wrapper accepts hardware GPU/validation modes; native
WARP also requires a fresh explicit software-adapter ID. A complete wrapper case
returns zero; guard/deadline/inconsistent cases return one while preserving their
native status and references. `--change selected-driver` selects the recorded
driver-change comparison rule, without installing or changing any driver.

Twelve new synthetic contracts cover exact work/remainder checkpoints, abort
prefixes, public budgets, schema separation, command/policy/reference binding and
work-coordinate comparisons. All **208 Python tests** pass without skips. Actual
MSVC Release compiler/linker-read evidence records **15 own compiler inputs** for
this helper, linking the unchanged core library plus Windows SDK/MSVC prerequisites.
No competitor dependency or native binary/PDB redistribution is introduced. The
old SDK-crash inventory still passes `--check`, and all eleven SDK ZIPs and prior
evidence are preserved. No new full native regression or SDK repack is claimed.
The fresh [combined inventory](../licenses/generated/windows-sdk-fixed-work-release/inventory.json)
contains 22 own compiler inputs/one component for the unchanged core plus this
helper, with actual linker-read library hashes and retained notices. It is generated
and verified by the frozen finish script, separately from the existing sdk-crash profile.

[Summary](../benchmarks/reports/2026-10-23-phase2-fixed-work/summary.json) and
[evidence](../benchmarks/reports/2026-10-23-phase2-fixed-work/evidence.json) bind
the fresh native cases, input hashes, build traces, commands and own source ZIP.
