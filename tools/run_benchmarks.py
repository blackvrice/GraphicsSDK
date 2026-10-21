"""Run independent processes; preserve raw observations, versions and percentiles."""
import argparse
import csv
import datetime
import hashlib
import json
import math
import pathlib
import random
import statistics
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]
GPU_SCENARIOS=("draw_gpu","instanced_gpu")
GPU_SCOPE="draw_calls_after_clear_and_bind_before_pass_end_v1"

def timestamp_contract(sdk):
    # Pinned Diligent QueryManagerD3D12::EndQuery resolves every timestamp
    # immediately, including the start marker written by BeginQuery(DURATION).
    # Its measured GPU interval therefore includes that first resolve. Never
    # silently equate it with the deferred/out-of-interval resolve adapters.
    return {"timestamp_scope":GPU_SCOPE,"timestamp_unit":"milliseconds",
            "timestamp_queue":"direct_graphics",
            "query_resolve":"start_marker_resolve_inside_interval_end_marker_resolve_after" if sdk=="diligent_core" else "outside_timestamp_interval_after_end_marker",
            "query_reuse":"one_completed_interval_per_sample"}

def diligent_dependencies(root=ROOT):
    return json.loads((root / "benchmarks/competitors/diligent-build.lock.json").read_text())["dependencies"]

def bgfx_dependencies(root=ROOT):
    return json.loads((root / "benchmarks/competitors/bgfx-build.lock.json").read_text())["dependencies"]

def forge_dependencies(root=ROOT):
    return json.loads((root / "benchmarks/competitors/forge-build.lock.json").read_text())["dependencies"]

def forge_fingerprint(root=ROOT):
    deps=forge_dependencies(root)
    identity="the_forge|"+deps["the_forge"]["commit"]+"\n"
    files=sorted([p for pattern in ("*.cpp","*.hpp","*.h","*.cmake") for p in (root/"benchmarks").rglob(pattern)]+[root/"benchmarks/CMakeLists.txt"])
    for path in files:
        identity+=path.relative_to(root).as_posix()+"|"+hashlib.sha256(path.read_bytes()).hexdigest()+"\n"
    return hashlib.sha256(identity.encode()).hexdigest()

def forge_runtime(root, executable):
    base=root/".deps/TheForge"
    sources={"amd_ags_x64.dll":"Common_3/Graphics/ThirdParty/OpenSource/ags/ags_lib/lib/amd_ags_x64.dll",
             "dxcompiler.dll":"Common_3/Graphics/ThirdParty/OpenSource/DirectXShaderCompiler/bin/x64/dxcompiler.dll",
             "forge-config/gpu.cfg":"Examples_3/Unit_Tests/src/01_Transformations/GPUCfg/gpu.cfg",
             "forge-config/gpu.data":"Common_3/OS/Windows/pc_gpu.data"}
    hashes={}
    for destination,source in sources.items():
        copied=(executable.parent/destination).read_bytes()
        if copied!=(base/source).read_bytes():
            raise RuntimeError(f"Forge runtime/config copy differs from pin: {destination}")
        hashes[destination]=hashlib.sha256(copied).hexdigest()
    return hashes

def bgfx_fingerprint(root=ROOT):
    deps = bgfx_dependencies(root)
    identity = "".join(f"{name}|{deps[name]['commit']}\n" for name in ("bgfx", "bx", "bimg"))
    files = sorted([p for pattern in ("*.cpp", "*.hpp", "*.cmake") for p in (root/"benchmarks").rglob(pattern)] + [root/"benchmarks/CMakeLists.txt"])
    for path in files:
        identity += path.relative_to(root).as_posix()+"|"+hashlib.sha256(path.read_bytes()).hexdigest()+"\n"
    return hashlib.sha256(identity.encode()).hexdigest()

def scenario_error(sdk, scenario):
    if scenario in GPU_SCENARIOS and sdk not in ("graphicssdk","llgl","diligent_core","the_forge"):
        return f"Unsupported {sdk}/{scenario}: no matching public draw-only GPU timestamp interval"
    if sdk == "bgfx" and scenario == "pipeline":
        return "Unsupported bgfx/pipeline: createProgram links shaders; D3D12 PSO creation is lazy on draw"
    return None

