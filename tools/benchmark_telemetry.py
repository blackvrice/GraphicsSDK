"""Contracts for opt-in Win32 process CPU/memory and DXGI checkpoint observations."""
import math
import statistics

SCOPE = "measured_batch_after_warmup_before_post_readback_v1"
NORMALIZATION = "one_logical_cpu_100_percent_all_process_threads"
MEMORY_FIELDS = ("working_set_bytes", "private_commit_bytes", "lifetime_peak_working_set_bytes",
                 "lifetime_peak_commit_bytes", "gpu_local_current_bytes", "gpu_nonlocal_current_bytes",
                 "gpu_local_budget_bytes", "gpu_nonlocal_budget_bytes")


def contract():
    return {"revision": 1, "scope": SCOPE, "cpu_normalization": NORMALIZATION,
            "cpu_source": "GetProcessTimes_kernel_plus_user_100ns_units",
            "cpu_memory": "GetProcessMemoryInfo_process_lifetime_peaks_and_checkpoint_current",
            "gpu_memory": "DXGI_QueryVideoMemoryInfo_process_current_usage_node_0_local_nonlocal",
            "checkpoints": "baseline_then_allocation_live_if_applicable_then_each_sample_end",
            "observer_cost": "included_in_batch_cpu_and_wall_allocation_frame_includes_live_checkpoint",
            "gpu_peak": "maximum_of_checkpoints_lower_bound_not_continuous_peak"}


def validate(result, enabled):
    t = result.get("telemetry")
    if not enabled:
        if t is not None:
            raise RuntimeError("Unexpected telemetry instrumentation")
        return
    if not isinstance(t, dict) or type(t.get("revision")) is not int or t["revision"] != 1 or t.get("scope") != SCOPE or t.get("cpu_normalization") != NORMALIZATION:
        raise RuntimeError("Missing/different telemetry contract")
    if t.get("gpu_luid") != result["environment"]["luid"] or type(t.get("gpu_node")) is not int or t["gpu_node"] != 0:
        raise RuntimeError("Telemetry GPU identity differs from workload")
    for key in ("wall_ms", "kernel_ms", "user_ms", "cpu_ms", "cpu_one_core_percent"):
        value = t.get(key)
        if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or value < 0:
            raise RuntimeError("Invalid telemetry CPU counter")
    if t["wall_ms"] <= 0 or not math.isclose(t["kernel_ms"] + t["user_ms"], t["cpu_ms"], rel_tol=1e-9, abs_tol=1e-9):
        raise RuntimeError("Invalid telemetry CPU interval")
    if not math.isclose(t["cpu_ms"] / t["wall_ms"] * 100, t["cpu_one_core_percent"], rel_tol=1e-9, abs_tol=1e-9):
        raise RuntimeError("Invalid telemetry CPU normalization")
    expected = [("baseline", 0)]
    for sample in range(len(result["samples"])):
        if result["scenario"] in ("buffers", "textures"):
            expected.append(("allocation_live", sample))
        expected.append(("sample_end", sample))
    points = t.get("checkpoints")
    if not isinstance(points, list) or len(points) != len(expected):
        raise RuntimeError("Missing telemetry checkpoints")
    previous = (0, 0)
    for point, (phase, index) in zip(points, expected):
        if not isinstance(point, dict) or point.get("phase") != phase or type(point.get("sample")) is not int or point["sample"] != index:
            raise RuntimeError("Wrong telemetry checkpoint order/sample")
        if any(type(point.get(key)) is not int or point[key] < 0 for key in MEMORY_FIELDS):
            raise RuntimeError("Missing/invalid telemetry memory observation")
        peaks = (point["lifetime_peak_working_set_bytes"], point["lifetime_peak_commit_bytes"])
        if any(a < b for a, b in zip(peaks, previous)) or peaks[0] < point["working_set_bytes"] or peaks[1] < point["private_commit_bytes"]:
            raise RuntimeError("Invalid process lifetime memory peak")
        previous = peaks


def metrics(result):
    """One observation per process, never fabricate frame-level CPU percentages."""
    t = result["telemetry"]
    values = {k: t[k] for k in ("wall_ms", "kernel_ms", "user_ms", "cpu_ms", "cpu_one_core_percent")}
    for key in MEMORY_FIELDS:
        # Budget is an OS target, not memory consumed. Preserve its range separately.
        if "budget" in key:
            values["min_" + key] = min(p[key] for p in t["checkpoints"])
        values[("max_" if "budget" in key else "observed_max_" if not key.startswith("lifetime_") else "") + key] = max(p[key] for p in t["checkpoints"])
    return values


def summarize(runs):
    values = [metrics(r) for r in runs]
    return {key: {"median": statistics.median(row[key] for row in values),
                  "min": min(row[key] for row in values), "max": max(row[key] for row in values),
                  "run_values": [row[key] for row in values], "process_count": len(values)}
            for key in values[0]}
