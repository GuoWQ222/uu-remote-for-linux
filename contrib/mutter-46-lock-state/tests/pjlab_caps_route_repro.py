import json
import os
from pathlib import Path
import subprocess
import time

for line in subprocess.check_output(['systemctl', '--user', 'show-environment'], text=True).splitlines():
    key, _, value = line.partition('=')
    if key in ['DISPLAY', 'XAUTHORITY', 'WAYLAND_DISPLAY']:
        os.environ[key] = value
os.environ['GDK_BACKEND'] = os.environ.get('PJLAB_TEST_BACKEND', 'wayland')
import gi
gi.require_version('Gtk', '3.0')
gi.require_version('IBus', '1.0')
from gi.repository import Gtk, Gdk, Gio, GLib, IBus

IBus.init()
ibus_bus = IBus.Bus()
original_engine = subprocess.run(['ibus', 'engine'], capture_output=True, text=True).stdout.strip() or 'rime'
factory = IBus.Factory.new(ibus_bus.get_connection())
ime_events = []
engine_pending = False


def switch_engine(name):
    global engine_pending
    engine_pending = True

    def complete(bus, result, _data):
        global engine_pending
        try:
            assert bus.set_global_engine_async_finish(result)
            print('ENGINE_ACTIVE', name, flush=True)
        except Exception as exc:
            error.append(str(exc))
        engine_pending = False

    ibus_bus.set_global_engine_async(name, 5000, None, complete, None)


class ConsumeCapsEngine(IBus.Engine):
    __gtype_name__ = 'PjlabCapsReproEngine'

    def do_process_key_event(self, keyval, keycode, state):
        if keyval == IBus.KEY_Caps_Lock:
            item = {'keyval': keyval, 'state': int(state), 'consumed': True}
            ime_events.append(item)
            print('IME_CONSUMED', json.dumps(item), flush=True)
            return True
        return False


factory.add_engine('pjlab-caps-repro', ConsumeCapsEngine.__gtype__)
component = IBus.Component.new('org.pjlab.CapsRepro', 'Caps event reproduction',
                              '1', 'MIT', 'PJLAB', '', '', '')
component.add_engine(IBus.EngineDesc.new('pjlab-caps-repro', 'PJLAB Caps Repro',
                                       'Controlled consumed-Caps reproduction', 'en', 'MIT', 'PJLAB', '', 'us'))
assert ibus_bus.register_component(component)

bus = Gio.bus_get_sync(Gio.BusType.SESSION, None)
session = bus.call_sync('org.gnome.Mutter.RemoteDesktop', '/org/gnome/Mutter/RemoteDesktop',
                       'org.gnome.Mutter.RemoteDesktop', 'CreateSession', None, None,
                       Gio.DBusCallFlags.NONE, 2000, None).unpack()[0]
pressed_keys = set()


def remote(method, params=None):
    return bus.call_sync('org.gnome.Mutter.RemoteDesktop', session,
                         'org.gnome.Mutter.RemoteDesktop.Session', method, params, None,
                         Gio.DBusCallFlags.NONE, 2000, None)


def key(code, pressed):
    remote('NotifyKeyboardKeycode', GLib.Variant('(ub)', (code, pressed)))
    if pressed:
        pressed_keys.add(code)
    else:
        pressed_keys.discard(code)


def native_caps():
    return bool(bus.call_sync('org.gnome.Mutter.RemoteDesktop', session,
                'org.freedesktop.DBus.Properties', 'Get',
                GLib.Variant('(ss)', ('org.gnome.Mutter.RemoteDesktop.Session', 'CapsLockState')),
                None, Gio.DBusCallFlags.NONE, 2000, None).unpack()[0])


window = Gtk.Window(title='PJLAB Caps Lock 通道对照实验（自动结束）')
window.set_default_size(700, 350)
box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL, spacing=15)
box.set_border_width(24)
window.add(box)
box.pack_start(Gtk.Label(label='正在自动比较 Caps Lock 事件通道，请暂时不要操作远端键盘。'), False, False, 0)
raw = Gtk.EventBox()
raw.set_can_focus(True)
raw.set_size_request(640, 60)
raw.add(Gtk.Label(label='原始键盘：不经过输入法'))
box.pack_start(raw, False, False, 0)
entries = {}
for module in ['ibus', 'wayland']:
    entry = Gtk.Entry()
    entry.set_property('im-module', 'ibus' if module == 'wayland' and os.environ['GDK_BACKEND'] == 'x11' else module)
    entry.set_placeholder_text(module + ' input route')
    box.pack_start(entry, False, False, 0)
    entries[module] = entry
