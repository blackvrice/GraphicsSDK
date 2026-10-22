"""Validate explicit public adapter selection through the installed window consumer."""
import argparse
import json
import pathlib
import subprocess
import sys

from audit_licenses import ROOT, record_file, sdk_fingerprint
from run_presentation_validation import snapshot, validate_events


def validate_list(events, fingerprint):
    if not events or events[-1].get("event") != "adapter_list":
        raise ValueError("Missing adapter-list footer")
    entries = events[:-1]
    if not entries or events[-1]["count"] != len(entries) or events[-1]["sdk_fingerprint"] != fingerprint:
        raise ValueError("Incomplete/stale adapter enumeration")
    seen = set()
    for entry in entries:
        identity = entry["adapter_id"]
        if (entry.get("event") != "adapter" or type(identity) is not int or not 0 <= identity < 2**64
                or identity in seen or not entry["name"] or type(entry["software"]) is not bool
                or any(type(entry[key]) is not int or entry[key] < 0 for key in
                       ("vendor_id", "device_id", "dedicated_video_bytes", "shared_system_bytes"))):
            raise ValueError("Invalid/duplicate adapter metadata")
        seen.add(identity)
    return entries


def validate_selection(events, entry):
    mode = "warp" if entry["software"] else "validation"
    result = validate_events(events, mode, False)
    device = events[0]
    recovered = [event for event in events if event.get("event") == "recovered"]
    if (device["adapter_id"] != entry["adapter_id"] or result["adapter_id"] != entry["adapter_id"]
            or device["adapter"] != entry["name"] or len(recovered) != 2
            or any(event["adapter_id"] != entry["adapter_id"] for event in recovered)):
        raise ValueError("Explicit adapter selection/recovery changed identity")
    return result


def frozen(exe):
    extra = [ROOT / path for path in ("tools/run_adapter_validation.py", "tests/AdapterTests.cpp",
             "tests/consumer/adapter.cpp", "out/core/Release/gfx_adapter_tests.exe", "out/consumer/Release/adapter_consumer.exe")]
    return snapshot(exe) + [record_file(ROOT, path) for path in extra]


def run(output, exe):
    output.mkdir(parents=True, exist_ok=False)
    before = frozen(exe)
    (output / "inputs-before.json").write_text(json.dumps(before, indent=2) + "\n", encoding="utf-8")
    process = subprocess.run([str(exe), "--list-adapters"], stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=30)
    (output / "adapters.jsonl").write_bytes(process.stdout)
    (output / "adapters.log").write_bytes(process.stderr)
    if process.returncode:
        raise ValueError("Public adapter enumeration failed")
    entries = validate_list([json.loads(line) for line in process.stdout.decode("utf-8").splitlines()], sdk_fingerprint(ROOT))
    cases = []
    for index, entry in enumerate(entries):
        path = output / f"selected-{index}.jsonl"
        mode = "warp" if entry["software"] else "validation"
        command = [str(exe), "--self-test", "--mode", mode, "--adapter", str(entry["adapter_id"]),
                   "--no-vsync", "--output", str(path)]
        process = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=120)
        path.with_suffix(".log").write_bytes(process.stdout)
        if process.returncode:
            raise ValueError(f"Explicit adapter {entry['adapter_id']} failed: {process.stdout.decode(errors='replace')}")
        result = validate_selection([json.loads(line) for line in path.read_text(encoding="utf-8").splitlines()], entry)
        cases.append({"adapter": entry, "command": command, "returncode": process.returncode, "result": result,
                      "evidence": record_file(ROOT, path)})
        print(f"PASS explicit adapter {entry['adapter_id']} {entry['name']}", flush=True)
    ids = {entry["adapter_id"] for entry in entries}
    missing = 2**64-1
    while missing in ids:
        missing -= 1
    second_missing = 0
    while second_missing in ids:
        second_missing += 1
    wrong_mode = "gpu" if entries[0]["software"] else "warp"
    negative = []
    for name, args in [("missing-id", ["--adapter", str(missing)]), ("missing-low-id", ["--adapter", str(second_missing)]),
                       ("wrong-kind", ["--mode", wrong_mode, "--adapter", str(entries[0]["adapter_id"])]),
                       ("malformed-id", ["--adapter", "gpu"]), ("overflow-id", ["--adapter", str(2**64)]),
                       ("duplicate-id", ["--adapter", "0", "--adapter", "1"]),
                       ("mixed-list-frames", ["--list-adapters", "--frames", "1"]),
                       ("mixed-list-selection", ["--list-adapters", "--adapter", "0"])]:
        command = [str(exe), *args]
        process = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
        (output / ("negative-" + name + ".log")).write_bytes(process.stdout)
        if process.returncode != 1 or b"FAIL:" not in process.stdout or b'"functional_passed":true' in process.stdout:
            raise ValueError("Selection rejection failed: " + name)
        negative.append({"case": name, "command": command, "returncode": process.returncode})
    after = frozen(exe)
    (output / "inputs-after.json").write_text(json.dumps(after, indent=2) + "\n", encoding="utf-8")
    if before != after:
        raise ValueError("Adapter validation source/binary inputs changed")
    summary = {"schema_version": 1, "sdk_build_source_sha256": sdk_fingerprint(ROOT), "adapters": entries, "cases": cases,
               "negative_cases": negative, "explicit_selection_passed": True, "interactive_acceptance": False,
               "unexpected_loss_acceptance": False, "memory_acceptance": False}
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print("PASS explicit selection/recovery and eight native refusal controls", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--exe", type=pathlib.Path, default=ROOT / "out/presentation/Release/gfx_presentation.exe")
    args = parser.parse_args()
    try:
        run(args.output.resolve(), args.exe.resolve())
        return 0
    except (OSError, ValueError, KeyError, subprocess.SubprocessError) as error:
        print("FAIL: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    sys.exit(main())
