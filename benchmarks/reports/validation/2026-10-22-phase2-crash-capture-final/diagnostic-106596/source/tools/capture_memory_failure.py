"""Launch one exact-adapter memory diagnostic under the own-child crash collector."""
import argparse
import hashlib
import json
import pathlib
import subprocess
import sys

from audit_licenses import ROOT, record_file, sdk_fingerprint
from run_memory_stress import adapter_argument, validate_native
from analyze_memory_stress import read_records, require
from crash_capture.evidence import validate


def run(args):
    args.output.mkdir(parents=True, exist_ok=False)
    capture = args.binary/"gfx_crash_capture.exe"
    stress = args.binary/"gfx_memory_stress_symbols.exe"
    fingerprint = sdk_fingerprint(ROOT)
    require(fingerprint.encode() in stress.read_bytes(), "Stale diagnostic SDK executable")
    paths = [capture, stress, capture.with_suffix(".pdb"), stress.with_suffix(".pdb"), ROOT/"out/core/Release/GraphicsSDKCore.lib",
             ROOT/"tools/MemoryStress.cpp", pathlib.Path(__file__), ROOT/"tools/run_memory_stress.py", ROOT/"tools/analyze_memory_stress.py",
             *sorted((ROOT/"tools/crash_capture").glob("*.cpp")), ROOT/"tools/crash_capture/CMakeLists.txt", ROOT/"tools/crash_capture/evidence.py"]
    frozen = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
    for p in paths:
        if p.suffix in (".cpp", ".py") or p.name == "CMakeLists.txt":
            target = args.output/"source"/p.relative_to(ROOT)
            target.parent.mkdir(parents=True, exist_ok=True)
            target.write_bytes(p.read_bytes())
    seconds = args.diagnostic_seconds or (900 if args.mode == "gpu" else 300)
    warmup = 1 if args.diagnostic_seconds else 30
    command = [str(capture), "--output", str(args.output/"capture.jsonl"), "--timeout-ms", str((seconds+warmup+90)*1000)]
    if args.dump:
        command += ["--dump", str(args.dump)]
    command += ["--", str(stress), "--mode", args.mode, "--adapter", str(args.adapter), "--seconds", str(seconds),
                "--warmup", str(warmup), "--interval", "5", "--max-private-growth-mib", str(args.max_private_growth_mib),
                "--output", str(args.output/"memory.jsonl")]
    result = {"SDK_source_fingerprint": fingerprint, "command": command, "frozen_inputs": frozen, "diagnostic_only": True,
              "memory_acceptance": False, "capture_complete": False, "status": "starting"}
    target = args.output/"summary.json"

    def save():
        target.write_text(json.dumps(result, indent=2)+"\n", encoding="utf-8")

    save()
    try:
        with (args.output/"native.log").open("xb") as log:
            # The collector owns a finite watchdog and only its newly created child.
            process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT, cwd=ROOT)
            result.update(collector_pid=process.pid, status="running")
            save()
            try:
                code = process.wait(timeout=seconds+warmup+150)
            except subprocess.TimeoutExpired:
                # Windows terminates this debugger's own child on debugger exit.
                process.kill()
                process.wait()
                raise ValueError("Collector exceeded its own-child watchdog deadline")
        result["collector_exit_code"] = code
        records = read_records(args.output/"capture.jsonl")
        require(records and records[-1].get("event") == "exit", "Incomplete debugger receipt")
        child_code = records[-1]["child_exit_code"]
        receipt = validate(records, child_code, code)
        result.update(capture=receipt, capture_complete=True, status="crashed" if receipt["second_chance_count"] else
                      "timed_out" if receipt["timed_out"] else "guard_aborted" if child_code == 2 else "completed" if child_code == 0 else "native_failed")
        if child_code in (0, 2) and not receipt["second_chance_count"] and not receipt["timed_out"]:
            result["references"] = validate_native(read_records(args.output/"memory.jsonl"), fingerprint, args.mode, args.adapter, child_code)
        # Disk files observed after exit; these hashes are not mapped-memory hashes.
        modules = sorted({r["path"] for r in records if r.get("event") == "module" and r.get("path")})
        result["module_files_after_exit"] = [{"path": p, "sha256": hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest()}
                                             for p in modules if pathlib.Path(p).is_file()]
        if args.dump and args.dump.exists():
            result["local_dump"] = {"path": str(args.dump), "bytes": args.dump.stat().st_size,
                                    "sha256": hashlib.sha256(args.dump.read_bytes()).hexdigest(), "redistributed": False}
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        result.update(status="capture_failed", error=str(error))
    finally:
        result["inputs_unchanged"] = sdk_fingerprint(ROOT) == fingerprint and all(p.exists() and hashlib.sha256(p.read_bytes()).hexdigest() == h for name, h in frozen.items() for p in [pathlib.Path(name)])
        if not result["inputs_unchanged"]:
            result.update(status="capture_failed", error="Frozen input changed")
        save()
    print(f"Diagnostic status={result['status']}; memory acceptance=false", flush=True)
    return 0 if result["status"] == "completed" else 1


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument("--output", type=pathlib.Path, required=True)
    p.add_argument("--binary", type=pathlib.Path, default=ROOT/"out/crash-capture/Release")
    p.add_argument("--mode", choices=("gpu", "validation", "warp"), default="validation")
    p.add_argument("--adapter", type=adapter_argument, required=True)
    p.add_argument("--diagnostic-seconds", type=int)
    p.add_argument("--max-private-growth-mib", type=int, default=512)
    p.add_argument("--dump", type=pathlib.Path, help="Optional fresh local minidump path; excluded from source/evidence archives")
    args = p.parse_args()
    if args.diagnostic_seconds is not None and not 1 <= args.diagnostic_seconds <= 120 or not 1 <= args.max_private_growth_mib <= 4096:
        p.error("diagnostic duration must be 1..120 seconds; private growth guard 1..4096 MiB")
    args.output, args.binary = args.output.resolve(), args.binary.resolve()
    if args.dump:
        args.dump = args.dump.resolve()
    try:
        return run(args)
    except (OSError, ValueError, subprocess.SubprocessError) as error:
        print("FAIL: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
