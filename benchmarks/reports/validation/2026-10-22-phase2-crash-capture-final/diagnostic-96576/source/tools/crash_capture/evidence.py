"""Validate bounded debugger receipts. A complete capture is never memory acceptance."""
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from analyze_memory_stress import read_records, require


def validate(records, expected_exit, monitor_exit=None):
    require(len(records) >= 2 and records[0].get("event") == "launch" and records[-1].get("event") == "exit", "Incomplete capture")
    h, f = records[0], records[-1]
    require(h.get("schema") == 1 and h.get("debug_only_this_process") is True and h.get("diagnostic_only") is True,
            "Wrong debugger scope")
    require(type(h.get("pid")) is int and h["pid"] > 0 and type(h.get("timeout_ms")) is int and 1 <= h["timeout_ms"] <= 900000, "Invalid launch receipt")
    require(f.get("capture_complete") is True and f.get("diagnostic_only") is True and f.get("dump_failed") is False, "Capture/dump failed")
    require(type(f.get("child_exit_code")) is int and f["child_exit_code"] == expected_exit
            and (monitor_exit is None or monitor_exit == expected_exit), "Child/monitor exit mismatch")
    for key in ("first_chance_count", "second_chance_count", "debug_message_count", "debug_text_bytes", "elapsed_ms"):
        require(type(f.get(key)) is int and f[key] >= 0, "Missing capture counters")
    require(f["debug_text_bytes"] <= 65536 and type(f.get("timed_out")) is bool, "Unbounded debug text/invalid timeout")
    exceptions = [r for r in records if r.get("event") == "exception"]
    second = [r for r in exceptions if r.get("first_chance") is False]
    first = [r for r in exceptions if r.get("first_chance") is True]
    require(len(second) == f["second_chance_count"] and len(first) == min(f["first_chance_count"], 64), "Missing exception receipts")
    for e in exceptions:
        require(type(e.get("thread_id")) is int and e["thread_id"] > 0 and type(e.get("code")) is int
                and 0 <= e["code"] < 2**32 and type(e.get("address")) is int and e["address"] >= 0
                and type(e.get("first_chance")) is bool and type(e.get("loader_breakpoint")) is bool, "Invalid exception receipt")
        require(not e["loader_breakpoint"] or e["first_chance"] and e["code"] == 0x80000003, "Exception swallowed as loader breakpoint")
    require(sum(r["loader_breakpoint"] for r in exceptions) <= 1, "Repeated swallowed breakpoint")
    contexts = [r for r in records if r.get("event") == "context"]
    require(len(contexts) == len(second), "Missing exception context")
    for exception, context in zip(second, contexts):
        require(exception["thread_id"] == context.get("thread_id") and type(context.get("available")) is bool, "Wrong exception context thread")
        if context["available"]:
            require(any(r.get("event") == "stack" and r.get("thread_id") == context["thread_id"]
                        and r.get("index") == 0 and r.get("kind") == "exception_pc" and r.get("address") == context.get("rip") for r in records), "Missing fault-PC stack receipt")
    timeouts = [r for r in records if r.get("event") == "timeout"]
    require(len(timeouts) == int(f["timed_out"]) and (not f["timed_out"] or expected_exit == 124), "Timeout/exit disagreement")
    require(all(r.get("written") is True for r in records if r.get("event") == "dump"), "Minidump failure")
    return {"child_exit_code": expected_exit, "timed_out": f["timed_out"], "unhandled_exceptions": second,
            "first_chance_count": f["first_chance_count"], "second_chance_count": f["second_chance_count"],
            "context_available": all(r["available"] for r in contexts) if contexts else None,
            "capture_complete": True, "diagnostic_only": True, "memory_acceptance": False}
