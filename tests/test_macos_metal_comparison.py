"""Reject corrupted comparison payloads, mislabelled devices and partial timing series."""

import csv
import json
import pathlib
import sys
import tempfile
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
import macos_metal_comparison as comparison


class MetalComparison(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = pathlib.Path(temporary.name)

    def write_case(self):
        (self.root/'commands').mkdir()
        out = self.root/'sdl-reference'
        out.mkdir()
        for count in comparison.DRAWS:
            (out/f'{count}.rgba').write_bytes(comparison.reference(count))
        result = dict(adapter='sdl3', identity='pinned-default-selection-with-single-adapter', mode='reference',
                      registry_id='0000000000000001', name='synthetic device', target_storage='private',
                      images=3, timing_samples=0, mac1=True, mac2=False, mac1_compatibility=True)
        (self.root/'commands/sdl-reference.stdout').write_text(json.dumps(result), encoding='utf-8')
        (self.root/'commands/sdl-reference.stderr').write_text('Metal API Validation Enabled\n', encoding='utf-8')
        return out, result

    def test_case_requires_every_rgba_byte(self):
        out, _ = self.write_case()
        comparison.case(self.root, 'sdl', 'reference', 'a'*64)
        path = out/'1024.rgba'
        path.write_bytes(b'\x00' + path.read_bytes()[1:-1] + b'\x00')
        with self.assertRaises(ValueError):
            comparison.case(self.root, 'sdl', 'reference', 'a'*64)

    def test_reference_needs_native_validation_banner_and_no_assertion(self):
        self.write_case()
        path = self.root/'commands/sdl-reference.stderr'
        for stderr in ('', 'Metal API Validation Enabled\nfailed assertion\n'):
            path.write_text(stderr, encoding='utf-8')
            with self.subTest(stderr=stderr), self.assertRaises(ValueError):
                comparison.case(self.root, 'sdl', 'reference', 'a'*64)

    def test_sdl_inference_cannot_claim_a_native_handle(self):
        _, result = self.write_case()
        path = self.root/'commands/sdl-reference.stdout'
        for change in (dict(identity='explicit-registry-id'), dict(registry_id='0000000000000000'),
                       dict(mac1_compatibility=False), dict(mac2=True)):
            path.write_text(json.dumps(dict(result, **change)), encoding='utf-8')
            with self.subTest(change=change), self.assertRaises(ValueError):
                comparison.case(self.root, 'sdl', 'reference', 'a'*64)

    def test_timing_requires_the_full_ordered_matrix(self):
        path = self.root/'samples.csv'
        fields = ['draws', 'sample', 'record_ns', 'submit_ns', 'queue_idle_wait_ns']
        rows = [(count, sample, 10, 20, 30) for count in comparison.DRAWS for sample in range(12)]
        def write(data):
            with path.open('w', encoding='utf-8', newline='') as file:
                writer = csv.writer(file)
                writer.writerow(fields)
                writer.writerows(data)
        write(rows)
        self.assertEqual(comparison.timing(path)['1'], dict(host_record_submit_median_ns=30, host_queue_wait_median_ns=30))
        for invalid in (rows[:-1], [rows[1], *rows[1:]], [(1, 0, -1, 20, 30), *rows[1:]]):
            write(invalid)
            with self.assertRaises(ValueError):
                comparison.timing(path)
