"""Run independent processes; preserve raw observations, versions and percentiles."""
import argparse
import csv
import datetime
import hashlib
import json
import pathlib
import random
import statistics
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]

def percentile(values, q):
    ordered = sorted(values)
    if not ordered:
        raise ValueError("No samples")
    index = (len(ordered) - 1) * q
    low = int(index)
    return ordered[low] + (ordered[min(low + 1, len(ordered) - 1)] - ordered[low]) * (index - low)

def summarize(runs):
    metrics = {}
    for key in runs[0]["samples"][0]:
        groups = [[s[key] for s in run["samples"] if s[key] is not None] for run in runs]
        if not all(groups):
            metrics[key] = None
            continue
        values = [v for group in groups for v in group]
        center = statistics.median(values)
        mad = statistics.median(abs(v - center) for v in values)
        metrics[key] = {"p50": center, "p95": percentile(values, .95), "p99": percentile(values, .99),
                        "min": min(values), "max": max(values), "mad": mad,
                        "run_p50": [statistics.median(g) for g in groups],
                        "outliers_over_3_mad": sum(abs(v - center) > 3 * mad for v in values) if mad else None,
                        "sample_count": len(values)}
    return metrics

def comparable(a, b):
    keys = ["api", "category", "scenario", "count", "validation", "configuration", "contract"]
    if any(k not in a or k not in b for k in keys):
        return False
    if any(a.get(k) != b.get(k) for k in keys):
        return False
    # Compare physical identity and compiler, including software/hardware distinction.
    env_keys = ["cpu", "ram_bytes", "os", "luid", "driver", "software_renderer", "compiler"]
    if any(k not in a.get("environment", {}) or k not in b.get("environment", {}) for k in env_keys):
        return False
    return all(a["environment"].get(k) == b["environment"].get(k) for k in env_keys)

