"""Parser controls only. The macOS CI runs the actual native assertion."""

import pathlib
import signal
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
import macos_metal_validation as validation


class MetalValidation(unittest.TestCase):
    def test_control_requires_native_abort_at_the_expected_copy(self):
        diagnostic = "-[MTLDebugBlitCommandEncoder copyFromBuffer:sourceOffset:toBuffer:destinationOffset:size:]: failed assertion 'sourceOffset + size exceeds source length'"
        validation.negative_control(-signal.SIGABRT, validation.MARKER, diagnostic)
        for status, marker, error in (
            (0, validation.MARKER, diagnostic),
            (-signal.SIGABRT, '', diagnostic),
            (-signal.SIGABRT, validation.MARKER, 'Metal API Validation Enabled'),
            (-signal.SIGABRT, validation.MARKER, diagnostic.replace('sourceOffset + size exceeds source length', 'encoder already closed')),
        ):
            with self.subTest(status=status, error=error), self.assertRaises(ValueError):
                validation.negative_control(status, marker, error)

    def test_screen_absence_cannot_report_a_display_result(self):
        missing = dict(presentation_executed=False, reason='no AppKit screen')
        self.assertFalse(validation.window_result(missing, 'a'*64, 77))
        with self.assertRaises(ValueError):
            validation.window_result(dict(missing, display_observed=True), 'a'*64, 77)

    def test_full_window_readback_can_only_claim_observed_timestamps(self):
        fp = 'a'*64
        report = dict(contract='metal_window_v1', SDK_source_fingerprint=fp, frames=8,
                      presentation_executed=True, interactive=False, before_resize_presented=1,
                      after_resize_presented=0, display_observed=False, native_control_frames=3, native_control_presented=0)
        self.assertTrue(validation.window_result(report, fp, 0))
        for change in (dict(display_observed=True), dict(native_control_presented=1), dict(frames=7),
                       dict(before_resize_presented=True), dict(SDK_source_fingerprint='b'*64)):
            with self.subTest(change=change), self.assertRaises(ValueError):
                validation.window_result(dict(report, **change), fp, 0)
