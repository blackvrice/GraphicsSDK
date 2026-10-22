"""Synthetic public-budget receipt rejection; no measured GPU acceptance claims."""
import copy
import unittest

from test_memory_campaign import current, aborted, with_budgets, runner, analyzer, FP, MiB


class BudgetCampaignTests(unittest.TestCase):
    def reject(self, mutate, message):
        records = with_budgets(current())
        mutate(records)
        for validate in (lambda: analyzer.analyze(records, FP),
                         lambda: runner.validate_native(records, FP, "gpu", 1, 0)):
            with self.assertRaisesRegex(ValueError, message):
                validate()

    def test_current_receipts_count_both_segments_and_legacy_makes_no_claim(self):
        for mode in ("gpu", "validation", "warp"):
            records = with_budgets(current(mode))
            result = analyzer.analyze(records, FP)["public_memory_budget"]
            self.assertTrue(result["recorded"])
            for counts in result["segments"].values():
                self.assertEqual(counts["stable_native_matches"], len(records)-2)
                self.assertEqual(counts["unsupported"], 0)
            self.assertTrue(runner.validate_native(records, FP, mode, 1, 0)["public_memory_budget"]["recorded"])
        self.assertEqual(analyzer.analyze(current(), FP)["public_memory_budget"], {"recorded": False})

    def test_all_four_stable_mapping_fields_are_checked(self):
        for segment in ("local", "nonlocal"):
            for field in analyzer.BUDGET_FIELDS:
                with self.subTest(segment=segment, field=field):
                    self.reject(lambda r: r[8]["sdk_budget_"+segment].update({field: 123}), "mapping mismatch")

    def test_identity_segment_status_and_native_code_cannot_be_changed(self):
        for field, value in (("adapter_id", 2), ("adapter_id", True), ("segment", "nonlocal"),
                             ("status", "error"), ("native_code", -1), ("native_code", False)):
            with self.subTest(field=field, value=value):
                self.reject(lambda r: r[8]["sdk_budget_local"].update({field: value}), "identity/segment|Failed SDK|native code")

    def test_missing_null_negative_boolean_float_and_overflow_bytes_are_rejected(self):
        for field in analyzer.BUDGET_FIELDS:
            for value in (None, -1, True, 1.0, 2**64):
                with self.subTest(field=field, value=value):
                    self.reject(lambda r: r[8]["sdk_budget_local"].update({field: value}), "byte counter")
            self.reject(lambda r: r[8]["sdk_budget_local"].pop(field), "Missing budget byte")
        self.reject(lambda r: r[8].pop("sdk_budget_nonlocal"), "Missing SDK/native")
        self.reject(lambda r: r[8].pop("gpu_local_reservation"), "Missing budget byte")
        self.reject(lambda r: r[8]["gpu_local_after"].pop("reservation_bytes"), "Missing budget byte")

    def test_dynamic_bracket_is_counted_without_asserting_equality(self):
        records = with_budgets(current())
        records[8]["gpu_local_after"]["budget_bytes"] += 1
        records[8]["sdk_budget_local"]["budget_bytes"] += 2
        result = analyzer.analyze(records, FP)["public_memory_budget"]["segments"]["local"]
        self.assertEqual(result["unstable_native_brackets"], 1)
        self.assertEqual(result["stable_native_matches"], len(records)-3)
        for sample in records[1:-1]:
            sample["gpu_local_after"]["budget_bytes"] += 1
        with self.assertRaisesRegex(ValueError, "No stable"):
            analyzer.analyze(records, FP)

    def test_zero_and_over_budget_uint64_snapshots_are_not_clamped(self):
        records = with_budgets(current())
        for sample in records[1:-1]:
            sample["gpu_nonlocal_bytes"] = 2**64-1
            sample["gpu_nonlocal_budget"] = 0
            for container in (sample["gpu_nonlocal_after"], sample["sdk_budget_nonlocal"]):
                container.update(usage_bytes=2**64-1, budget_bytes=0)
        result = analyzer.analyze(records, FP)
        self.assertTrue(result["accepted"])

    def test_native_failure_malformed_after_and_fake_zero_cannot_pass(self):
        self.reject(lambda r: r[8]["gpu_local_after"].update(hresult=-1), "status/value")
        self.reject(lambda r: r[8]["gpu_local_after"].update(hresult=False), "HRESULT")
        self.reject(lambda r: r[8]["gpu_local_after"].update(reservation_bytes=2**64), "status/value")
        self.reject(lambda r: r[8]["sdk_budget_local"].update(status="unsupported"), "Unsupported SDK")

    def test_unsupported_warp_requires_explicit_nulls_and_matching_failure(self):
        records = with_budgets(current("warp"))
        code = -2005270524  # DXGI_ERROR_UNSUPPORTED
        for sample in records[1:-1]:
            for segment in ("local", "nonlocal"):
                sample[f"gpu_{segment}_hresult"] = code
                for suffix in analyzer.NATIVE_SUFFIXES:
                    sample[f"gpu_{segment}_{suffix}"] = None
                sample[f"gpu_{segment}_after"] = dict(hresult=code, **dict.fromkeys(analyzer.BUDGET_FIELDS))
                sample["sdk_budget_"+segment].update(status="unsupported", native_code=code, **dict.fromkeys(analyzer.BUDGET_FIELDS))
        result = analyzer.analyze(records, FP)["public_memory_budget"]
        self.assertEqual(result["segments"]["local"]["unsupported"], len(records)-2)
        for mutate in (lambda r: r[8]["sdk_budget_local"].update(usage_bytes=0),
                       lambda r: r[8]["sdk_budget_local"].pop("usage_bytes"),
                       lambda r: r[8]["sdk_budget_local"].update(native_code=-1)):
            changed = copy.deepcopy(records)
            mutate(changed)
            with self.assertRaises(ValueError):
                analyzer.analyze(changed, FP)

    def test_sdk_positive_control_cannot_be_replaced_by_native_only_success(self):
        records = with_budgets(current())
        # An unstable native bracket allows a changing value, but cannot replace
        # the separate SDK retain/release positive control.
        records[4]["gpu_local_after"]["usage_bytes"] += 1
        records[4]["sdk_budget_local"]["usage_bytes"] = 10*MiB
        with self.assertRaisesRegex(ValueError, "SDK GPU usage"):
            analyzer.analyze(records, FP)

    def test_guard_abort_still_retains_budget_receipts_without_acceptance(self):
        records = with_budgets(aborted())
        result = runner.validate_native(records, FP, "validation", 1, 2)
        self.assertFalse(result["accepted"])
        self.assertTrue(result["public_memory_budget"]["recorded"])
        with self.assertRaisesRegex(ValueError, "guard aborted"):
            analyzer.analyze(records, FP)


if __name__ == "__main__":
    unittest.main()
