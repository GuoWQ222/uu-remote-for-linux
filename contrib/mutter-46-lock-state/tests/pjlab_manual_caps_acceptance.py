import json
import os
from pathlib import Path
import subprocess
import time

for line in subprocess.check_output(['systemctl', '--user', 'show-environment'], text=True).splitlines():
    key, _, value = line.partition('=')
    if key in ['DISPLAY', 'XAUTHORITY', 'WAYLAND_DISPLAY']:
        os.environ[key] = value
backend = os.environ.get('PJLAB_TEST_BACKEND', 'wayland')
os.environ['GDK_BACKEND'] = backend
import gi
gi.require_version('Gtk', '3.0')
from gi.repository import Gtk, Gdk, Gio, GLib

bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
session = bus.call_sync('org.gnome.Mutter.RemoteDesktop', '/org/gnome/Mutter/RemoteDesktop',
                       'org.gnome.Mutter.RemoteDesktop', 'CreateSession', None, None,
                       Gio.DBusCallFlags.NONE, 2000, None).unpack()[0]
window = Gtk.Window(title='PJLAB 输入最终验收 · ' + ('Wayland' if backend == 'wayland' else 'XWayland'))
window.set_default_size(820, 580)
window.set_position(Gtk.WindowPosition.CENTER)
box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=16)
box.set_border_width(26)
window.add(box)
title = Gtk.Label()
title.set_markup('<span size="20000">输入最终验收 · ' + ('Wayland' if backend == 'wayland' else 'XWayland') + '</span>')
box.pack_start(title, False, False, 0)
instruction = Gtk.Label(label='请在远程客户端中实际输入。以下三个英文框会直接输入英文。\n测试完成后，可关闭此窗口。窗口不注入任何按键。')
instruction.set_line_wrap(True)
box.pack_start(instruction, False, False, 0)
entries = []
for expected, description in [('abc', '本机 Caps Lock 关闭：输入 abc'),
                              ('ABC', '本机 Caps Lock 开启：输入 abc'),
                              ('abc', '本机 Caps Lock 再次关闭：输入 abc')]:
    entry = Gtk.Entry()
    entry.set_property('im-module', 'gtk-im-context-simple')
    entry.set_placeholder_text(description)
    box.pack_start(entry, False, False, 0)
    entries.append(entry)
rime = Gtk.Entry()
rime.set_property('im-module', 'ibus')
rime.set_placeholder_text('左 Shift 验收：输入 nihao，切换后再输入 nihao（中文请空格上屏）')
box.pack_start(rime, False, False, 0)
state_label = Gtk.Label(label='等待输入')
box.pack_start(state_label, False, False, 0)
output = Path.home() / ('.cache/pjlab-pressure-evidence/manual-' + backend + '.json')
keymap = Gdk.Keymap.get_for_display(Gdk.Display.get_default())
started = time.strftime('%Y-%m-%dT%H:%M:%S%z')
history = []

def read_caps():
    return bool(bus.call_sync('org.gnome.Mutter.RemoteDesktop', session,
                'org.freedesktop.DBus.Properties', 'Get', GLib.Variant('(ss)',
                ('org.gnome.Mutter.RemoteDesktop.Session', 'CapsLockState')), None,
                Gio.DBusCallFlags.NONE, 2000, None).unpack()[0])

def update(*_):
    try:
        native, client = read_caps(), keymap.get_caps_lock_state()
        values = [entry.get_text() for entry in entries]
        row = {'time': time.strftime('%H:%M:%S'), 'native_caps': native,
               'client_caps': client, 'texts': values, 'rime_text': rime.get_text(),
               'focused': window.has_toplevel_focus()}
        if not history or any(row[k] != history[-1][k] for k in row if k != 'time'):
            history.append(row)
        passed = values == ['abc', 'ABC', 'abc']
        state_label.set_text(('大小写三项已通过' if passed else '请按顺序填写三个英文框') +
            f'   后端 Caps={int(native)}，应用 Caps={int(client)}')
        output.write_text(json.dumps({'backend': backend, 'started_at': started,
            'caps_text_passed': passed, 'latest': row, 'history': history}, ensure_ascii=False, indent=2) + '\n')
    except Exception as exc:
        print(type(exc).__name__, str(exc), flush=True)
    return True

for entry in [*entries, rime]:
    entry.connect('changed', update)
window.connect('destroy', Gtk.main_quit)
window.show_all()
window.present()
entries[0].grab_focus()
GLib.timeout_add(150, update)
GLib.timeout_add_seconds(1200, lambda: (window.destroy(), False)[1])
Gtk.main()
print('MANUAL_ACCEPTANCE_WINDOW_CLOSED', backend, flush=True)
