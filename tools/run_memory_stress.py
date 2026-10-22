"""Run the frozen three-process memory campaign sequentially. No other GPU work during this run."""
import argparse
import datetime
import hashlib
import json
import pathlib
import subprocess
import sys
import time

from audit_licenses import sdk_fingerprint
from analyze_memory_stress import POLICY, analyze, read_records, require

ROOT = pathlib.Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--executable", type=pathlib.Path, default=ROOT/"out/core/Release/gfx_memory_stress.exe")
    p.add_argument("--output", required=True, type=pathlib.Path)
    args = p.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    exe = args.executable.resolve()
    fingerprint = sdk_fingerprint(ROOT)
    require(fingerprint.encode() in exe.read_bytes(), "Stress executable is stale")
    frozen_paths = [exe, POLICY, pathlib.Path(__file__), ROOT/"tools/MemoryStress.cpp", ROOT/"tools/analyze_memory_stress.py", ROOT/"out/core/Release/GraphicsSDKCore.lib"]
    frozen = {str(path.resolve()): digest(path) for path in frozen_paths}
    manifest = {"started_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(), "SDK_source_fingerprint": fingerprint,
                "base_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
                "serialized": True, "frozen_inputs": frozen, "processes": []}
    target = args.output/"campaign.json"
    target.write_text(json.dumps(manifest, indent=2)+"\n", encoding="utf-8")
    policy = json.loads(POLICY.read_text())
    results = []
    for mode, seconds in policy["minimum_seconds"].items():
        require(sdk_fingerprint(ROOT) == fingerprint and all(digest(pathlib.Path(path)) == sha for path, sha in frozen.items()), "Frozen campaign input changed")
        command = [str(exe), "--mode", mode, "--seconds", str(seconds), "--warmup", str(policy["minimum_warmup_seconds"]), "--interval", "5", "--output", str((args.output/(mode+".jsonl")).resolve())]
        started = time.monotonic()
        with (args.output/(mode+".log")).open("xb") as log:
            process = subprocess.Popen(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
            print(f"Started {mode}: pid={process.pid}, {seconds}s measurement + 30s warmup", flush=True)
            try:
                code = process.wait(timeout=seconds+180)
            except subprocess.TimeoutExpired:
                process.kill();process.wait();raise
        manifest["processes"].append({"mode": mode, "pid": process.pid, "command": command, "returncode": code, "wall_seconds": time.monotonic()-started})
        target.write_text(json.dumps(manifest, indent=2)+"\n", encoding="utf-8")
        require(code == 0, "Native stress failed: " + mode)
        results.append(analyze(read_records(args.output/(mode+".jsonl")), fingerprint, policy))
        print(f"PASS: {mode}, {results[-1]['frames']} frames", flush=True)
    require(sdk_fingerprint(ROOT) == fingerprint and all(digest(pathlib.Path(path)) == sha for path, sha in frozen.items()), "Frozen campaign input changed")
    manifest["completed_utc"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    manifest["accepted"] = True
    target.write_text(json.dumps(manifest, indent=2)+"\n", encoding="utf-8")
    acceptance = {"accepted": True, "policy_sha256": digest(POLICY), "SDK_source_fingerprint": fingerprint, "continuous_peak_claim": False, "campaigns": results}
    (args.output/"acceptance.json").write_text(json.dumps(acceptance, indent=2)+"\n", encoding="utf-8")
    print("PASS: all three bounded stress campaigns", flush=True)


if __name__ == "__main__":
    main()
