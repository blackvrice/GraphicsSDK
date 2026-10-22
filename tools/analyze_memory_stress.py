"""Fail-closed acceptance for the bounded D3D12 memory campaign (not peak analysis)."""
import argparse
import hashlib
import json
import math
import pathlib
import re
import statistics

POLICY = pathlib.Path(__file__).with_name("memory_stress_policy.json")
OWNED = ("objects", "resources", "allocation_bytes", "pending", "pending_refs")
COUNTERS = ("frames", "rounds", *OWNED, "private_bytes", "working_set_bytes", "handles")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def number(value):
    return type(value) in (int, float) and math.isfinite(value)


def unique_object(pairs):
    result = {}
    for k, v in pairs:
        require(k not in result, "Duplicate JSON field: " + k)
        result[k] = v
    return result


def read_records(path):
    data = path.read_bytes()
    require(data.endswith(b"\n"), "Truncated JSONL evidence")
    return [json.loads(line, object_pairs_hook=unique_object) for line in data.decode("utf-8").splitlines()]


def analyze(records, expected_fingerprint, policy=None):
    policy = json.loads(POLICY.read_text()) if policy is None else policy
    require(len(records) >= 10, "Missing evidence records")
    h, f = records[0], records[-1]
    require(h.get("type") == "header" and h.get("schema") == 1 and f.get("type") == "footer", "Header/footer missing")
    require(re.fullmatch(r"[a-f0-9]{64}", expected_fingerprint) is not None and h.get("sdk_sha256") == expected_fingerprint, "SDK source fingerprint mismatch")
    mode = h.get("mode")
    require(mode in policy["minimum_seconds"], "Unknown mode")
    require(type(h.get("seconds")) is int and h["seconds"] >= policy["minimum_seconds"][mode], "Short smoke is not soak acceptance")
    require(type(h.get("warmup")) is int and h["warmup"] >= policy["minimum_warmup_seconds"], "Insufficient warmup")
    require(type(h.get("interval")) is int and 1 <= h["interval"] <= 10, "Invalid sample interval")
    require(h.get("ring") == 8 and h.get("side") == 64 and h.get("copy_bytes") == 262144, "Workload contract mismatch")
    require(isinstance(h.get("adapter"), str) and h["adapter"] and type(h.get("luid_high")) is int and type(h.get("luid_low")) is int, "Missing adapter identity")
    samples = records[1:-1]
    previous = None
    for s in samples:
        require(s.get("type") == "sample" and number(s.get("elapsed")) and s["elapsed"] >= 0, "Invalid sample")
        for name in COUNTERS:
            require(type(s.get(name)) is int and s[name] >= 0, "Missing or invalid counter: " + name)
        require(s["frames"] == s["rounds"] * h["ring"], "Frame/round mismatch")
        for segment in ("local", "nonlocal"):
            result = s.get(f"gpu_{segment}_hresult")
            require(type(result) is int and -(2**31) <= result < 2**31, "Missing DXGI query status")
            for suffix in ("bytes", "budget"):
                value = s.get(f"gpu_{segment}_{suffix}")
                require((result < 0 and value is None) or (result >= 0 and type(value) is int and value >= 0), "DXGI status/value mismatch")
        if previous:
            require(s["elapsed"] >= previous["elapsed"] and s["frames"] >= previous["frames"], "Non-monotonic evidence")
        previous = s
    phases = [s["phase"] for s in samples]
    require(phases[:6] == ["baseline", "cpu_held", "cpu_released", "gpu_held", "gpu_released", "measurement_start"] and phases[-1] == "final", "Positive control/sample order mismatch")
    require(len(phases[6:-1]) % 2 == 0 and phases[6:-1] == ["live", "drained"] * (len(phases[6:-1]) // 2), "Incomplete live/drained pairs")
    baseline, cpu, cpu_released, gpu, released, start = samples[:6]
    for s in (baseline, cpu, cpu_released, released, start, samples[-1]):
        require(all(s[n] == 0 for n in OWNED), "Owned resources failed to return to baseline")
    require(cpu["private_bytes"] - baseline["private_bytes"] >= policy["cpu_control_minimum_delta_bytes"], "CPU observer positive control failed")
    require(cpu["private_bytes"] - cpu_released["private_bytes"] >= policy["cpu_control_minimum_delta_bytes"], "CPU control release not observed")
    require(gpu["objects"] == gpu["resources"] == 1 and gpu["allocation_bytes"] >= policy["gpu_control_minimum_allocation_bytes"] and gpu["pending"] == gpu["pending_refs"] == 0, "GPU ownership positive control failed")
    if mode != "warp":
        require(all(s["gpu_local_hresult"] >= 0 and s["gpu_nonlocal_hresult"] >= 0 for s in samples), "Hardware DXGI observation unavailable")
        delta = gpu["gpu_local_bytes"] - cpu_released["gpu_local_bytes"]
        release_delta = gpu["gpu_local_bytes"] - released["gpu_local_bytes"]
        require(min(delta, release_delta) >= policy["hardware_gpu_control_minimum_usage_delta_bytes"], "GPU usage retain/release control failed")
    require(start["elapsed"] - released["elapsed"] >= h["warmup"], "Warmup timing mismatch")
    require(f.get("status") == "passed" and f.get("statistics_contracts") is True and type(f.get("validation_errors")) is int and f["validation_errors"] == 0, "Missing successful native validation")
    require(number(f.get("measured_seconds")) and f["measured_seconds"] >= h["seconds"], "Incomplete measurement duration")
    require(number(f.get("elapsed")) and f["elapsed"] >= samples[-1]["elapsed"] and abs(f["elapsed"] - start["elapsed"] - f["measured_seconds"]) < 1, "Footer timing mismatch")
    require(f.get("frames") == samples[-1]["frames"] and f.get("full_images") == f["frames"] * 3 and f.get("verified_buffer_bytes") == f["frames"] * h["copy_bytes"], "Reference coverage mismatch")
    require(f["frames"] - start["frames"] >= h["seconds"] * policy["minimum_measured_frames_per_second"], "Insufficient measured work")
    drained = samples[7:-1:2]
    require(len(drained) >= h["seconds"] // h["interval"], "Missing measurement checkpoints")
    for live, drain in zip(samples[6:-1:2], drained):
        require(all(drain[n] == 0 for n in OWNED), "Drained SDK ownership is nonzero")
        require(live["resources"] >= h["ring"] * 4 and live["allocation_bytes"] >= live["resources"] * 65536, "Live resource observation did not see workload")
        require(drain["frames"] == live["frames"] + h["ring"], "Checkpoint reference verification mismatch")
    times = [start["elapsed"], *[s["elapsed"] for s in drained], samples[-1]["elapsed"]]
    require(all(b - a <= policy["maximum_sample_gap_seconds"] for a, b in zip(times, times[1:])), "Checkpoint gap exceeds policy")
    require(samples[-1]["elapsed"] - start["elapsed"] >= h["seconds"], "Final sample before campaign end")
    tail = [s for s in drained if s["elapsed"] >= start["elapsed"] + h["seconds"] * (1 - policy["tail_fraction"])]
    require(len(tail) >= 8, "Insufficient plateau observations")
    metrics = {}
    for name, limits in policy["limits"].items():
        values = [s[name] for s in tail]
        if any(v is None for v in values):
            require(mode == "warp" and name.startswith("gpu_") and all(v is None for v in values), "Intermittent missing memory metric")
            metrics[name] = {"supported": False}
            continue
        x = [(s["elapsed"] - tail[0]["elapsed"]) / 60 for s in tail]
        xm, ym = statistics.mean(x), statistics.mean(values)
        slope = sum((a-xm)*(b-ym) for a, b in zip(x, values)) / sum((a-xm)**2 for a in x)
        quarter = max(2, len(values)//4)
        drift = statistics.median(values[-quarter:]) - statistics.median(values[:quarter])
        spread = max(values) - min(values)
        require(drift <= limits["median_drift"] and slope <= limits["slope_per_minute"] and spread <= limits["range"], "Memory plateau gate failed: " + name)
        metrics[name] = {"supported": True, "median_drift": drift, "slope_per_minute": slope, "range": spread, "tail_checkpoint_maximum": max(values)}
    return {"mode": mode, "accepted": True, "measured_seconds": f["measured_seconds"], "frames": f["frames"], "full_images": f["full_images"], "verified_buffer_bytes": f["verified_buffer_bytes"], "drained_samples": len(drained), "tail_samples": len(tail), "owned_after_drain": 0, "metrics": metrics}


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("inputs", nargs="+", type=pathlib.Path)
    p.add_argument("--fingerprint", required=True)
    p.add_argument("--output", required=True, type=pathlib.Path)
    args = p.parse_args()
    results = [analyze(read_records(path), args.fingerprint) for path in args.inputs]
    require(sorted(r["mode"] for r in results) == ["gpu", "validation", "warp"], "Exactly one campaign per mode required")
    result = {"accepted": True, "policy_sha256": hashlib.sha256(POLICY.read_bytes()).hexdigest(), "SDK_source_fingerprint": args.fingerprint, "continuous_peak_claim": False, "campaigns": results}
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print("PASS: bounded memory campaign acceptance, all three modes")


if __name__ == "__main__":
    main()
