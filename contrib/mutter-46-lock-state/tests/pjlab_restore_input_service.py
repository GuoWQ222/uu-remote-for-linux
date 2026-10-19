import os
from pathlib import Path
import subprocess

environment = dict(os.environ)
for line in subprocess.check_output(['systemctl', '--user', 'show-environment'], text=True).splitlines():
    key, _, value = line.partition('=')
    if key in ['DISPLAY', 'XAUTHORITY', 'WAYLAND_DISPLAY', 'XDG_CURRENT_DESKTOP', 'XDG_SESSION_TYPE']:
        environment[key] = value
root = Path.home()
state = root / '.local/state/uu-remote-for-linux'
prefix = root / '.local/share/uu-remote-for-linux'
command = ['systemd-run', '--user', '--collect', '--unit=uu-remote-for-linux-input',
           '--property=Restart=on-failure', '--property=RestartSec=1s', '--property=TimeoutStopSec=5s',
           '--property=StandardOutput=append:' + str(state / 'input-bridge.log'),
           '--property=StandardError=append:' + str(state / 'input-bridge.log')]
for key in ['DISPLAY', 'WAYLAND_DISPLAY', 'XAUTHORITY', 'XDG_RUNTIME_DIR', 'DBUS_SESSION_BUS_ADDRESS',
            'XDG_CURRENT_DESKTOP', 'XDG_SESSION_TYPE', 'PATH']:
    if key in environment:
        command.append('--setenv=' + key + '=' + environment[key])
command += ['--setenv=UU_REMOTE_DESKTOP_BACKEND=wayland-xwayland',
            '/usr/lib/uu-remote-for-linux/uu-remote-input-bridge', 'watch', str(state),
            str(prefix / 'wineprefix/drive_c/uu-remote-input-bridge.endpoint'),
            str(state / 'input-bridge.log'), str(state / 'input-bridge.lock'), '15',
            str(prefix / 'controlled-monitor.json')]
active = subprocess.run(['systemctl', '--user', 'is-active', '--quiet',
                         'uu-remote-for-linux-input.service']).returncode == 0
if not active:
    subprocess.run(command, env=environment, check=True)
    print('UU_INPUT_SERVICE_RECREATED')
