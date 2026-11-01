# Linux LLGL PSO lifecycle correctness

Separate native Linux consumer of the approved `llgl-fixed-pin-transfer-src-v1`
shared libraries. Preserves frozen Windows PSO creation/use/public-release,
full-frame reference and validation controls. Rebuilds the own pass-through draw
observer from its original source with Linux exports; records draw dispatches,
not native pipeline creation counts.

After a clean tracked same-revision Linux LLGL probe:

```sh
python3 tools/linux_llgl_pso.py run --probe out/linux-llgl-evidence --output out/linux-llgl-pso-evidence
python3 tools/linux_llgl_pso.py check --output out/linux-llgl-pso-evidence --live
```

Requires Linux x86_64, GCC 14, Ninja, Python, shared system OpenSSL Crypto development
files and the probe's explicit software ICD/isolated pinned VVL. The workflow
`.github/workflows/linux-comparison-probe.yml` prepares these prerequisites.

`pso1` and `pso8` run in plain/required-validation modes. Each creates fresh public
pipeline and shader handles, releases shader handles after pipeline creation,
draws all changing stripes, waits for GPU use, releases pipeline handles, then
verifies full readback. Checks 16 changing images / 153,920 bytes, 72 public
pipeline create/release pairs and 144 shader handle create/release pairs.

Seventeen serial processes include four normal cases, two core/sync controls
and eleven intended rejections (layer, UUID, kind, corrupt readback, sync off,
missing pipeline, skipped release, wrong pipeline, skipped region, scissor,
missing observer). Invalid native control commands are never submitted.

Retains exact source/translation, input/license/runtime hashes, two actual GNU
compiler commands/depfiles, raw logs and independent Python reference hashes.
Offline `check` executes no archived code. This supplies zero performance samples;
physical-GPU `P3-LBEN-03`, driver cache state, native pipeline counts and GPU
memory reclamation remain unverified. No new SDK dependency or vendor patch.
