import os
from pathlib import Path

base = (Path.home() / '.cache/pjlab_caps_route_repro.py').read_text()
workload = r'''
reconnect_records = []
restart_process = None
restart_before_pid = 0
restart_expected = False
state_dir = Path.home() / '.local/state/uu-remote-for-linux'

def status():
    return json.loads((state_dir / 'input-bridge-status.json').read_text())

def restart_bridge(expected):
    global restart_process, restart_before_pid, restart_expected
    restart_before_pid = status()['pid']
    restart_expected = expected
    restart_process = subprocess.Popen(['systemctl', '--user', 'restart', 'uu-remote-for-linux-input.service'])

def restart_ready():
    if restart_process.poll() is None:
        return False
    assert restart_process.returncode == 0
    state = status()
    if state.get('pid') == restart_before_pid or state.get('capture_state') != 'active' or len(state.get('capture_streams', [])) != 2:
        return False
    import configparser
    endpoint = configparser.ConfigParser()
    endpoint.read(Path.home() / '.local/share/uu-remote-for-linux/wineprefix/drive_c/uu-remote-input-bridge.endpoint')
    if endpoint.getint('bridge', 'lock_state_valid', fallback=0) != 1:
        return False
    if bool(endpoint.getint('bridge', 'lock_mask') & 1) != restart_expected:
        return False
    assert native_caps() == restart_expected and keymap.get_caps_lock_state() == restart_expected
    row = {'before_pid': restart_before_pid, 'after_pid': state['pid'],
           'expected_caps': restart_expected, 'backend': os.environ['GDK_BACKEND'], 'passed': True}
    reconnect_records.append(row)
    print('BRIDGE_RECONNECT_PASSED', json.dumps(row), flush=True)
    return True

def steps():
    yield lambda: switch_engine('rime')
    yield plain.grab_focus
    for index in range(6):
        desired = index % 2 == 0
        if native_caps() != desired:
            yield lambda: key(58, True)
            yield lambda: key(58, False)
        yield lambda expected=desired: restart_bridge(expected)
        yield lambda: None
        yield lambda: plain.set_text('')
        for code in [30, 48, 46]:
            yield lambda c=code: key(c, True)
            yield lambda c=code: key(c, False)
        yield lambda i=index, e=desired: check_text('reconnect-' + str(i), 'ABC' if e else 'abc')
    yield lambda: snapshot('reconnection-complete')
'''
start = base.index('def steps():')
end = base.index('\n\niterator = steps()', start)
base = base[:start] + workload + base[end:]
base = base.replace('        next(iterator)()', '''        if restart_process is not None and len(reconnect_records) < getattr(tick, 'restarts', 0):
            assert time.monotonic() - tick.restart_started < 25, 'Bridge reconnect timed out'
            if not restart_ready():
                return True
        operation = next(iterator)
        previous = restart_process
        operation()
        if restart_process is not previous:
            tick.restarts = getattr(tick, 'restarts', 0) + 1
            tick.restart_started = time.monotonic()''', 1)
base = base.replace('if bridge_pid and not bridge_paused:', 'if False and bridge_pid and not bridge_paused:', 1)
base = base.replace("'text_records': text_records,", "'text_records': text_records, 'reconnect_records': reconnect_records,", 1)
base = base.replace("'.cache/pjlab-caps-route-repro.json'", repr('.cache/pjlab-caps-reconnect-' + os.environ.get('PJLAB_TEST_BACKEND', 'wayland') + '.json'))
base = base.replace("print('ROUTE_EXPERIMENT_COMPLETE', flush=True)", "assert len(reconnect_records) == 6\nprint('ALL_BRIDGE_RECONNECTS_PASSED', flush=True)")
exec(compile(base, 'pjlab-caps-reconnection-runtime.py', 'exec'), {'__name__': '__main__'})
