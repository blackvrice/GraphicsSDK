"""Run the installed Win32 sample in six fresh, serial processes; retain raw evidence.

This accepts only the automated lifecycle contract. Display/compositor quality and
unexpected TDR/hot-unplug acceptance cannot be granted by this tool.
"""
import argparse
import copy
import json
import pathlib
import subprocess
import sys

from audit_licenses import ARTIFACTS, ROOT, record_file, sdk_fingerprint


def validate_events(events, mode, vsync):
    def require(condition, message):
        if not condition:
            raise ValueError(message)

    def one(name):
        values = [event for event in events if event.get("event") == name]
        require(len(values) == 1, f"Expected exactly one {name} event")
        return values[0]

    device, result, shutdown = one("device"), one("result"), one("shutdown")
    require(events[0] is device and events[-1] is result, "Incomplete or trailing process output")
    require(device["warp"] is (mode == "warp") and device["gbv"] is (mode == "validation"), "Wrong mode")
    require(device["vsync"] is vsync and bool(device["adapter"]), "Wrong VSync/adapter")
    require(result["functional_passed"] is True and result["interactive_acceptance"] is False
            and result["unexpected_loss_acceptance"] is False, "Invalid acceptance scope")
    require(result["per_monitor_v2"] is True, "DPI manifest missing")
    require(result["validation_errors"] == 0 if mode == "validation" else result["validation_errors"] is None,
            "Missing/error validation coverage")
    require(result["frames"] == result["full_images"] == 41, "Incomplete image sequence")
    require(result["resizes"] == 9 and result["minimize_restore_cycles"] == 9
            and result["paused_checks"] == 26 and result["recovery_cycles"] == 2, "Incomplete lifecycle coverage")
    require(all(shutdown[key] == 0 for key in ("sdk_objects", "sdk_resources", "pending_submissions"))
            and shutdown["hwnd_alive"] is True, "Incorrect shutdown order or retained resources")
    images = [event for event in events if event.get("event") == "image"]
    require(len(images) == 41 and [image["frame"] for image in images] == list(range(41)), "Missing/repeated image")
    require(all(image["width"] > 0 and image["height"] > 0 and image["row_pitch"] >= image["width"] * 4
                and image["row_pitch"] % 256 == 0 for image in images), "Invalid readback footprint")
    require(any(image["row_pitch"] > image["width"] * 4 for image in images), "Padded readback not exercised")
    require(sum(image["width"] * image["height"] for image in images) == result["verified_pixels"], "Pixel coverage mismatch")
    expected = [(w, h) for w, h in [(321,181),(641,359),(127,95),(803,451)] * 2 for _ in range(4)]
    require([(image["width"], image["height"]) for image in images[2:34]] == expected, "Wrong resize/restore extents")
    require(all((image["width"], image["height"]) == (639,357) for image in images[34:]), "Stale recovery extent")
    cycles = [event for event in events if event.get("event") == "minimize_restore"]
    require([event["cycle"] for event in cycles] == list(range(8)) and all(event["paused_submissions"] == 0 for event in cycles),
            "Incomplete minimize/restore sequence")
    losses = [event for event in events if event.get("event") == "observed_loss"]
    require(len(losses) == 2 and all(isinstance(event["native_reason"], int) and event["native_reason"] < 0 for event in losses),
            "Missing native loss reasons")
    coalesced = one("coalesced_resize")
    require(coalesced["events"] == 3 and coalesced["gpu_resizes"] == 1, "Uncoalesced resize")
    minimized = one("minimized_loss_restore")
    require(minimized["paused_submissions"] == 0 and minimized["recovery_cycles"] == 1, "Invalid paused recovery")
    require(one("hide_restore")["paused_submissions"] == 0, "Rendered while hidden")
    return copy.deepcopy(result)


def snapshot(exe):
    sources = list((ROOT / "include").rglob("*.hpp")) + list((ROOT / "src").rglob("*.cpp"))
    sources += [ROOT / "CMakeLists.txt", ROOT / "tools/run_presentation_validation.py", ROOT / "tools/audit_licenses.py"]
    sources += [ROOT / "examples/presentation" / name for name in ("CMakeLists.txt", "Windowed.cpp", "app.manifest")]
    sources += [ROOT / ARTIFACTS["core"], ROOT / "out/install/lib/GraphicsSDKCore.lib", exe]
    sources += list((ROOT / "out/install/include/graphics").glob("*.hpp"))
    return [record_file(ROOT, path) for path in sorted(set(sources))]


def run(output, exe):
    output.mkdir(parents=True, exist_ok=False)
    before = snapshot(exe)
    (output / "inputs-before.json").write_text(json.dumps(before, indent=2) + "\n", encoding="utf-8")
    cases = []
    for mode in ("gpu", "validation", "warp"):
        for vsync in (True, False):
            name = mode + ("-vsync" if vsync else "-immediate")
            events_path = output / (name + ".jsonl")
            command = [str(exe), "--self-test", "--mode", mode, "--output", str(events_path)]
            if not vsync:
                command.append("--no-vsync")
            process = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=90)
            (output / (name + ".log")).write_bytes(process.stdout)
            if process.returncode:
                raise ValueError(f"{name} failed with exit {process.returncode}: {process.stdout.decode(errors='replace')}")
            events = [json.loads(line) for line in events_path.read_text(encoding="utf-8").splitlines()]
            result = validate_events(events, mode, vsync)
            cases.append({"case": name, "command": command, "returncode": process.returncode, "result": result,
                          "evidence": record_file(ROOT, events_path)})
            print("PASS " + name, flush=True)
    # Fail closed before opening a window; protect earlier output bytes.
    sentinel = output / "existing-output.jsonl"
    sentinel.write_bytes(b"preserve earlier observations\n")
    negative = []
    for name, args in [("bad-mode", ["--mode", "unknown"]), ("bad-count", ["--frames", "0"]),
                       ("mixed-contract", ["--self-test", "--frames", "2"]), ("existing-output", ["--output", str(sentinel)])]:
        command = [str(exe), *args]
        process = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=15)
        (output / ("negative-" + name + ".log")).write_bytes(process.stdout)
        if process.returncode != 1 or b"FAIL:" not in process.stdout or b'"functional_passed":true' in process.stdout:
            raise ValueError("Invalid native rejection: " + name)
        negative.append({"case": name, "command": command, "returncode": process.returncode})
    if sentinel.read_bytes() != b"preserve earlier observations\n":
        raise ValueError("Native consumer overwrote previous output")
    after = snapshot(exe)
    (output / "inputs-after.json").write_text(json.dumps(after, indent=2) + "\n", encoding="utf-8")
    if before != after:
        raise ValueError("Source/SDK/consumer inputs changed during validation")
    summary = {"schema_version": 1, "sdk_build_source_sha256": sdk_fingerprint(ROOT), "cases": cases,
               "negative_cases": negative, "automated_lifecycle_passed": True,
               "interactive_acceptance": False, "unexpected_loss_acceptance": False, "actual_dpi_transition_tested": False}
    (output / "summary.json").write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    print("PASS six lifecycle processes and four native rejection controls", flush=True)


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
