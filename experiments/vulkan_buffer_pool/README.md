# Isolated Vulkan small-buffer pool candidate

This experiment patches a copied SDK under `out/`, guarded by the exact product
fingerprint. It does not modify product or vendor sources. The comparison and
decision are documented in [the report](../../docs/PHASE3_VULKAN_BUFFER_POOL_REPORT.md).

Only device buffers whose native requirements fit 4KiB are eligible. Power-of-two
size classes from 64 bytes through 4KiB share 64KiB blocks, separated by memory
type and stride. Native alignment is respected; dedicated requirements/preferences,
larger resources, uploads, readbacks and images use dedicated memory. Empty blocks
are released immediately. An allocation owns its slot for the complete retained
native lifetime, including ended recordings and pending submissions. Statistics
count each shared block's reserved bytes once. A block allocation OOM attempts the
original exact-size path; fault-injected OOM acceptance is outside this experiment.

The fixed block policy reserves 64KiB for a single eligible buffer. A 4KiB buffer
therefore requests 16 times the baseline memory; 1,000 such buffers use 63 blocks,
32KiB more than baseline. These are Vulkan request bytes, not physical residency.
The candidate remains experimental pending a policy that addresses sparse use and
broader lifetime/failure acceptance. It is not the default SDK implementation.

```powershell
python tools/prepare_vulkan_buffer_pool.py --output out/your-new-pool-source
cmake -S out/your-new-pool-source -B out/your-new-pool-build -G "Visual Studio 17 2022" -A x64 -DGFX_ENABLE_VULKAN=ON -DGFX_ENABLE_D3D12=ON -DGFX_VULKAN_VALIDATION_TESTS=ON -DGFX_BUILD_EXAMPLES=OFF -DGFX_VULKAN_HEADERS=D:/source/GraphicsSDK/.deps/Vulkan-Headers
cmake --build out/your-new-pool-build --config Release --parallel 1
```

Configure the pinned VVL child environment before running validation tests. Select
`-E '^memory_reclamation_'` from the start when invoking integrated CTest; the user
has deferred that separate gate. The checked build, install, benchmark and trace
roots are recorded in the report's verification scripts. Use new roots for reruns.
