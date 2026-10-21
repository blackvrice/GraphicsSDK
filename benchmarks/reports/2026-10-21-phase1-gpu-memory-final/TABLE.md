# Phase 1 GPU memory event trace acceptance

Windows D3D12 / LLGL, five complete fresh process lifetimes. TDH-decoded private DxgKrnl session, zero events/buffers lost. Process usage/commitment are change-event peaks; registered allocation sizes are a separate metric. Setup, readback and teardown are included; no frame-only or physical VRAM occupancy claim.

| Process | GPU local usage peak MiB | GPU nonlocal usage peak MiB | VidMm live allocation size peak MiB | Allocations fully paired | Destroyed in System/other context |
|---|---:|---:|---:|---:|---:|
| positive_control | 79.941406 | 129.890625 | 211.910156 | 211 | 177 |
| resource_inflight | 13.617188 | 30.281250 | 45.976562 | 649 | 138 |
| multi_upload | 75.328125 | 43.031250 | 120.437500 | 144 | 120 |
| video_4k | 67.390625 | 66.781250 | 136.250000 | 135 | 120 |
| video_p010 | 91.203125 | 74.718750 | 168.000000 | 135 | 120 |

| Positive interval | Overlapping 32MiB+ allocations | Trace local peak MiB | All-checkpoint local maximum MiB | Peak missed by checkpoints MiB |
|---|---:|---:|---:|---:|
| 0 | 2 | 79.933594 | 15.941406 | 63.992188 |
| 1 | 2 | 79.941406 | 15.941406 | 64.000000 |
| 2 | 2 | 79.941406 | 15.941406 | 64.000000 |

Raw system ETL and full-system decoded events stay local under repository out/. This report versions only target-process events and native outputs, plus hashes of every raw input. The earlier CPU/GPU timing observations remain unchanged. This trace run is correctness/memory evidence, not a new timing comparison.
