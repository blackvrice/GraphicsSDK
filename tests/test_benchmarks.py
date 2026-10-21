import importlib.util
import pathlib
import json
import tempfile
import subprocess
import sys
import unittest

spec = importlib.util.spec_from_file_location("bench", pathlib.Path(__file__).resolve().parents[1] / "tools/run_benchmarks.py")
bench = importlib.util.module_from_spec(spec)
spec.loader.exec_module(bench)

class StatisticsTests(unittest.TestCase):
    def test_bgfx_pool_policy_cannot_enter_default_allocation_comparison(self):
        standard={"sdk":"graphicssdk","scenario":"buffers","allocation_model":"default_gpu_resources"}
        pooled={"sdk":"bgfx","scenario":"buffers","allocation_model":"bgfx_pooled_dynamic_vertex_buffers"}
        self.assertNotEqual(bench.allocation_policy(standard),bench.allocation_policy(pooled))
        with self.assertRaises(RuntimeError):
            bench.allocation_policy({"sdk":"bgfx","scenario":"buffers"})
        with self.assertRaises(RuntimeError):
            bench.allocation_policy({**pooled,"scenario":"textures"})
        self.assertEqual(bench.allocation_policy({**standard,"sdk":"bgfx","scenario":"textures"}),bench.allocation_policy(standard))
        env={"cpu":"same","ram_bytes":1024,"os":"same","luid":"1:2","driver":"same","software_renderer":False,"compiler":"same"}
        common={"api":"d3d12","category":"end_to_end","scenario":"buffers","count":1000,"validation":False,"configuration":"Release","environment":env}
        a={**common,"contract":{"resource_policy":bench.allocation_policy(standard)}}
        b={**common,"contract":{"resource_policy":bench.allocation_policy(pooled)}}
        self.assertFalse(bench.comparable(a,b))

    def test_bgfx_lazy_program_is_not_a_measured_pso(self):
        self.assertIn("lazy",bench.scenario_error("bgfx","pipeline"))
        for scenario in ("draw","instanced","textures","buffers","upload4k16f"):
            self.assertIsNone(bench.scenario_error("bgfx",scenario))

    def test_bgfx_unsupported_request_fails_before_creating_measurements(self):
        with tempfile.TemporaryDirectory() as directory:
            output=pathlib.Path(directory)/"should-not-exist"
            proc=subprocess.run([sys.executable,str(bench.ROOT/"tools/run_benchmarks.py"),"--sdks","bgfx","--scenarios","pipeline","--count","1","--output",str(output)],capture_output=True,text=True)
            self.assertEqual(proc.returncode,2)
            self.assertIn("PSO creation is lazy",proc.stderr)
            self.assertFalse(output.exists())

    def test_bgfx_fingerprint_includes_build_options_and_fixed_dependency_commits(self):
        with tempfile.TemporaryDirectory() as directory:
            root=pathlib.Path(directory); folder=root/"benchmarks/competitors"; folder.mkdir(parents=True)
            deps={key:{"commit":"first"} for key in ("bgfx","bx","bimg")}
            lock=folder/"bgfx-build.lock.json"; lock.write_text(json.dumps({"dependencies":deps}))
            (root/"benchmarks/CMakeLists.txt").write_text("project")
            config=folder/"Bgfx.cmake"; config.write_text("single thread")
            before=bench.bgfx_fingerprint(root); config.write_text("multiple threads")
            self.assertNotEqual(before,bench.bgfx_fingerprint(root))
            config.write_text("single thread"); deps["bx"]["commit"]="changed"
            lock.write_text(json.dumps({"dependencies":deps})); self.assertNotEqual(before,bench.bgfx_fingerprint(root))

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
