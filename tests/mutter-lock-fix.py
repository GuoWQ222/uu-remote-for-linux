#!/usr/bin/python3
"""Exercise repair decisions/transactions without running host APT or a GUI."""
import contextlib
import hashlib
import importlib.machinery
import importlib.util
import io
import json
import os
from pathlib import Path
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest import mock

repo = Path(__file__).resolve().parents[1]
helper = repo / 'lib/uu-remote-for-linux/uu-remote-wayland-lock-fix'
loader = importlib.machinery.SourceFileLoader('mutter_repair', str(helper))
module = importlib.util.module_from_spec(importlib.util.spec_from_loader(loader.name, loader))
loader.exec_module(module)


class FakeHost:
    def __init__(self, manifest):
        self.manifest = manifest
        self.versions = dict.fromkeys(module.PACKAGES, module.BASE)
        self.versions['gnome-shell'] = '46.0-0ubuntu6'
        self.platform_value = ('ubuntu', '24.04', 'amd64')
        self.modified = set()
        self.held = {'libmutter-14-0', 'unrelated-package'}
        self.calls = []
        self.cached = []
        self.fail_install = self.fail_restore = False
        self.bad_plan = self.audit = ''
        self.hash = manifest['library_sha256']

    def platform(self):
        return self.platform_value

    def version(self, name):
        return self.versions.get(name)

    def raw_version(self, name):
        return self.versions.get(name)

    def pristine(self, name):
        return name not in self.modified

    def library_hash(self):
        return self.hash

    def holds(self):
        return sorted(self.held)

    def cache_archives(self, paths):
        self.cached.extend(str(path) for path in paths)

    def run(self, args):
        self.calls.append(list(args))
        if args[0] == '/usr/bin/dpkg-deb':
            item = next(item for key in ['packages', 'rollback_packages'] for item in self.manifest[key]
                        if item['file'] == Path(args[2]).name)
            return item[{'Package': 'package', 'Version': 'version', 'Architecture': 'architecture'}[args[3]]] + '\n'
        if args[:2] == ['/usr/bin/dpkg', '--audit']:
            return self.audit
        if args[0] == '/usr/bin/apt-get':
            if '--simulate' in args:
                return self.bad_plan or '\n'.join('Inst ' + name for name in sorted(module.PACKAGES))
            restoring = '--allow-downgrades' in args
            if restoring and self.fail_restore:
                raise RuntimeError('Injected recovery error')
            if not restoring and self.fail_install:
                self.versions['libmutter-14-0'] = module.PATCHED
                self.fail_install = False
                raise RuntimeError('Injected partial installation failure')
            self.versions.update(dict.fromkeys(module.PACKAGES, module.BASE if restoring else module.PATCHED))
            return 'Configured four packages\n'
        if args[0] == '/usr/bin/apt-mark':
            for name in args[2:]:
                (self.held.add if args[1] == 'hold' else self.held.discard)(name)
            return ''
        raise AssertionError(args)


class TestRepair(module.Repair):
    def trusted(self):
        pass  # Never elevate or operate the real host in unit tests.

    def lock(self):
        self.state_dir.mkdir(parents=True, exist_ok=True)
        return (self.state_dir / 'transaction.lock').open('a')


class RepairTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.payload = self.root / 'payload'
        self.payload.mkdir()
        self.manifest = {'format': 1, 'base_version': module.BASE, 'patched_version': module.PATCHED,
                         'library_sha256': 'verified-library', 'packages': [], 'rollback_packages': []}
        for key, version in [('packages', module.PATCHED), ('rollback_packages', module.BASE)]:
            for name in sorted(module.PACKAGES):
                arch = 'all' if name == 'mutter-common' else 'amd64'
                file = f'{name}_{version}_{arch}.deb'
                data = (name + version).encode()
                (self.payload / file).write_bytes(data)
                self.manifest[key].append({'package': name, 'version': version, 'architecture': arch,
                                          'file': file, 'sha256': hashlib.sha256(data).hexdigest()})
        (self.payload / 'manifest.json').write_text(json.dumps(self.manifest))
        self.host = FakeHost(self.manifest)
        self.repair = TestRepair(self.payload, self.root / 'system-state', self.host)

    def operations(self):
        return [args for args in self.host.calls if args[0] == '/usr/bin/apt-get' and '--simulate' not in args]

    def test_apply_exact_versions_offline_and_retains_originals(self):
        result = self.repair.apply()
        self.assertTrue(result['requires_relogin'])
        self.assertEqual(self.repair.status()['state'], 'installed')
        self.assertEqual(len(self.operations()), 1)
        self.assertIn('--no-download', self.operations()[0])
        self.assertIn('--no-remove', self.operations()[0])
        self.assertEqual(len(self.host.cached), 4)
        self.assertEqual(len(list((self.repair.state_dir / 'backup').glob('*.deb'))), 4)
        self.assertTrue(module.PACKAGES.issubset(self.host.held))
        self.assertEqual(json.loads((self.repair.state_dir / 'state.json').read_text())['phase'], 'installed')

    def test_already_patched_does_not_reinstall(self):
        self.repair.apply()
        count = len(self.operations())
        self.repair.apply()
        self.assertEqual(len(self.operations()), count)

    def test_partial_install_failure_restores_all_packages_and_holds(self):
        self.host.fail_install = True
        with self.assertRaisesRegex(RuntimeError, '已恢复原版'):
            self.repair.apply()
        self.assertEqual({self.host.version(n) for n in module.PACKAGES}, {module.BASE})
        self.assertEqual(self.host.held, {'libmutter-14-0', 'unrelated-package'})
        self.assertEqual(json.loads((self.repair.state_dir / 'state.json').read_text())['phase'], 'rolled_back')

    def test_failed_recovery_is_not_reported_as_success(self):
        self.host.fail_install = self.host.fail_restore = True
        with self.assertRaisesRegex(RuntimeError, '自动回退未完成'):
            self.repair.apply()
        self.assertEqual(json.loads((self.repair.state_dir / 'state.json').read_text())['phase'], 'recovery_required')

    def test_retry_rollback_after_partial_failed_recovery(self):
        self.host.fail_install = self.host.fail_restore = True
        with self.assertRaises(RuntimeError):
            self.repair.apply()
        self.host.fail_restore = False
        self.assertEqual(self.repair.rollback()['state'], 'rolled_back')
        self.assertEqual({self.host.version(n) for n in module.PACKAGES}, {module.BASE})

    def test_recovery_does_not_overwrite_a_subsequent_upgrade(self):
        self.host.fail_install = self.host.fail_restore = True
        with self.assertRaises(RuntimeError):
            self.repair.apply()
        self.host.versions['libmutter-14-0'] = '46.2-1ubuntu0.24.04.99'
        with self.assertRaisesRegex(RuntimeError, '其他系统修改'):
            self.repair.rollback()

    def test_recovery_refuses_changed_distribution(self):
        self.host.fail_install = self.host.fail_restore = True
        with self.assertRaises(RuntimeError):
            self.repair.apply()
        self.host.platform_value = ('ubuntu', '26.04', 'amd64')
        with self.assertRaisesRegex(RuntimeError, '其他系统修改'):
            self.repair.rollback()

    def test_source_automatic_prompt_is_silent(self):
        result = subprocess.run([str(helper), '--prompt'], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0)
        self.assertEqual(result.stdout + result.stderr, '')

    def test_explicit_rollback_preserves_prior_holds(self):
        self.repair.apply()
        result = self.repair.rollback()
        self.assertEqual(result['state'], 'rolled_back')
        self.assertTrue(result['requires_relogin'])
        self.assertEqual(self.host.held, {'libmutter-14-0', 'unrelated-package'})
        self.assertIn('--allow-downgrades', self.operations()[-1])

    def test_second_rollback_does_not_run_apt(self):
        self.repair.apply()
        self.repair.rollback()
        count = len(self.operations())
        self.repair.rollback()
        self.assertEqual(len(self.operations()), count)

    def test_missing_rollback_record_is_refused(self):
        with self.assertRaisesRegex(RuntimeError, '没有本工具'):
            self.repair.rollback()
        self.assertFalse(self.operations())

    def test_corrupt_payload_is_refused_before_any_install(self):
        (self.payload / self.manifest['packages'][0]['file']).write_bytes(b'tampered')
        with self.assertRaisesRegex(RuntimeError, '校验失败'):
            self.repair.apply()
        self.assertFalse(self.operations())

    def test_corrupt_saved_backup_prevents_rollback(self):
        self.repair.apply()
        path = self.repair.state_dir / 'backup' / self.manifest['rollback_packages'][0]['file']
        path.write_bytes(b'tampered')
        with self.assertRaisesRegex(RuntimeError, '回退包校验失败'):
            self.repair.rollback()
        self.assertEqual(len(self.operations()), 1)

    def test_symlink_payload_is_refused(self):
        path = self.payload / self.manifest['packages'][0]['file']
        saved = path.with_suffix('.saved')
        path.rename(saved)
        path.symlink_to(saved)
        with self.assertRaisesRegex(RuntimeError, '校验失败'):
            self.repair.apply()
        self.assertFalse(self.operations())

    def test_newer_mutter_is_never_downgraded(self):
        self.host.versions.update(dict.fromkeys(module.PACKAGES, '46.2-1ubuntu0.24.04.99'))
        with self.assertRaisesRegex(RuntimeError, '版本不匹配'):
            self.repair.apply()
        self.assertFalse(self.operations())

    def test_mixed_package_versions_are_refused(self):
        self.host.versions['mutter-common'] = module.PATCHED
        self.assertEqual(self.repair.status()['state'], 'unsupported')

    def test_other_distribution_or_architecture_is_untouched(self):
        for platform in [('debian', '13', 'amd64'), ('ubuntu', '26.04', 'amd64'), ('ubuntu', '24.04', 'arm64')]:
            self.host.platform_value = platform
            self.assertEqual(self.repair.status()['state'], 'unsupported')
        self.assertFalse(self.operations())

    def test_other_shell_major_is_untouched(self):
        self.host.versions['gnome-shell'] = '48.1'
        self.assertEqual(self.repair.status()['state'], 'unsupported')

    def test_local_modifications_are_not_overwritten(self):
        self.host.modified.add('libmutter-14-0')
        self.assertEqual(self.repair.status()['state'], 'modified')
        with self.assertRaises(RuntimeError):
            self.repair.apply()
        self.assertFalse(self.operations())

    def test_installed_version_with_wrong_library_is_not_accepted(self):
        self.host.versions.update(dict.fromkeys(module.PACKAGES, module.PATCHED))
        self.host.hash = 'different-library'
        self.assertEqual(self.repair.status()['state'], 'modified')

    def test_unrelated_package_removal_is_refused_in_preflight(self):
        self.host.bad_plan = 'Remv ubuntu-desktop [1.0]\n'
        with self.assertRaisesRegex(RuntimeError, '变更其他系统包'):
            self.repair.apply()
        self.assertFalse(self.operations())
        self.assertFalse((self.repair.state_dir / 'state.json').exists())

    def test_unrelated_upgrade_is_refused_in_preflight(self):
        self.host.bad_plan = 'Inst unrelated-package [1] (2 Ubuntu)\n'
        with self.assertRaisesRegex(RuntimeError, '变更其他系统包'):
            self.repair.apply()
        self.assertFalse(self.operations())

    def test_unfinished_dpkg_transaction_is_refused(self):
        self.host.audit = 'Unconfigured unrelated package\n'
        with self.assertRaisesRegex(RuntimeError, '未完成的软件包事务'):
            self.repair.apply()
        self.assertFalse(self.operations())

    def test_real_trust_gate_rejects_source_checkout(self):
        real = module.Repair(self.payload, self.root / 'state', self.host)
        with self.assertRaisesRegex(RuntimeError, '/usr/lib'):
            real.trusted()

    def test_declining_prompt_is_remembered_until_manual_retry(self):
        with self.frontend(), mock.patch.object(module.subprocess, 'run', return_value=SimpleNamespace(returncode=1)) as run:
            self.assertEqual(module.prompt(self.repair), 0)
            self.assertEqual(module.prompt(self.repair), 0)
            self.assertEqual(run.call_count, 1)
            module.prompt(self.repair, force=True)
            self.assertEqual(run.call_count, 2)

    def test_accept_prompt_elevates_only_system_helper(self):
        replies = [SimpleNamespace(returncode=0), SimpleNamespace(returncode=0, stdout='installed'),
                   SimpleNamespace(returncode=0)]
        with self.frontend(), mock.patch.object(module.subprocess, 'run', side_effect=replies) as run:
            self.assertEqual(module.prompt(self.repair), 0)
            self.assertEqual(run.call_args_list[1].args[0][:3], ['/usr/bin/pkexec', str(helper), '--apply'])
            self.assertIn('没有重启', run.call_args_list[2].args[0][-1])

    def test_failed_authorization_does_not_suppress_retry(self):
        replies = [SimpleNamespace(returncode=0), SimpleNamespace(returncode=126, stdout='Authorization cancelled'),
                   SimpleNamespace(returncode=0)]
        with self.frontend(), mock.patch.object(module.subprocess, 'run', side_effect=replies):
            self.assertEqual(module.prompt(self.repair), 126)
        self.assertFalse((self.root / 'user-state/uu-remote-for-linux/wayland-keyboard-prompt.json').exists())

    def test_x11_start_does_not_show_system_repair_prompt(self):
        with self.frontend(), mock.patch.dict(os.environ, XDG_SESSION_TYPE='x11'), \
                mock.patch.object(module.subprocess, 'run') as run:
            self.assertEqual(module.prompt(self.repair), 0)
            run.assert_not_called()

    def test_already_installed_does_not_prompt_or_elevate(self):
        self.host.versions.update(dict.fromkeys(module.PACKAGES, module.PATCHED))
        with self.frontend(), mock.patch.object(module.subprocess, 'run') as run:
            self.assertEqual(module.prompt(self.repair), 0)
            run.assert_not_called()

    def test_no_graphical_session_does_not_apply(self):
        with self.frontend(), mock.patch.object(module.shutil, 'which', return_value=None), \
                mock.patch.object(module.subprocess, 'run') as run, contextlib.redirect_stdout(io.StringIO()):
            self.assertEqual(module.prompt(self.repair, force=True), 0)
            run.assert_not_called()

    @contextlib.contextmanager
    def frontend(self):
        with mock.patch.object(module, 'SYSTEM_HELPER', helper), \
                mock.patch.object(module.shutil, 'which', return_value='/usr/bin/zenity'), \
                mock.patch.dict(os.environ, {'XDG_STATE_HOME': str(self.root / 'user-state'),
                    'XDG_SESSION_TYPE': 'wayland', 'XDG_CURRENT_DESKTOP': 'ubuntu:GNOME', 'WAYLAND_DISPLAY': 'test-wayland'}):
            yield


if __name__ == '__main__':
    unittest.main(verbosity=2)
