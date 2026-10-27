#!/usr/bin/env python3
"""Lifecycle regression; run on an isolated D-Bus, never the desktop bus."""
import importlib.machinery
import importlib.util
import json
import os
import tempfile
import time
from pathlib import Path
from types import SimpleNamespace

root = Path(__file__).resolve().parents[1]
loader = importlib.machinery.SourceFileLoader("portal_close_test", str(root / "lib/uu-remote-for-linux/uu-remote-input-bridge"))
spec = importlib.util.spec_from_loader(loader.name, loader)
m = importlib.util.module_from_spec(spec)
loader.exec_module(m)
b = m.WaylandPortalBackend.__new__(m.WaylandPortalBackend)
b._load_gio()
b.connection = b.Gio.bus_get_sync(b.Gio.BusType.SESSION, None)
# Use an isolated bus and a unique sender, without owning the real Portal name.
m.PORTAL_BUS_NAME = b.connection.get_unique_name()
b.session_handle = "/test/session"
b._portal_closed = False
b.fake_trace = False
b._eis = SimpleNamespace(poll=lambda: 1)
b._portal_closed_subscription = b.connection.signal_subscribe(
    m.PORTAL_BUS_NAME, m.PORTAL_SESSION, "Closed", b.session_handle, None,
    b.Gio.DBusSignalFlags.NONE, b._on_portal_closed, None)
b.connection.emit_signal(None, b.session_handle, m.PORTAL_SESSION, "Closed", None)
b.connection.flush_sync(None)
deadline = time.monotonic() + 2
while not b._portal_closed and time.monotonic() < deadline:
    try:
        b._check_portal_session()
    except m.PortalSessionClosed:
        break
assert b._portal_closed, "idle socket loop did not dispatch Portal Closed"
try:
    b.ensure_healthy()
except m.PortalSessionClosed:
    pass
else:
    raise AssertionError("helper death overrode explicit Portal closure")
b.connection.signal_unsubscribe(b._portal_closed_subscription)
# Probe a removed object when helper EOF wins the race with Closed delivery.
b._portal_closed = False
b._check_portal_session = m.WaylandPortalBackend._check_portal_session.__get__(b)
try:
    b._check_portal_session(probe=True)
except m.PortalSessionClosed:
    pass
else:
    raise AssertionError("vanished Portal session treated as recoverable helper failure")
print("PASS Portal Closed signal and removed-session race")

# Exercise the real socket watch/error/finally path without a desktop grant.
os.environ["UU_REMOTE_DESKTOP_BACKEND"] = "wayland"
for failure, expected_code, expected_state in [
    (m.PortalSessionClosed("closed by desktop"), 0, "stopped"),
    (RuntimeError("EIS input helper exited (status=1)"), 1, "error"),
]:
    with tempfile.TemporaryDirectory() as directory:
        base = Path(directory)
        backend = m.FakeBackend(base / "trace")
        def unhealthy():
            raise failure
        backend.ensure_healthy = unhealthy
        m.make_backend = lambda *_args: backend
        endpoint = base / "endpoint"
        result = m.watch(base, endpoint, base / "log", base / "lock", 15)
        assert result == expected_code, (result, expected_code)
        status = json.loads((base / "input-bridge-status.json").read_text())
        assert status["state"] == expected_state, status
        if expected_state == "stopped":
            assert status["capture_state"] == "stopped", status
        assert not endpoint.exists()
        assert "close" in (base / "trace").read_text()
print("PASS explicit stop exits 0; helper failure exits 1; both clean up")