plain = Gtk.Entry()
plain.set_property('im-module', 'gtk-im-context-simple')
plain.set_placeholder_text('Caps Lock: abc -> ABC -> abc')
box.pack_start(plain, False, False, 0)
text_records = []
preedits = []
entries['ibus'].connect('preedit-changed', lambda _e, value: preedits.append(value))
label = Gtk.Label(label='准备中')
box.pack_start(label, False, False, 0)
keymap = Gdk.Keymap.get_for_display(Gdk.Display.get_default())
records = []
events = []
error = []
focus_wait = 0


def observe(widget, event):
    if event.type not in (Gdk.EventType.KEY_PRESS, Gdk.EventType.KEY_RELEASE):
        return False
    name = Gdk.keyval_name(event.keyval)
    if name in ['Caps_Lock', 'a', 'A']:
        events.append({'field': next((k for k, v in entries.items() if v is widget), 'raw'),
                       'key': name, 'press': event.type == Gdk.EventType.KEY_PRESS,
                       'mask': int(event.state), 'time': time.monotonic()})
    return widget is raw


for widget in [raw, *entries.values()]:
    widget.connect('event', observe)
raw.connect('button-press-event', lambda *_: (raw.grab_focus(), False)[1])


def snapshot(phase):
    row = {'phase': phase, 'native': native_caps(), 'wayland': keymap.get_caps_lock_state(),
           'modifiers': int(keymap.get_modifier_state()), 'focused': window.has_toplevel_focus(),
           'time': time.monotonic()}
    row['split'] = row['native'] != row['wayland']
    records.append(row)
    print(json.dumps(row), flush=True)
    label.set_text(phase + f" | native={row['native']} Wayland={row['wayland']}")


def check_text(phase, expected=None, widget=plain):
    row = {'phase': phase, 'text': widget.get_text(), 'preedits': list(preedits),
           'native': native_caps(), 'wayland': keymap.get_caps_lock_state()}
    text_records.append(row)
    print('ACTUAL_TEXT', json.dumps(row, ensure_ascii=False), flush=True)
    assert row['native'] == row['wayland'], row
    if expected is not None:
        assert row['text'] == expected, row


def steps():
    yield lambda: switch_engine(original_engine)
    yield lambda: snapshot('baseline')
    quick = os.environ.get('PJLAB_CAPS_QUICK') == '1'
    for name, widget in ([] if quick else [('raw', raw), *entries.items()]):
        yield widget.grab_focus
        yield lambda n=name: snapshot(n + ':before')
        for index in range(4):
            yield lambda: key(58, True)
            yield lambda: key(58, False)
            yield lambda n=name, i=index: snapshot(n + ':cycle-' + str(i + 1))
    for index in range(0 if quick else 6):
        yield entries['wayland'].grab_focus
        yield lambda: key(58, True)
        yield raw.grab_focus
        yield lambda: key(58, False)
        yield lambda i=index: snapshot('focus-change:cycle-' + str(i + 1))
    for index in range(0 if quick else 4):
        yield raw.grab_focus
        yield lambda: globals().__setitem__('allow_shell_focus', True)
        yield lambda: key(56, True)
        yield lambda: key(60, True)
        yield lambda: key(60, False)
        yield lambda: key(56, False)
        yield lambda: snapshot('shell-dialog:opened')
        yield lambda: key(58, True)
        yield lambda: key(58, False)
        yield lambda: key(1, True)
        yield lambda: key(1, False)
        yield lambda i=index: snapshot('shell-dialog:cycle-' + str(i + 1))
        yield lambda: globals().__setitem__('allow_shell_focus', False)
    for index in range(int(os.environ.get('PJLAB_CAPS_CONSUME_CYCLES', '2'))):
        yield entries['wayland'].grab_focus
        yield lambda: switch_engine('pjlab-caps-repro')
        yield lambda: None
        yield lambda: None
        yield lambda: snapshot('consume-caps:before')
        yield lambda: key(58, True)
        yield lambda: key(58, False)
        yield lambda: None
        yield lambda: switch_engine(original_engine)
        yield raw.grab_focus
        yield lambda i=index: snapshot('consume-caps:cycle-' + str(i + 1))
    if os.environ.get('PJLAB_VERIFY_TEXT') == '1':
        yield plain.grab_focus
        if native_caps():
            yield lambda: key(58, True)
            yield lambda: key(58, False)
        for index, expected in enumerate(['abc', 'ABC', 'abc']):
            yield lambda: plain.set_text('')
            for code in [30, 48, 46]:
                yield lambda c=code: key(c, True)
                yield lambda c=code: key(c, False)
            yield lambda i=index, e=expected: check_text('caps-' + str(i), e)
            if index < 2:
                yield lambda: key(58, True)
                yield lambda: key(58, False)
        yield entries['ibus'].grab_focus
        for index in range(2):
            yield lambda: key(1, True)
            yield lambda: key(1, False)
            yield lambda: entries['ibus'].set_text('')
            yield preedits.clear
            yield lambda: key(42, True)
            yield lambda: key(42, False)
            for code in [49, 23, 35, 30, 24]:
                yield lambda c=code: key(c, True)
                yield lambda c=code: key(c, False)
            if any(preedits):
                yield lambda: key(57, True)
                yield lambda: key(57, False)
            yield lambda i=index: check_text('rime-shift-' + str(i), widget=entries['ibus'])
        yield lambda: key(1, True)
        yield lambda: key(1, False)


