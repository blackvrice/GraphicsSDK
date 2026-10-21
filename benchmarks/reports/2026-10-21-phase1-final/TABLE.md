# Advanced LLGL observations

22 scenarios, 66 processes, 7,920 samples. Other advanced adapters are unimplemented. This is not Phase 1 completion. GPU and CPU columns have different intervals.

| Workload | CPU frame P50 ms | GPU explicit-list P50 ms | CPU process % median | Lifetime CPU peak WS MiB max | GPU local checkpoint MiB max |
|---|---:|---:|---:|---:|---:|
| compute_blur | 0.110800 | 0.006944 | 112.20 | 81.44 | 12.15 |
| compute_buffer | 0.105700 | 0.005936 | 117.77 | 81.32 | 12.09 |
| compute_color | 0.110450 | 0.006688 | 0.00 | 81.45 | 12.09 |
| compute_graphics | 0.115500 | 0.007776 | 0.00 | 82.34 | 12.21 |
| compute_reduce | 0.111200 | 0.008768 | 0.00 | 81.60 | 12.09 |
| mesh_draw | 0.210250 | 0.079520 | 119.48 | 81.56 | 11.86 |
| mesh_instanced | 0.200300 | 0.079648 | 62.92 | 81.54 | 11.86 |
| multi_upload | 6.421650 | 0.319472 | 43.69 | 132.96 | 75.34 |
| pso_hit | 0.000000 | null | 0.00 | 79.52 | 11.65 |
| pso_miss | 0.004300 | null | 0.00 | 79.53 | 11.65 |
| resource_inflight | 1.185300 | null | 57.08 | 87.00 | 13.38 |
| resource_reuse | 0.252000 | null | 48.79 | 81.18 | 13.36 |
| shader_cold | 1.412050 | null | 97.75 | 77.28 | 11.59 |
| shader_warm | 0.000100 | null | 0.00 | 77.17 | 11.59 |
| sprites_10000 | 0.116700 | 0.012288 | 105.95 | 81.01 | 11.71 |
| sprites_100000 | 0.198700 | 0.094208 | 0.00 | 80.88 | 11.71 |
| switch_binding | 0.219800 | 0.042624 | 58.43 | 81.38 | 11.77 |
| video_1080 | 0.816550 | 0.167616 | 46.26 | 109.47 | 26.27 |
| video_4k | 2.640050 | 0.534816 | 44.43 | 195.21 | 67.40 |
| video_filter | 1.039350 | 0.356288 | 36.75 | 109.94 | 34.77 |
| video_layers | 1.585800 | 0.329360 | 48.09 | 112.73 | 32.59 |
| video_p010 | 4.950700 | 0.579584 | 45.20 | 215.06 | 91.21 |