def allocation_policy(result):
    model = result.get("allocation_model", "default_gpu_resources")
    if model == "bgfx_pooled_dynamic_vertex_buffers" and result["sdk"] == "bgfx" and result["scenario"] == "buffers":
        return "recreate_uninitialized_pooled_dynamic_vertex_buffers_then_release_drain_frames_then_idle"
    if model != "default_gpu_resources" or (result["sdk"] == "bgfx" and result["scenario"] == "buffers"):
        raise RuntimeError("Unknown or missing allocation model; comparison requires an explicit contract")
    return "recreate_uninitialized_default_gpu_memory_then_release_then_idle"

def verify_source(source, commit):
    actual = subprocess.check_output(["git", "-C", str(source), "rev-parse", "HEAD"], text=True).strip()
    dirty = subprocess.check_output(["git", "-C", str(source), "status", "--porcelain", "--untracked-files=no"], text=True).strip()
    if actual != commit or dirty:
        raise RuntimeError(f"Unpinned or modified source: {source}")

def diligent_fingerprint(root=ROOT):
    dependencies = diligent_dependencies(root)
    identity = "".join(f"{name}|{dep['commit']}\n" for name, dep in dependencies.items())
    files = sorted(list((root/"benchmarks").rglob("*.cpp")) + list((root/"benchmarks").rglob("*.hpp")) + [root/"benchmarks/CMakeLists.txt"])
    for path in files:
        identity += path.relative_to(root).as_posix() + "|" + hashlib.sha256(path.read_bytes()).hexdigest() + "\n"
    return hashlib.sha256(identity.encode()).hexdigest()

