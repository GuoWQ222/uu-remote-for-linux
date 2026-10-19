import datetime
import json
import os
from pathlib import Path
import re
import shutil
import subprocess

assert os.geteuid() == 0
version = subprocess.check_output(['dpkg-query', '-W', '-f=${Version}', 'libmutter-14-0'], text=True)
assert version == '46.2-1ubuntu0.24.04.18+pjlablock3'
stamp = datetime.datetime.now().strftime('%Y%m%d-%H%M%S')
backup = Path('/usr/local/share/pjlab-recovery') / ('patched-restart-' + stamp)
backup.mkdir(mode=0o700)
gdm = Path('/etc/gdm3/custom.conf')
shutil.copy2(gdm, backup / 'gdm-original.conf')
config = re.sub(r'^\s*(?:AutomaticLoginEnable|AutomaticLogin)\s*=.*\n?', '', gdm.read_text(), flags=re.M)
assert config.count('[daemon]') == 1
gdm.write_text(config.replace('[daemon]', '[daemon]\nAutomaticLoginEnable=true\nAutomaticLogin=guowenqi', 1))
pointer = Path('/home/guowenqi/.cache/pjlab-last-restart.json')
pointer.write_text(json.dumps({'backup': str(backup), 'version': version}, indent=2) + '\n')
os.chown(pointer, 101614594, 101614594)
subprocess.run(['systemd-run', '--unit=pjlab-patched-restart-' + stamp, '--on-active=4s', '/usr/bin/systemctl', 'restart', 'gdm'], check=True)
print('PATCHED_DESKTOP_RESTART_SCHEDULED', backup, flush=True)
