"""Describe complete native campaigns, preserving memory-gate failures as failures."""
import argparse
import hashlib
import json
import pathlib
import statistics
import subprocess
import sys

from analyze_memory_stress import POLICY, OWNED, analyze, read_records, require
from audit_licenses import sdk_fingerprint

ROOT = pathlib.Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--campaign", required=True, type=pathlib.Path)
    p.add_argument("--output", required=True, type=pathlib.Path)
    p.add_argument("--observed-inputs", type=pathlib.Path, help="Preserved pre-repair sources/binaries for historical observations")
    args = p.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    policy = json.loads(POLICY.read_text())
    manifest = json.loads((args.campaign/"campaign.json").read_text())
    fingerprint = manifest["SDK_source_fingerprint"]
    if args.observed_inputs:
        preserved = json.loads(args.observed_inputs.read_text())
        require(preserved["observed_SDK_source_fingerprint"] == fingerprint, "Observed source identity mismatch")
        require(digest(ROOT/preserved["source_archive"]["path"]) == preserved["source_archive"]["sha256"], "Observed source archive changed")
        copies = {str((ROOT/r["original_path"]).resolve()):r for r in preserved["frozen_input_copies"]}
        for path, sha in manifest["frozen_inputs"].items():
            r = copies[str(pathlib.Path(path).resolve())]
            require(r["sha256"] == sha and digest(ROOT/r["preserved_path"]) == sha, "Preserved input changed: " + path)
    else:
        require(sdk_fingerprint(ROOT) == fingerprint, "Stale campaign SDK; supply preserved observed inputs")
        for path, sha in manifest["frozen_inputs"].items():
            require(digest(pathlib.Path(path)) == sha, "Changed frozen input: " + path)
    results = []
    for mode in ("gpu", "validation", "warp"):
        records = read_records(args.campaign/(mode+".jsonl"))
        try:
            result = analyze(records, fingerprint, policy)
        except ValueError as error:
            # Only a measured plateau failure can be described as functional success.
            # Missing/invalid/short native evidence remains a fatal report error.
            require(str(error).startswith("Memory plateau gate failed:"), str(error))
            h, f = records[0], records[-1]
            require(f["status"] == "passed" and f["validation_errors"] == 0, "Native execution failed")
            result = {"mode": mode, "accepted": False, "gate_error": str(error), "measured_seconds": f["measured_seconds"],
                      "frames": f["frames"], "full_images": f["full_images"], "verified_buffer_bytes": f["verified_buffer_bytes"]}
            start = next(r for r in records if r.get("phase") == "measurement_start")
            drained = [r for r in records if r.get("phase") == "drained"]
            require(all(all(r[n] == 0 for n in OWNED) for r in drained), "Owned resources did not drain")
            tail = [r for r in drained if r["elapsed"] >= start["elapsed"] + h["seconds"]*(1-policy["tail_fraction"])]
            metrics, failed = {}, []
            for name, limits in policy["limits"].items():
                values = [r[name] for r in tail]
                x = [(r["elapsed"]-tail[0]["elapsed"])/60 for r in tail]
                xm, ym = statistics.mean(x), statistics.mean(values)
                slope = sum((a-xm)*(b-ym) for a,b in zip(x,values))/sum((a-xm)**2 for a in x)
                q = max(2,len(values)//4)
                drift = statistics.median(values[-q:])-statistics.median(values[:q])
                spread = max(values)-min(values)
                metrics[name] = {"supported": True, "median_drift": drift, "slope_per_minute": slope, "range": spread, "tail_checkpoint_maximum": max(values)}
                if drift>limits["median_drift"] or slope>limits["slope_per_minute"] or spread>limits["range"]:
                    failed.append(name)
            result.update(drained_samples=len(drained),tail_samples=len(tail),owned_after_drain=0,metrics=metrics,failed_metrics=failed)
        result["functional_references_passed"] = True
        result["validation_enabled"] = mode == "validation"
        result["native_error_coverage"] = "unproven: native queue overflow observed in same-source factor probes" if mode=="validation" and args.observed_inputs else "not_requested" if mode!="validation" else "check SDK source accounting model"
        result["final_private_bytes"] = records[-2]["private_bytes"]
        result["maximum_observed_live_pending_submissions"] = max(r["pending"] for r in records if r.get("phase") == "live")
        results.append(result)
    # Preserve the exact nonzero strict-gate result, independently of descriptive reporting.
    target = args.output/"strict-gate-output.json"
    command = [sys.executable,str(ROOT/"tools/analyze_memory_stress.py"),*[str(args.campaign/(m+".jsonl")) for m in ("gpu","validation","warp")],"--fingerprint",fingerprint,"--output",str(target)]
    process = subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    (args.output/"strict-gate.log").write_bytes(process.stdout)
    accepted = all(r["accepted"] for r in results)
    require((process.returncode==0) == accepted, "Strict/descriptive gate disagreement")
    result = {"accepted": accepted,"strict_gate_exit":process.returncode,"policy_sha256":digest(POLICY),"SDK_source_fingerprint":fingerprint,
              "current_SDK_source_fingerprint":sdk_fingerprint(ROOT),"observed_inputs":str(args.observed_inputs) if args.observed_inputs else None,
              "continuous_peak_claim":False,"campaigns":results,"original_runner_stopped_after_GBV_failure":not manifest.get("accepted",False),
              "WARP_completed_separately_with_unchanged_frozen_inputs":True}
    (args.output/"acceptance.json").write_text(json.dumps(result,indent=2)+"\n",encoding="utf-8")
    print("REPORT: memory acceptance="+str(accepted)+"; strict gate exit="+str(process.returncode))


if __name__ == "__main__":
    main()
