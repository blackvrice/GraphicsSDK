import copy
import pathlib
import sys
import unittest
from unittest import mock

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1] / 'tools'))
import check_vulkan_competitors as v


class VulkanCompetitorContract(unittest.TestCase):
    def setUp(self):
        self.plan = v.read(v.DIRECTORY / 'selection.json')
        self.review = v.read(v.DIRECTORY / 'source-review.json')

    def check(self):
        return v.validate(self.plan, self.review)

    def rebind_review(self):
        self.plan['source_review_sha256'] = v.canonical(self.review)

    def test_offline_review_without_dependency_checkouts(self):
        with mock.patch.object(v, 'git', side_effect=AssertionError('Offline check invoked git')):
            self.assertIs(self.check(), self.plan)

    def test_source_review_cannot_grant_runtime_acceptance(self):
        for key, value in v.POLICY.items():
            with self.subTest(key=key):
                plan = copy.deepcopy(self.plan)
                plan['policy'][key] = not value if type(value) is bool else 'done'
                with self.assertRaises(ValueError):
                    v.validate(plan, self.review)

    def test_no_candidate_can_be_silently_removed(self):
        for name in v.CANDIDATES:
            with self.subTest(name=name):
                plan = copy.deepcopy(self.plan)
                del plan['candidates'][name]
                with self.assertRaises(ValueError):
                    v.validate(plan, self.review)

    def test_no_equivalence_requirement_can_be_removed(self):
        for item in v.REQUIREMENTS:
            with self.subTest(item=item):
                plan = copy.deepcopy(self.plan)
                plan['required_before_measurement'].remove(item)
                with self.assertRaises(ValueError):
                    v.validate(plan, self.review)

    def test_primary_selection_does_not_select_conditional_tasks(self):
        self.plan['candidates']['bgfx']['selection'] = 'selected_next'
        with self.assertRaises(ValueError): self.check()

    def test_empty_deferral_reason_rejected(self):
        self.plan['candidates']['dawn']['reason'] = ' '
        with self.assertRaises(ValueError): self.check()

    def test_pin_drift_rejected(self):
        self.plan['candidates']['llgl']['commit'] = '0' * 40
        with self.assertRaises(ValueError): self.check()

    def test_old_build_license_audit_does_not_transfer(self):
        self.plan['candidates']['the_forge']['build_inputs_approved'] = True
        with self.assertRaises(ValueError): self.check()

    def test_sandbox_dependency_license_is_not_project_license(self):
        self.plan['candidates']['sandbox']['root_license'] = 'MIT'
        with self.assertRaises(ValueError): self.check()

    def test_workload_or_timing_baseline_drift_rejected(self):
        for name in self.plan['workloads']:
            with self.subTest(name=name):
                plan = copy.deepcopy(self.plan)
                plan['workloads'][name]['baseline_semantic_sha256'] = '0' * 64
                with self.assertRaises(ValueError): v.validate(plan, self.review)

    def test_workload_task_mapping_cannot_skip_correctness(self):
        self.plan['workloads']['allocation']['correctness_task'] = 'P3-CMP-10'
        with self.assertRaises(ValueError): self.check()

    def test_wrong_API_evidence_rejected(self):
        self.plan['workloads']['upload']['evidence'] = ['sdl-API']
        with self.assertRaises(ValueError): self.check()

    def test_build_validation_mode_cannot_silently_change(self):
        self.plan['LLGL_build_requirements']['LLGL_VK_ENABLE_GPU_ASSISTED_VALIDATION'] = 'ON'
        with self.assertRaises(ValueError): self.check()

    def test_modified_review_requires_reinspection(self):
        self.review['files'][0]['sha256'] = '0' * 64
        with self.assertRaises(ValueError): self.check()

    def test_rebound_review_cannot_use_mutable_source_URL(self):
        self.review['files'][0]['url'] = 'https://github.com/example/repo/blob/main/LICENSE'
        self.rebind_review()
        with self.assertRaises(ValueError): self.check()

    def test_rebound_review_cannot_use_path_traversal(self):
        self.review['files'][0]['path'] = '../secret'
        self.rebind_review()
        with self.assertRaises(ValueError): self.check()

    def test_rebound_review_cannot_change_provenance(self):
        self.review['files'][0]['origin'] = 'unverified_working_copy'
        self.rebind_review()
        with self.assertRaises(ValueError): self.check()

    def test_source_locations_required_even_with_rebound_digest(self):
        self.review['files'][0]['anchors'][0]['lines'] = []
        self.rebind_review()
        with self.assertRaises(ValueError): self.check()

    def test_live_check_rejects_wrong_source_bytes(self):
        locks = v.read(v.ROOT / 'benchmarks/competitors/sources.lock.json')
        def fake_git(checkout, *args):
            sdk = next(n for n, directory in v.CHECKOUTS.items() if checkout.name == directory)
            return locks[sdk]['commit'].encode() if args[0] == 'rev-parse' else b''
        with mock.patch.object(v, 'git', side_effect=fake_git), mock.patch.object(v, 'source_bytes', return_value=b'tampered'):
            with self.assertRaisesRegex(ValueError, 'Live source hash drift'):
                v.validate_live(self.review)


if __name__ == '__main__': unittest.main()
