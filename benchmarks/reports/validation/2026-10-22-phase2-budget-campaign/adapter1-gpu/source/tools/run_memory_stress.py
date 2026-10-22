"""Run serial, frozen memory campaigns; preserve explicit rejection and abort receipts."""
import argparse
import datetime
import hashlib
import json
import pathlib
import subprocess
import sys
import time

from audit_licenses import sdk_fingerprint
from analyze_memory_stress import POLICY, OWNED, analyze, read_records, require, validate_identity, validate_campaign, validate_budget_samples

ROOT = pathlib.Path(__file__).resolve().parents[1]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def adapter_argument(text):
    if not text.isascii() or not text.isdecimal() or int(text) >= 2**64:
        raise argparse.ArgumentTypeError("adapter must be a uint64 decimal ID from fresh enumeration")
    return int(text)


def validate_native(records, fingerprint, mode, adapter, code):
    require(len(records) >= 4 and records[0].get("type") == "header" and records[-1].get("type") == "footer", "Incomplete native receipts")
    h, f = records[0], records[-1]
    require(type(h.get("schema")) is int and h["schema"] in (2, 3) and h.get("mode") == mode and h.get("sdk_sha256") == fingerprint, "Stale/wrong native workload")
    identity = validate_identity(h, f, adapter)
    budget_receipts = validate_budget_samples(records)
    require(code in (0, 2) and f.get("status") == ("passed" if code == 0 else "aborted")
            and f.get("stop_reason") == ("completed" if code == 0 else "private_growth_limit"), "Native exit/footer disagreement")
    require(type(f.get("frames")) is int and f["frames"] > 0 and f.get("full_images") == f["frames"]*3
            and f.get("verified_buffer_bytes") == f["frames"]*262144, "Incomplete native reference coverage")
    require(f.get("statistics_contracts") is True and type(f.get("validation_errors")) is int and f["validation_errors"] == 0, "Native statistics/validation failed")
    samples = records[1:-1]
    require(all(s.get("type") == "sample" for s in samples) and samples[-1].get("phase") == "final", "Missing final native checkpoint")
    for sample in samples:
        if sample.get("phase") in ("drained", "final", "guard_aborted"):
            require(all(type(sample.get(n)) is int and sample[n] == 0 for n in OWNED), "Native ownership did not drain")
    stops = [s for s in samples if s.get("phase") == "guard_aborted"]
    require(len(stops) == (1 if code == 2 else 0), "Guard receipt missing or contradictory")
    if stops:
        require(type(stops[0].get("private_bytes")) is int
                and stops[0]["private_bytes"] - h["guard_baseline_private_bytes"] > h["private_growth_limit_bytes"], "Guard threshold not observed")
    return {"mode": mode, "adapter_id": identity, "frames": f["frames"], "full_images": f["full_images"],
            "verified_buffer_bytes": f["verified_buffer_bytes"], "functional_references_passed": True,
            "validation_errors": 0, "status": f["status"], "accepted": False, "public_memory_budget": budget_receipts}


