"""Validate bounded debugger receipts. A complete capture is never memory acceptance."""
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]))
from analyze_memory_stress import read_records, require


def validate_snapshots(records, second, header):
    require(type(header.get("thread_limit")) is int and header["thread_limit"] == 16
            and type(header.get("frame_limit")) is int and header["frame_limit"] == 64, "Wrong thread/frame bounds")
    rows = [(i, r) for i, r in enumerate(records) if r.get("event") in ("context", "thread_context", "stack", "thread_snapshot")]
    require(all(type(r.get("snapshot_id")) is int and 1 <= r["snapshot_id"] <= len(second) for _, r in rows), "Orphan snapshot row")
    results = []
    for number, exception in enumerate(second, 1):
        group = [(i, r) for i, r in rows if r["snapshot_id"] == number]
        start = next(i for i, r in enumerate(records) if r is exception)
        stop = next((i for i, r in enumerate(records) if i > start and r.get("event") == "exception" and r.get("first_chance") is False), len(records)-1)
        require(group and all(start < i < stop for i, _ in group), "Snapshot outside fault interval")
        summaries = [r for _, r in group if r["event"] == "thread_snapshot"]
        require(len(summaries) == 1 and group[-1][1] is summaries[0], "Missing/duplicate/unclosed thread snapshot")
        summary = summaries[0]
        for key in ("observed_threads", "captured_threads", "omitted_threads", "contexts_available", "stack_frames"):
            require(type(summary.get(key)) is int and summary[key] >= 0, "Invalid snapshot counters")
        observed, captured = summary["observed_threads"], summary["captured_threads"]
        require(observed >= 1 and captured == min(observed, header["thread_limit"])
                and summary["omitted_threads"] == observed-captured, "Thread selection/truncation mismatch")
        contexts = [r for _, r in group if r["event"] in ("context", "thread_context")]
        require(len(contexts) == captured and contexts[0]["event"] == "context"
                and contexts[0]["thread_id"] == summary.get("fault_thread_id") == exception["thread_id"]
                and all(c["event"] == "thread_context" for c in contexts[1:]), "Fault thread missing/not first")
        ids = [c.get("thread_id") for c in contexts]
        require(all(type(t) is int and 0 < t < 2**32 for t in ids) and len(set(ids)) == captured
                and ids[1:] == sorted(ids[1:]), "Duplicate/invalid/unsorted snapshot thread")
        frames = [r for _, r in group if r["event"] == "stack"]
        require(all(r.get("thread_id") in ids and type(r.get("thread_id")) is int for r in frames), "Stack belongs to unselected thread")
        for context in contexts:
            require(type(context.get("available")) is bool and type(context.get("win32_error")) is int
                    and 0 <= context["win32_error"] < 2**32, "Missing context availability/error")
            for key in ("rip", "rsp", "rbp"):
                require(type(context.get(key)) is int and 0 <= context[key] < 2**64, "Invalid context registers")
            stack = [r for r in frames if r["thread_id"] == context["thread_id"]]
            require((1 <= len(stack) <= header["frame_limit"]) if context["available"] else not stack, "Missing/unbounded/unavailable thread stack")
            if not context["available"]:
                continue
            require(context["win32_error"] == 0, "Available context has an error")
            require(stack[0].get("address") == context["rip"] and stack[0].get("kind") ==
                    ("exception_pc" if context["event"] == "context" else "thread_pc"), "Wrong initial thread PC")
            for index, frame in enumerate(stack):
                require(type(frame.get("index")) is int and frame["index"] == index
                        and type(frame.get("address")) is int and 0 <= frame["address"] < 2**64, "Invalid/discontinuous stack frames")
                if index:
                    require(frame.get("kind") == "unwound" or index == 1 and context["event"] == "context"
                            and context["rip"] == 0 and frame.get("kind") == "null_pc_stack_top_candidate", "Mislabelled stack candidate")
        available = sum(c["available"] for c in contexts)
        require(summary["contexts_available"] == available and summary["stack_frames"] == len(frames)
                and type(summary.get("symbols_available")) is bool
                and type(summary.get("all_threads_captured")) is bool
                and summary["all_threads_captured"] == (observed == captured == available), "Snapshot completeness mismatch")
        results.append(summary)
    return results


def validate(records, expected_exit, monitor_exit=None):
    require(len(records) >= 2 and records[0].get("event") == "launch" and records[-1].get("event") == "exit", "Incomplete capture")
    h, f = records[0], records[-1]
    require(type(h.get("schema")) is int and h["schema"] in (1, 2) and h.get("debug_only_this_process") is True and h.get("diagnostic_only") is True,
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
    if h["schema"] == 1:
        require(not any(r.get("event") in ("thread_snapshot", "thread_context") or "snapshot_id" in r for r in records), "Downgraded snapshot schema")
    snapshots = validate_snapshots(records, second, h) if h["schema"] == 2 else None
    result = {"child_exit_code": expected_exit, "timed_out": f["timed_out"], "unhandled_exceptions": second,
            "first_chance_count": f["first_chance_count"], "second_chance_count": f["second_chance_count"],
            "context_available": all(r["available"] for r in contexts) if contexts else None,
            "capture_complete": True, "diagnostic_only": True, "memory_acceptance": False}
    if snapshots is not None:
        result["thread_snapshots"] = snapshots
    return result
