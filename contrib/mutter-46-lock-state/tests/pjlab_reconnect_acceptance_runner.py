import json
import os
from pathlib import Path
import signal
import subprocess

cache = Path.home() / '.cache'
root = cache / 'pjlab-pressure-evidence'
records = []
with (root / 'reconnection-animation.log').open('w') as animation_log:
    animation = subprocess.Popen(['python3', '-u', str(cache / 'pjlab_dual_frame_check.py')],
        env=dict(os.environ, PJLAB_FRAME_DURATION='360'), stdout=animation_log, stderr=subprocess.STDOUT)
    try:
        for backend in ['wayland', 'x11']:
            with (root / ('reconnection-' + backend + '.log')).open('w') as log:
                result = subprocess.run(['python3', '-u', str(cache / 'pjlab_caps_reconnect_verify.py')],
                    env=dict(os.environ, PJLAB_TEST_BACKEND=backend), stdout=log, stderr=subprocess.STDOUT, timeout=180)
            assert result.returncode == 0, backend
            report = json.loads((cache / ('pjlab-caps-reconnect-' + backend + '.json')).read_text())
            (root / ('reconnection-' + backend + '.json')).write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n')
            records.append({'backend': backend, 'cycles': report['reconnect_records'], 'texts': report['text_records'], 'passed': True})
            print('RECONNECTION_BACKEND_PASSED', backend, flush=True)
    finally:
        animation.send_signal(signal.SIGTERM)
        animation.wait(timeout=10)
(root / 'reconnection-summary.json').write_text(json.dumps(records, ensure_ascii=False, indent=2) + '\n')
assert len(records) == 2
print('TWELVE_CAPS_STATE_PRESERVING_RECONNECTIONS_PASSED', flush=True)
