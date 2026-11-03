# Linux three-SDK resource campaign

The frozen Windows LLGL allocation consumer is adapted to native Linux, preserving
the complete resource implementation and timed loop. SDK/SDL use the same-revision
qualified Linux consumers. Software diagnostics never export performance statistics.

```sh
python3 tools/linux_allocation_campaign.py smoke --readiness out/linux-workloads-evidence --llgl-correctness out/linux-llgl-allocation-evidence --output out/linux-allocation-campaign-smoke
python3 tools/linux_allocation_campaign.py check --output out/linux-allocation-campaign-smoke --live
```

Use `run` with matching physical readiness and LLGL allocation correctness receipts
to execute the physical contract. Fresh output/build directories under `out/` are
required. Controlled O2 qualification, the pinned VVL runtime and approved LLGL
profile are mandatory. The physical workflow is prepared but has no available GPU
runner; its positive path is unexecuted.

The generated consumer can also be built with native Linux x86_64, GCC 14, Ninja,
OpenSSL, pinned headers and the qualified LLGL shared-library build:

```sh
cmake -S benchmarks/linux_allocation_campaign -B out/linux-allocation-timer -G Ninja \
  -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++-14 \
  -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -DLLGL_PROBE_BUILD="$LLGL_BUILD" \
  -DVulkan_LIBRARY="$VULKAN_LOADER" -DGFX_CAMPAIGN_MODE=software-diagnostic
cmake --build out/linux-allocation-timer --target linux_allocation_compare --verbose -- -d keepdepfile
```

See `docs/PHASE3_LINUX_ALLOCATION_CAMPAIGN_REPORT.md` for the fixed measurements,
validation gates, native handle attribution, full payloads and acceptance limits.
