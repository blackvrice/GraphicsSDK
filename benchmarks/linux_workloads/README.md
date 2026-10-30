# Linux workload consumers (P3-BEN-08 preparation)

Build the four frozen SDK/SDL workload programs on Linux using an installed SDK
and pinned static SDL qualified by `tools/linux_comparison.py` at the same source
revision. The original Windows source files and build files remain unchanged.
The deterministic generator changes only the platform include, validation-library
lookup, pre-device exact-UUID/kind guard, identity attribution and null Linux LUID.
SDK/SDL workload implementations and their timer/readback boundaries are reused.
Generated source, its adaptation manifest and exact hashes are retained.

```sh
python3 tools/linux_workloads.py run --mode software-diagnostic --qualification out/linux-comparison-evidence --output out/linux-workloads-new
python3 tools/linux_workloads.py check --output out/linux-workloads-new --live
```

Each new executable runs a no-work probe for both SDKs (eight processes). Eight
required-validation representative smokes then run odd upload, draw1, pso1 and
buffer1 with 1 warmup + 3 diagnostic iterations. Every full logical image/buffer
must match; shader, instance, pipeline-use/release and allocation-policy contracts
are checked. The 24 diagnostic timing rows are validation output, never retained
as performance samples or summarized as a ranking. The remaining workload cases
are compiled but not accepted by these representative smokes.

Twenty-four controls reject wrong UUID, wrong physical/software kind and a
missing required validation layer for every profile/SDK. Commands, per-process
environments, PIDs/serialized intervals, stdout/stderr, generated sources and
input hashes are recorded. Physical mode additionally requires an actual physical
BEN-07 qualification at the same source revision. Software cannot promote either
BEN-07 or BEN-08 physical acceptance; BEN-08 remains pending while BEN-07 does.
Full Linux workload/performance comparisons remain separate roadmap tasks.

The SDL upload consumer keeps its default instance API request; draw, PSO and
allocation keep their explicit Vulkan 1.1 request. The SDK general buffer/SDL
vertex-buffer difference and public release/cache limitations also remain.
CPU fence wall time is not a GPU timestamp, and release calls do not prove
physical reclamation. No reference target using local user test changes is built.

Use `--mode physical` with a successful physical qualification on a prepared
Linux GPU host. The manual physical comparison workflow builds both the probe
and these consumers. A software diagnostic success is preparation evidence only.

## Full software correctness matrix

After a fresh same-revision software qualification and the representative build
checks above, run every case without rebuilding or changing the workload bodies:

```sh
python3 tools/linux_workload_matrix.py run --readiness out/linux-workloads-new --output out/linux-workload-matrix-new
python3 tools/linux_workload_matrix.py check --output out/linux-workload-matrix-new --live
```

The matrix schedules 13 cases (four upload, three draw/instancing, two PSO and four
resource creation) for SDK and SDL in both plain and required-validation modes:
52 separate serialized processes, each with one warmup and three diagnostic rows.
Every warmup and diagnostic result receives full changing-image/buffer readback.
The runner verifies exact source/device identity, actual ICD/loader library load,
required validation library presence or absence, draw/instance counts, pipeline
use/release, all resources and byte counts, and immutable inputs. It records raw
stdout/stderr, commands, environments, process IDs and intervals, source ZIP and
the complete prior qualification. An error or timeout preserves a failed receipt.

This tool accepts native CPU/software ICD qualification only. Its CPU intervals
remain diagnostic raw output: it emits no percentiles, SDK ranking, performance
samples or hardware acceptance. LLGL is not included in this matrix. The existing
representative readiness suite retains its rejection controls; the matrix does
not repeat the separate native core/sync positive error controls. Linux physical
GPU, full three-SDK comparison and P3-LBEN completion remain separate requirements.
