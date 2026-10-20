import importlib.util
import pathlib
import json
import tempfile
import unittest

spec = importlib.util.spec_from_file_location("bench", pathlib.Path(__file__).resolve().parents[1] / "tools/run_benchmarks.py")
bench = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bench)

class StatisticsTests(unittest.TestCase):
    def test_workload_and_readback_must_match_the_request(self):
        result = {"sdk":"diligent_core", "scenario":"draw", "count":1000, "warmup":5,
                  "validation":True, "api":"d3d12", "category":"end_to_end", "configuration":"Release",
                  "readback_verified":True, "samples":[{"frame_ms":1}]}
        bench.validate_result(result, "diligent_core", "draw", 1000, 5, 1, True)
        for change in ({"count":1}, {"readback_verified":False}, {"samples":[]}, {"sdk":"llgl"}):
            with self.subTest(change=change), self.assertRaises(RuntimeError):
                bench.validate_result({**result, **change}, "diligent_core", "draw", 1000, 5, 1, True)

    def test_diligent_fingerprint_detects_source_and_dependency_changes(self):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            lock = root/"benchmarks/competitors/diligent-build.lock.json"
            lock.parent.mkdir(parents=True)
            lock.write_text(json.dumps({"dependencies":{"diligent_core":{"commit":"first"}}}))
            source=root/"benchmarks/competitors/Diligent.cpp"; source.write_text("first adapter")
            (root/"benchmarks/CMakeLists.txt").write_text("build settings")
            before=bench.diligent_fingerprint(root)
            source.write_text("changed adapter")
            self.assertNotEqual(before, bench.diligent_fingerprint(root))
            source.write_text("first adapter")
            lock.write_text(json.dumps({"dependencies":{"diligent_core":{"commit":"second"}}}))
            self.assertNotEqual(before, bench.diligent_fingerprint(root))

    def test_tail_and_run_medians(self):
        summary = bench.summarize([{"samples": [{"frame_ms": n, "gpu_ms": None} for n in [1, 2, 3, 100]]}] * 3)
        self.assertEqual(summary["frame_ms"]["p50"], 2.5)
        self.assertEqual(summary["frame_ms"]["run_p50"], [2.5] * 3)
        self.assertEqual(summary["frame_ms"]["p99"], 100)
        self.assertIsNone(summary["gpu_ms"])

    def test_incomparable_hardware_and_validation(self):
        env = {"cpu":"test", "ram_bytes":1024, "os":"test", "luid":"1:2", "driver":"test", "software_renderer":False, "compiler":"test"}
        a = {"environment": env, "validation": False, "api":"d3d12", "category":"end_to_end", "scenario":"draw", "count":1000, "configuration":"Release", "contract":{"revision":1}}
        self.assertTrue(bench.comparable(a, a))
        self.assertFalse(bench.comparable(a, {**a, "environment": {**env, "luid": "1:3"}}))
        self.assertFalse(bench.comparable(a, {**a, "validation": True}))
        self.assertFalse(bench.comparable(a, {**a, "category": "low_level"}))
        self.assertFalse(bench.comparable(a, {**a, "contract":{"revision":2}}))
        self.assertFalse(bench.comparable(a, {**a, "environment":{}}))

    def test_noise_and_repeatability(self):
        baseline = {"p50": 10, "mad": .1, "run_p50": [9.9, 10, 10.1]}
        noisy = {"p50": 10.2, "mad": .2, "run_p50": [9.5, 10.2, 11]}
        slow = {"p50": 15, "mad": .1, "run_p50": [14.9, 15, 15.1]}
        self.assertEqual(bench.regression(baseline, noisy)["status"], "inconclusive")
        self.assertEqual(bench.regression(baseline, slow)["status"], "warning")
        self.assertEqual(bench.regression(baseline, {**slow, "run_p50": [15]})["status"], "inconclusive")

if __name__ == "__main__":
    unittest.main()
