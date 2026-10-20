"""Reproduce the fixed Diligent expansion report; raw campaign files are never rewritten."""
import csv
import hashlib
import json
import pathlib
import zipfile
from compare_results import compare
from run_benchmarks import comparable, diligent_fingerprint

ROOT = pathlib.Path(__file__).resolve().parents[1]
SDKS = ["llgl", "sdl", "diligent_core", "graphicssdk"]
CAMPAIGNS = ["2026-10-20-diligent-draw", "2026-10-20-diligent-upload-pipeline", "2026-10-20-diligent-allocation"]
SCENARIOS = ["buffers", "textures", "draw", "instanced", "upload1080", "upload4k", "upload4k16f", "pipeline"]


def main():
    rows = []
    for campaign in CAMPAIGNS:
        rows += json.loads((ROOT/"benchmarks/results"/campaign/"summary.json").read_text())
    if len(rows) != 32 or {(r["sdk"],r["scenario"]) for r in rows} != {(sdk,scenario) for sdk in SDKS for scenario in SCENARIOS}:
        raise ValueError("Missing or duplicate SDK/scenario summaries")
    for scenario in SCENARIOS:
        group = [r for r in rows if r["scenario"] == scenario]
        if not all(comparable(group[0],r) for r in group):
            raise ValueError("Incompatible measurement contract/environment")
        if any(r["validation"] or len(r["metrics"]["frame_ms"]["run_p50"]) != 3 for r in group):
            raise ValueError("Expected three independent performance processes with validation disabled")
    hashes = {r["executable_sha256"] for r in rows}
    if len(hashes) != 1:
        raise ValueError("Campaigns used different executables")
    own_bench = diligent_fingerprint()
    if {r["sdk_build_source_sha256"] for r in rows if r["sdk"] == "diligent_core"} != {own_bench}:
        raise ValueError("Archive measured benchmark sources before changing them")
    source_paths = list((ROOT/"benchmarks").rglob("*.cpp")) + list((ROOT/"benchmarks").rglob("*.hpp"))
    source_paths += list((ROOT/"src").rglob("*.cpp")) + list((ROOT/"include").rglob("*.hpp"))
    source_paths += [ROOT/name for name in ["CMakeLists.txt", "benchmarks/CMakeLists.txt", "benchmarks/competitors/sources.lock.json",
                     "benchmarks/competitors/diligent-build.lock.json", "tools/run_benchmarks.py", "LICENSE"]]
    archive = ROOT/"benchmarks/reports/sources"/f"diligent-{own_bench}.zip"
    if not archive.exists():
        with zipfile.ZipFile(archive,"x",compression=zipfile.ZIP_DEFLATED) as out:
            for path in sorted(source_paths): out.write(path,path.relative_to(ROOT).as_posix())
    else:
        with zipfile.ZipFile(archive) as saved:
            if set(saved.namelist()) != {p.relative_to(ROOT).as_posix() for p in source_paths} or any(saved.read(p.relative_to(ROOT).as_posix()) != p.read_bytes() for p in source_paths):
                raise ValueError("Existing measured source archive differs; preserve it")
    output = ROOT/"benchmarks/reports/2026-10-20-diligent"
    output.mkdir(exist_ok=True)
    comparisons = compare(rows,rows,"diligent_core","graphicssdk")
    with (output/"comparison.csv").open("w",newline="",encoding="utf-8") as file:
        writer=csv.DictWriter(file,fieldnames=list(comparisons[0])); writer.writeheader(); writer.writerows(comparisons)
    with (output/"frame-percentiles.csv").open("w",newline="",encoding="utf-8") as file:
        writer=csv.writer(file); writer.writerow(["sdk","scenario","count","p50_ms","p95_ms","p99_ms","mad_ms","run_p50_ms"])
        for row in sorted(rows,key=lambda r:(SCENARIOS.index(r["scenario"]),SDKS.index(r["sdk"]))):
            values=row["metrics"]["frame_ms"]
            writer.writerow([row["sdk"],row["scenario"],row["count"],*[values[k] for k in ("p50","p95","p99","mad")],json.dumps(values["run_p50"])])
    evidence={"executable_sha256":next(iter(hashes)),"benchmark_build_fingerprint":own_bench,
              "source_archive":archive.relative_to(ROOT).as_posix(),"source_archive_sha256":hashlib.sha256(archive.read_bytes()).hexdigest(),
              "campaigns":CAMPAIGNS,"summary_files":[{"path":f"benchmarks/results/{c}/summary.json","sha256":hashlib.sha256((ROOT/"benchmarks/results"/c/"summary.json").read_bytes()).hexdigest()} for c in CAMPAIGNS]}
    (output/"evidence.json").write_text(json.dumps(evidence,indent=2)+"\n")
    samples=sum(r["metrics"]["frame_ms"]["sample_count"] for r in rows)
    env=rows[0]["environment"]
    lines=["# Diligent Core D3D12 비교 추가", "", "측정일: 2026-10-20. 기존 LLGL/SDL3/GraphicsSDK와 standalone public Diligent Core를 같은 실행 파일로 다시 측정했다.",
           f"8개 시나리오 × 4개 SDK × 3개 독립 프로세스 = 96개 프로세스, 측정 표본 {samples:,}개다.", "",
           "## 실행 조건과 범위", "",
           f"{env['cpu']}, RAM {env['ram_bytes']:,} bytes, {env['gpu']}, DXGI LUID `{env['luid']}`, driver `{env['driver']}`.",
           f"{env['os']}, {env['compiler']}, x64 Release `/O2 /MD`, LTO 없음. validation OFF, headless, single caller thread, 한 프레임 완료 후 다음 작업을 시작한다.",
           "기본 Diligent의 `/GL`, `/arch:AVX2` 추가 옵션을 끄고 비동기 shader compile을 비활성화했다. GPU timestamp 값은 전부 null이다.", "",
           "DiligentEngine superproject의 submodule 조합이 아닌 연구 시 고정한 standalone Core 커밋을 사용했다. commercial Metal은 사용하지 않는다.",
           "[Core/xxHash/Abseil/NVAPI 고정 버전](../benchmarks/competitors/diligent-build.lock.json), [실제 의존성·고지](DEPENDENCY_LICENSES.md#diligent-core-추가-profile)를 참조한다.", "",
           "## 측정한 frame 비용", "", "단위 ms. 각 열은 전체 표본의 P50이다. P95/P99/MAD와 각 프로세스 P50은 [전체 percentile CSV](../benchmarks/reports/2026-10-20-diligent/frame-percentiles.csv)에 있다.", "",
           "| 시나리오 | Count | LLGL | SDL3 | Diligent Core | GraphicsSDK |", "|---|---:|---:|---:|---:|---:|"]
    for scenario in SCENARIOS:
        group={r["sdk"]:r for r in rows if r["scenario"]==scenario}
        lines.append(f"| {scenario} | {group['llgl']['count']} | "+" | ".join(f"{group[sdk]['metrics']['frame_ms']['p50']:.6f}" for sdk in SDKS)+" |")
    lines += ["", "Draw/instancing은 warmup 20 + 측정 120회/프로세스, 업로드/PSO는 warmup 10 + 측정 120회, 할당은 warmup 5 + 측정 30회다.",
              "할당은 64 KiB vertex-capable default buffer 또는 64×64 RGBA8 texture를 1,000개 생성·해제하고 idle까지 기다린다.",
              "Draw는 동일한 DXBC bytecode로 64×64 RGBA8에 vertex-ID triangle을 10,000번 그린다. Instancing은 3 vertices × 10,000 instances의 한 호출이다.",
              "업로드는 persistent texture에 1080p RGBA8 / 4K RGBA8 / 4K RGBA16F 전체를 복사한다. PSO는 동일한 사전 컴파일 bytecode에서 반복 생성한다.", "",
              "## 경계와 검증", "",
              "Diligent는 immediate context에서 기록하고 `Flush()`로 제출한다. `WaitForIdle()`, `FinishFrame()`, `ReleaseStaleResources(true)`로 GPU 완료와 headless 임시 자원 회수를 처리한다.",
              "각 SDK의 기록/제출/캐싱 경계는 다르므로 부분 타이머만으로 native API overhead를 단정하지 않는다. 비교 분류는 end_to_end다.",
              "Draw frame은 기록·제출·완료 대기를 포함하고, allocation frame은 생성·release API·idle을 포함한다. PSO frame은 생성·release API가 있는 loop body이며 다음 반복 전 idle은 제외한다.",
              "`release_api_ms`는 호출 비용이다. 모든 native allocation의 즉시 회수를 의미하지 않으며 SDK 내부 캐시·지연 회수를 허용한다. driver shader/PSO cache cold/warm 상태는 통제하지 않았다.",
              "Draw/PSO는 측정 전후 빨간 중심·파란 clear 모서리를 확인하고, 업로드는 측정 전후 모든 byte를 readback 비교했다. 고정 셰이더 hash 및 workload/environment 계약 일치를 검사했다.",
              "별도의 validation ON 실행은 8개 시나리오 모두 통과했고 Diligent native D3D12 info queue ERROR/CORRUPTION은 없었다. [실행 기록](../benchmarks/reports/validation/2026-10-20-diligent/summary.json)은 성능 표본과 분리했다.", "",
              "같은 데스크톱에서 순서를 고정 seed로 섞어 실행했다. 전용 장비의 background activity·clock/power 상태는 통제하지 않았으며 결과는 이 장비·작업량의 관측값이다. 일반 SDK 우열이나 통계적 유의성을 주장하지 않는다.", "",
              "## 보존한 증거", "",
              *[f"- [{campaign} 원본 JSON/CSV](../benchmarks/results/{campaign}/summary.json)" for campaign in CAMPAIGNS],
              "- [Diligent baseline ↔ GraphicsSDK 세부 비용 비교 CSV](../benchmarks/reports/2026-10-20-diligent/comparison.csv)",
              "- [실행 파일·소스 archive·결과 hash](../benchmarks/reports/2026-10-20-diligent/evidence.json)",
              f"- [측정한 자체 어댑터·코어 소스 archive](../{archive.relative_to(ROOT).as_posix()})", "",
              "기존 측정 및 배포 ZIP은 보존했다. bgfx/The Forge 어댑터와 전체 A–H 비교, GPU timestamp 공통 비교, CPU/GPU 메모리·cache 통제는 남아 있다."]
    (ROOT/"docs/DILIGENT_BENCHMARK_REPORT.md").write_text("\n".join(lines)+"\n",encoding="utf-8")
    print(f"PASS: {len(rows)} summaries, {samples} observations; {output}")


if __name__ == "__main__":
    main()
