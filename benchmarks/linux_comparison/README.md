# Linux comparison probes (P3-BEN-07 preparation)

This target separates Linux `dlopen`/native Vulkan UUID and `/proc`/`uname`
environment discovery from the frozen Windows DXGI comparison consumers.
It creates SDK/SDL logical devices without resources, rendering, timestamps or
performance samples. It does not port the four workload consumers (BEN-08).

Two fresh processes per SDK/validation mode must agree on the complete native
inventory, selected UUID/driver, compiler, loader and machine environment. SDK
selection uses the complete UUID-derived exact adapter ID. SDL exposes a public
device name, so its selected name must match exactly one native device. Duplicate
names/UUIDs, different selections and fallback are rejected. This attribution
does not inspect SDL's native physical-device handle.

The runner builds an installed Vulkan-only SDK, pinned static SDL and both native
probe/CPU selection-control executables in fresh roots. It requires the isolated
runtime produced by `tools/prepare_linux_validation.py`. Required validation
processes record the actual pinned library path and enable synchronization
validation. Plain probes use empty layer/settings directories. Wrong UUID,
missing required layer and wrong physical/software kind are rejected for both
SDKs. Process arguments, environments, output bytes, PID/order and source/build/
runtime hashes remain in the receipt. These are probe checks, not workload
validation or performance acceptance.

## Software diagnostic

On Linux x86_64, use Python 3, CMake >=3.25, Ninja, gcc/g++-14, a functioning
Vulkan loader and software ICD. Dependency checkouts must be clean at the commits
in `benchmarks/competitors/sources.lock.json` (SDL) and `tools/vulkan-validation.lock.json`
(Vulkan-Headers and VVL). The software workflow installs its Ubuntu prerequisites;
the runner itself only creates workspace build/evidence files.

```sh
python3 tools/prepare_linux_validation.py
python3 tools/linux_comparison.py run --mode software-diagnostic --icd /absolute/path/to/lvp_icd.json --runtime-receipt out/linux-vulkan-validation-evidence/runtime/receipt.json --output out/linux-comparison-new
python3 tools/linux_comparison.py check --output out/linux-comparison-new --live
```

Software mode may discover a UUID only when exactly one native CPU device is
visible through the explicit ICD. Its success receipt always retains
`P3_BEN_07_completed=false`, `physical_probe_qualified=false`, zero timing samples
and no workload/performance acceptance. The Linux hosted workflow exercises
this mode and real CPU/software rejection by the physical gate.

## Physical environment when available

Use an installed physical ICD and explicit lowercase 32-digit UUID. A matching
PCI vendor/device and Linux GPU driver must corroborate an integrated/discrete
native device. The PCI record is supporting evidence, not an exact UUID-to-PCI
address mapping. Software, virtual and other device types are rejected. SDL's
actual public selection must agree; the runner does not silently switch targets.

```sh
python3 tools/linux_comparison.py run --mode physical --icd /absolute/path/to/physical_icd.json --device-uuid 0123456789abcdef0123456789abcdef --runtime-receipt out/linux-vulkan-validation-evidence/runtime/receipt.json --output out/linux-comparison-physical-new
python3 tools/linux_comparison.py check --output out/linux-comparison-physical-new --live
```

Replace the example UUID with the actual device UUID. The separate manual
`linux-comparison-physical.yml` workflow expects a prepared self-hosted runner
labelled `Linux`, `X64`, `graphicssdk-gpu`; each run/attempt receives a fresh
checkout. It has not been dispatched as part of the software preparation.

Offline `check` replays raw evidence without loading a GPU or requiring the
original absolute paths. `--live` additionally verifies local source, binary and
runtime bytes. Archived source capsules permit replay after future source changes.
