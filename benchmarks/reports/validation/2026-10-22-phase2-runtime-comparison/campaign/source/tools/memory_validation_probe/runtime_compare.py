"""Fail-closed runtime selection/counter/lifetime observations; not memory acceptance."""
import argparse
import hashlib
import json
import os
import pathlib
import platform
import subprocess
import sys

ROOT = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT/"tools"))
from analyze_memory_stress import read_records, require, number
from audit_licenses import sdk_fingerprint
from prepare_runtime import LOCK, verify_package

SOURCES = ("global", "inbox-factory", "agility")
MODES = ("hardware-cpu", "hardware-gbv", "warp-gbv")


def uint(value):
    return type(value) is int and 0 <= value < 2**64


def win(path):
    return pathlib.PureWindowsPath(path)


def analyze(records, source, mode, runtime, system, version):
    require(len(records) >= 9 and all(isinstance(r, dict) for r in records), "Missing runtime records")
    h, f = records[0], records[-1]
    require(h.get("type") == "header" and h.get("schema") == 1 and f.get("type") == "footer", "Header/footer missing")
    require(source in SOURCES and mode in MODES and h.get("source") == source and h.get("mode") == mode and h.get("native_only") is True, "Runtime profile mismatch")
    gpu, warp = mode != "hardware-cpu", mode == "warp-gbv"
    flags = h.get("device_flags")
    require(uint(flags) and flags & 1 == 1 and bool(flags & 2) == gpu and flags & 4 == 0 and h.get("gpu_validation") is gpu and h.get("gpu_validation_flags") == 0 and h.get("experimental_features") == 0, "Actual validation configuration mismatch")
    require(uint(h.get("actual_sdk_version")) and h["actual_sdk_version"] > 0, "Missing actual SDK version")
    selected = win(runtime if source == "agility" else system)
    require(h.get("requested_sdk_version") == (version if source == "agility" else 0), "Requested SDK mismatch")
    if source == "agility":
        require(h["actual_sdk_version"] == version and win(h.get("runtime_path", "")) == win(runtime), "Requested runtime not active")
    else:
        require(h.get("runtime_path") == "-", "Unexpected runtime override")
    require(win(h.get("configuration_provider", "")) == selected/"d3d12SDKLayers.dll", "Wrong configuration provider/fallback")
    modules = h.get("runtime_modules")
    require(isinstance(modules, list) and all(isinstance(p, str) and win(p).is_absolute() for p in modules), "Missing loaded modules")
    modules = [win(p) for p in modules]
    expected = {selected/"D3D12Core.dll", selected/"d3d12SDKLayers.dll", win(system)/"d3d12.dll"}
    require(len(modules) == len(set(modules)) and expected.issubset(modules) and set(modules).issubset(expected | {win(system)/"d3d10warp.dll"}), "Unselected or mixed runtime DLLs")
    require(isinstance(h.get("adapter"), str) and h["adapter"] and type(h.get("luid_high")) is int and type(h.get("luid_low")) is int and h.get("software") is warp, "Missing adapter identity")
    n = h.get("iterations")
    require(type(n) is int and 1 <= n <= 512, "Invalid workload")
    samples = records[1:-1]
    phases = ["baseline", "counter_control", "alias_held", "alias_released"]+["checkpoint"]*(n//64)+["resources_released", "device_released", "settled"]
    require([r.get("phase") for r in samples] == phases, "Missing runtime checkpoints")
    previous = -1
    for i, r in enumerate(samples):
        require(r.get("type") == "sample" and number(r.get("elapsed")) and r["elapsed"] >= previous, "Invalid runtime sample time")
        previous = r["elapsed"]
        require(all(uint(r.get(k)) for k in ("iterations", "resource_creations", "resource_destructions", "device_destructions", "validation_errors_total", "private_bytes", "handles")), "Missing runtime counters")
        require(r["validation_errors_total"] == (0 if i == 0 else 1), "Unfiltered counter control failed or unexpected error")
        for segment in ("local", "nonlocal"):
            status, value = r.get("gpu_"+segment+"_hresult"), r.get("gpu_"+segment+"_bytes")
            require(type(status) is int and -(2**31) <= status < 2**31 and ((status < 0 and value is None) or (status >= 0 and uint(value))), "DXGI status/value mismatch")
            require(warp or status >= 0, "Hardware observation unavailable")
    expected_counts = [(0,0,0,0), (0,0,0,0), (0,1,0,0), (0,1,1,0)]
    expected_counts += [(i, 1+3*i, 1+3*i, 0) for i in range(64, n+1, 64)]
    expected_counts += [(n, 1+3*n, 1+3*n, 0), (n, 1+3*n, 1+3*n, 1), (n, 1+3*n, 1+3*n, 1)]
    require([(r["iterations"], r["resource_creations"], r["resource_destructions"], r["device_destructions"]) for r in samples] == expected_counts, "Alias/resource/device destruction contract failed")
    require(f.get("functional_passed") is True and f.get("memory_acceptance_claim") is False and f.get("validation_counter") == "unfiltered_callback_before_teardown", "Incomplete runtime result")
    require(f.get("resource_creations") == f.get("resource_destructions") == 1+3*n and f.get("device_destructions") == 1, "Incomplete final lifetimes")
    require(f.get("validation_errors_total") == f.get("intentional_validation_errors") == 1 and type(f.get("unexpected_validation_errors")) is int and f["unexpected_validation_errors"] == 0, "Incomplete final error accounting")
    first, last, settled = samples[1], samples[-3], samples[-1]
    return {"source": source, "mode": mode, "actual_sdk_version": h["actual_sdk_version"], "provider": h["configuration_provider"],
            "resource_creations": f["resource_creations"], "resource_destructions": f["resource_destructions"], "device_destructions": 1,
            "intentional_validation_errors": 1, "unexpected_validation_errors": 0, "functional_contracts_passed": True,
            "memory_acceptance_claim": False, "private_growth_before_device_release": last["private_bytes"]-first["private_bytes"],
            "private_after_device_release": settled["private_bytes"],
            "nonlocal_growth_before_device_release": None if first["gpu_nonlocal_bytes"] is None or last["gpu_nonlocal_bytes"] is None else last["gpu_nonlocal_bytes"]-first["gpu_nonlocal_bytes"],
            "nonlocal_after_device_release": settled["gpu_nonlocal_bytes"],
            "adapter": h["adapter"], "luid_high": h["luid_high"], "luid_low": h["luid_low"]}


def validate_matrix(cases):
    require(len(cases) == 9 and {(c["source"], c["mode"]) for c in cases} == {(s,m) for s in SOURCES for m in MODES}, "Incomplete runtime matrix")
    for software in (False, True):
        group = [c for c in cases if (c["mode"] == "warp-gbv") == software]
        require(len({(c["adapter"], c["luid_high"], c["luid_low"]) for c in group}) == 1, "Adapter changed across runtimes")
    require(len({c["actual_sdk_version"] for c in cases if c["source"] != "agility"}) == 1, "Global/factory runtime mismatch")


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--runtime", type=pathlib.Path, required=True)
    p.add_argument("--output", type=pathlib.Path, required=True)
    p.add_argument("--iterations", type=int, default=512)
    a = p.parse_args()
    if not 1 <= a.iterations <= 512:
        p.error("iterations must be 1..512")
    runtime = a.runtime.resolve();system = pathlib.Path(os.environ["WINDIR"])/"System32"
    lock = json.loads(LOCK.read_text())
    package = runtime/"runtime.nupkg";members = verify_package(package.read_bytes(), lock)
    for name, payload in members.items():
        require((runtime/name).read_bytes() == payload, "Prepared runtime file changed: "+name)
    a.output.mkdir(parents=True, exist_ok=False)
    exe = ROOT/"out/memory-validation-probe/Release/gbv_runtime_probe.exe"
    source = [pathlib.Path(__file__), ROOT/"tools/memory_validation_probe/Runtime.cpp", ROOT/"tools/memory_validation_probe/Lifetime.cpp", ROOT/"tools/memory_validation_probe/prepare_runtime.py", LOCK,
              ROOT/"tools/memory_validation_probe/CMakeLists.txt", ROOT/"tools/memory_validation_probe/native/CMakeLists.txt", ROOT/"tools/audit_licenses.py", ROOT/"tools/analyze_memory_stress.py"]
    binaries = [exe, package, ROOT/"out/core/Release/GraphicsSDKCore.lib", *[runtime/n for n in members], *[system/n for n in ("d3d12.dll", "D3D12Core.dll", "d3d12SDKLayers.dll", "d3d10warp.dll")]]
    frozen = {str(path): digest(path) for path in [*source, *binaries]}
    fp = sdk_fingerprint(ROOT)
    for path in source:
        target = a.output/"source"/path.relative_to(ROOT);target.parent.mkdir(parents=True, exist_ok=True);target.write_bytes(path.read_bytes())
    inputs = {"SDK_source_fingerprint": fp, "frozen_inputs": frozen, "runtime_path": str(runtime), "system_path": str(system), "platform": platform.platform(), "package_version": lock["version"], "iterations": a.iterations}
    (a.output/"inputs.json").write_text(json.dumps(inputs, indent=2)+"\n", encoding="utf-8")
    cases = []
    for selected in SOURCES:
        for mode in MODES:
            name = selected+"-"+mode;output = (a.output/(name+".jsonl")).resolve()
            command = [str(exe), selected, mode, str(lock["sdk_version"] if selected == "agility" else 0), str(runtime)+"/" if selected == "agility" else "-", str(a.iterations), str(output)]
            print("Running "+name, flush=True)
            with (a.output/(name+".log")).open("xb") as log:
                process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
                try:
                    code = process.wait(timeout=120)
                except subprocess.TimeoutExpired:
                    process.kill();process.wait();raise
            require(code == 0, "Runtime process failed: "+name)
            result = analyze(read_records(output), selected, mode, str(runtime), str(system), lock["sdk_version"])
            cases.append({"command": command, "pid": process.pid, "returncode": code, **result})
            print("PASS actual runtime/counter/lifetime contracts: "+name, flush=True)
    validate_matrix(cases)
    require(sdk_fingerprint(ROOT) == fp and all(digest(pathlib.Path(p)) == h for p,h in frozen.items()), "Frozen runtime/source inputs changed")
    result = {"SDK_source_fingerprint": fp, "production_SDK_unchanged": True, "memory_acceptance_claim": False, "original_memory_gate_accepted": False, "cases": cases}
    (a.output/"summary.json").write_text(json.dumps(result, indent=2)+"\n", encoding="utf-8")


if __name__ == "__main__":
    main()
