"""Regenerate the report/figure only from complete, real measurement campaigns."""
import json
import pathlib
import statistics
import sys
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/".deps/python"))

ROOT = pathlib.Path(__file__).resolve().parents[1]
CAMPAIGNS = {
    "draw1000": "2026-10-20-draw-1000",
    "draw10000": "2026-10-20-draw-after",
    "draw100000": "2026-10-20-draw-100000",
    "instanced10000": "2026-10-20-instanced-10000",
    "upload_pipeline": "2026-10-20-upload-pipeline",
    "pipeline": "2026-10-20-pipeline-final",
    "allocation": "2026-10-20-allocation-quiet",
}

def main():
    datasets = {key: json.loads((ROOT/"benchmarks/results"/name/"summary.json").read_text(encoding="utf-8")) for key,name in CAMPAIGNS.items()}
    # The final pipeline campaign excludes JSON-formatting work from its batch timer.
    datasets["upload_pipeline"]=[item for item in datasets["upload_pipeline"] if item["scenario"]!="pipeline"]
    items = [item for dataset in datasets.values() for item in dataset]
    env = items[0]["environment"]
    before = json.loads((ROOT/"benchmarks/results/2026-10-20-draw-before/summary.json").read_text())
    old = next(item for item in before if item["sdk"] == "graphicssdk")
    new = next(item for item in datasets["draw10000"] if item["sdk"] == "graphicssdk")
    out = ["# Benchmark report", "", "측정일: 2026-10-20 (Asia/Seoul). 실제 원자료에서 tools/report_benchmarks.py로 생성.", "",
        "## 측정 환경", "",
        f"- CPU: {env['cpu']}; RAM: {env['ram_bytes']} bytes.",
        f"- GPU: {env['gpu']}; LUID `{env['luid']}`; driver `{env['driver']}`; software={env['software_renderer']}.",
        f"- OS: {env['os']}; compiler: {env['compiler']}; Release /O2, /MD, x64, no LTO.",
        "- D3D12, validation off, single CPU thread, one frame in flight, headless, VSync 비해당.",
        "- LLGL `649c0cc8fa608312c5e4b94dae409a5859fc7553`; SDL `3b2bc951500ca6a1f428af77e036e732d9a8afe3`.",
        "- GraphicsSDK는 각 원자료의 sdk_build_source_sha256로 식별한다. 실행 파일 SHA도 보존한다.",
        "- Desktop 공유 환경이다. 별도 빌드와 GPU 테스트는 이 캠페인 도중 실행하지 않았다. 사용자/OS background 부하는 통제하지 않았다.", "",
        "## 범위와 측정 경계", "",
        "각 workload는 3개 독립 프로세스 회차다. draw 10k는 회차당 warmup 30/표본 240, 다른 draw/instancing은 20/120, upload/PSO는 10/120, allocation은 5/30이다.",
        "Allocation은 시간이 긴 작업이어서 30표본으로 줄였다. 회차간 변동이 커 P99는 탐색 지표다. 원자료와 회차별 median/MAD는 JSON/CSV에 있다.",
        "Draw: 동일 DXBC, 64×64 RGBA8, triangle 3 vertices, no depth/blend/cull, one clear, 같은 draw/instance 수. 100k draw는 인위적 stress로 실사용 성능을 대표하지 않는다.",
        "Upload: 이미 준비한 CPU payload에서 pre-existing texture의 GPU copy 완료까지. 1920×1080/3840×2160 RGBA8 및 3840×2160 RGBA16F. 행은 256-byte 정렬, source memcpy와 내부 staging 비용 포함. setup/readback은 측정 밖이다.",
        "PSO: 미리 compile한 shader로 반복 생성/해제. SDK 내부 caching 허용, driver cache는 통제하지 않았으며 cold compilation/cache hit-miss로 부르지 않는다. core는 root signature 생성도 포함한다.",
        "Allocation: default GPU buffers 64KiB ×1000, sampled textures 64×64 RGBA8 ×1000, uninitialized, create/release/idle. 실제 물리 allocation 수나 GPU 메모리 peak를 의미하지 않는다.",
        "모두 end-to-end 범주다. LLGL End와 SDL Submit의 내부 경계가 다르므로 record와 submit 구간만으로 저수준 API 우위를 주장하지 않는다. GPU 시간은 공통 suite에서 null이며 CPU wait를 GPU 시간으로 대체하지 않았다.",
        "이미지/업로드 시나리오는 측정 전후 readback을 통과했다. 별도 GraphicsSDK correctness 테스트에서 실제 GPU timestamp를 확인했다.", "",
        "## Draw와 Instancing", "", "단위 ms. Frame은 record+submit+완료 대기 wall time이다.", "",
        "| Scenario | Count | SDK | Record P50 | Submit P50 | Frame P50 | Frame P95 | Frame P99 |",
        "|---|---:|---|---:|---:|---:|---:|---:|"]
    for item in sorted([i for i in items if i["scenario"] in ("draw","instanced")], key=lambda i:(i["scenario"],i["count"],i["sdk"])):
        m=item["metrics"]; f=m["frame_ms"]
        out.append(f"| {item['scenario']} | {item['count']} | {item['sdk']} | {m['record_ms']['p50']:.6f} | {m['submit_ms']['p50']:.6f} | {f['p50']:.6f} | {f['p95']:.6f} | {f['p99']:.6f} |")
    out += ["", "## Texture upload", "", "GB/s는 decimal payload bytes / P50 latency의 파생 유효 처리량이며 PCIe counter가 아니다.", "",
            "| Scenario | SDK | Latency P50 ms | P95 ms | P99 ms | Effective GB/s |", "|---|---|---:|---:|---:|---:|"]
    for item in sorted([i for i in items if i["scenario"].startswith("upload")], key=lambda i:(i["scenario"],i["sdk"])):
        m=item["metrics"]["upload_latency_ms"]; throughput=item["payload_bytes"]/m["p50"]/1e6
        out.append(f"| {item['scenario']} | {item['sdk']} | {m['p50']:.6f} | {m['p95']:.6f} | {m['p99']:.6f} | {throughput:.3f} |")
    out += ["", "## Allocation 및 Pipeline", "", "단위 ms. Release API는 실제 회수 비용과 다를 수 있어 전체 batch 비용을 함께 본다.", "",
            "| Scenario | SDK | Create P50 | Release API P50 | Frame P50 | Frame P95 | Frame P99 |", "|---|---|---:|---:|---:|---:|---:|"]
    for item in sorted([i for i in items if i["scenario"] in ("buffers","textures","pipeline")], key=lambda i:(i["scenario"],i["sdk"])):
        metrics=item["metrics"]; create=metrics.get("allocation_ms",metrics.get("pipeline_creation_ms")); f=metrics["frame_ms"]
        out.append(f"| {item['scenario']} | {item['sdk']} | {create['p50']:.6f} | {metrics['release_api_ms']['p50']:.6f} | {f['p50']:.6f} | {f['p95']:.6f} | {f['p99']:.6f} |")
    out += ["", "## 그래프", "", "![P50 and P95 measured costs](../benchmarks/reports/2026-10-20-summary.png)",
            "", "## 확인된 개선과 해석", ""]
    for metric in ("record_ms","frame_ms"):
        a=old["metrics"][metric]; b=new["metrics"][metric]; change=100*(b["p50"]/a["p50"]-1)
        out.append(f"- OPT-001 draw 10k {metric}: P50 {a['p50']:.6f} → {b['p50']:.6f} ms ({change:.2f}%). 이전 회차 median={a['run_p50']}; 이후={b['run_p50']}.")
    out += ["- 매 draw의 GetDeviceRemovedReason 호출을 begin/submit/wait 경계 검사로 옮겼다. 수명/상태/스레드 검증은 유지했고 전체 GPU correctness 및 validation을 재실행했다.",
            "- Draw 10k 공통 비교에서 GraphicsSDK frame P50은 LLGL/SDL과 가까운 범위지만 꼬리와 회차 변동이 있어 성능 우위를 확정하지 않는다.",
            "- 낮게 관측된 항목: 4K RGBA8 upload P50은 GraphicsSDK 10.552150 ms, LLGL 13.422850 ms, SDL 11.106250 ms다. 회차별 변동과 P95 범위가 겹치므로 보편적 우위나 통계적 유의성으로 해석하지 않는다.",
            "- 뒤처진 항목: draw 10k record P50은 GraphicsSDK 0.137100 ms, LLGL 0.115950 ms, SDL 0.128400 ms다. PSO create도 표의 SDK별 차이를 별도로 평가해야 한다. 아직 cache/driver profiler로 원인을 확정하지 않았다.",
            "- 반복 PSO 생성은 core의 root signature 재생성 비용과 SDK 캐시 정책을 포함한다. 큰 차이가 관측되면 cache hit를 통제한 D 확장 및 profiler로 원인을 확인한다.",
            "- LLGL/SDL의 allocation/release 위치가 달라 API release 시간만의 순위는 무의미하다. upload는 staging 재사용 정책에 따른 차이를 포함한다.", "",
            "## 개선 우선순위", "",
            "1. PSO/root signature 재사용 및 cold/warm/cache-hit 조건 분리 측정.",
            "2. committed allocation 병목에 대해 memory budget/peak와 실제 native allocation을 계측한 뒤 allocator/pooling 검토.",
            "3. 다중 texture/upload frame slots와 범용 binding의 정확성부터 확장.",
            "4. 공통 GPU timestamp/프로파일러 캡처와 bgfx/Diligent/Forge adapters 확장 후 Vulkan으로 진행.", "",
            "## 미검증 및 제외한 자료", "",
            "- `2026-10-20-competitors-c`는 빌드 작업과 겹친 최초 탐색 측정이다. 원자료는 보존하되 위 표와 성능 목표 기준에서 제외했다.",
            "- Shader cold/warm compiler cache, controlled PSO cache hit/miss, E/F/G/H, multi-texture/reuse/in-flight allocation, CPU utilization, peak GPU/CPU memory, build-time comparison은 미측정이다.",
            "- PIX/RenderDoc/Nsight/Tracy capture, Linux/Vulkan/macOS/Metal과 실제 game/video/UI는 미검증이다.",
            "- CPU-only allocation과 실 GPU rendering을 섞어 전체 순위를 만들지 않는다. WARP 결과는 기능 테스트에만 사용한다.", "",
            "## 원자료", ""]
    for key,name in CAMPAIGNS.items():
        out.append(f"- [{name} JSON](../benchmarks/results/{name}/summary.json), [CSV](../benchmarks/results/{name}/summary.csv).")
    out.append("- [OPT-001 전후 비교](../benchmarks/reports/2026-10-20-opt-001/comparison.md). 비용 변화율 = 100*(after_p50/before_p50-1).")
    out.append("- [실제 측정 build의 SDK source snapshots](../benchmarks/reports/sources/README.md). Archive 이름의 SHA는 원자료 build fingerprint와 일치한다.")
    (ROOT/"docs/BENCHMARK_REPORT.md").write_text("\n".join(out)+"\n",encoding="utf-8")
    import matplotlib
    matplotlib.use("Agg")
    import matplotlib.pyplot as plt
    figure, axes=plt.subplots(1,3,figsize=(13,4.5),layout="constrained")
    selections=[(datasets["draw10000"],"draw","record_ms","10k draw: CPU recording"),
                (datasets["upload_pipeline"],"upload4k","upload_latency_ms","4K RGBA8: upload completion"),
                (datasets["pipeline"],"pipeline","pipeline_creation_ms","Repeated PSO creation")]
    for axis,(dataset,scenario,metric,title) in zip(axes,selections):
        subset=sorted([i for i in dataset if i["scenario"]==scenario],key=lambda i:i["sdk"])
        medians=[i["metrics"][metric]["p50"] for i in subset]
        upper=[i["metrics"][metric]["p95"]-i["metrics"][metric]["p50"] for i in subset]
        axis.bar([i["sdk"] for i in subset],medians,yerr=[[0]*len(upper),upper],capsize=4,color=["#2462b3","#36a77c","#cc8b25"])
        axis.set_title(title); axis.set_ylabel("milliseconds (P50; whisker = P95)"); axis.set_ylim(bottom=0); axis.grid(axis="y",alpha=.25)
    figure.suptitle("D3D12 / GTX 1660 SUPER — exploratory same-machine costs, no overall SDK ranking")
    figure.savefig(ROOT/"benchmarks/reports/2026-10-20-summary.png",dpi=170)
    plt.close(figure)
    print(ROOT/"docs/BENCHMARK_REPORT.md")

if __name__ == "__main__": main()
