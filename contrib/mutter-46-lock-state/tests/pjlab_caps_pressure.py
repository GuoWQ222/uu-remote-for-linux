"""Run a real GTK/IBus/Wayland/XWayland keyboard regression workload.

Uses the focused diagnostic shell already exercised by the route reproducer.
No fake backend, lock reset API, or state-mirroring helper is used.
"""
import os
from pathlib import Path

base = (Path.home() / '.cache/pjlab_caps_route_repro.py').read_text()
backend_name = os.environ.get('PJLAB_TEST_BACKEND', 'wayland')
transport_name = os.environ.get('PJLAB_TEST_TRANSPORT', 'native')
suffix = backend_name + '-' + transport_name

workload = r'''
import configparser
import random
import socket
import struct

pressure_started = time.monotonic()
pressure_summary = {'backend': os.environ['GDK_BACKEND'],
                    'transport': os.environ.get('PJLAB_TEST_TRANSPORT', 'native'),
                    'key_events': 0, 'caps_toggles': 0, 'text_characters': 0,
                    'text_checks': 0, 'state_checks': 0, 'ime_cycles': 0,
                    'burst_groups': 0, 'max_settle_seconds': 0.0}
pressure_summary['monitor_moves'] = 0
consume_phase = 'both'
wait_operation = None
udp = None
udp_sequence = 0
endpoint_path = Path.home() / '.local/share/uu-remote-for-linux/wineprefix/drive_c/uu-remote-input-bridge.endpoint'
native_key = key

if pressure_summary['transport'] == 'uu-udp':
    endpoint = configparser.ConfigParser()
    endpoint.read(endpoint_path)
    udp_token = bytes.fromhex(endpoint.get('bridge', 'token'))
    udp_address = ('127.0.0.1', endpoint.getint('bridge', 'port'))
    udp = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    udp_header = struct.Struct('<IBBHI16s')

    def send_packet(kind, payload):
        global udp_sequence
        udp_sequence += 1
        udp.sendto(udp_header.pack(0x50495555, 1, kind, udp_header.size + len(payload),
                                  udp_sequence, udp_token) + payload, udp_address)

    def hello():
        send_packet(1, struct.pack('<II', os.getpid(), 8))

    hello()

def key(code, down):
    pressure_summary['key_events'] += 1
    if code == 58 and down:
        pressure_summary['caps_toggles'] += 1
    if udp is None:
        native_key(code, down)
    else:
        vk = {58: 0x14, 42: 0xa0, 54: 0xa1, 29: 0xa2,
              56: 0xa4, 1: 0x1b, 15: 9, 60: 0x71}.get(code, 0)
        send_packet(3, struct.pack('<HHIIQ', vk, code, 8 | (0 if down else 2), 0, 0))
        (pressed_keys.add if down else pressed_keys.discard)(code)

def tap(code):
    key(code, True)
    key(code, False)

class Wait:
    def __init__(self, predicate, description, timeout=3.0):
        self.predicate = predicate
        self.description = description
        self.timeout = timeout
        self.started = None

    def poll(self):
        if self.started is None:
            self.started = time.monotonic()
        done = self.predicate()
        elapsed = time.monotonic() - self.started
        if done:
            pressure_summary['max_settle_seconds'] = max(pressure_summary['max_settle_seconds'], elapsed)
            return True
        assert elapsed < self.timeout, ('Timeout', self.description, elapsed)
        return False

def text_matches(expected):
    actual = plain.get_text()
    if len(actual) < len(expected):
        return False
    assert actual == expected, ('TEXT_MISMATCH', expected, actual)
    return True

def state_matches(expected):
    if native_caps() != expected or keymap.get_caps_lock_state() != expected:
        return False
    if udp is not None:
        info = configparser.ConfigParser()
        info.read(endpoint_path)
        if info.getint('bridge', 'lock_state_valid', fallback=0) != 1:
            return False
        if bool(info.getint('bridge', 'lock_mask', fallback=-1) & 1) != expected:
            return False
    return True

def checked_state(expected):
    def finish():
        if not state_matches(expected):
            return False
        pressure_summary['state_checks'] += 1
        return True
    return Wait(finish, 'caps=' + str(expected))

def checked_text(expected):
    def finish():
        if not text_matches(expected):
            return False
        pressure_summary['text_checks'] += 1
        pressure_summary['text_characters'] += len(expected)
        return True
    return Wait(finish, 'text=' + expected[:40])

def pause(seconds):
    start = time.monotonic()
    return Wait(lambda: time.monotonic() - start >= seconds, 'settle')

def progress(phase, index):
    row = dict(pressure_summary, phase=phase, index=index,
               elapsed=round(time.monotonic() - pressure_started, 2))
    print('PRESSURE_PROGRESS', json.dumps(row), flush=True)
    label.set_text(phase + ' ' + str(index))

def monitor_x():
    display = Gdk.Display.get_default()
    return display.get_monitor_at_window(window.get_window()).get_geometry().x

def move_to_other_monitor():
    before = monitor_x()
    direction = 106 if before == 0 else 105
    assert window.has_toplevel_focus()
    for code in [125, 42, direction]:
        native_key(code, True)
    for code in [direction, 42, 125]:
        native_key(code, False)
    pressure_summary['key_events'] += 6
    return before

def steps():
    global consume_phase
    rng = random.Random(20261019)
    yield lambda: switch_engine('rime')
    yield plain.grab_focus
    yield pause(.1)
    caps_expected = native_caps()
    yield checked_state(caps_expected)
    cycles = int(os.environ.get('PJLAB_STRESS_CYCLES', '400'))
    for i in range(cycles):
        if udp is not None and i % 50 == 0:
            yield hello
        yield lambda: plain.set_text('')
        yield lambda: tap(58)
        caps_expected = not caps_expected
        yield checked_state(caps_expected)
        shifted = rng.choice([False, True])
        shift_key = rng.choice([42, 54])
        if shifted:
            yield lambda s=shift_key: key(s, True)
        for code in [30, 2, 44, 10]:
            yield lambda c=code: tap(c)
        if shifted:
            yield lambda s=shift_key: key(s, False)
        upper = caps_expected != shifted
        expected = ('A' if upper else 'a') + ('!' if shifted else '1') + ('Z' if upper else 'z') + ('(' if shifted else '9')
        yield checked_text(expected)
        yield checked_state(caps_expected)
        if i % 50 == 0:
            yield lambda n=i: progress('normal-shift', n)
        if i % 50 == 49:
            previous_x = monitor_x()
            yield move_to_other_monitor
            yield Wait(lambda x=previous_x: monitor_x() != x, 'move diagnostic to other monitor')
            pressure_summary['monitor_moves'] += 1
            yield checked_state(caps_expected)
    burst_groups = int(os.environ.get('PJLAB_STRESS_BURSTS', '80'))
    burst_size = int(os.environ.get('PJLAB_STRESS_BURST_SIZE', '16'))
    for i in range(burst_groups):
        yield lambda: plain.set_text('')
        expected = ''
        for _ in range(burst_size):
            caps_expected = not caps_expected
            expected += ('A1Z' if caps_expected else 'a1z')
        def burst(count=burst_size):
            for _ in range(count):
                tap(58)
                for code in [30, 2, 44]:
                    tap(code)
        yield burst
        yield checked_text(expected)
        yield checked_state(caps_expected)
        pressure_summary['burst_groups'] += 1
        if i % 10 == 0:
            yield lambda n=i: progress('rapid-bursts', n)
    ime_cycles = int(os.environ.get('PJLAB_STRESS_IME', '160'))
    for i in range(ime_cycles):
        consume_phase = ['both', 'press', 'release'][i % 3]
        yield entries['wayland'].grab_focus
        yield lambda: switch_engine('pjlab-caps-repro')
        yield pause(.04)
        yield lambda: tap(58)
        caps_expected = not caps_expected
        yield pause(.04)
        yield lambda: switch_engine('rime')
        yield raw.grab_focus
        yield checked_state(caps_expected)
        yield plain.grab_focus
        yield lambda: plain.set_text('')
        for code in [30, 48, 46]:
            yield lambda c=code: tap(c)
        yield checked_text('ABC' if caps_expected else 'abc')
        pressure_summary['ime_cycles'] += 1
        if i % 20 == 0:
            yield lambda n=i: progress('filtered-lock-events', n)
    if caps_expected:
        yield plain.grab_focus
        yield lambda: tap(58)
        caps_expected = False
        yield checked_state(False)
    yield lambda: switch_engine('rime')
    yield lambda: progress('complete', 0)
    pressure_summary['elapsed_seconds'] = round(time.monotonic() - pressure_started, 3)
    pressure_summary['consumed_events'] = len([e for e in ime_events if e['consumed']])
    assert pressure_summary['consumed_events'] >= ime_cycles // 2, ('IME was not exercised', pressure_summary)
    pressure_summary['passed'] = True
'''

