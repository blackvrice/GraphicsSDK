import pathlib
import sys
import tempfile
import unittest
from unittest.mock import Mock,patch
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
from xcb_window_manager import wait_ready


class WindowManagerHandshake(unittest.TestCase):
    def test_native_client_waits_for_startup_acknowledgement(self):
        with tempfile.TemporaryDirectory() as directory:
            marker=pathlib.Path(directory)/'ready';manager=Mock();calls=[]
            def poll():
                calls.append('poll')
                if len(calls)==2:marker.write_text('ready')
                return None
            manager.poll.side_effect=poll
            with patch('xcb_window_manager.time.sleep') as sleep:wait_ready(manager,marker)
            self.assertEqual(len(calls),2);sleep.assert_called_once_with(.02)
    def test_exit_before_startup_is_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            manager=Mock();manager.poll.return_value=-11
            with self.assertRaisesRegex(RuntimeError,'-11'):wait_ready(manager,pathlib.Path(directory)/'ready')
    def test_stale_marker_does_not_hide_dead_manager(self):
        with tempfile.TemporaryDirectory() as directory:
            marker=pathlib.Path(directory)/'ready';marker.write_text('ready');manager=Mock();manager.poll.return_value=0
            with self.assertRaises(RuntimeError):wait_ready(manager,marker)
    def test_live_but_unready_manager_is_bounded_failure(self):
        with tempfile.TemporaryDirectory() as directory:
            manager=Mock();manager.poll.return_value=None
            with patch('xcb_window_manager.time.monotonic',side_effect=[0,9]),self.assertRaisesRegex(RuntimeError,'deadline'):wait_ready(manager,pathlib.Path(directory)/'ready')
