import json
import os
from pathlib import Path
import subprocess
import time

cache = Path.home() / '.cache'
root = cache / 'pjlab-pressure-evidence'
root.mkdir(exist_ok=True)

def shell_pid():
    result = subprocess.run(['pgrep', '-u', str(os.getuid()), '-x', 'gnome-shell'], capture_output=True, text=True)
    return [int(p) for p in result.stdout.split()]

def desktop_ready(previous):
    for _ in range(100):
        current = shell_pid()
        if current and current != previous:
            result = subprocess.run(['gdbus', 'call', '--session', '--dest', 'org.gnome.Mutter.DisplayConfig',
              '--object-path', '/org/gnome/Mutter/DisplayConfig', '--method', 'org.gnome.Mutter.DisplayConfig.GetCurrentState'],
              stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            if result.returncode == 0:
                return current
        time.sleep(.5)
    raise RuntimeError('Patched desktop failed to recover')

results = []
for index in range(2):
    previous = shell_pid()
    print('RESTART_BEGIN', index + 1, previous, flush=True)
    subprocess.run(['sudo', '-n', 'python3', str(cache / 'pjlab_restart_patched_desktop.py')], check=True)
    current = desktop_ready(previous)
    info = json.loads((cache / 'pjlab-last-restart.json').read_text())
    subprocess.run(['sudo', '-n', 'cp', info['backup'] + '/gdm-original.conf', '/etc/gdm3/custom.conf'], check=True)
    # Let graphical autostarts restore their native input and capture connections.
    time.sleep(15)
    subprocess.run(['python3', str(cache / 'pjlab_restore_input_service.py')], check=True)
    last_pid, stable = None, 0
    for _ in range(40):
        pid = subprocess.check_output(['systemctl', '--user', 'show',
          'uu-remote-for-linux-input.service', '-p', 'MainPID', '--value'], text=True).strip()
        active = subprocess.run(['systemctl', '--user', 'is-active', '--quiet',
          'uu-remote-for-linux-client.service', 'uu-remote-for-linux-input.service']).returncode == 0
        stable = stable + 1 if active and pid != '0' and pid == last_pid else 0
        last_pid = pid
        if stable >= 5:
            break
        time.sleep(.5)
    assert stable >= 5, 'UU desktop autostart did not stabilize'
    record = {'cycle': index + 1, 'previous_shell_pids': previous, 'new_shell_pids': current, 'checks': []}
    for backend in ['wayland', 'x11']:
        name = f'restart-{index + 1}-{backend}'
        env = dict(os.environ, PJLAB_TEST_BACKEND=backend, PJLAB_CAPS_QUICK='1',
                   PJLAB_EXPECT_ALIGNED='1', PJLAB_VERIFY_TEXT='1')
        log_path = root / (name + '.log')
        started = time.time()
        with log_path.open('w') as log:
            completed = subprocess.run(['python3', '-u', str(cache / 'pjlab_caps_route_repro.py')],
                                       env=env, stdout=log, stderr=subprocess.STDOUT, timeout=120)
        report = cache / 'pjlab-caps-route-repro.json'
        assert report.stat().st_mtime >= started, (name, 'No fresh result')
        data = json.loads(report.read_text())
        (root / (name + '.json')).write_text(json.dumps(data, ensure_ascii=False, indent=2) + '\n')
        assert completed.returncode == 0 and not data['errors'], (name, str(log_path))
        record['checks'].append({'backend': backend, 'passed': True, 'text_records': data['text_records']})
    with (root / f'restart-{index + 1}-frames.log').open('w') as log:
        frames = subprocess.run(['python3', '-u', str(cache / 'pjlab_dual_frame_check.py')], stdout=log, stderr=subprocess.STDOUT, timeout=35)
    assert frames.returncode == 0
    frame_report = json.loads((cache / 'pjlab-dual-frame-check.json').read_text())
    record['frames'] = frame_report
    record['passed'] = True
    results.append(record)
    (root / 'restart-summary.json').write_text(json.dumps(results, ensure_ascii=False, indent=2) + '\n')
    print('RESTART_PASSED', index + 1, current, flush=True)
print('ALL_DESKTOP_RESTARTS_PASSED', flush=True)