def run(args):
    args.output.mkdir(parents=True, exist_ok=False)
    exe = args.executable.resolve()
    fingerprint = sdk_fingerprint(ROOT)
    require(fingerprint.encode() in exe.read_bytes(), "Stress executable is stale")
    paths = [exe, POLICY, pathlib.Path(__file__), ROOT/"tools/MemoryStress.cpp", ROOT/"tools/analyze_memory_stress.py", ROOT/"out/core/Release/GraphicsSDKCore.lib"]
    frozen = {str(path.resolve()): digest(path) for path in paths}
    for path in paths:
        if path.suffix in (".cpp", ".py", ".json"):
            target = args.output/"source"/path.resolve().relative_to(ROOT)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(path.read_bytes())
    manifest = {"started_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(), "SDK_source_fingerprint": fingerprint,
                "base_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
                "serialized": True, "frozen_inputs": frozen, "processes": [], "accepted": False,
                "diagnostic_only": args.diagnostic_seconds is not None, "continuous_peak_claim": False}
    results, errors = [], []
    policy = json.loads(POLICY.read_text())
    policy_digest = digest(POLICY)

    def unchanged():
        return sdk_fingerprint(ROOT) == fingerprint and all(digest(pathlib.Path(p)) == sha for p, sha in frozen.items())

    def save():
        (args.output/"campaign.json").write_text(json.dumps(manifest, indent=2)+"\n", encoding="utf-8")
        receipt = {"accepted": manifest["accepted"], "diagnostic_only": manifest["diagnostic_only"],
                   "SDK_source_fingerprint": fingerprint, "policy_sha256": policy_digest,
                   "continuous_peak_claim": False, "campaigns": results, "errors": errors}
        (args.output/"acceptance.json").write_text(json.dumps(receipt, indent=2)+"\n", encoding="utf-8")

    save()
    try:
        for mode in (policy["minimum_seconds"] if args.mode == "all" else [args.mode]):
            require(unchanged(), "Frozen campaign input changed")
            seconds = args.diagnostic_seconds or policy["minimum_seconds"][mode]
            warmup = 1 if args.diagnostic_seconds else policy["minimum_warmup_seconds"]
            command = [str(exe), "--mode", mode, "--seconds", str(seconds), "--warmup", str(warmup),
                       "--interval", "1" if args.diagnostic_seconds else "5", "--max-private-growth-mib", str(args.max_private_growth_mib),
                       "--output", str((args.output/(mode+".jsonl")).resolve())]
            if args.adapter is not None:
                command += ["--adapter", str(args.adapter)]
            receipt = {"mode": mode, "command": command, "status": "starting"}
            manifest["processes"].append(receipt)
            save()
            started = time.monotonic()
            with (args.output/(mode+".log")).open("xb") as log:
                process = subprocess.Popen(command, cwd=ROOT, stdout=log, stderr=subprocess.STDOUT)
                receipt.update(pid=process.pid, status="running")
                save()
                print(f"Started {mode}: pid={process.pid}, {seconds}s + {warmup}s warmup", flush=True)
                try:
                    code = process.wait(timeout=seconds+warmup+120)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait()
                    receipt.update(returncode=process.returncode, status="timed_out")
                    raise ValueError("Native stress timed out: " + mode)
            receipt.update(returncode=code, wall_seconds=time.monotonic()-started, status="exited")
            save()
            try:
                require(code in (0, 2), f"Native process failed: {mode}, exit={code} (0x{code & 0xffffffff:08x})")
                records = read_records(args.output/(mode+".jsonl"))
                require(records and records[0].get("schema") == 3, "Current runner requires public budget receipts; rebuild stress executable")
                result = validate_native(records, fingerprint, mode, args.adapter, code)
                results.append(result)
                if code == 2:
                    raise ValueError("Private growth guard aborted: " + mode)
                if not args.diagnostic_seconds:
                    result.update(analyze(records, fingerprint, policy))
                print(f"PASS references: {mode}, {result['frames']} frames; memory acceptance={result['accepted']}", flush=True)
            except (ValueError, KeyError, OSError) as error:
                errors.append({"mode": mode, "error": str(error)})
                print("REJECT: " + str(error), flush=True)
            save()
        require(unchanged(), "Frozen campaign input changed")
        if args.mode == "all" and len(results) == 3:
            validate_campaign(results)
        manifest["accepted"] = not args.diagnostic_seconds and not errors and args.mode == "all" and len(results) == 3 and all(r["accepted"] for r in results)
        manifest["completed_utc"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        errors.append({"error": str(error)})
    finally:
        try:
            manifest["inputs_unchanged"] = unchanged()
        except OSError:
            manifest["inputs_unchanged"] = False
        if not manifest["inputs_unchanged"]:
            manifest["accepted"] = False
            errors.append({"error": "Frozen campaign input changed"})
        save()
    return 1 if errors else 0


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--executable", type=pathlib.Path, default=ROOT/"out/core/Release/gfx_memory_stress.exe")
    p.add_argument("--output", required=True, type=pathlib.Path)
    p.add_argument("--mode", choices=("all", "gpu", "validation", "warp"), default="all")
    p.add_argument("--adapter", type=adapter_argument)
    p.add_argument("--max-private-growth-mib", type=int, default=512)
    p.add_argument("--diagnostic-seconds", type=int, help="Short reference/identity check only; cannot accept memory gate")
    args = p.parse_args()
    if args.adapter is not None and args.mode == "all":
        p.error("An explicit adapter requires one mode; hardware and WARP have different IDs")
    if not 1 <= args.max_private_growth_mib <= 4096 or args.diagnostic_seconds is not None and not 1 <= args.diagnostic_seconds <= 120:
        p.error("growth guard must be 1..4096 MiB; diagnostic duration must be 1..120 seconds")
    try:
        return run(args)
    except (ValueError, OSError, subprocess.SubprocessError) as error:
        print("FAIL: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
