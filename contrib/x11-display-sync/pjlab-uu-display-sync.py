#!/usr/bin/python3
"""Forward native X11 layout changes to the matching UU Wine prefix."""
import ctypes as C
import fcntl
import json
import os
from pathlib import Path
import signal
import subprocess
import time

PREFIX = Path.home() / '.local/share/uu-remote-for-linux/wineprefix'
STATE = Path.home() / '.local/state/uu-remote-for-linux'
HELPER = '/usr/local/libexec/pjlab-uu-display-refresh.exe'
STATUS = STATE / 'display-sync-status.json'
STATE.mkdir(parents=True, exist_ok=True)
lock = (STATE / 'display-sync.lock').open('w')
try:
    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
except BlockingIOError:
    raise SystemExit(0)

class Monitor(C.Structure):
    _fields_ = [('name', C.c_ulong), ('primary', C.c_int), ('automatic', C.c_int),
                ('noutput', C.c_int), ('x', C.c_int), ('y', C.c_int),
                ('width', C.c_int), ('height', C.c_int), ('mwidth', C.c_int),
                ('mheight', C.c_int), ('outputs', C.POINTER(C.c_ulong))]

x = C.CDLL('libX11.so.6')
r = C.CDLL('libXrandr.so.2')
x.XOpenDisplay.restype = C.c_void_p
x.XDefaultRootWindow.argtypes = [C.c_void_p]
x.XDefaultRootWindow.restype = C.c_ulong
x.XGetGeometry.argtypes = [C.c_void_p, C.c_ulong, C.POINTER(C.c_ulong)] + [C.POINTER(C.c_int)]*2 + [C.POINTER(C.c_uint)]*4
r.XRRGetMonitors.argtypes = [C.c_void_p, C.c_ulong, C.c_int, C.POINTER(C.c_int)]
r.XRRGetMonitors.restype = C.POINTER(Monitor)
r.XRRFreeMonitors.argtypes = [C.POINTER(Monitor)]
d = x.XOpenDisplay(None)
if not d:
    raise SystemExit('Cannot open native X11 display')
root = x.XDefaultRootWindow(d)

def layout():
    rr, xx, yy = C.c_ulong(), C.c_int(), C.c_int()
    w, h, border, depth = (C.c_uint() for _ in range(4))
    if not x.XGetGeometry(d, root, C.byref(rr), C.byref(xx), C.byref(yy), C.byref(w), C.byref(h), C.byref(border), C.byref(depth)):
        raise RuntimeError('Cannot read current X11 root geometry')
    count = C.c_int()
    monitors = r.XRRGetMonitors(d, root, True, C.byref(count))
    try:
        details = sorted((int(m.name), bool(m.primary), m.x, m.y, m.width, m.height)
                         for m in monitors[:count.value]) if monitors else []
    finally:
        if monitors:
            r.XRRFreeMonitors(monitors)
    return (w.value, h.value, tuple(details))

def wine_server():
    for proc in Path('/proc').iterdir():
        if not proc.name.isdigit():
            continue
        try:
            if (proc / 'comm').read_text().strip() != 'wineserver':
                continue
            environment = (proc / 'environ').read_bytes().split(b'\0')
            if os.fsencode('WINEPREFIX=' + str(PREFIX)) in environment:
                return int(proc.name)
        except (OSError, ProcessLookupError):
            pass
    return None

def write_status(**fields):
    fields.update(pid=os.getpid(), updated_at=time.strftime('%Y-%m-%dT%H:%M:%S%z'))
    temp = STATUS.with_suffix('.tmp')
    temp.write_text(json.dumps(fields, ensure_ascii=False) + '\n')
    temp.replace(STATUS)

running = True
def stop(*_args):
    global running
    running = False
signal.signal(signal.SIGTERM, stop)
signal.signal(signal.SIGINT, stop)
last = pending = None
changed = 0.0
retry_at = 0.0
retries = 0
server = None
server_checked = 0.0
while running:
    now = time.monotonic()
    if now - server_checked >= 1.0:
        server = wine_server()
        server_checked = now
    current = (server, layout())
    if current != pending:
        pending, changed, retries = current, now, 0
        retry_at = now
    if server and current != last and now - changed >= .12 and now >= retry_at:
        env = os.environ.copy()
        env.update(WINEPREFIX=str(PREFIX), WINEDEBUG='-all')
        # Wine's forced cache refresh also asks Vulkan to map RandR outputs.
        # NVIDIA 580's vkGetRandROutputDisplayEXT fails on this headless
        # output and then poisons the next native CRTC growth transaction.
        # This short-lived notification helper never renders with Vulkan.
        # Restrict the workaround to its child environment; the running UU
        # client, GPU encoding and the user's applications keep their drivers.
        env.update(VK_DRIVER_FILES='/dev/null', VK_ICD_FILENAMES='/dev/null')
        try:
            result = subprocess.run(['/usr/bin/wine', HELPER, str(current[1][0]), str(current[1][1])],
                env=env, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                stderr=subprocess.PIPE, text=True, timeout=8)
            detail = result.stdout.strip()[-800:]
            code = result.returncode
            if code and result.stderr.strip():
                detail = (detail + ' ' + result.stderr.strip().splitlines()[-1]).strip()[-800:]
        except subprocess.TimeoutExpired:
            code, detail = 124, 'Wine display notification timed out'
        print(time.strftime('%T'), 'layout=', current[1], 'exit=', code, detail, flush=True)
        write_status(status='synchronized' if code == 0 else 'error',
                     wine_server_pid=server, layout=current[1], exit_code=code, detail=detail)
        if code == 0 or code == 78 or retries >= 2:
            last = current
        else:
            retries += 1
            retry_at = time.monotonic() + 2
    time.sleep(.05)
