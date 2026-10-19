import json
import math
import multiprocessing as mp
import os
from pathlib import Path
import signal
import subprocess
import time

root = Path.home() / '.cache/pjlab-pressure-evidence'
root.mkdir(exist_ok=True)
cache = Path.home() / '.cache'
summary = {'started_at': time.strftime('%Y-%m-%dT%H:%M:%S%z'),
           'cpu_workers': 8, 'layout': [[3840, 2160], [3840, 2160]], 'errors': []}

def load_cpu():
    stop = time.monotonic() + 900
    value = 1.5
    while time.monotonic() < stop:
        for _ in range(10000):
            value = math.sqrt(value + 1.1234)

def restart_bridge():
    subprocess.run(['systemctl', '--user', 'restart', 'uu-remote-for-linux-input.service'], check=True)
    subprocess.run(['python3', str(cache / 'pjlab_restore_input_service.py')], check=True)
    expected_pid = int(subprocess.check_output(['systemctl', '--user', 'show',
       'uu-remote-for-linux-input.service', '-p', 'MainPID', '--value'], text=True))
    path = Path.home() / '.local/state/uu-remote-for-linux/input-bridge-status.json'
    for _ in range(100):
        try:
            status = json.loads(path.read_text())
            if status.get('pid') == expected_pid and status.get('capture_state') in ('active', 'streaming') and len(status.get('capture_streams', [])) == 2:
                return {key: status.get(key) for key in ['pid', 'state', 'backend', 'capture_state', 'capture_streams']}
        except Exception:
            pass
        time.sleep(.2)
    raise RuntimeError('Two live capture streams did not recover after bridge restart')

workers = []
animation = None
animation_log = None
completed = None
try:
    subprocess.run(['python3', str(cache / 'pjlab_pressure_layout.py')], check=True)
    gate = root / ('capture-gate-' + str(time.time_ns()))
    animation_log = (root / 'dual-4k-animation.log').open('w')
    animation = subprocess.Popen(['python3', '-u', str(cache / 'pjlab_dual_frame_check.py')],
        env=dict(os.environ, PJLAB_FRAME_DURATION='900', PJLAB_FRAME_START_FILE=str(gate)), stdout=animation_log, stderr=subprocess.STDOUT)
    summary['bridge_before'] = restart_bridge()
    gate.write_text(str(summary['bridge_before']['pid']))
    for _ in range(summary['cpu_workers']):
        worker = mp.Process(target=load_cpu)
        worker.start()
        workers.append(worker)
    print('DUAL_4K_PRESSURE_STARTED', flush=True)
    completed = subprocess.run(['python3', '-u', str(cache / 'pjlab_caps_pressure_runner.py')], check=False)
    assert completed.returncode == 0, completed.returncode
except Exception as exc:
    summary['errors'].append(str(exc))
finally:
    for worker in workers:
        worker.terminate()
        worker.join(timeout=5)
    if animation is not None:
        animation.send_signal(signal.SIGTERM)
        try:
            summary['animation_returncode'] = animation.wait(timeout=10)
        except subprocess.TimeoutExpired:
            animation.kill()
            animation.wait()
            summary['errors'].append('Animation monitor did not stop cleanly')
    if animation_log:
        animation_log.close()
    try:
        subprocess.run(['python3', str(cache / 'pjlab_pressure_layout.py')],
                       env=dict(os.environ, PJLAB_RESTORE_LAYOUT='1'), check=True)
        summary['bridge_after'] = restart_bridge()
    except Exception as exc:
        summary['errors'].append('Restore: ' + str(exc))
    report = cache / 'pjlab-dual-frame-check.json'
    if report.exists():
        summary['dual_frame_check'] = json.loads(report.read_text())
        (root / 'dual-4k-frame-check.json').write_text(report.read_text())
    summary['finished_at'] = time.strftime('%Y-%m-%dT%H:%M:%S%z')
    summary['passed'] = bool(completed and completed.returncode == 0 and
        not summary['errors'] and summary.get('animation_returncode') == 0)
    (root / 'campaign.json').write_text(json.dumps(summary, indent=2) + '\n')
print('PRESSURE_CAMPAIGN_RESULT', json.dumps({key: summary.get(key) for key in
      ['started_at', 'finished_at', 'passed', 'errors', 'animation_returncode']}), flush=True)
assert summary['passed'], summary['errors']
