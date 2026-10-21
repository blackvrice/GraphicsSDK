import copy
import pathlib
import sys
import subprocess
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'tools'))
import benchmark_telemetry as telemetry
import run_benchmarks as bench


class TelemetryContracts(unittest.TestCase):
    def test_known_invalid_llgl_upload_is_rejected_before_any_output_or_execution(self):
        for scenario in ('upload1080', 'upload4k', 'upload4k16f'):
            self.assertIn('COPY_SOURCE', bench.scenario_error('llgl', scenario))
        with tempfile.TemporaryDirectory() as directory:
            output = pathlib.Path(directory) / 'absent'
            proc = subprocess.run([sys.executable, str(bench.ROOT / 'tools/run_benchmarks.py'), '--sdks', 'llgl', '--scenarios', 'upload1080', '--telemetry', '--output', str(output)], capture_output=True, text=True)
            self.assertEqual(proc.returncode, 2); self.assertFalse(output.exists())
    def result(self, allocation=False):
        point = {key: 100 for key in telemetry.MEMORY_FIELDS}
        points = [{**point, 'phase': 'baseline', 'sample': 0}]
        for i in range(2):
            if allocation:
                points.append({**point, 'gpu_local_current_bytes': 200, 'phase': 'allocation_live', 'sample': i})
            points.append({**point, 'phase': 'sample_end', 'sample': i})
        return {'scenario': 'buffers' if allocation else 'draw', 'samples': [{}, {}],
                'environment': {'luid': '1:2'}, 'telemetry': {
                    'revision': 1, 'scope': telemetry.SCOPE, 'cpu_normalization': telemetry.NORMALIZATION,
                    'gpu_luid': '1:2', 'gpu_node': 0, 'wall_ms': 1000, 'kernel_ms': 100,
                    'user_ms': 1900, 'cpu_ms': 2000, 'cpu_one_core_percent': 200, 'checkpoints': points}}

    def test_cpu_percent_is_one_core_and_process_aggregate_not_frame_samples(self):
        result = self.result(); telemetry.validate(result, True)
        # All process threads are counted; >100% is legitimate.
        summary = telemetry.summarize([result, result, result])
        self.assertEqual(summary['cpu_one_core_percent']['median'], 200)
        self.assertEqual(summary['cpu_ms']['process_count'], 3)
        self.assertEqual(len(summary['cpu_ms']['run_values']), 3)

    def test_opt_in_and_scope_and_gpu_identity_cannot_be_silently_changed(self):
        result = self.result()
        with self.assertRaises(RuntimeError): telemetry.validate(result, False)
        for change in ({'scope': 'whole_process'}, {'cpu_normalization': 'machine'}, {'gpu_luid': '1:3'}, {'gpu_node': 1}, {'revision': True}):
            bad = copy.deepcopy(result); bad['telemetry'].update(change)
            with self.subTest(change=change), self.assertRaises(RuntimeError): telemetry.validate(bad, True)
        with self.assertRaises(RuntimeError): telemetry.validate({'telemetry': None}, True)
        telemetry.validate({}, False)

    def test_missing_invalid_or_inconsistent_cpu_counters_fail(self):
        for key in ('wall_ms', 'kernel_ms', 'user_ms', 'cpu_ms', 'cpu_one_core_percent'):
            for value in (None, True, -1, float('nan'), float('inf'), '100'):
                bad = self.result(); bad['telemetry'][key] = value
                with self.subTest(key=key, value=value), self.assertRaises(RuntimeError): telemetry.validate(bad, True)
        for change in ({'wall_ms': 0}, {'cpu_ms': 2001}, {'cpu_one_core_percent': 2}):
            bad = self.result(); bad['telemetry'].update(change)
            with self.assertRaises(RuntimeError): telemetry.validate(bad, True)

    def test_live_allocations_are_observed_before_release_and_order_is_required(self):
        result = self.result(True); telemetry.validate(result, True)
        self.assertEqual(telemetry.metrics(result)['observed_max_gpu_local_current_bytes'], 200)
        self.assertEqual(telemetry.metrics(result)['max_gpu_local_budget_bytes'], 100)
        for mutate in ('delete', 'reorder', 'wrong_index', 'invalid_object'):
            bad = copy.deepcopy(result); points = bad['telemetry']['checkpoints']
            if mutate == 'delete': points.pop(1)
            if mutate == 'reorder': points[1], points[2] = points[2], points[1]
            if mutate == 'wrong_index': points[1]['sample'] = 1
            if mutate == 'invalid_object': points[1] = None
            with self.subTest(mutate=mutate), self.assertRaises(RuntimeError): telemetry.validate(bad, True)

    def test_missing_memory_is_never_zero_and_lifetime_peaks_cannot_reverse(self):
        for key in telemetry.MEMORY_FIELDS:
            for value in (None, True, -1, .5):
                bad = self.result(); bad['telemetry']['checkpoints'][1][key] = value
                with self.subTest(key=key), self.assertRaises(RuntimeError): telemetry.validate(bad, True)
        for key in ('lifetime_peak_working_set_bytes', 'lifetime_peak_commit_bytes'):
            bad = self.result(); bad['telemetry']['checkpoints'][1][key] = 99
            with self.assertRaises(RuntimeError): telemetry.validate(bad, True)
        result = self.result()
        for point in result['telemetry']['checkpoints']: point['gpu_nonlocal_current_bytes'] = 0
        telemetry.validate(result, True)  # Real supported zero is different from missing.

    def test_telemetry_comparisons_cannot_mix_with_original_uninstrumented_contract(self):
        env = {'cpu': 'same', 'ram_bytes': 100, 'os': 'same', 'luid': '1:2', 'driver': 'same', 'software_renderer': False, 'compiler': 'same'}
        common = {'api': 'd3d12', 'category': 'end_to_end', 'scenario': 'draw', 'count': 1000,
                  'validation': False, 'configuration': 'Release', 'environment': env}
        old = {**common, 'contract': {'revision': 2}}
        new = {**common, 'contract': {'revision': 4, 'telemetry': telemetry.contract()}}
        self.assertFalse(bench.comparable(old, new)); self.assertTrue(bench.comparable(new, copy.deepcopy(new)))


if __name__ == '__main__': unittest.main()
