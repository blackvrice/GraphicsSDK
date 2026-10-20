import importlib.util
import pathlib
import unittest

spec = importlib.util.spec_from_file_location("bench", pathlib.Path(__file__).resolve().parents[1] / "tools/run_benchmarks.py")
bench = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bench)

class StatisticsTests(unittest.TestCase):
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
