import json
import os
from pathlib import Path
import subprocess
import time
import signal

for line in subprocess.check_output(['systemctl', '--user', 'show-environment'], text=True).splitlines():
    key, _, value = line.partition('=')
    if key in ['DISPLAY', 'XAUTHORITY']:
        os.environ[key] = value
os.environ['GDK_BACKEND'] = 'x11'

import gi
gi.require_version('Gtk', '3.0')
from gi.repository import Gtk, GLib

status = Path.home() / '.local/state/uu-remote-for-linux/input-bridge-status.json'
samples = []
windows = []
labels = []
progress_bars = []
started = time.monotonic()
counter = 0


def sample():
    state = json.loads(status.read_text())
    gate = os.environ.get('PJLAB_FRAME_START_FILE')
    if gate and (not Path(gate).exists() or state.get('pid') != int(Path(gate).read_text())):
        assert time.monotonic() - started < 40, 'Capture restart did not complete'
        return True
    if not samples and (state.get('capture_state') != 'active' or len(state.get('capture_streams', [])) != 2):
        assert time.monotonic() - started < 20, 'Both capture streams failed to become active'
        return True
    item = {'elapsed': round(time.monotonic() - started, 2),
            'state': state.get('state'), 'pid': state.get('pid'),
            'capture_state': state.get('capture_state'),
            'streams': [{k: stream.get(k) for k in ['node', 'frames', 'native_updates', 'hash_changes',
                        'published_frames', 'transport', 'corrupted_frames_dropped', 'map_failures_dropped']}
                        for stream in state.get('capture_streams', [])]}
    samples.append(item)
    print(json.dumps(item), flush=True)
    return True


from gi.repository import Gdk
display = Gdk.Display.get_default()
positions = [display.get_monitor(i).get_geometry().x + 100 for i in range(display.get_n_monitors())]
assert len(positions) == 2
for index, x in enumerate(positions):
    window = Gtk.Window(title=f'PJLAB display {index + 1} capture check')
    window.set_default_size(600, 280)
    window.set_focus_on_map(False)
    window.set_accept_focus(False)
    box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=24)
    box.set_border_width(28)
    label = Gtk.Label(label=f'PJLAB display {index + 1}')
    progress = Gtk.ProgressBar()
    box.pack_start(label, True, True, 0)
    box.pack_start(progress, True, True, 0)
    labels.append(label)
    progress_bars.append(progress)
    window.add(box)
    window.show_all()
    GLib.timeout_add(250, lambda w=window, px=x: (w.move(px, 950), False)[1])
    windows.append(window)


def tick():
    global counter
    counter += 1
    for index, (label, progress) in enumerate(zip(labels, progress_bars)):
        label.set_markup(f'<span size="24000">PJLAB display {index + 1}\nFrame {counter}</span>')
        progress.set_fraction((counter % 20) / 20)
    return True


def finish():
    sample()
    for window in windows:
        window.destroy()
    Gtk.main_quit()
    return False


sample()
GLib.timeout_add(100, tick)
GLib.timeout_add(2000, sample)
signal.signal(signal.SIGTERM, lambda *_: GLib.idle_add(finish))
GLib.timeout_add(int(os.environ.get('PJLAB_FRAME_DURATION', '16')) * 1000, finish)
Gtk.main()
result = {'samples': samples, 'same_pid': len({s['pid'] for s in samples}) == 1,
          'two_streams_throughout': all(len(s['streams']) == 2 for s in samples)}
assert result['same_pid'] and result['two_streams_throughout']
for before, after in zip(samples[0]['streams'], samples[-1]['streams']):
    assert after['frames'] > before['frames'], (before, after)
    assert after['hash_changes'] > before['hash_changes'], (before, after)
result['passed'] = True
(Path.home() / '.cache/pjlab-dual-frame-check.json').write_text(json.dumps(result, indent=2) + '\n')
print('DUAL_NATIVE_FRAME_CHECK_PASSED', flush=True)
