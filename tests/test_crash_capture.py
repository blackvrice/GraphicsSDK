"""Synthetic fail-closed debugger receipt tests, separate from real native controls."""
import copy
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]/"tools"))
from crash_capture.evidence import validate


def evidence(crash=False):
    records = [{"event": "launch", "schema": 1, "pid": 42, "timeout_ms": 1000, "debug_only_this_process": True, "diagnostic_only": True},
               {"event": "exception", "thread_id": 7, "code": 0x80000003, "first_chance": True, "loader_breakpoint": True, "address": 1}]
    if crash:
        records += [{"event": "exception", "thread_id": 7, "code": 0xc0000005, "first_chance": True, "loader_breakpoint": False, "address": 0},
                    {"event": "exception", "thread_id": 7, "code": 0xc0000005, "first_chance": False, "loader_breakpoint": False, "address": 0},
                    {"event": "context", "thread_id": 7, "available": True, "rip": 0},
                    {"event": "stack", "thread_id": 7, "index": 0, "kind": "exception_pc", "address": 0}]
    records += [{"event": "exit", "child_exit_code": 0xc0000005 if crash else 0, "first_chance_count": 2 if crash else 1,
                 "second_chance_count": int(crash), "debug_message_count": 0, "debug_text_bytes": 0,
                 "elapsed_ms": 10, "timed_out": False, "capture_complete": True, "dump_failed": False, "diagnostic_only": True}]
    return records


def thread_evidence(observed=2):
    records = evidence(True)
    records[0].update(schema=2, thread_limit=16, frame_limit=64)
    captured = min(observed, 16)
    rows = []
    for index in range(captured):
        tid = 7 if index == 0 else 10+index
        rows += [{"event": "context" if index == 0 else "thread_context", "snapshot_id": 1,
                  "thread_id": tid, "available": True, "win32_error": 0, "rip": index, "rsp": 4096, "rbp": 4096},
                 {"event": "stack", "snapshot_id": 1, "thread_id": tid, "index": 0,
                  "kind": "exception_pc" if index == 0 else "thread_pc", "address": index}]
    rows += [{"event": "thread_snapshot", "snapshot_id": 1, "fault_thread_id": 7, "observed_threads": observed,
              "captured_threads": captured, "omitted_threads": observed-captured, "contexts_available": captured,
              "stack_frames": captured, "symbols_available": True, "all_threads_captured": captured == observed}]
    records[4:6] = rows
    return records


class CrashEvidenceTests(unittest.TestCase):
    def reject(self, mutate, crash=False):
        records = evidence(crash)
        mutate(records)
        with self.assertRaises((ValueError, KeyError)):
            validate(records, 0xc0000005 if crash else 0)

    def test_completed_and_crashed_capture_never_accept_memory(self):
        for crash in (False, True):
            result = validate(evidence(crash), 0xc0000005 if crash else 0)
            self.assertTrue(result["capture_complete"])
            self.assertFalse(result["memory_acceptance"])

    def test_truncated_capture_not_complete(self):
        self.reject(lambda r: r.pop())
        self.reject(lambda r: r[-1].update(capture_complete=False))

    def test_child_exit_and_collector_exit_must_agree(self):
        with self.assertRaisesRegex(ValueError, "exit mismatch"):
            validate(evidence(True), 0xc0000005, 0)

    def test_second_chance_and_fault_context_cannot_disappear(self):
        self.reject(lambda r: r.pop(3), True)
        self.reject(lambda r: r.pop(4), True)
        self.reject(lambda r: r.pop(5), True)

    def test_application_breakpoint_not_labelled_as_loader_event(self):
        self.reject(lambda r: r[3].update(loader_breakpoint=True), True)
        self.reject(lambda r: r[2].update(loader_breakpoint=True), True)

    def test_missing_counter_debug_text_overflow_and_dump_failure_rejected(self):
        self.reject(lambda r: r[-1].pop("debug_message_count"))
        self.reject(lambda r: r[-1].update(debug_text_bytes=65537))
        self.reject(lambda r: r[-1].update(dump_failed=True))
        self.reject(lambda r: r.insert(-1, {"event": "dump", "written": False}))

    def test_wrong_scope_or_nondiagnostic_result_rejected(self):
        self.reject(lambda r: r[0].update(debug_only_this_process=False))
        self.reject(lambda r: r[-1].update(diagnostic_only=False))

    def test_timeout_requires_matching_native_exit_and_receipt(self):
        self.reject(lambda r: r[-1].update(timed_out=True))
        records = evidence()
        records.insert(-1, {"event": "timeout", "elapsed_ms": 1000})
        records[-1].update(timed_out=True, child_exit_code=124)
        self.assertTrue(validate(records, 124, 124)["timed_out"])


