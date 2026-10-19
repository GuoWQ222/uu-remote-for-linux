import json
import os
from pathlib import Path
import gi
gi.require_version('Gio', '2.0')
from gi.repository import Gio, GLib

bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
def call(name, values=None):
    return bus.call_sync('org.gnome.Mutter.DisplayConfig', '/org/gnome/Mutter/DisplayConfig',
                         'org.gnome.Mutter.DisplayConfig', name, values, None,
                         Gio.DBusCallFlags.NONE, 10000, None).unpack()

state = call('GetCurrentState')
assert len(state[1]) == 2
save = Path.home() / '.cache/pjlab-before-pressure-layout.json'
if not save.exists():
    save.write_text(json.dumps(state, indent=2) + '\n')
logical, offset = [], 0
restore = os.environ.get('PJLAB_RESTORE_LAYOUT') == '1'
desired = [(2560, 1440), (3840, 2160)] if restore else [(3840, 2160), (3840, 2160)]
for i, (monitor, dimensions) in enumerate(zip(sorted(state[1], key=lambda m: m[0][0]), desired)):
    mode = next(m for m in monitor[1] if (m[1], m[2]) == dimensions)
    logical.append((offset, 0, 1.0, 0, i == 0, [(monitor[0][0], mode[0], {})]))
    offset += dimensions[0]
call('ApplyMonitorsConfig', GLib.Variant('(uua(iiduba(ssa{sv}))a{sv})', (state[0], 1, logical, {})))
current = call('GetCurrentState')
for monitor, dimensions in zip(sorted(current[1], key=lambda m: m[0][0]), desired):
    mode = next(m for m in monitor[1] if m[6].get('is-current'))
    assert (mode[1], mode[2]) == dimensions
print('LIVE_LAYOUT_VERIFIED', desired, flush=True)
