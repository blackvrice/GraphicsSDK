# Linux three-SDK upload campaign

The separate LLGL timer consumer ports the frozen Windows comparison main and
public Upload body. It reuses the approved shared LLGL build and generated Linux
UUID/device guard. Payload generation, full readback, setup/teardown and warmups
remain outside the timed upload-write/completed-fence interval. The observer is
loaded only for a separate untimed probe. Performance processes load neither the
observer nor VVL, and disable loader debug tracing.

```sh
python3 tools/linux_upload_campaign.py smoke --readiness out/linux-workloads-evidence --llgl-correctness out/linux-llgl-upload-evidence --output out/linux-upload-campaign-smoke
python3 tools/linux_upload_campaign.py check --output out/linux-upload-campaign-smoke --live
```

Software smoke executes 42 serialized processes, verifies 96 full images, retains
72 diagnostic interval rows, and exports no statistics/performance acceptance.

On a real qualified Linux GPU, `run` instead requires same-revision SDK/SDL and
LLGL physical correctness receipts. The fixed 90-process campaign includes all
validation/negative controls before 60 measurement processes. Each SDK/scenario
has five fresh processes, five warmups and thirty retained samples per process.

```sh
python3 tools/linux_upload_campaign.py run --readiness out/linux-workloads-physical --llgl-correctness out/linux-llgl-upload-physical --output out/linux-upload-campaign-physical
python3 tools/linux_upload_campaign.py check --output out/linux-upload-campaign-physical --live
```

See [the report](../../docs/PHASE3_LINUX_UPLOAD_CAMPAIGN_REPORT.md) for observation
limits, evidence, and remaining actual GPU acceptance. Draw/PSO/allocation timed
campaigns are separate remaining work. No GPU timestamps or SDK-wide rankings.
