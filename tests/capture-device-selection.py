#!/usr/bin/python3
"""Selection policy tests; no Portal dialog or desktop changes."""
import importlib.machinery
import importlib.util
import os
from pathlib import Path
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parent.parent
loader = importlib.machinery.SourceFileLoader('capture_bridge', str(
    ROOT / 'lib/uu-remote-for-linux/uu-remote-input-bridge'))
spec = importlib.util.spec_from_loader(loader.name, loader)
m = importlib.util.module_from_spec(spec)
loader.exec_module(m)


class SelectionTests(unittest.TestCase):
    def backend(self, candidates, failures=()):
        b = m.WaylandPortalBackend.__new__(m.WaylandPortalBackend)
        b.portal_cursor_mode = m.PORTAL_CURSOR_METADATA
        b.capture_device_candidates = candidates
        b.capture_device_attempted = []
        b.capture_device_failures = []
        b.streams = [{"node": 77}]
        b.calls = []
        b.cursor_resets = []
        b._check_capture_authorization = lambda: b.calls.append('authorize')
        def cursor(reset_cursor=True):
            b.calls.append('cursor:' + b.capture_device)
            b.cursor_resets.append(reset_cursor)
        b._start_cursor_bridge = cursor
        b._stop_device_attempt = lambda: b.calls.append('stop:' + b.capture_device)

        def start():
            b.calls.append('frame:' + b.capture_device)
            if b.capture_device in failures:
                raise m.CaptureDeviceError('actual frame import failed')
            b.capture_transport = b._capture_transport_name()
        b._start_capture_pipeline = start
        return b

    def test_first_gpu_works(self):
        b = self.backend(['/nvidia', '/intel', 'cpu'])
        b._start_compatible_capture()
        self.assertEqual(b.capture_device_attempted, ['/nvidia'])
        self.assertEqual(b.capture_transport, m.CAPTURE_TRANSPORT_DMABUF)
        self.assertFalse(hasattr(b, 'capture_attempt_deadline'))

    def test_other_gpu_before_cpu(self):
        b = self.backend(['/nvidia', '/intel', 'cpu'], ['/nvidia'])
        b._start_compatible_capture()
        self.assertEqual(b.capture_device_attempted, ['/nvidia', '/intel'])
        self.assertLess(b.calls.index('stop:/nvidia'), b.calls.index('cursor:/intel'))

    def test_cpu_last_and_same_for_cursor(self):
        b = self.backend(['/nvidia', '/intel', 'cpu'], ['/nvidia', '/intel'])
        b._start_compatible_capture()
        self.assertEqual(b.capture_device_attempted, ['/nvidia', '/intel', 'cpu'])
        self.assertEqual(b.capture_transport, m.CAPTURE_TRANSPORT_CPU)
        self.assertIn('cursor:cpu', b.calls)
        self.assertEqual(b.cursor_resets, [True, False, False])
        env = b._capture_helper_environment()
        self.assertEqual(env['UU_REMOTE_CAPTURE_MODE'], 'cpu')
        self.assertEqual(env['UU_REMOTE_EGL_DEVICE'], '')

    def test_explicit_never_falls_back(self):
        b = self.backend(['/intel'], ['/intel'])
        with self.assertRaises(m.CaptureSelectionError):
            b._start_compatible_capture()
        self.assertEqual(b.capture_device_attempted, ['/intel'])

    def test_exhaustion_and_recovery_do_not_repeat_candidates(self):
        b = self.backend(['/nvidia', 'cpu'], ['/nvidia', 'cpu'])
        for recover in (False, True):
            with self.assertRaises(m.CaptureSelectionError):
                b._start_compatible_capture(recover=recover)
        self.assertEqual(b.capture_device_attempted, ['/nvidia', 'cpu'])

    def test_runtime_failure_uses_remaining_candidates(self):
        b = self.backend(['/nvidia', '/intel', 'cpu'])
        b._start_compatible_capture()
        b._start_compatible_capture(recover=True)
        self.assertEqual(b.capture_device_attempted, ['/nvidia', '/intel'])
        self.assertEqual(b.cursor_resets, [True, False])

    def test_denial_does_not_try_another_device(self):
        b = self.backend(['/nvidia', 'cpu'])
        def denied():
            raise m.PortalRequestError('closed')
        b._check_capture_authorization = denied
        with self.assertRaises(m.PortalRequestError):
            b._start_compatible_capture()
        self.assertEqual(b.capture_device_attempted, [])

    def test_generic_error_does_not_hide_broken_program(self):
        b = self.backend(['/nvidia', 'cpu'])
        def broken():
            raise OSError('missing helper')
        b._start_capture_pipeline = broken
        with self.assertRaises(OSError):
            b._start_compatible_capture()
        self.assertEqual(b.capture_device_attempted, ['/nvidia'])

    def test_candidates_and_strict_manual_configuration(self):
        b = m.WaylandPortalBackend.__new__(m.WaylandPortalBackend)
        b.capture_helper_path = '/helper'
        with patch.dict(os.environ, {'UU_REMOTE_CAPTURE_MODE': 'auto',
                                     'UU_REMOTE_EGL_DEVICE': ''}):
            with patch.object(m.subprocess, 'run', return_value=SimpleNamespace(
                    stdout='/nvidia\n/intel\n/nvidia\n')):
                self.assertEqual(b._capture_candidates(), ['/nvidia', '/intel', 'cpu'])
            with patch.object(m.subprocess, 'run', return_value=SimpleNamespace(stdout='')):
                self.assertEqual(b._capture_candidates(), ['', 'cpu'])
            os.environ['UU_REMOTE_EGL_DEVICE'] = '/manual'
            self.assertEqual(b._capture_candidates(), ['/manual'])
            os.environ['UU_REMOTE_CAPTURE_MODE'] = 'cpu'
            with self.assertRaises(m.CaptureSelectionError):
                b._capture_candidates()
            os.environ['UU_REMOTE_EGL_DEVICE'] = ''
            self.assertEqual(b._capture_candidates(), ['cpu'])

    def test_budget_reserves_cpu_and_caps_slow_attempts(self):
        b = self.backend(['/nvidia', '/intel', 'cpu'], ['/nvidia', '/intel'])
        clock = [0.0]
        budgets = []
        original = b._start_capture_pipeline
        def start():
            budgets.append(b.capture_attempt_deadline - clock[0])
            clock[0] = b.capture_attempt_deadline
            original()
        b._start_capture_pipeline = start
        with patch.object(m.time, 'monotonic', side_effect=lambda: clock[0]):
            b._start_compatible_capture()
        self.assertEqual(budgets, [15, 15, 15])
        self.assertEqual(b.capture_device, 'cpu')

    def test_cleanup_cursor_even_when_capture_cleanup_fails(self):
        b = m.WaylandPortalBackend.__new__(m.WaylandPortalBackend)
        events = []
        b.cursor_helper_process = SimpleNamespace(
            poll=lambda: None, terminate=lambda: events.append('terminate'),
            wait=lambda **kwargs: events.append('wait'))
        def fail():
            raise RuntimeError('staging thread timeout')
        b._stop_capture_pipeline = fail
        with self.assertRaises(RuntimeError):
            b._stop_device_attempt()
        self.assertEqual(events, ['terminate', 'wait'])
        self.assertIsNone(b.cursor_helper_process)

    def test_real_frame_required_from_every_stream(self):
        b = m.WaylandPortalBackend.__new__(m.WaylandPortalBackend)
        b.capture_device = '/nvidia'
        b.capture_frames = 0
        b._reset_capture_attempt = lambda **kwargs: None
        b.capture_stream_stats = [{'node': 77, 'frames': 1}, {'node': 78, 'frames': 0}]
        b.capture_write_stream = lambda *args: True
        b.fake_video = False
        b.capture_callback_error = ''
        b.capture_native_processes = [SimpleNamespace(poll=lambda: None)]
        b._start_native_capture_pipeline = lambda: None
        # Timeout is a device failure even if another screen already works.
        with patch.object(m, 'CAPTURE_START_TIMEOUT_SECONDS', 0):
            with self.assertRaises(m.CaptureDeviceError):
                b._start_capture_pipeline()
            b.capture_stream_stats[1]['frames'] = 1
            b._start_capture_pipeline()
        self.assertEqual(b.capture_transport, m.CAPTURE_TRANSPORT_DMABUF)

    def test_exit_status_distinguishes_device_and_other_errors(self):
        b = m.WaylandPortalBackend.__new__(m.WaylandPortalBackend)
        b.capture_frames = 0
        b._reset_capture_attempt = lambda **kwargs: None
        b.capture_stream_stats = [{'node': 77, 'frames': 0}]
        b.capture_write_stream = lambda *args: True
        b.fake_video = False
        b.capture_callback_error = ''
        b._start_native_capture_pipeline = lambda: None
        for code, error_type in ((68, m.CaptureDeviceError), (1, RuntimeError)):
            b.capture_native_processes = [SimpleNamespace(poll=lambda: code, returncode=code)]
            with self.assertRaises(error_type) as caught:
                b._start_capture_pipeline()
            self.assertEqual(type(caught.exception), error_type)

    def test_runtime_device_failure_takes_priority_over_broken_pipe(self):
        b = self.backend(['/nvidia', 'cpu'])
        b._start_compatible_capture()
        b.fake_trace = ''
        b.capture_callback_error = 'release pipe closed during device failure'
        b.capture_native_processes = [SimpleNamespace(poll=lambda: 68)]
        b.cursor_helper_process = None
        b.ensure_healthy()
        self.assertEqual(b.capture_device, 'cpu')


if __name__ == '__main__':
    unittest.main()
