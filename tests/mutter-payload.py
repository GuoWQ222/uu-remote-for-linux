#!/usr/bin/python3
"""Validate the actual outer DEB's inert repair payload and source archive."""
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tarfile
import tempfile

root = Path(sys.argv[1])
runtime = root / 'usr/lib/uu-remote-for-linux'
payload = runtime / 'mutter-lock-fix'
manifest = json.loads((payload / 'manifest.json').read_text())
packages = {'libmutter-14-0', 'gir1.2-mutter-14', 'mutter-common', 'mutter-common-bin'}
assert not (root / 'usr/lib/x86_64-linux-gnu/libmutter-14.so.0').exists()
assert not (root / 'usr/share/keyrings').exists()
assert (runtime / 'uu-remote-wayland-lock-fix').stat().st_mode & 0o111
assert len(list(payload.glob('*.deb'))) == 8
for category, version in [('packages', manifest['patched_version']), ('rollback_packages', manifest['base_version'])]:
    assert {item['package'] for item in manifest[category]} == packages
    with tempfile.TemporaryDirectory(prefix='uu-mutter-payload-') as temp:
        for item in manifest[category]:
            path = payload / item['file']
            assert not path.is_symlink() and not path.stat().st_mode & 0o111
            assert hashlib.sha256(path.read_bytes()).hexdigest() == item['sha256']
            for field, value in [('Package', item['package']), ('Version', version), ('Architecture', item['architecture'])]:
                actual = subprocess.check_output(['dpkg-deb', '-f', str(path), field], text=True).strip()
                assert actual == value, (path.name, field, actual)
            subprocess.run(['dpkg-deb', '-x', str(path), temp], check=True)
        library = Path(temp) / 'usr/lib/x86_64-linux-gnu/libmutter-14.so.0.0.0'
        sha = hashlib.sha256(library.read_bytes()).hexdigest()
        assert sha == (manifest['library_sha256'] if category == 'packages' else
                       'a66cbe9b4409c17c83b88d355ea0fd360eb8f792be068245017ffac720cecc77')

docs = root / 'usr/share/doc/uu-remote-for-linux'
source = docs / 'source' / manifest['source']['file']
assert hashlib.sha256(source.read_bytes()).hexdigest() == manifest['source']['sha256']
prefix = 'mutter-46.2-pjlablock3-source/'
with tarfile.open(source) as archive:
    members = {item.name: item for item in archive.getmembers()}
    for name in ['COPYING', 'meson.build', 'debian/copyright', 'debian/rules',
                 'src/wayland/meta-wayland-keyboard.c', 'pjlab-tests/pjlab_shift_isolated_session.py']:
        assert prefix + name in members, name
    assert members[prefix + 'debian/rules'].mode & 0o111
    changelog = archive.extractfile(prefix + 'debian/changelog').read()
    assert changelog.startswith(('mutter (' + manifest['patched_version'] + ')').encode())
    assert archive.extractfile(prefix + 'COPYING').read().startswith(b'                    GNU GENERAL PUBLIC LICENSE')
for name in ['README.md', 'SOURCE.json', 'acceptance.json', 'mutter-46-lock-state.patch']:
    assert (docs / 'mutter-lock-fix' / name).is_file()

# Exercise the public CLI on extracted bytes without Wine setup or elevation.
with tempfile.TemporaryDirectory(prefix='uu-mutter-cli-') as temp:
    env = dict(os.environ, HOME=temp, XDG_DATA_HOME=temp + '/data', XDG_STATE_HOME=temp + '/state',
               XDG_CONFIG_HOME=temp + '/config', XDG_CACHE_HOME=temp + '/cache', UU_REMOTE_RUNTIME_DIR=str(runtime))
    value = subprocess.check_output([str(root / 'usr/bin/uu-remote-for-linux'), '--wayland-keyboard-status'],
                                    env=env, text=True)
    assert json.loads(value)['state'] in ['installed', 'eligible', 'unsupported', 'modified']
print('PASS: eight verified packages, matching library hashes, full GPL source, inert system paths and CLI status')
