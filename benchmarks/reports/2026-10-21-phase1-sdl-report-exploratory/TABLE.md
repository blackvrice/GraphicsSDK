# LLGL / SDL3 E-H CPU completion observations

14 workloads × 2 SDKs × 3 independent processes × 120 samples = 84 processes / 10,080 samples. GPU queries disabled in both. Fixed seed interleaving; no outliers removed.

Public semantic workloads match; native shader binding ABI differs. LLGL structured uint32 vs SDL raw byte-address buffers. Integer video planes use LLGL sampled SRV vs SDL read-only storage SRV. Fragment register spaces and mesh input semantics follow each public API.

CPU times include SDK-managed staging/copies/record/submit/completion. Untimed full CPU readbacks run before and after each process. Headless, one batch in flight. Telemetry checkpoints run outside frame timing, inside process batch. Driver/OS caches, GPU clocks and background load uncontrolled.

SDL / LLGL is a cost ratio for this workload and machine, not a universal speedup or a matched GPU timestamp comparison.

| Workload | LLGL CPU P50 ms | SDL CPU P50 ms | SDL / LLGL | LLGL P95 / P99 ms | SDL P95 / P99 ms |
|---|---:|---:|---:|---:|---:|
| compute_blur | 0.070250 | 0.072350 | 1.030 | 0.113090 / 0.137794 | 0.123815 / 0.139997 |
| compute_color | 0.084450 | 0.080950 | 0.959 | 0.150715 / 0.210064 | 0.141625 / 0.167473 |
| compute_reduce | 0.083050 | 0.081350 | 0.980 | 0.147965 / 0.218190 | 0.174835 / 0.243163 |
| compute_buffer | 0.070750 | 0.065350 | 0.924 | 0.137805 / 0.180243 | 0.096245 / 0.116846 |
| compute_graphics | 0.097450 | 0.073400 | 0.753 | 0.184075 / 0.226941 | 0.127840 / 0.155692 |
| sprites_10000 | 0.075950 | 0.074050 | 0.975 | 0.117715 / 0.141600 | 0.100920 / 0.114332 |
| sprites_100000 | 0.195150 | 0.188800 | 0.967 | 0.284130 / 0.333792 | 0.231105 / 0.260725 |
| mesh_draw | 0.189550 | 0.215450 | 1.137 | 0.274470 / 0.352109 | 0.312070 / 0.389096 |
| mesh_instanced | 0.197050 | 0.193800 | 0.984 | 0.277445 / 0.313690 | 0.299525 / 0.381102 |
| video_1080 | 0.976400 | 0.724000 | 0.741 | 1.731165 / 2.548931 | 1.288610 / 1.544618 |
| video_4k | 5.861750 | 2.890150 | 0.493 | 9.782095 / 12.505418 | 4.604315 / 5.579757 |
| video_p010 | 8.362850 | 10.489500 | 1.254 | 13.128345 / 16.183503 | 15.011620 / 16.944063 |
| video_layers | 2.285050 | 1.420300 | 0.622 | 4.840530 / 8.947015 | 2.430190 / 3.200334 |
| video_filter | 1.203850 | 0.912100 | 0.758 | 2.048275 / 2.334615 | 1.413280 / 1.695443 |

| SDK / workload | CPU process % median | Lifetime CPU peak WS MiB max | GPU local checkpoint MiB max | CPU frame MAD ms | Independent run P50 ms |
|---|---:|---:|---:|---:|---|
| llgl / compute_blur | 148.47 | 81.89 | 12.14 | 0.005100 | 0.071300, 0.066800, 0.072600 |
| llgl / compute_buffer | 160.29 | 81.75 | 12.08 | 0.007850 | 0.077950, 0.066800, 0.068000 |
| llgl / compute_color | 109.45 | 81.85 | 12.08 | 0.013400 | 0.075400, 0.078850, 0.102200 |
| llgl / compute_graphics | 103.92 | 82.72 | 12.20 | 0.017200 | 0.113600, 0.081100, 0.105850 |
| llgl / compute_reduce | 0.00 | 81.97 | 12.08 | 0.012800 | 0.073150, 0.079550, 0.099550 |
| llgl / mesh_draw | 62.09 | 81.98 | 11.85 | 0.020700 | 0.193700, 0.169900, 0.198450 |
| llgl / mesh_instanced | 62.99 | 81.92 | 11.85 | 0.022150 | 0.181550, 0.197500, 0.215650 |
| llgl / sprites_10000 | 0.00 | 81.42 | 11.70 | 0.006000 | 0.077200, 0.070800, 0.083700 |
| llgl / sprites_100000 | 61.67 | 81.35 | 11.70 | 0.028100 | 0.233750, 0.164550, 0.196250 |
| llgl / video_1080 | 56.00 | 110.00 | 26.27 | 0.137650 | 1.271000, 0.907700, 0.930650 |
| llgl / video_4k | 57.97 | 195.71 | 67.39 | 1.086700 | 5.451600, 6.584850, 5.439000 |
| llgl / video_filter | 55.21 | 110.57 | 34.77 | 0.150150 | 1.464700, 1.153950, 1.096200 |
| llgl / video_layers | 79.00 | 113.34 | 32.58 | 0.444750 | 2.336600, 1.803350, 2.667150 |
| llgl / video_p010 | 65.16 | 215.64 | 91.20 | 1.593700 | 7.793400, 10.980200, 7.139800 |
| sdl / compute_blur | 0.00 | 75.38 | 19.70 | 0.011650 | 0.074200, 0.060850, 0.093300 |
| sdl / compute_buffer | 0.00 | 75.30 | 19.70 | 0.004100 | 0.068100, 0.062200, 0.066950 |
| sdl / compute_color | 0.00 | 74.88 | 19.70 | 0.017450 | 0.074150, 0.106000, 0.064450 |
| sdl / compute_graphics | 0.00 | 76.95 | 19.82 | 0.007300 | 0.069500, 0.068050, 0.089000 |
| sdl / compute_reduce | 0.00 | 75.12 | 19.70 | 0.019150 | 0.075450, 0.063500, 0.128800 |
| sdl / mesh_draw | 0.00 | 76.14 | 19.46 | 0.027800 | 0.234650, 0.182200, 0.228700 |
| sdl / mesh_instanced | 64.76 | 76.16 | 19.46 | 0.034700 | 0.183100, 0.250950, 0.162300 |
| sdl / sprites_10000 | 0.00 | 74.17 | 19.32 | 0.004950 | 0.072050, 0.073650, 0.075300 |
| sdl / sprites_100000 | 0.00 | 74.25 | 19.32 | 0.012100 | 0.186550, 0.188050, 0.193550 |
| sdl / video_1080 | 60.43 | 105.46 | 30.70 | 0.119300 | 0.608500, 0.908000, 0.741800 |
| sdl / video_4k | 49.78 | 194.09 | 63.07 | 0.369050 | 2.900250, 2.888250, 2.892200 |
| sdl / video_filter | 47.22 | 105.68 | 39.20 | 0.096100 | 0.835100, 1.024700, 0.896400 |
| sdl / video_layers | 56.88 | 111.95 | 33.82 | 0.203000 | 1.268850, 1.352000, 1.684350 |
| sdl / video_p010 | 71.32 | 217.91 | 75.01 | 1.481450 | 9.534850, 10.928900, 11.765700 |

GPU memory maxima are checkpoint lower bounds. Short-batch process CPU values are limited by OS accounting resolution. No continuous transient GPU peak, actual presentation drops, decoder or external texture import acceptance. Phase 1 remains incomplete.
