"""Regressions for cross-campaign scope and clock attribution, without GPU runs."""
import copy
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
import phase3_comparison_report as report


class ComparisonReportTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.receipts, cls.summaries = [], []
        for _, name, _, _ in report.CAMPAIGNS:
            folder = report.ROOT/('benchmarks/reports/validation/2026-10-29-phase3-' + name)
            cls.receipts.append(report.read(folder/'native/receipt.json'))
            cls.summaries.append(report.read(folder/'summary.json'))
        cls.roadmap = report.read(report.ROOT/'docs/ROADMAP_TASKS.json')

    def setUp(self):
        self.r = copy.deepcopy(self.receipts)
        self.s = copy.deepcopy(self.summaries)
        self.road = copy.deepcopy(self.roadmap)

    def build(self):
        bindings = [[{'path': 'benchmarks/reports/validation/2026-10-29-phase3-' + name + '/native/receipt.json',
                      'sha256': 'a'*64}] for _, name, _, _ in report.CAMPAIGNS]
        return report.compose(self.r, self.s, self.road, bindings)

    def reject(self):
        with self.assertRaises(ValueError):
            self.build()

    def sync_groups(self, index=0):
        self.s[index]['groups'] = copy.deepcopy(self.r[index]['groups'])

    def test_complete_inventory_and_clock_counts(self):
        result = self.build()
        metrics = [m for c in result['campaigns'] for m in c['metrics']]
        self.assertEqual(len(metrics), 108)
        self.assertEqual(sum(m['clock'] == 'GPU' for m in metrics), 6)
        self.assertEqual(result['status'], 'partial_windows_inventory')
        self.assertFalse(result['final_comparison_accepted'])
        self.assertFalse(result['Phase3_completed'])
        self.assertFalse(result['new_measurements_collected'])

    def test_software_readiness_keeps_physical_prerequisites(self):
        pending = {t['id'] for t in self.build()['pending_prerequisites']}
        self.assertTrue({'P3-BEN-07', 'P3-BEN-08', 'P3-LBEN-01', 'P3-LBEN-02',
                         'P3-LBEN-03', 'P3-LBEN-04'}.issubset(pending))
        self.assertNotIn('P3-BEN-09', pending)

    def test_render_has_separate_modes_and_no_overall_score(self):
        text = report.render(self.build())
        self.assertIn('GPU / gpu_ms', text)
        self.assertIn('CPU / cpu_total_ms', text)
        self.assertIn('baseline', text)
        self.assertIn('audit', text)
        self.assertIn('timestamp', text)
        self.assertIn('물리 메모리 회수', text)

    def test_cross_campaign_identity_difference(self):
        for field in self.r[1]['identity']:
            with self.subTest(field=field):
                prior = self.r[1]['identity'][field]
                self.r[1]['identity'][field] = None
                self.reject()
                self.r[1]['identity'][field] = prior

    def test_cross_campaign_source_difference(self):
        for field in ('SDK_source_fingerprint', 'SDL_commit'):
            with self.subTest(field=field):
                self.r[1][field] = 'wrong'
                self.reject()
                self.r[1][field] = self.r[0][field]

    def test_summary_not_authoritative(self):
        self.s[0]['groups'][0]['statistics']['p50_ms'] += 1
        self.reject()

    def test_missing_campaign(self):
        self.r.pop()
        self.reject()

    def test_missing_metric(self):
        self.r[0]['groups'].pop()
        self.sync_groups()
        self.reject()

    def test_duplicate_metric(self):
        self.r[0]['groups'].append(copy.deepcopy(self.r[0]['groups'][0]))
        self.sync_groups()
        self.reject()

    def test_foreign_sdk(self):
        self.r[0]['groups'][0]['sdk'] = 'bgfx'
        self.sync_groups()
        self.reject()

    def test_cpu_relabelled_as_gpu(self):
        self.r[0]['groups'][0]['column'] = 'gpu_ms'
        self.sync_groups()
        self.reject()

    def test_gpu_baseline_cannot_have_timestamp(self):
        self.r[4]['groups'][0]['statistics']['gpu_ms'] = copy.deepcopy(
            self.r[4]['groups'][0]['statistics']['cpu_total_ms'])
        self.sync_groups(4)
        self.reject()

    def test_gpu_missing_timestamp(self):
        next(g for g in self.r[4]['groups'] if g['mode'] == 'timestamp')['statistics'].pop('gpu_ms')
        self.sync_groups(4)
        self.reject()

    def test_bad_statistics(self):
        base = copy.deepcopy(self.r[0]['groups'][0]['statistics'])
        for field, value in [('p50_ms', float('nan')), ('p95_ms', float('inf')),
                             ('MAD_ms', -1), ('samples', True), ('samples', 149),
                             ('processes', 4), ('samples_removed', 1),
                             ('p50_ms', base['max_ms']+1), ('process_medians_ms', [])]:
            with self.subTest(field=field, value=value):
                self.r[0]['groups'][0]['statistics'] = {**base, field: value}
                self.sync_groups()
                self.reject()

    def test_cpu_multi_column_samples_not_summed(self):
        self.r[2]['measurement_samples'] *= 2
        self.s[2]['measurement_samples'] *= 2
        self.reject()

    def test_gpu_sample_count_not_cpu_impact_count(self):
        self.r[4]['measurement_GPU_samples'] = 2700
        self.s[4]['measurement_GPU_samples'] = 2700
        self.reject()

    def test_missing_frozen_inputs_or_ranking_claim(self):
        for field, value in [('inputs_unchanged', False), ('overall_SDK_ranking', True),
                             ('historical_timings_reused', True), ('performance_accepted', False)]:
            with self.subTest(field=field):
                self.r = copy.deepcopy(self.receipts)
                self.r[0][field] = value
                self.reject()

    def test_windows_cannot_promote_linux(self):
        self.s[0]['Linux_hardware_accepted'] = True
        self.reject()

    def test_missing_roadmap_prerequisite(self):
        self.road['tasks'] = [t for t in self.road['tasks'] if t['id'] != 'P3-BEN-07']
        self.reject()

    def test_duplicate_roadmap_id(self):
        self.road['tasks'].append(copy.deepcopy(self.road['tasks'][0]))
        self.reject()

    def test_gate_cannot_be_completed_by_windows_inventory(self):
        next(t for t in self.road['tasks'] if t['id'] == 'P3-GATE-02')['status'] = 'done'
        self.reject()

    def test_campaign_must_be_accepted(self):
        next(t for t in self.road['tasks'] if t['id'] == 'P3-CMP-07')['status'] = 'todo'
        self.reject()


if __name__ == '__main__':
    unittest.main()
