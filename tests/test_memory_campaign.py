"""Synthetic rejection/orchestration tests; these are not measured GPU evidence."""
import argparse
import copy
import json
import pathlib
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/"tools"))
import analyze_memory_stress as analyzer
import run_memory_stress as runner
from test_memory_stress import evidence, FP, MiB


def current(mode="gpu", identity=1):
    records = evidence(mode)
    records[0].update(schema=2, adapter_id=identity, observer_adapter_id=identity,
                      requested_adapter_id=identity, software=mode == "warp", gpu_validation=mode == "validation",
                      gpu_state_tracking=mode == "validation", private_growth_limit_bytes=512*MiB,
                      guard_baseline_private_bytes=100*MiB, luid_low=identity & 0xffffffff,
                      luid_high=(identity >> 32) if identity < 2**63 else (identity >> 32)-2**32)
    records[-1].update(adapter_id=identity, stop_reason="completed")
    return records


def aborted(mode="validation"):
    records = current(mode)
    stop = copy.deepcopy(records[-2])
    stop.update(phase="guard_aborted", private_bytes=613*MiB)
    records.insert(-2, stop)
    records[-1].update(status="aborted", stop_reason="private_growth_limit")
    return records


def with_budgets(records):
    records[0]["schema"] = 3
    for sample in records[1:-1]:
        for segment in ("local", "nonlocal"):
            sample[f"gpu_{segment}_available_for_reservation"] = 2**30
            sample[f"gpu_{segment}_reservation"] = 0
            values = dict(zip(analyzer.BUDGET_FIELDS, (sample[f"gpu_{segment}_{n}"] for n in analyzer.NATIVE_SUFFIXES)))
            sample[f"gpu_{segment}_after"] = dict(hresult=0, **values)
            sample["sdk_budget_"+segment] = dict(status="ok", native_code=0, adapter_id=records[0]["adapter_id"], segment=segment, **values)
    return records


class MemoryIdentityTests(unittest.TestCase):
    def test_zero_unsigned_high_bits_and_all_modes(self):
        for mode in ("gpu", "validation", "warp"):
            for identity in (0, 1, 2**64-1, 2**63):
                r = current(mode, identity)
                self.assertEqual(analyzer.validate_identity(r[0], r[-1], identity), identity)
                self.assertEqual(analyzer.analyze(r, FP)["adapter_id"], identity)

    def test_sdk_observer_request_and_footer_cannot_disagree(self):
        for where, key in ((0, "adapter_id"), (0, "observer_adapter_id"), (0, "requested_adapter_id"), (-1, "adapter_id"), (0, "luid_low")):
            r = current()
            r[where][key] = 2
            with self.subTest(key=key), self.assertRaisesRegex(ValueError, "identity mismatch"):
                analyzer.analyze(r, FP)

    def test_missing_explicit_request_and_boolean_identity_rejected(self):
        for value in (None, False, 2):
            r = current(identity=0)
            r[0]["requested_adapter_id"] = value
            with self.subTest(value=value), self.assertRaises(ValueError):
                runner.validate_native(r, FP, "gpu", 0, 0)

    def test_kind_and_full_gbv_cannot_be_reduced(self):
        for field in ("software", "gpu_validation", "gpu_state_tracking"):
            r = current("validation")
            r[0][field] = not r[0][field]
            with self.subTest(field=field), self.assertRaisesRegex(ValueError, "validation contract"):
                analyzer.analyze(r, FP)

    def test_guard_abort_stays_rejected_even_with_full_soak_shape(self):
        r = aborted()
        self.assertFalse(runner.validate_native(r, FP, "validation", 1, 2)["accepted"])
        with self.assertRaisesRegex(ValueError, "guard aborted"):
            analyzer.analyze(r, FP)
        r[-1].update(status="passed", stop_reason="completed")
        with self.assertRaisesRegex(ValueError, "guard aborted"):
            analyzer.analyze(r, FP)
        with self.assertRaisesRegex(ValueError, "Guard receipt"):
            runner.validate_native(r, FP, "validation", 1, 0)

    def test_exit_code_threshold_and_guard_record_must_agree(self):
        r = aborted()
        for mutate in (lambda x: x[-3].update(private_bytes=100*MiB), lambda x: x.pop(-3)):
            changed = copy.deepcopy(r)
            mutate(changed)
            with self.assertRaisesRegex(ValueError, "Guard"):
                runner.validate_native(changed, FP, "validation", 1, 2)
        with self.assertRaisesRegex(ValueError, "exit/footer"):
            runner.validate_native(r, FP, "validation", 1, 0)

    def test_invalid_luid_and_missing_guard_rejected(self):
        for name, value in (("luid_high", 2**31), ("luid_low", -1), ("luid_low", 2**32),
                            ("private_growth_limit_bytes", 0), ("guard_baseline_private_bytes", None)):
            r = current()
            r[0][name] = value
            with self.subTest(name=name), self.assertRaises(ValueError):
                analyzer.analyze(r, FP)

    def test_historical_record_cannot_prove_new_explicit_request(self):
        r = evidence()
        self.assertTrue(analyzer.analyze(r, FP)["accepted"])
        with self.assertRaisesRegex(ValueError, "Historical"):
            analyzer.validate_identity(r[0], r[-1], 1)

    def test_short_diagnostics_never_accept_soak(self):
        r = current()
        r[0].update(seconds=3)
        self.assertFalse(runner.validate_native(r, FP, "gpu", 1, 0)["accepted"])
        with self.assertRaisesRegex(ValueError, "Short smoke"):
            analyzer.analyze(r, FP)

    def test_adapter_cli_rejects_sign_trailing_text_unicode_and_overflow(self):
        for value in ("-1", "+1", "1x", " 1", "1 ", "١", str(2**64), ""):
            with self.subTest(value=value), self.assertRaises(argparse.ArgumentTypeError):
                runner.adapter_argument(value)
        self.assertEqual(runner.adapter_argument("0"), 0)

    def test_campaign_cannot_mix_hardware_or_warp_ids(self):
        results = [{"mode": mode, "adapter_id": identity} for mode, identity in (("gpu", 0), ("validation", 0), ("warp", 2))]
        analyzer.validate_campaign(results)
        for index in (1, 2):
            changed = copy.deepcopy(results)
            changed[index]["adapter_id"] = 3 if index == 1 else 0
            with self.subTest(index=index), self.assertRaisesRegex(ValueError, "Mixed campaign"):
                analyzer.validate_campaign(changed)


