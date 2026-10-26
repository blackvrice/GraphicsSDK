import copy
import hashlib
import json
import pathlib
import sys
import unittest

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
from manual_presentation_evidence import CHECKS, validate


def fixture():
    states = [dict(event='window_state', elapsed_ns=i, frames=f, minimized=m, closing=False, close_reason='none')
              for i, (f, m) in enumerate(((0, False), (5, True), (5, False), (12, False)))]
    states[-1].update(closing=True, close_reason='wm_close')
    rows = [dict(event='device', adapter_id=42, warp=False, gbv=False, vsync=True),
            dict(event='normal_session', adapter_id=42, sdk_fingerprint='source', loss_injection=False, interactive_acceptance=False),
            dict(event='loop_ready', frames=1), *states,
            dict(event='extent', width=600, height=400), dict(event='extent', width=800, height=500),
            dict(event='shutdown', sdk_objects=0, sdk_resources=0, pending_submissions=0, hwnd_alive=True),
            dict(event='interactive_exit', frames=12, target_fps=60, close_reason='wm_close',
                 interactive_acceptance=False, minimize_transitions=1, restore_transitions=1,
                 maximize_transitions=1, suspended_waits=1, resizes=1)]
    return rows


def inputs(rows):
    native='\n'.join(json.dumps(r) for r in rows).encode()
    digest=hashlib.sha256(native).hexdigest()
    session=dict(status='exited', native_exit=0, inputs_unchanged=True, native_sha256=digest,
                 SDK_source_fingerprint='source', selected_adapter=dict(adapter_id=42))
    human=dict(origin='direct_user_messages', native_sha256=digest, statements=['I checked this window.'],
               checks=dict.fromkeys(CHECKS, True))
    return session, native, human


class ManualPresentationEvidenceTests(unittest.TestCase):
    def test_native_events_alone_do_not_grant_human_acceptance(self):
        s, n, h=inputs(fixture())
        self.assertFalse(validate(s, n)['interactive_acceptance'])
        self.assertTrue(validate(s, n, h)['interactive_acceptance'])

    def test_human_cannot_replace_missing_native_minimize(self):
        rows=[r for r in fixture() if not r.get('minimized')]
        with self.assertRaisesRegex(ValueError, 'minimize'):
            validate(*inputs(rows))

    def test_failed_exit_changed_inputs_or_mismatched_receipt_rejected(self):
        for field, value in (('native_exit', 1), ('status', 'watchdog_terminated'),
                             ('inputs_unchanged', False), ('native_sha256', 'different')):
            s,n,h=inputs(fixture());s[field]=value
            with self.subTest(field=field), self.assertRaises(ValueError):validate(s,n,h)

    def test_negative_unbound_or_automated_observation_rejected(self):
        for change in (dict(origin='agent_screenshot'), dict(native_sha256='another-session'), dict(statements=[])):
            s,n,h=inputs(fixture());h.update(change)
            with self.assertRaises(ValueError):validate(s,n,h)
        for key in CHECKS:
            s,n,h=inputs(fixture());h['checks'][key]=False
            with self.subTest(key=key), self.assertRaises(ValueError):validate(s,n,h)

    def test_frames_during_minimize_and_after_close_rejected(self):
        for event,field,value in (('interactive_exit','frames',13),('window_state','frames',6)):
            rows=copy.deepcopy(fixture())
            target=next(r for r in rows if r['event']==event and (event!='window_state' or r.get('minimized')))
            target[field]=value
            with self.assertRaises(ValueError):validate(*inputs(rows))

    def test_native_self_acceptance_loss_or_missing_shutdown_rejected(self):
        for mutate in (lambda r:r[-1].update(interactive_acceptance=True),
                       lambda r:r.insert(-1,dict(event='observed_loss')),
                       lambda r:r.pop(-2)):
            rows=fixture();mutate(rows)
            with self.assertRaises(ValueError):validate(*inputs(rows))


if __name__=='__main__':unittest.main()
