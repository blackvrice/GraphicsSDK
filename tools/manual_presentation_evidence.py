"""Check window events separately from a human's session-bound visual observation."""
import hashlib
import json


CHECKS = ('visible_animation', 'resize', 'minimize_restore', 'maximize_restore',
          'cover_uncover', 'responsive', 'close_button')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def validate(session, native_bytes, observation=None):
    digest = hashlib.sha256(native_bytes).hexdigest()
    require(session['status'] == 'exited' and session['native_exit'] == 0
            and session['inputs_unchanged'] is True and session['native_sha256'] == digest,
            'Incomplete, failed or changed native session')
    rows = [json.loads(line) for line in native_bytes.decode('utf-8').splitlines()]

    def one(name):
        found = [r for r in rows if r.get('event') == name]
        require(len(found) == 1, 'Missing or duplicate ' + name)
        return found[0]

    device, start, ready, shutdown, end = [one(n) for n in
        ('device', 'normal_session', 'loop_ready', 'shutdown', 'interactive_exit')]
    require(rows[0] == device and rows[-1] == end and start['loss_injection'] is False
            and start['interactive_acceptance'] is False and end['interactive_acceptance'] is False,
            'Wrong execution scope')
    require(start['sdk_fingerprint'] == session['SDK_source_fingerprint']
            and device['adapter_id'] == start['adapter_id'] == session['selected_adapter']['adapter_id']
            and device['warp'] is False and device['gbv'] is False and device['vsync'] is True,
            'Wrong source, adapter or display mode')
    require(ready['frames'] == 1 and end['frames'] > 1 and end['target_fps'] == 60
            and shutdown['sdk_objects'] == shutdown['sdk_resources'] == shutdown['pending_submissions'] == 0
            and shutdown['hwnd_alive'] is True and end['close_reason'] == 'wm_close',
            'Invalid frame or shutdown receipt')
    require(not any(r.get('event') in ('observed_loss', 'recovered', 'image', 'result') for r in rows),
            'Not an ordinary uninterrupted display session')
    states = [r for r in rows if r.get('event') == 'window_state']
    require(states and states[-1]['closing'] is True and states[-1]['close_reason'] == 'wm_close'
            and states[-1]['frames'] == end['frames'], 'Close rendered an extra frame')
    require(all(a['elapsed_ns'] <= b['elapsed_ns'] and a['frames'] <= b['frames']
                for a, b in zip(states, states[1:])), 'Window checkpoints went backwards')
    paused = [i for i, r in enumerate(states) if r['minimized']]
    require(paused and end['minimize_transitions'] >= 1 and end['restore_transitions'] >= 1
            and end['maximize_transitions'] >= 1 and end['suspended_waits'] >= 1,
            'Actual minimize, restore or maximize not observed')
    for i in paused:
        restore = next((r for r in states[i+1:] if not r['minimized']), None)
        require(restore is not None and restore['frames'] == states[i]['frames']
                and end['frames'] > restore['frames'], 'Submitted while minimized or failed to resume')
    extents = {(r['width'], r['height']) for r in rows if r.get('event') == 'extent'}
    require(len(extents) > 1 and all(w > 0 and h > 0 for w, h in extents) and end['resizes'] >= 1,
            'No effective client resize')
    accepted = False
    if observation is not None:
        require(observation['origin'] == 'direct_user_messages' and observation['native_sha256'] == digest
                and observation['statements'] and all(isinstance(s, str) and s for s in observation['statements'])
                and all(observation['checks'].get(k) is True for k in CHECKS),
                'Missing, negative or mismatched human observation')
        accepted = True
    return dict(task='P2-ACC-03', native_window_events_passed=True, human_confirmed=accepted,
                interactive_acceptance=accepted, native_sha256=digest, frames=end['frames'],
                resizes=end['resizes'], minimize_transitions=end['minimize_transitions'],
                restore_transitions=end['restore_transitions'], maximize_transitions=end['maximize_transitions'],
                scope='NVIDIA hardware, normal loop, VSync on, 60fps cap, validation off; no DPI/other-adapter acceptance')