def validate_result(result, sdk, scenario, count, warmup, samples, validation):
    expected = {"sdk": sdk, "scenario": scenario, "count": count, "warmup": warmup, "validation": validation,
                "api": "d3d12", "category": "gpu_timestamp" if scenario in GPU_SCENARIOS else "end_to_end", "configuration": "Release"}
    if any(result.get(key) != value for key, value in expected.items()) or len(result.get("samples", [])) != samples:
        raise RuntimeError("Executable returned a different workload or sample count")
    if scenario not in ("buffers", "textures") and result.get("readback_verified") is not True:
        raise RuntimeError("Executable did not verify readback correctness")
    if scenario in GPU_SCENARIOS:
        if result.get("gpu_timestamp_scope")!=GPU_SCOPE:
            raise RuntimeError("Different or missing GPU timestamp interval")
        for sample in result["samples"]:
            for name in ("gpu_ms","query_read_ms"):
                value=sample.get(name)
                if isinstance(value,bool) or not isinstance(value,(int,float)) or not math.isfinite(value) or value<0:
                    raise RuntimeError("Missing/invalid completed GPU timestamp result or query-read cost")
    elif any(sample.get("gpu_ms") is not None for sample in result["samples"]):
        raise RuntimeError("Unexpected timestamp instrumentation in an end-to-end workload")

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
    p.add_argument("--scenarios", nargs="+", choices=["buffers", "textures", "draw", "instanced", "upload1080", "upload4k", "upload4k16f", "pipeline", *GPU_SCENARIOS], default=["buffers", "textures"])
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
        if sdk not in ("llgl", "sdl", "graphicssdk", "diligent_core", "bgfx", "the_forge"):
            p.error(f"Adapter is not implemented: {sdk}")
        for scenario in args.scenarios:
            if error := scenario_error(sdk, scenario):
                p.error(error)
        if sdk == "graphicssdk":
            continue
        if sdk == "the_forge":
            deps=forge_dependencies()
            if deps[sdk]["commit"]!=lock[sdk]["commit"]:
                raise RuntimeError("Research pin and Forge build pin differ")
            for dep in deps.values(): verify_source(ROOT/dep["directory"],dep["commit"])
            continue
        if sdk == "bgfx":
            deps = bgfx_dependencies()
            if deps[sdk]["commit"] != lock[sdk]["commit"]:
                raise RuntimeError("Research pin and bgfx build pin differ")
            for dep in deps.values():
                verify_source(ROOT / dep["directory"], dep["commit"])
            continue
        if sdk == "diligent_core":
            deps = diligent_dependencies()
            if deps[sdk]["commit"] != lock[sdk]["commit"]:
                raise RuntimeError("Research pin and Diligent build pin differ")
            for dep in deps.values():
                verify_source(ROOT / dep["directory"], dep["commit"])
            continue
        source = ROOT / ".deps" / ("LLGL" if sdk == "llgl" else "SDL")
        verify_source(source, lock[sdk]["commit"])
    executable = args.exe.resolve()
    executable_hash = hashlib.sha256(executable.read_bytes()).hexdigest()
    runtime_hashes = {}
    forge_hashes=forge_runtime(ROOT,executable) if "the_forge" in args.sdks else {}
    if "diligent_core" in args.sdks:
        built = ROOT / "out/bench-diligent/diligent/Graphics/GraphicsEngineD3D12/Release/GraphicsEngineD3D12_64r.dll"
        copied = executable.parent / built.name
        if copied.read_bytes() != built.read_bytes():
            raise RuntimeError("Diligent runtime DLL does not match the reviewed build")
        runtime_hashes[built.name] = hashlib.sha256(copied.read_bytes()).hexdigest()
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
        validate_result(result, sdk, scenario, args.count, args.warmup, args.samples, args.validation)
        if sdk == "diligent_core" and result.get("sdk_build_source_sha256") != diligent_fingerprint():
            raise RuntimeError("Stale Diligent benchmark build; rebuild after source/pin changes")
        if sdk == "bgfx" and result.get("sdk_build_source_sha256") != bgfx_fingerprint():
            raise RuntimeError("Stale bgfx benchmark library; rebuild after source/pin changes")
        if sdk == "the_forge" and result.get("sdk_build_source_sha256") != forge_fingerprint():
            raise RuntimeError("Stale Forge benchmark library; rebuild after source/pin changes")
        source_hash = hashlib.sha256()
        for source_file in sorted(list((ROOT/"src").rglob("*.cpp"))+list((ROOT/"include").rglob("*.hpp"))+[ROOT/"CMakeLists.txt"]):
            source_hash.update((source_file.relative_to(ROOT).as_posix()+"|"+hashlib.sha256(source_file.read_bytes()).hexdigest()+"\n").encode())
        if sdk == "graphicssdk" and source_hash.hexdigest() != result.get("sdk_build_source_sha256"):
            raise RuntimeError("GraphicsSDK source changed since compilation; rebuild before measurement")
        allocation = scenario in ("buffers","textures")
        draw = scenario in ("draw","instanced",*GPU_SCENARIOS)
        gpu_timestamps=scenario in GPU_SCENARIOS
        result.update({"run": run, "sdk_commit": lock[sdk]["commit"] if sdk != "graphicssdk" else None,
                       "sdk_source_sha256": source_hash.hexdigest() if sdk == "graphicssdk" else None,
                       "measured_at_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
                       "executable_sha256": executable_hash,
                       "contract": {"revision": 3 if gpu_timestamps else 2, "resource_policy": allocation_policy(result) if allocation else "reuse_resources_one_frame_in_flight_sdk_managed_staging",
                                    "threads": 1, "buffer_bytes": 65536 if allocation else None,
                                    "texture_extent": [1920,1080] if scenario == "upload1080" else [3840,2160] if scenario.startswith("upload") else [64,64],
                                    "texture_format": "rgba16_float" if scenario == "upload4k16f" else "rgba8_unorm", "mip_levels": 1, "samples": 1,
                                    "vsync": "not_applicable_headless", "compiler_optimization": "/O2", "runtime": "/MD", "lto": False,
                                    "frames_in_flight": 1, "gpu_work": "none_allocation_only" if allocation else "vertex_id_triangle_no_blend_no_depth_no_cull" if draw else "full_texture_copy",
                                    "gpu_timestamps": gpu_timestamps, "shader_fnv1a64": result.get("shader_fnv1a64"), "payload_bytes": result.get("payload_bytes",0)}})
        if gpu_timestamps:
            result["contract"].update(timestamp_contract(sdk))
        if sdk == "diligent_core":
            result["build_dependencies"] = diligent_dependencies()
            result["runtime_sha256"] = runtime_hashes
        if sdk == "bgfx":
            result["build_dependencies"] = bgfx_dependencies()
            result["submission_model"] = "single_thread_sequential_view_frame_then_supplied_D3D12_queue_fence"
        if sdk == "the_forge":
            result["build_dependencies"]=forge_dependencies()
            result["runtime_sha256"]=forge_hashes
            result["submission_model"]="single_thread_direct_draw_queue_and_resource_loader_graphics_upload_queue_explicit_fence_wait"
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
