import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
import check_vulkan_pool_workload as workload


class PoolWorkloadTests(unittest.TestCase):
    def test_matched_sparse_budget(self):
        result = workload.check()
        self.assertEqual(result['historical_runs'], 8)
        self.assertEqual(result['survivor_ordinals'], [0, 1, 3, 7, 15])
        dedicated, adaptive, capped = (result['model'][name] for name in
                                       ('dedicated', 'historical_adaptive', 'four_slot'))
        self.assertEqual([policy['sparse']['reserved'] for policy in (dedicated, adaptive, capped)],
                         [27200, 168640, 81600])
        self.assertEqual([policy['refilled']['allocation_calls'] for policy in (dedicated, adaptive, capped)],
                         [156, 20, 36])
        self.assertEqual(capped['reset_empty']['reserved'], 0)

    def test_wrong_historical_sparse_reservation_is_rejected(self):
        archive = workload.read(workload.ARCHIVE)
        run = next(entry['result'] for entry in archive['runs'] if entry['name'] == 'candidate-gpu0-v0-sparse')
        row = next(row for row in run['checkpoints'] if row['label'] == 'one-per-block')
        row['reserved'] = row['required']
        with self.assertRaisesRegex(ValueError, 'one-per-block round 0 differs'):
            workload.verify_archive(archive, workload.model(), workload.model(1))
