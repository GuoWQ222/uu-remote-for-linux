#!/usr/bin/env python3
"""Exercise the real helper against a local libeis server, never a desktop."""
import importlib.machinery
import importlib.util
import queue
import socket
import subprocess
import sys
import tempfile
import threading
import time
from pathlib import Path
from types import SimpleNamespace

root = Path(__file__).resolve().parents[1]
helper = root / "lib/uu-remote-for-linux/uu-remote-eis-pointer"
server_binary = sys.argv[1]
loader = importlib.machinery.SourceFileLoader(
    "eis_test_bridge", str(root / "lib/uu-remote-for-linux/uu-remote-input-bridge")
)
spec = importlib.util.spec_from_loader(loader.name, loader)
module = importlib.util.module_from_spec(spec)
loader.exec_module(module)


def lines(process):
    output = queue.Queue()
    def read():
        for line in iter(process.stdout.readline, b""):
            output.put(line.decode().strip())
        output.put("EOF")
    threading.Thread(target=read, daemon=True).start()
    return output


def expect(output, value):
    actual = output.get(timeout=4)
    assert actual == value, (actual, value)


class Peer:
    def __init__(self, directory, regions=1):
        path = str(Path(directory) / "eis.sock")
        self.server = subprocess.Popen(
            [server_binary, path, str(regions)], stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, bufsize=0,
        )
        self.events = lines(self.server)
        expect(self.events, "LISTEN")
        sock = socket.socket(socket.AF_UNIX)
        sock.connect(path)
        self.client = subprocess.Popen(
            [str(helper), str(sock.fileno())], pass_fds=(sock.fileno(),),
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, bufsize=0,
        )
        sock.close()
        self.backend = module.WaylandPortalBackend.__new__(module.WaylandPortalBackend)
        self.backend._eis = self.client
        self.backend._eis_region = (10, 20, 1600, 1000)
        self.backend.streams = [{"width":1600,"height":1000,
                                 "buffer_width":3200,"buffer_height":2000}]

    def health_check(self, exited=False):
        # Keep capture healthy: input failure alone must fail the bridge,
        # including with embedded cursors and without another input packet.
        self.backend.fake_trace = False
        self.backend.capture_callback_error = None
        self.backend.capture_transport = module.CAPTURE_TRANSPORT_DMABUF
        self.backend.capture_native_processes = [SimpleNamespace(poll=lambda: None)]
        self.backend.portal_cursor_mode = module.PORTAL_CURSOR_EMBEDDED
        try:
            self.backend.ensure_healthy()
        except RuntimeError as error:
            assert exited and "EIS" in str(error), str(error)
            assert str(self.client.returncode) in str(error), str(error)
        else:
            assert not exited, "dead EIS helper still reported healthy"

    def ready(self):
        assert self.backend._eis_line(4) == "READY 10 20 1600 1000"
        expect(self.events, "START")
        self.health_check()

    def command(self, command, accepted=True):
        self.client.stdin.write((command + "\n").encode())
        result = self.backend._eis_line()
        assert result == "OK" if accepted else result.startswith("ERROR"), result

    def control(self, command, response):
        self.server.stdin.write(command.encode())
        expect(self.events, response)

    def close(self):
        self.backend._stop_eis_pointer()
        self.health_check()  # Disabled EIS leaves the legacy health path intact.
        if self.server.poll() is None:
            self.server.stdin.write(b"q")
        self.server.wait(timeout=4)
        self.server.stdin.close()
        self.server.stdout.close()


with tempfile.TemporaryDirectory() as directory:
    peer = Peer(directory)
    try:
        peer.ready()
        # Edge pixels stay in logical EIS coordinates despite 2x buffers.
        assert peer.backend._eis_notify("NotifyPointerMotionAbsolute", (None,{},1,1599,999))
        expect(peer.events,"A 1609.00 1019.00")
        peer.command("R 3 -5")
        expect(peer.events,"R 3.00 -5.00")
        peer.command("B 272 1")
        expect(peer.events,"B 272 1")
        peer.command("B 272 0")
        expect(peer.events,"B 272 0")
        assert peer.backend._eis_notify("NotifyPointerAxisDiscrete",(None,{},0,-1))
        expect(peer.events,"S 0 -120")
        assert peer.backend._eis_notify("NotifyKeyboardKeycode",(None,{},30,1))
        expect(peer.events,"K 30 1")
        peer.command("K 30 0")
        expect(peer.events,"K 30 0")
        for invalid in ["A 1610 1020","A nan 3","R inf 0","B 30 1",
                        "K -1 1","K 768 1","K 30 2","B 272 2","B 272.5 1","S 1.5 0","A 20 30 trailing"]:
            peer.command(invalid,False)
        peer.control("d","REMOVED")
        assert peer.client.wait(timeout=4) != 0
        expect(peer.events,"DISCONNECTED")
        peer.health_check(exited=True)
    finally:
        peer.close()
print("PASS EIS pointer coordinates, buttons, scroll, validation, removal")

for mode in ("disconnect", "eof", "multiple-regions"):
    with tempfile.TemporaryDirectory() as directory:
        peer = Peer(directory,2 if mode == "multiple-regions" else 1)
        try:
            if mode == "multiple-regions":
                assert peer.client.wait(timeout=4) != 0
            else:
                peer.ready()
                peer.command("B 272 1")
                expect(peer.events,"B 272 1")
                peer.command("K 42 1")
                expect(peer.events,"K 42 1")
                if mode == "disconnect":
                    peer.server.terminate()
                    peer.server.wait(timeout=4)
                    assert peer.client.wait(timeout=4) != 0
                    peer.health_check(exited=True)
                else:
                    peer.client.stdin.close()
                    assert peer.client.wait(timeout=4) == 0
                    expect(peer.events,"B 272 0")
                    expect(peer.events,"K 42 0")
                    peer.health_check(exited=True)
        finally:
            peer.close()
    print("PASS EIS",mode)

# A partial reply must not defeat the deadline or leave an orphan child.
backend = module.WaylandPortalBackend.__new__(module.WaylandPortalBackend)
backend._eis = subprocess.Popen(
    [sys.executable,"-c","import os,time;os.write(1,b'RE');time.sleep(30)"],
    stdin=subprocess.PIPE,stdout=subprocess.PIPE,bufsize=0,
)
process = backend._eis
try:
    backend._eis_line(.1)
except RuntimeError as error:
    assert "timed out" in str(error)
else:
    raise AssertionError("partial reply bypassed timeout")
backend._stop_eis_pointer()
assert process.poll() is not None and process.stdout.closed
backend._stop_eis_pointer()  # cleanup is idempotent
backend.streams = [{},{}]
try:
    backend._start_eis_pointer()
except RuntimeError as error:
    assert "one monitor" in str(error)
else:
    raise AssertionError("multiple streams accepted")
print("PASS EIS timeout cleanup and multi-monitor guard")
