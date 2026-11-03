# Linux LLGL PSO timer preparation

The frozen Windows PSO consumer is adapted through generated Linux sources. The
complete PSO implementation and iteration loop are preserved. The three-SDK runner is `tools/linux_pso_campaign.py`. Software smoke does
not export performance statistics or qualify physical hardware.

Use native Linux x86_64, GCC 14, Ninja, OpenSSL, the pinned Vulkan headers and the
approved patched LLGL shared-library build described in
`docs/PHASE3_LINUX_LLGL_PROBE_REPORT.md`. Run from the repository root:

```sh
cmake -S benchmarks/linux_pso_campaign -B out/linux-pso-timer -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++-14 \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
  -DLLGL_PROBE_BUILD="$LLGL_BUILD" -DVulkan_LIBRARY="$VULKAN_LOADER"
cmake --build out/linux-pso-timer --target linux_pso_compare --verbose -- -d keepdepfile
python3 tools/linux_pso_timer_smoke.py \
  --binary out/linux-pso-timer/linux_pso_compare \
  --observer-directory out/linux-pso-timer \
  --llgl-library-directory "$LLGL_BUILD/lib" --icd "$SOFTWARE_ICD" \
  --uuid "$SOFTWARE_UUID" --output out/linux-pso-timer-smoke
```

`LLGL_BUILD`, `VULKAN_LOADER`, `SOFTWARE_ICD` and `SOFTWARE_UUID` must name the
intended build, loader, CPU ICD and exact native device. The output must be new.
The runner sanitizes Vulkan/loader overrides, runs serially with bounded process
timeouts, keeps stdout/stderr and checks the six native input files did not change.
Its receipt stores only explicit Vulkan/loader overrides, not the host environment.
Do not treat its small diagnostic timings as performance measurements.

`GFX_CAMPAIGN_MODE=physical` generates a distinct physical adapter/fingerprint, but
no physical GPU positive path has been executed. The runner now requires matched physical qualification, controlled O2 builds,
VVL controls and SDK/SDL prerequisites before randomized five-process repetitions.
Actual physical execution and performance acceptance remain pending.

## Integrated campaign

```sh
python3 tools/linux_pso_campaign.py smoke --readiness out/linux-workloads-evidence --llgl-correctness out/linux-llgl-pso-evidence --output out/linux-pso-campaign-smoke
python3 tools/linux_pso_campaign.py check --output out/linux-pso-campaign-smoke --live
```

For a qualified physical GPU, use `run` with the same-revision physical readiness
and LLGL PSO correctness receipts. The fixed plan requires 5 warmups, 30 samples
and 5 independent processes for each SDK/scenario. See
`docs/PHASE3_LINUX_PSO_CAMPAIGN_REPORT.md` for scope and evidence boundaries.