class CampaignReceiptsTests(unittest.TestCase):
    def campaign(self, mutate=None, timeout=False, crash=False):
        with tempfile.TemporaryDirectory() as tmp:
            root = pathlib.Path(tmp)
            for relative in ("tools/MemoryStress.cpp", "tools/run_memory_stress.py", "tools/analyze_memory_stress.py", "out/core/Release/GraphicsSDKCore.lib"):
                path = root/relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text("frozen")
            policy = root/"tools/memory_stress_policy.json"
            policy.write_bytes(analyzer.POLICY.read_bytes())
            exe = root/"out/core/Release/stress.exe"
            exe.write_text(FP)
            args = argparse.Namespace(output=root/"campaign", executable=exe, mode="all", adapter=None,
                                      diagnostic_seconds=None, max_private_growth_mib=512)

            class Process:
                pid = 42

                def __init__(self, command, **kwargs):
                    mode = command[command.index("--mode")+1]
                    self.returncode = (3221225477 if crash else 2) if mode == "validation" else 0
                    if crash and mode == "validation":
                        return  # A real access violation may leave no footer or even no file.
                    records = aborted(mode) if self.returncode else current(mode)
                    if mode == "warp":
                        records = current(mode, 2)
                    records = with_budgets(records)
                    if mutate:
                        mutate(root, records, mode)
                    pathlib.Path(command[command.index("--output")+1]).write_text("".join(json.dumps(r)+"\n" for r in records))

                def wait(self, timeout=None):
                    if timeout is not None and timeout_case:
                        raise runner.subprocess.TimeoutExpired("fake", timeout)
                    return self.returncode

                def kill(self):
                    self.returncode = -1

            timeout_case = timeout
            with patch.object(runner, "ROOT", root), patch.object(runner, "__file__", str(root/"tools/run_memory_stress.py")), patch.object(runner, "POLICY", policy), patch.object(runner, "sdk_fingerprint", return_value=FP), patch.object(runner.subprocess, "check_output", return_value="base\n"), patch.object(runner.subprocess, "Popen", Process):
                code = runner.run(args)
            return code, json.loads((args.output/"campaign.json").read_text()), json.loads((args.output/"acceptance.json").read_text())

    def test_guard_failure_receipt_survives_and_warp_still_runs(self):
        code, manifest, acceptance = self.campaign()
        self.assertEqual(code, 1)
        self.assertFalse(acceptance["accepted"])
        self.assertTrue(manifest["inputs_unchanged"])
        self.assertEqual([r["mode"] for r in manifest["processes"]], ["gpu", "validation", "warp"])
        self.assertEqual(manifest["processes"][1]["returncode"], 2)
        self.assertIn("guard aborted", acceptance["errors"][0]["error"])

    def test_timeout_is_preserved_and_child_is_reaped(self):
        code, manifest, acceptance = self.campaign(timeout=True)
        self.assertEqual(code, 1)
        self.assertEqual(manifest["processes"][0]["status"], "timed_out")
        self.assertEqual(manifest["processes"][0]["returncode"], -1)
        self.assertFalse(acceptance["accepted"])

    def test_changed_input_invalidates_campaign(self):
        def mutate(root, records, mode):
            if mode == "gpu":
                (root/"tools/MemoryStress.cpp").write_text("changed")
        code, manifest, acceptance = self.campaign(mutate)
        self.assertEqual(code, 1)
        self.assertFalse(manifest["inputs_unchanged"])
        self.assertFalse(acceptance["accepted"])

    def test_native_crash_retains_exit_code_and_runs_remaining_mode(self):
        code, manifest, acceptance = self.campaign(crash=True)
        self.assertEqual(code, 1)
        self.assertEqual(manifest["processes"][1]["returncode"], 3221225477)
        self.assertIn("0xc0000005", acceptance["errors"][0]["error"])
        self.assertEqual(manifest["processes"][-1]["mode"], "warp")
        self.assertFalse(acceptance["accepted"])

    def test_old_executable_or_corrupt_budget_cannot_pass_current_runner(self):
        for mutate, message in ((lambda root, records, mode: records[0].update(schema=2), "requires public budget"),
                                (lambda root, records, mode: records[8]["sdk_budget_local"].update(adapter_id=3), "budget identity")):
            code, manifest, acceptance = self.campaign(mutate)
            self.assertEqual(code, 1)
            self.assertFalse(acceptance["accepted"])
            self.assertEqual(len(manifest["processes"]), 3)
            self.assertIn(message, acceptance["errors"][0]["error"])


if __name__ == "__main__":
    unittest.main()
