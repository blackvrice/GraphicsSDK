"""Native controls for real handled/unhandled faults, quoting, timeout and overwrite refusal."""
import argparse
import hashlib
import json
import pathlib
import subprocess

from evidence import read_records, validate, require

ROOT = pathlib.Path(__file__).resolve().parents[2]


def run(output, binary):
    output.mkdir(parents=True, exist_ok=False)
    capture = binary/"gfx_crash_capture.exe"
    fixture = binary/"gfx_crash_fixture.exe"
    paths = [capture, fixture, *sorted(pathlib.Path(__file__).parent.glob("*.cpp")),
             pathlib.Path(__file__), pathlib.Path(__file__).with_name("evidence.py"), pathlib.Path(__file__).with_name("CMakeLists.txt")]
    hashes = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in paths}
    cases = []
    for mode, code in (("handled", 0), ("arguments", 0), ("exit-seven", 7), ("null-call", 0xc0000005),
                       ("worker-crash", 0xc0000005), ("thread-limit", 0xc0000005),
                       ("breakpoint", 0x80000003), ("sleep", 124), ("debug-spam", 0)):
        path = output/(mode+".jsonl")
        dump = output/(mode+".dmp")
        command = [str(capture), "--output", str(path), "--timeout-ms", "150" if mode == "sleep" else "15000"]
        if mode == "null-call":
            command += ["--dump", str(dump)]
        command += ["--", str(fixture), mode]
        if mode == "arguments":
            command += ["", "space and 한글", 'quote"here', "trailing slash\\"]
        process = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
        (output/(mode+".log")).write_bytes(process.stdout)
        records = read_records(path)
        require(records[0].get("schema") == 2, "Rebuild the current thread-snapshot collector")
        result = validate(records, code, process.returncode)
        if mode == "handled":
            require(result["first_chance_count"] >= 2 and result["second_chance_count"] == 0, "Handled exception was swallowed/reclassified")
        if mode == "arguments":
            require(any(r.get("text") == "Unicode diagnostic 한글\n" for r in read_records(path)), "Unicode debugger text length/encoding mismatch")
        if mode == "null-call":
            r = read_records(path)
            require(result["second_chance_count"] == 1 and result["context_available"] is True, "Missing real null-call context")
            require(any(e["address"] == 0 and e["module"] is None for e in result["unhandled_exceptions"]), "Null PC guessed as a module")
            require(any(e.get("kind") == "null_pc_stack_top_candidate" and e.get("module", "") and "gfx_crash_fixture" in e["module"] for e in r), "Missing labelled caller candidate")
            require(dump.read_bytes().startswith(b"MDMP"), "Minidump missing/invalid")
            result["local_minidump_sha256"] = hashlib.sha256(dump.read_bytes()).hexdigest()
        if mode == "breakpoint":
            require(result["second_chance_count"] == 1, "Application breakpoint swallowed")
        if mode in ("worker-crash", "thread-limit"):
            require(result["second_chance_count"] == 1 and result["context_available"], "Missing worker fault")
            snapshot = result["thread_snapshots"][0]
            require(snapshot["contexts_available"] == snapshot["captured_threads"], "Missing real worker contexts")
            waiters = {r["thread_id"] for r in records if r.get("event") == "stack" and "fixtureWaiter" in (r.get("symbol") or "")}
            require(len(waiters) >= (3 if mode == "worker-crash" else 13), "Missing independently waiting worker stacks")
            if mode == "worker-crash":
                require(snapshot["observed_threads"] >= 5 and snapshot["all_threads_captured"], "Incomplete small thread set")
            else:
                require(snapshot["observed_threads"] >= 26 and snapshot["captured_threads"] == 16
                        and snapshot["omitted_threads"] >= 10 and not snapshot["all_threads_captured"], "Thread cap/truncation not exercised")
        if mode == "debug-spam":
            footer = read_records(path)[-1]
            require(footer["debug_message_count"] == 1000 and 0 < footer["debug_text_bytes"] <= 65536,
                    "Debug text limit or total message receipt failed")
        cases.append({"case": mode, "command": command, "returncode": process.returncode, "result": result})
        print("PASS native control " + mode, flush=True)
    original = (output/"handled.jsonl").read_bytes()
    command = [str(capture), "--output", str(output/"handled.jsonl"), "--timeout-ms", "1000", "--", str(fixture), "handled"]
    p = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=10)
    (output/"overwrite.log").write_bytes(p.stdout)
    require(p.returncode == 125 and (output/"handled.jsonl").read_bytes() == original, "Existing capture overwritten")
    require(all(hashlib.sha256(pathlib.Path(p).read_bytes()).hexdigest() == h for p, h in hashes.items()), "Frozen native control input changed")
    (output/"summary.json").write_text(json.dumps({"cases": cases, "overwrite_refused": True, "frozen_inputs": hashes,
        "diagnostic_only": True, "memory_acceptance": False}, indent=2)+"\n", encoding="utf-8")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--binary", type=pathlib.Path, default=ROOT/"out/crash-capture/Release")
    args = parser.parse_args()
    run(args.output.resolve(), args.binary.resolve())