def regression(old, new):
    """Advisory only: median cost increase must exceed noise and repeat in all runs."""
    if not old or not new or old["p50"] <= 0:
        return {"status": "unavailable"}
    delta = 100 * (new["p50"] / old["p50"] - 1)
    margin = max(.05 * old["p50"], 3 * (old["mad"] + new["mad"]))
    consistent = min(new["run_p50"]) > max(old["run_p50"]) + margin
    repeated = len(new["run_p50"]) >= 3 and len(old["run_p50"]) >= 3
    return {"status": "warning" if repeated and consistent else "inconclusive", "cost_change_percent": delta,
            "noise_margin_ms": margin, "formula": "100 * (new_p50 / baseline_p50 - 1)", "advisory_only": True}

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--exe", type=pathlib.Path, default=ROOT / "out/bench/Release/gfx_bench.exe")
    p.add_argument("--sdks", nargs="+", default=["llgl", "sdl"])
    p.add_argument("--scenarios", nargs="+", choices=["buffers", "textures", "draw", "instanced", "upload1080", "upload4k", "upload4k16f", "pipeline"], default=["buffers", "textures"])
    p.add_argument("--count", type=int, default=1000)
    p.add_argument("--runs", type=int, default=3)
    p.add_argument("--samples", type=int, default=120)
    p.add_argument("--warmup", type=int, default=10)
    p.add_argument("--validation", action="store_true")
    p.add_argument("--output", type=pathlib.Path, required=True)
    args = p.parse_args()
    if args.runs < 1 or args.samples < 1 or args.warmup < 0 or args.count < 1:
        p.error("Invalid measurement count")
    if args.output.exists():
        p.error("Output directory already exists; preserve original measurements")
    lock = json.loads((ROOT / "benchmarks/competitors/sources.lock.json").read_text())
    # Verify built competitor checkout has the recorded version and no local tracked patches.
    for sdk in args.sdks:
        if sdk not in ("llgl", "sdl", "graphicssdk"):
            p.error(f"Adapter is not implemented: {sdk}")
        if sdk == "graphicssdk":
            continue
        source = ROOT / ".deps" / ("LLGL" if sdk == "llgl" else "SDL")
        commit = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
        dirty = subprocess.check_output(["git", "-C", str(source), "status", "--porcelain", "--untracked-files=no"], text=True).strip()
        if commit != lock[sdk]["commit"] or dirty:
            raise RuntimeError(f"Unpinned or modified source: {sdk}")
    executable = args.exe.resolve()
    executable_hash = hashlib.sha256(executable.read_bytes()).hexdigest()
    jobs = [(sdk, scenario, run) for run in range(args.runs) for scenario in args.scenarios for sdk in args.sdks]
    random.Random(20261020).shuffle(jobs)
    collected = {}
    args.output.mkdir(parents=True)
    (args.output / "schedule.json").write_text(json.dumps(jobs, indent=2) + "\n")
    for sdk, scenario, run in jobs:
        proc = subprocess.run([str(executable), sdk, scenario, str(args.count), str(args.warmup), str(args.samples), str(int(args.validation))],
                              cwd=executable.parent, capture_output=True, text=True, encoding="utf-8", errors="replace", timeout=600)
        if proc.returncode:
            (args.output / f"failure-{sdk}-{scenario}-{run}.txt").write_text(proc.stdout + proc.stderr, encoding="utf-8")
            raise RuntimeError(f"{sdk}/{scenario}: failed with {proc.returncode}: {proc.stderr[-1500:]}")
        # Permit SDK diagnostics before the structured final line, preserve them separately.
        lines = proc.stdout.splitlines()
        result = json.loads(lines[-1])
        source_hash = hashlib.sha256()
        for source_file in sorted(list((ROOT/"src").rglob("*.cpp"))+list((ROOT/"include").rglob("*.hpp"))+[ROOT/"CMakeLists.txt"]):
            source_hash.update((source_file.relative_to(ROOT).as_posix()+"|"+hashlib.sha256(source_file.read_bytes()).hexdigest()+"\n").encode())
        if sdk == "graphicssdk" and source_hash.hexdigest() != result.get("sdk_build_source_sha256"):
            raise RuntimeError("GraphicsSDK source changed since compilation; rebuild before measurement")
        allocation = scenario in ("buffers","textures")
        draw = scenario in ("draw","instanced")
        result.update({"run": run, "sdk_commit": lock[sdk]["commit"] if sdk != "graphicssdk" else None,
                       "sdk_source_sha256": source_hash.hexdigest() if sdk == "graphicssdk" else None,
                       "measured_at_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
                       "executable_sha256": executable_hash,
                       "contract": {"revision": 2, "resource_policy": "recreate_uninitialized_default_gpu_memory_then_release_then_idle" if allocation else "reuse_resources_one_frame_in_flight_sdk_managed_staging",
                                    "threads": 1, "buffer_bytes": 65536 if allocation else None,
                                    "texture_extent": [1920,1080] if scenario == "upload1080" else [3840,2160] if scenario.startswith("upload") else [64,64],
                                    "texture_format": "rgba16_float" if scenario == "upload4k16f" else "rgba8_unorm", "mip_levels": 1, "samples": 1,
                                    "vsync": "not_applicable_headless", "compiler_optimization": "/O2", "runtime": "/MD", "lto": False,
                                    "frames_in_flight": 1, "gpu_work": "none_allocation_only" if allocation else "vertex_id_triangle_no_blend_no_depth_no_cull" if draw else "full_texture_copy",
                                    "gpu_timestamps": False, "shader_fnv1a64": result.get("shader_fnv1a64"), "payload_bytes": result.get("payload_bytes",0)}})
        if scenario == "pipeline":
            result["contract"]["gpu_work"] = "none_repeated_pso_creation_driver_cache_uncontrolled"
            result["contract"]["resource_policy"] = "reuse_shader_binaries_create_release_pso_sdk_internal_caching_allowed"
        (args.output / f"{sdk}-{scenario}-{run}.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
        if proc.stderr or len(lines) > 1:
            (args.output / f"{sdk}-{scenario}-{run}.log").write_text("\n".join(lines[:-1]) + proc.stderr, encoding="utf-8")
        collected.setdefault((sdk, scenario), []).append(result)
        print(f"{sdk}/{scenario} run {run + 1}: {len(result['samples'])} samples", flush=True)
    summary = []
    for (sdk, scenario), runs in sorted(collected.items()):
        runs.sort(key=lambda r: r["run"])
        if not all(comparable(runs[0], run) for run in runs):
            raise RuntimeError("Environment changed within measurement")
        item = {k: v for k, v in runs[0].items() if k not in ("samples", "run")}
        item["metrics"] = summarize(runs)
        summary.append(item)
    (args.output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    (args.output / "environment.json").write_text(json.dumps(summary[0]["environment"], indent=2) + "\n", encoding="utf-8")
    with (args.output / "summary.csv").open("w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f); writer.writerow(["sdk", "scenario", "metric", "p50_ms", "p95_ms", "p99_ms", "mad_ms", "run_p50_ms"])
        for item in summary:
            for metric, values in item["metrics"].items():
                if values:
                    writer.writerow([item["sdk"], item["scenario"], metric, values["p50"], values["p95"], values["p99"], values["mad"], json.dumps(values["run_p50"])])
    print(args.output / "summary.csv")

if __name__ == "__main__":
    main()