iterator = steps()


def tick():
    global focus_wait, bridge_paused
    try:
        if engine_pending:
            return True
        assert not error, error
        if not window.has_toplevel_focus() and not globals().get('allow_shell_focus', False):
            focus_wait += 1
            if focus_wait == 1:
                print('WAITING_FOR_FOCUS', flush=True)
                # Only navigate window focus; do not send text or lock keys.
                key(1, True)
                key(1, False)
                GLib.timeout_add(300, lambda: (window.present(), False)[1])
                key(56, True)
                key(15, True)
                key(15, False)
                key(56, False)
                return True
            if focus_wait == 12:
                remote('NotifyPointerMotionRelative', GLib.Variant('(dd)', (-30000.0, -30000.0)))
                remote('NotifyPointerMotionRelative', GLib.Variant('(dd)', (400.0, 165.0)))
                remote('NotifyPointerButton', GLib.Variant('(ib)', (272, True)))
                remote('NotifyPointerButton', GLib.Variant('(ib)', (272, False)))
                return True
            assert focus_wait < 150, 'Diagnostic window lost focus; refusing to inject keys'
            return True
        focus_wait = 0
        if bridge_pid and not bridge_paused:
            import signal
            os.kill(bridge_pid, signal.SIGSTOP)
            bridge_paused = True
        next(iterator)()
        return True
    except StopIteration:
        Gtk.main_quit()
    except Exception as exc:
        error.append(str(exc))
        Gtk.main_quit()
    return False


bridge_was_active = subprocess.run(['systemctl', '--user', 'is-active', '--quiet',
                                  'uu-remote-for-linux-input.service']).returncode == 0
bridge_pid = int(subprocess.check_output(['systemctl', '--user', 'show',
                 'uu-remote-for-linux-input.service', '-p', 'MainPID', '--value'], text=True)) if bridge_was_active else 0
bridge_paused = False
started = False
try:
    remote('Start')
    started = True
    window.show_all()
    window.present()
    raw.grab_focus()
    GLib.timeout_add(1000, lambda: (GLib.timeout_add(180, tick), False)[1])
    Gtk.main()
finally:
    try:
        if started:
            for code in list(pressed_keys):
                key(code, False)
            remote('Stop')
    finally:
        ibus_bus.set_global_engine(original_engine)
        window.destroy()
        if bridge_paused:
            import signal
            try:
                os.kill(bridge_pid, signal.SIGCONT)
            except ProcessLookupError:
                # The desktop autostart can replace the old transient bridge.
                # A terminated process no longer needs resuming.
                print('BRIDGE_REPLACED_DURING_TEST', bridge_pid, flush=True)
        result = {'records': records, 'events': events, 'ime_events': ime_events,
                  'text_records': text_records, 'backend': os.environ['GDK_BACKEND'], 'errors': error}
        path = Path.home() / '.cache/pjlab-caps-route-repro.json'
        if path.exists():
            import shutil
            shutil.copy2(path, path.with_name('pjlab-caps-route-repro-before-' + str(int(time.time())) + '.json'))
        path.write_text(json.dumps(result, indent=2) + '\n')
assert not error, error
if os.environ.get('PJLAB_EXPECT_ALIGNED') == '1':
    assert ime_events and all(e['consumed'] for e in ime_events)
    assert not any(r['split'] for r in records), [r for r in records if r['split']]
if os.environ.get('PJLAB_VERIFY_TEXT') == '1':
    shifts = [r for r in text_records if r['phase'].startswith('rime-shift-')]
    assert len(shifts) == 2
    assert sorted(bool(any(p for p in r['preedits'])) for r in shifts) == [False, True], shifts
    assert sorted(r['text'] for r in shifts) == sorted(['nihao', '你好']), shifts
print('ROUTE_EXPERIMENT_COMPLETE', flush=True)
