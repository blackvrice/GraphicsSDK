"""The diagnostic receipt must bind the selected linked SDK library and policy."""
import argparse
import contextlib
import hashlib
import io
import json
import pathlib
import sys
import tempfile
import unittest
from unittest import mock

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]/"tools"))
import capture_memory_failure as capture


class SelectedLibraryTests(unittest.TestCase):
    def exercise(self, change_library=False):
        with tempfile.TemporaryDirectory() as directory:
            root = pathlib.Path(directory)
            binary = root/"binary"
            library = root/"separate-symbol-sdk/GraphicsSDKCore.lib"
            wrapper = root/"tools/capture_memory_failure.py"
            files = [library, wrapper, root/"tools/MemoryStress.cpp", root/"tools/run_memory_stress.py",
                     root/"tools/analyze_memory_stress.py", root/"tools/memory_stress_policy.json",
                     root/"tools/crash_capture/Capture.cpp", root/"tools/crash_capture/CMakeLists.txt",
                     root/"tools/crash_capture/evidence.py",
                     *[binary/name for name in ("gfx_crash_capture.exe", "gfx_crash_capture.pdb",
                                                "gfx_memory_stress_symbols.exe", "gfx_memory_stress_symbols.pdb")]]
            for path in files:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"frozen-source-fingerprint")
            original = hashlib.sha256(library.read_bytes()).hexdigest()
            args = argparse.Namespace(output=root/"receipt", binary=binary, core_library=library,
                                      diagnostic_seconds=1, mode="validation", adapter=123, dump=None,
                                      max_private_growth_mib=512)

            def wait(**kwargs):
                if change_library:
                    library.write_bytes(b"changed")
                return 0

            process = mock.Mock(pid=1234)
            process.wait.side_effect = wait
            records = [{"schema": 2}, {"event": "exit", "child_exit_code": 0}]
            with mock.patch.multiple(capture, ROOT=root, __file__=str(wrapper)), \
                    mock.patch.object(capture, "sdk_fingerprint", return_value="frozen-source-fingerprint"), \
                    mock.patch.object(capture.subprocess, "Popen", return_value=process), \
                    mock.patch.object(capture, "read_records", return_value=records), \
                    mock.patch.object(capture, "validate", return_value={"second_chance_count": 0, "timed_out": False}), \
                    mock.patch.object(capture, "validate_native", return_value={}), contextlib.redirect_stdout(io.StringIO()):
                code = capture.run(args)
            result = json.loads((args.output/"summary.json").read_text())
            self.assertEqual(result["core_library"], str(library))
            self.assertEqual(result["frozen_inputs"][str(library)], original)
            self.assertFalse(result["memory_acceptance"])
            self.assertEqual((args.output/"source/tools/memory_stress_policy.json").read_bytes(),
                             (root/"tools/memory_stress_policy.json").read_bytes())
            return code, result

    def test_separate_symbol_library_needs_no_default_core_library(self):
        code, result = self.exercise()
        self.assertEqual(code, 0)
        self.assertTrue(result["inputs_unchanged"])
        self.assertEqual(result["status"], "completed")

    def test_selected_library_mutation_rejects_completed_child(self):
        code, result = self.exercise(change_library=True)
        self.assertEqual(code, 1)
        self.assertFalse(result["inputs_unchanged"])
        self.assertEqual(result["status"], "capture_failed")


if __name__ == "__main__":
    unittest.main()