start = base.index('def steps():')
end = base.index('\n\niterator = steps()', start)
base = base[:start] + workload + base[end:]
base = base.replace('global focus_wait, bridge_paused', 'global focus_wait, bridge_paused, wait_operation', 1)
base = base.replace('        next(iterator)()', '''        if wait_operation is not None:
            if wait_operation.poll():
                wait_operation = None
            return True
        operation = next(iterator)
        if isinstance(operation, Wait):
            wait_operation = operation
        else:
            operation()''', 1)
base = base.replace("if bridge_pid and not bridge_paused:", "if False and bridge_pid and not bridge_paused:", 1)
base = base.replace('GLib.timeout_add(180, tick)', 'GLib.timeout_add(10, tick)', 1)
# Focus navigation must not depend on the UU endpoint handshake being ready.
focus_start = base.index('            if focus_wait == 1:')
focus_end = base.index('            assert focus_wait < 150', focus_start)
base = base[:focus_start] + base[focus_start:focus_end].replace('key(', 'native_key(') + base[focus_end:]
base = base.replace("'.cache/pjlab-caps-route-repro.json'", repr('.cache/pjlab-caps-pressure-' + suffix + '.json'))
base = base.replace("'text_records': text_records,", "'text_records': text_records, 'pressure': pressure_summary,", 1)
base = base.replace("return True\n        return False\n\n\nfactory.add_engine", "return True\n        return False\n\n\nfactory.add_engine", 1)
base = base.replace("item = {'keyval': keyval, 'state': int(state), 'consumed': True}",
    "consume = (consume_phase == 'both' or (consume_phase == 'release') == bool(state & (1 << 30)))\n            item = {'keyval': keyval, 'state': int(state), 'consumed': consume, 'pattern': consume_phase}")
base = base.replace("print('IME_CONSUMED', json.dumps(item), flush=True)\n            return True", "return consume")
base = base.replace("print('ROUTE_EXPERIMENT_COMPLETE', flush=True)", "print('PRESSURE_TEST_PASSED', json.dumps(pressure_summary), flush=True)")
compile(base, 'pjlab-caps-pressure-runtime.py', 'exec')
exec(compile(base, 'pjlab-caps-pressure-runtime.py', 'exec'), {'__name__': '__main__'})
