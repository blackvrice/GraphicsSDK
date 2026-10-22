"""Native destruction receipts and SDK generation lifetimes; never a soak acceptance gate."""
import argparse
import hashlib
import json
import os
import pathlib
import platform
import re
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/"tools"))
from analyze_memory_stress import OWNED, number, read_records, require
from audit_licenses import sdk_fingerprint

NATIVE_MODES = ("hardware-cpu", "hardware-gbv", "warp-gbv")
SDK_MODES = ("hardware", "warp")


def integer(value):
    return type(value) is int and value >= 0


def analyze(records, sdk=False, fingerprint=None):
    require(len(records) >= 17 and all(isinstance(r, dict) for r in records), "Missing lifetime records")
    h, f = records[0], records[-1]
    require(h.get("type") == "header" and h.get("schema") == 1 and f.get("type") == "footer", "Header/footer missing")
    require(h.get("mode") in (SDK_MODES if sdk else NATIVE_MODES), "Wrong profile")
    require(h.get("gpu_state_tracking") is True and h.get("gpu_validation") is (sdk or h["mode"] != "hardware-cpu"), "Validation configuration mismatch")
    cycles = h.get("cycles")
    require(type(cycles) is int and 2 <= cycles <= 16, "Repeated cycles required")
    parameter = h.get("batches_per_cycle" if sdk else "iterations_per_cycle")
    require(type(parameter) is int and 1 <= parameter <= (16 if sdk else 512), "Invalid workload")
    require(f.get("lifetime_contracts_passed") is True and f.get("memory_acceptance_claim") is False and f.get("cycles") == cycles, "Incomplete lifetime result")
    if sdk:
        require(isinstance(fingerprint, str) and re.fullmatch(r"[a-f0-9]{64}", fingerprint) is not None and h.get("sdk_sha256") == fingerprint, "SDK fingerprint mismatch")
        require((h.get("ring"), h.get("side"), h.get("copy_bytes")) == (8, 64, 262144), "SDK workload mismatch")
        require(f.get("validation_counter") == "unfiltered_callback_before_device_teardown", "Missing SDK error coverage")
        adapter = records[1]
        require(adapter.get("type") == "adapter", "Missing SDK adapter")
        body = records[2:-1]
    else:
        require(h.get("native_only") is True, "Native reproduction must exclude SDK")
        adapter = h
        baseline = records[1]
        require(baseline.get("type") == "sample" and baseline.get("phase") == "baseline" and baseline.get("cycle") == 0, "Missing native baseline")
        require(all(type(baseline.get(n)) is int and baseline[n] == 0 for n in ("resource_creations", "resource_destructions", "device_destructions", "validation_errors")), "Invalid native baseline")
        body = records[2:-1]
    require(isinstance(adapter.get("adapter"), str) and adapter["adapter"] and type(adapter.get("luid_high")) is int and type(adapter.get("luid_low")) is int, "Missing adapter identity")
    require(adapter.get("software") is (h["mode"].startswith("warp")), "Software adapter mismatch")
    phases = (["device_ready", "resources_released", "child_ready", "sdk_released_child_held", "last_alias_held", "native_device_destroyed", "settled"] if sdk else
              ["device_ready", "alias_held", "alias_released", "resources_released", "runtime_modules", "device_released", "settled"])
    require(len(body) == cycles * len(phases), "Missing cycle checkpoints")
    prior_time = -1
    for r in records[1:-1]:
        if r.get("type") != "sample":
            continue
        require(number(r.get("elapsed")) and r["elapsed"] >= prior_time, "Invalid elapsed time")
        prior_time = r["elapsed"]
        require(integer(r.get("private_bytes")) and integer(r.get("handles")) and integer(r.get("device_destructions")), "Missing process/lifetime counters")
        if not sdk:
            require(integer(r.get("resource_creations")) and integer(r.get("resource_destructions")), "Missing resource callbacks")
        for segment in ("local", "nonlocal"):
            status, value = r.get("gpu_"+segment+"_hresult"), r.get("gpu_"+segment+"_bytes")
            require(type(status) is int and -(2**31) <= status < 2**31, "Missing DXGI status")
            require((status < 0 and value is None) or (status >= 0 and integer(value)), "DXGI observation mismatch")
            require(adapter["software"] or status >= 0, "Hardware observation unavailable")
    results = []
    for cycle in range(1, cycles+1):
        group = body[(cycle-1)*len(phases):cycle*len(phases)]
        require([r.get("phase", r.get("type")) for r in group] == phases and all(r.get("cycle") == cycle for r in group), "Wrong cycle order")
        samples = [r for r in group if r.get("type") == "sample"]
        require(len(samples) == (7 if sdk else 6), "Missing sample types")
        require([r["device_destructions"] for r in samples] == [cycle-1]*(5 if sdk else 4)+[cycle]*2, "Native device destruction/alias contract failed")
        if sdk:
            before = (cycle-1)*parameter*8
            after = cycle*parameter*8
            require([r.get("frames") for r in samples] == [before]+[after]*6, "Incomplete SDK frames")
            for index, r in enumerate(samples):
                observed = index < 3
                require(r.get("sdk_statistics_available") is observed, "SDK statistics availability mismatch")
                if observed:
                    require(all(integer(r.get(n)) for n in OWNED), "Missing SDK statistics")
                    expected = [0, 0, 0, 0, 0] if index != 2 else [1, 1, r["allocation_bytes"], 0, 0]
                    require([r[n] for n in OWNED] == expected and (index != 2 or r["allocation_bytes"] >= 65536), "SDK resources not reclaimed or alias missing")
                else:
                    require(all(n not in r for n in OWNED), "Invented post-device SDK statistics")
            released = samples[1]
        else:
            before, after = (cycle-1)*(parameter*3+1), cycle*(parameter*3+1)
            require([r["resource_creations"] for r in samples] == [before, before+1, before+1, after, after, after], "Resource workload mismatch")
            require([r["resource_destructions"] for r in samples] == [before, before, before+1, after, after, after], "Native resource destruction/alias contract failed")
            require(all(type(r.get("validation_errors")) is int and r["validation_errors"] == 0 for r in samples), "Native validation error")
            require(isinstance(group[4].get("paths"), list) and group[4]["paths"] and all(isinstance(p, str) and p for p in group[4]["paths"]), "Missing loaded runtime identity")
            released = samples[3]
        settled = samples[-1]
        metrics = {name: {"before_device_release": released[name], "after_device_release": settled[name], "release_delta": released[name]-settled[name]}
                   for name in ("private_bytes", "handles", "gpu_local_bytes", "gpu_nonlocal_bytes") if released[name] is not None and settled[name] is not None}
        results.append({"cycle": cycle, "metrics": metrics})
    require(type(f.get("validation_errors")) is int and f["validation_errors"] == 0 and f.get("device_destructions") == cycles, "Missing final destruction/error result")
    if sdk:
        frames = cycles*parameter*8
        require(f.get("frames") == frames and f.get("full_images") == frames*3 and f.get("verified_buffer_bytes") == frames*262144, "Incomplete SDK reference coverage")
    else:
        expected = cycles*(parameter*3+1)
        require(f.get("resource_creations") == expected and f.get("resource_destructions") == expected, "Missing final resource destruction")
    return {"mode": h["mode"], "sdk_workload": sdk, "lifetime_contracts_passed": True, "memory_acceptance_claim": False,
            "cycles": cycles, "adapter": adapter["adapter"], "luid_high": adapter["luid_high"], "luid_low": adapter["luid_low"],
            "device_destructions": cycles, "validation_errors_before_teardown": 0, "observations": results,
            "references": {k: f[k] for k in (("frames", "full_images", "verified_buffer_bytes") if sdk else ("resource_creations", "resource_destructions"))}}


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output", type=pathlib.Path, required=True)
    p.add_argument("--cycles", type=int, default=8)
    p.add_argument("--iterations", type=int, default=256)
    p.add_argument("--batches", type=int, default=8)
    a = p.parse_args()
    if not (2 <= a.cycles <= 16 and 1 <= a.iterations <= 512 and 1 <= a.batches <= 16):
        p.error("counts outside bounded diagnostic limits")
    a.output.mkdir(parents=True, exist_ok=False)
    directory = ROOT/"out/memory-validation-probe/Release"
    sources = [pathlib.Path(__file__), ROOT/"tools/memory_validation_probe/Lifetime.cpp", ROOT/"tools/memory_validation_probe/SDKLifetime.cpp", ROOT/"tools/memory_validation_probe/CMakeLists.txt", ROOT/"tools/MemoryStress.cpp", ROOT/"tools/analyze_memory_stress.py", ROOT/"tools/audit_licenses.py"]
    inputs = [*sources, directory/"gbv_lifetime_probe.exe", directory/"sdk_lifetime_probe.exe", ROOT/"out/core/Release/GraphicsSDKCore.lib"]
    fp = sdk_fingerprint(ROOT)
    require(fp.encode() in (directory/"sdk_lifetime_probe.exe").read_bytes(), "Stale SDK lifetime executable")
    frozen = {str(path): digest(path) for path in inputs}
    system = pathlib.Path(os.environ["WINDIR"])/"System32"
    runtime = {str(path): digest(path) for path in (system/n for n in ("d3d12.dll", "D3D12Core.dll", "d3d12SDKLayers.dll", "d3d10warp.dll")) if path.is_file()}
    for path in sources:
        target = a.output/"source"/path.relative_to(ROOT)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(path.read_bytes())
    (a.output/"inputs.json").write_text(json.dumps({"SDK_source_fingerprint": fp, "frozen_inputs": frozen, "runtime_inputs": runtime, "platform": platform.platform(), "parameters": vars(a) | {"output": str(a.output)}}, indent=2)+"\n", encoding="utf-8")
    results = []
    for sdk, modes in ((False, NATIVE_MODES), (True, SDK_MODES)):
        for mode in modes:
            name = ("sdk-" if sdk else "native-")+mode
            output = (a.output/(name+".jsonl")).resolve()
            command = [str(directory/("sdk_lifetime_probe.exe" if sdk else "gbv_lifetime_probe.exe")), mode, str(a.cycles), str(a.batches if sdk else a.iterations), str(output)]
            print("Running "+name, flush=True)
            with (a.output/(name+".log")).open("xb") as log:
                process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
                try:
                    code = process.wait(timeout=240)
                except subprocess.TimeoutExpired:
                    process.kill();process.wait();raise
            require(code == 0, "Lifetime process failed: "+name)
            records = read_records(output)
            result = analyze(records, sdk, fp)
            if not sdk:
                for r in records:
                    if r.get("type") == "runtime_modules":
                        for name in r["paths"]:
                            path = pathlib.Path(name)
                            require(path.resolve().is_relative_to(system.resolve()) and any(path == pathlib.Path(n) for n in runtime), "Unfrozen runtime module")
            results.append({"command": command, "pid": process.pid, "returncode": code, **result})
            print("PASS destruction/readback contracts: "+mode, flush=True)
    require(sdk_fingerprint(ROOT) == fp and all(digest(pathlib.Path(path)) == sha for path, sha in (frozen | runtime).items()), "Frozen inputs changed")
    summary = {"SDK_source_fingerprint": fp, "memory_acceptance_claim": False, "original_texture_churn_gate_accepted": False, "cases": results}
    (a.output/"summary.json").write_text(json.dumps(summary, indent=2)+"\n", encoding="utf-8")


if __name__ == "__main__":
    main()
