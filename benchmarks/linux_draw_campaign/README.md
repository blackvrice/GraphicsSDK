# Linux three-SDK draw/instancing campaign

The frozen Draw body, timer partitions and observation loop are preserved.
Linux UUID/native-handle checks and an explicit compiled kind guard replace the
Windows platform adapter. The approved ten-line LLGL overlay is unchanged.

```sh
python3 tools/linux_draw_campaign.py smoke --readiness out/linux-workloads-evidence --llgl-correctness out/linux-llgl-draw-evidence --output out/linux-draw-campaign-smoke
python3 tools/linux_draw_campaign.py check --output out/linux-draw-campaign-smoke --live
```

Software smoke retains 63 diagnostic rows from 42 serialized processes and checks
84 full images. It emits zero measurements/statistics/performance acceptance.

Physical timing requires same-revision completed SDK/SDL and LLGL physical
qualification/correctness plus matched GNU O2/no IPO/unity build evidence. Three
scenarios and three SDKs each run five fresh processes, five warmups and thirty
retained samples. The 78-process campaign produces 1,350 primary samples and 36
descriptive rows across record/submit/wait/total columns.

```sh
python3 tools/linux_draw_campaign.py run --readiness out/linux-workloads-physical --llgl-correctness out/linux-llgl-draw-physical --output out/linux-draw-campaign-physical
python3 tools/linux_draw_campaign.py check --output out/linux-draw-campaign-physical --live
```

Observer-only audits and required VVL validation precede all measurements.
Performance processes load neither observer nor VVL and disable loader tracing.
See [the report](../../docs/PHASE3_LINUX_DRAW_CAMPAIGN_REPORT.md) for observation
limits and the still-unexecuted real-GPU acceptance. No GPU timestamps or SDK-wide
ranking. PSO/allocation timed campaigns remain separate work.
