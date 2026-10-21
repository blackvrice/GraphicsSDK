# CPU/memory observation data

63 independent processes, 5760 frame/batch samples. CPU % is a process-batch observation; memory maxima have different scopes. See docs/CPU_MEMORY_BENCHMARK_REPORT.md.

| SDK | Workload | Count | CPU one-core % median | CPU lifetime peak WS MiB max | CPU lifetime peak commit MiB max | GPU local checkpoint max MiB | GPU nonlocal checkpoint max MiB |
|---|---|---:|---:|---:|---:|---:|---:|
| graphicssdk | buffers | 1000 | 34.98 | 73.86 | 153.27 | 73.32 | 23.77 |
| graphicssdk | textures | 1000 | 32.87 | 70.59 | 151.42 | 73.32 | 23.77 |
| llgl | buffers | 1000 | 31.43 | 80.51 | 159.54 | 73.70 | 25.16 |
| llgl | textures | 1000 | 37.38 | 77.52 | 156.66 | 73.70 | 25.16 |
| sdl | buffers | 1000 | 34.14 | 77.02 | 164.19 | 81.70 | 23.77 |
| sdl | textures | 1000 | 36.55 | 74.31 | 161.37 | 81.70 | 23.77 |
| graphicssdk | draw | 100000 | 24.06 | 73.29 | 89.61 | 10.96 | 27.16 |
| graphicssdk | instanced | 100000 | 4.60 | 72.29 | 88.18 | 10.96 | 26.16 |
| llgl | draw | 100000 | 28.02 | 82.17 | 98.47 | 11.64 | 30.16 |
| llgl | instanced | 100000 | 4.40 | 79.20 | 95.35 | 11.64 | 27.16 |
| sdl | draw | 100000 | 26.41 | 75.22 | 99.72 | 19.26 | 27.16 |
| sdl | instanced | 100000 | 4.60 | 74.77 | 96.99 | 19.26 | 26.16 |
| graphicssdk | upload1080 | 1 | 46.16 | 110.00 | 135.45 | 18.89 | 34.10 |
| graphicssdk | upload4k | 1 | 41.92 | 228.70 | 278.00 | 42.77 | 57.85 |
| graphicssdk | upload4k16f | 1 | 50.74 | 386.85 | 468.39 | 74.64 | 89.47 |
| sdl | upload1080 | 1 | 36.14 | 105.49 | 137.98 | 27.20 | 34.09 |
| sdl | upload4k | 1 | 46.79 | 200.21 | 255.69 | 51.07 | 57.84 |
| sdl | upload4k16f | 1 | 51.87 | 326.98 | 415.64 | 82.95 | 89.47 |
| graphicssdk | pipeline | 1 | 0.00 | 72.22 | 87.86 | 10.96 | 26.16 |
| llgl | pipeline | 1 | 0.00 | 77.70 | 93.39 | 11.64 | 26.16 |
| sdl | pipeline | 1 | 0.00 | 74.70 | 97.04 | 19.26 | 26.16 |