class ThreadSnapshotTests(unittest.TestCase):
    def reject(self, mutate):
        records = thread_evidence()
        mutate(records)
        with self.assertRaises((ValueError, KeyError)):
            validate(records, 0xc0000005)

    def test_complete_and_explicitly_truncated_sets(self):
        for observed in (2, 26):
            result = validate(thread_evidence(observed), 0xc0000005)
            snapshot = result["thread_snapshots"][0]
            self.assertEqual(snapshot["all_threads_captured"], observed <= 16)
            self.assertFalse(result["memory_acceptance"])

    def test_unavailable_context_is_explicit_and_not_complete(self):
        records = thread_evidence()
        records[6].update(available=False, win32_error=5, rip=0, rsp=0, rbp=0)
        records.pop(7)
        records[-2].update(contexts_available=1, stack_frames=1, all_threads_captured=False)
        self.assertFalse(validate(records, 0xc0000005)["thread_snapshots"][0]["all_threads_captured"])

    def test_missing_worker_context_or_pc(self):
        self.reject(lambda r: r.pop(6))
        self.reject(lambda r: r.pop(7))

    def test_thread_and_frame_limits_cannot_be_relaxed(self):
        self.reject(lambda r: r[0].update(thread_limit=17))
        self.reject(lambda r: r[0].update(frame_limit=65))

    def test_truncation_and_false_completeness_rejected(self):
        self.reject(lambda r: r[-2].update(omitted_threads=1))
        self.reject(lambda r: r[-2].update(observed_threads=3))
        self.reject(lambda r: r[-2].update(contexts_available=1))
        records = thread_evidence(26)
        records[-2]["all_threads_captured"] = True
        with self.assertRaises(ValueError):
            validate(records, 0xc0000005)

    def test_duplicate_threads_and_fault_misclassification_rejected(self):
        self.reject(lambda r: r[6].update(thread_id=7))
        self.reject(lambda r: r[7].update(kind="exception_pc"))
        self.reject(lambda r: r[-2].update(fault_thread_id=11))

    def test_snapshot_correlation_and_order_rejected(self):
        self.reject(lambda r: r[7].update(snapshot_id=2))
        self.reject(lambda r: r.insert(2, r.pop(6)))
        self.reject(lambda r: r.insert(4, r.pop(-2)))

    def test_unbounded_or_discontinuous_stacks_rejected(self):
        self.reject(lambda r: r[7].update(index=1))
        def overflow(records):
            frames = [dict(records[7], index=i, kind="unwound") for i in range(1, 65)]
            records[-2]["stack_frames"] += 64
            records[8:8] = frames
        self.reject(overflow)

    def test_unavailable_context_cannot_have_frames(self):
        self.reject(lambda r: r[6].update(available=False, win32_error=5))

    def test_downgraded_or_orphan_receipts_rejected(self):
        self.reject(lambda r: r[0].update(schema=1))
        self.reject(lambda r: r.insert(-1, dict(r[-2])))
        records = thread_evidence()
        records[0]["schema"] = True
        with self.assertRaises(ValueError):
            validate(records, 0xc0000005)


if __name__ == "__main__":
    unittest.main()
