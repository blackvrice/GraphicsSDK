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


if __name__ == "__main__":
    unittest.main()
