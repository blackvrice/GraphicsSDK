# Advanced LLGL observations

22 scenarios, 66 processes, 7,920 samples. Other advanced adapters are unimplemented. This is not Phase 1 completion. GPU and CPU columns have different intervals.

| Workload | CPU frame P50 ms | GPU explicit-list P50 ms | CPU process % median | Lifetime CPU peak WS MiB max | GPU local checkpoint MiB max |
|---|---:|---:|---:|---:|---:|
| compute_blur | 0.110500 | 0.006144 | 111.74 | 81.02 | 12.15 |
| compute_buffer | 0.107800 | 0.005824 | 0.00 | 80.89 | 12.09 |
| compute_color | 0.110000 | 0.006432 | 110.32 | 80.96 | 12.09 |
| compute_graphics | 0.113750 | 0.007792 | 105.73 | 81.89 | 12.21 |
| compute_reduce | 0.113500 | 0.008544 | 0.00 | 81.05 | 12.09 |
| mesh_draw | 0.212900 | 0.079872 | 58.04 | 81.18 | 11.86 |
| mesh_instanced | 0.206300 | 0.084544 | 0.00 | 81.06 | 11.86 |
| multi_upload | 6.595950 | 0.319024 | 53.19 | 132.45 | 75.34 |
| pso_hit | 0.000000 | null | 0.00 | 79.07 | 11.65 |
| pso_miss | 0.004500 | null | 0.00 | 79.07 | 11.65 |
| resource_inflight | 1.149050 | null | 46.47 | 86.57 | 13.38 |
| resource_reuse | 0.245050 | null | 0.00 | 80.71 | 13.36 |
| shader_cold | 1.342200 | null | 100.09 | 76.79 | 11.59 |
| shader_warm | 0.000100 | null | 0.00 | 76.75 | 11.59 |
| sprites_10000 | 0.115800 | 0.010048 | 101.95 | 80.44 | 11.71 |
| sprites_100000 | 0.221450 | 0.112640 | 0.00 | 80.50 | 11.71 |
| switch_binding | 0.221550 | 0.034544 | 112.99 | 80.96 | 11.77 |
| video_1080 | 0.832900 | 0.167936 | 44.45 | 109.00 | 26.27 |
| video_4k | 2.591900 | 0.534368 | 38.70 | 194.77 | 67.40 |
| video_filter | 1.034200 | 0.352480 | 25.96 | 109.57 | 34.77 |
| video_layers | 0.875850 | 0.200704 | 14.63 | 109.42 | 26.34 |
| video_p010 | 5.091100 | 0.578544 | 52.62 | 214.58 | 91.21 |
