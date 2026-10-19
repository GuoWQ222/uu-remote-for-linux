import json
import os
from pathlib import Path
import subprocess
import sys
import time

root = Path.home() / '.cache/pjlab-pressure-evidence'
root.mkdir(exist_ok=True)
smoke = '--smoke' in sys.argv
cases = [('wayland', 'native'), ('x11', 'native'), ('wayland', 'uu-udp'), ('x11', 'uu-udp')]
if os.environ.get('PJLAB_STRESS_ONLY_TRANSPORT'):
    cases = [case for case in cases if case[1] == os.environ['PJLAB_STRESS_ONLY_TRANSPORT']]
results = []
for backend, transport in cases:
    name = ('smoke-' if smoke else 'stress-') + backend + '-' + transport
    env = dict(os.environ, PJLAB_TEST_BACKEND=backend, PJLAB_TEST_TRANSPORT=transport)
    for key in ['PJLAB_EXPECT_ALIGNED', 'PJLAB_VERIFY_TEXT', 'PJLAB_CAPS_QUICK']:
        env.pop(key, None)
    env.update(PJLAB_STRESS_CYCLES='10' if smoke else '400',
               PJLAB_STRESS_BURSTS='3' if smoke else '80',
               PJLAB_STRESS_IME='9' if smoke else '160')
    print('START_CASE', name, flush=True)
    started = time.time()
    log_path = root / (name + '.log')
    if log_path.exists():
        log_path.rename(root / (name + '-previous-' + str(int(time.time())) + '.log'))
    with log_path.open('w') as log:
        completed = subprocess.run(['python3', '-u', str(Path.home() / '.cache/pjlab_caps_pressure.py')],
                                   env=env, stdout=log, stderr=subprocess.STDOUT, timeout=600)
    report = Path.home() / ('.cache/pjlab-caps-pressure-' + backend + '-' + transport + '.json')
    assert report.exists() and report.stat().st_mtime >= started, (name, 'No fresh report', log_path)
    data = json.loads(report.read_text())
    (root / (name + '.json')).write_text(json.dumps(data, ensure_ascii=False, indent=2) + '\n')
    summary = dict(data.get('pressure', {}), case=name, returncode=completed.returncode,
                   errors=data.get('errors', []))
    results.append(summary)
    (root / ('smoke-summary.json' if smoke else 'stress-summary.json')).write_text(json.dumps(results, indent=2) + '\n')
    print('CASE_RESULT', json.dumps(summary), flush=True)
    assert completed.returncode == 0 and not summary['errors'] and summary.get('passed'), (name, log_path)
print('ALL_PRESSURE_CASES_PASSED', flush=True)
