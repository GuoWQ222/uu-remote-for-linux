# Ubuntu 24.04 Mutter screen-cast latency patches

These patches are scoped to Ubuntu's `mutter` source version
`46.2-1ubuntu0.24.04.16`.

For monitor streams backed by MemFd, Mutter 46.2 schedules the detached
screen-cast recording callback with `g_idle_add()`. That uses
`G_PRIORITY_DEFAULT_IDLE`; ordinary X11, input, and D-Bus work can therefore
delay the only clean video frame for hundreds of milliseconds. While that
idle is pending, later stage paints do not schedule a second recording.

`uuremote-bound-monitor-memfd-capture-latency.patch` keeps the existing
one-shot/coalescing semantics but schedules the callback after 1 ms at
`G_PRIORITY_DEFAULT`. Mutter 46.2's own priority design notes in
`src/meta/common.h` say work that should happen immediately belongs at this
priority and warn that work below redraw can be delayed arbitrarily while the
screen updates rapidly. The timeout source is created only after
`stage_painted()` completes, so it cannot overtake the paint that created it;
when it becomes ready, it can be dispatched with the other default-priority
ready sources instead of remaining behind continuously ready redraw work.
This eliminates the starvation mode left by the `+uuremote2` priority
(`CLUTTER_PRIORITY_REDRAW + 1`) during IME composition.

There is a second, independent failure mode when all negotiated PipeWire
buffers are still held by consumers. Mutter 46.2 drops the frame after
`pw_stream_dequeue_buffer()` returns `NULL` and does not retry until new damage
arrives. `uuremote-retry-pipewire-overrun.patch` adapts upstream Mutter commit
`652a8145b1a48a5d5354494933c82dde245e9119`: full-frame overruns are retried
asynchronously at the negotiated frame interval. The timer is coalesced and
frame-paced so the synchronous area/window follow-up implementations cannot
recurse or spin at 1 kHz. The window follow-up also stops after its captured
window actor has been destroyed; this hardens both the new overrun retry and
Mutter 46.2's existing maximum-framerate retry. Cursor-only retry behavior is
unchanged.

The public profile version is
`46.2-1ubuntu0.24.04.16+0uuremote3`. The `+0` keeps the repair newer than the
exact `.16` base while allowing same-base Ubuntu suffixes such as `+esm1` or
`+ubuntu1`, as well as `.16.1` and `.17`, to supersede it naturally. The
manager never creates an APT hold or pin.

The four matching repair packages, four byte-verified Ubuntu rollback packages,
and complete corresponding source are embedded as inert data in the UU Remote
system `.deb`. Installing UU does not install the repair. On an exact Ubuntu
24.04 Noble amd64 GNOME baseline, inspect and explicitly enable it with:

```bash
/usr/bin/uu-remote-for-linux --mutter-fix-status
/usr/bin/uu-remote-for-linux --install-mutter-fix
```

The user-facing manager explains the system change and asks for explicit
confirmation. Its fixed-path PolicyKit helper then repeats every check as root:
OS/codename/architecture, all four installed package states and versions,
foreign architectures and holds, root ownership, SHA256, Debian control fields,
dependency coupling, `dpkg --audit`, and an APT simulation whose changed set
must be exactly the four Mutter packages with no removal. The simulation is
only a preflight: immediately before the real `dpkg` invocation, the helper
revalidates the starting package versions, library hash, multiarch and hold
state after APT has acquired its locks. A version-1 `Pre-Install-Pkgs` hook then
binds every real input file to the expected four package names, versions,
architectures, and byte hashes.

APT's normal configure phase is deliberately retained. A transaction succeeds
only after all four packages report `install ok installed`, the installed
library has the expected hash, `dpkg --audit` is empty, and `apt-get -s check`
succeeds. The four packages are installed through one APT transaction; their
normal package configuration and declared triggers may also run. Their previous
auto/manual APT marks are restored. A Wayland logout/login is required before
GNOME Shell uses the new library.

Explicit offline recovery uses the official packages copied to the root-only
state directory before the first system change:

```bash
/usr/bin/uu-remote-for-linux --rollback-mutter-fix
```

Removing UU never rolls back or otherwise changes Mutter. The manager refuses
unknown package revisions, non-Noble systems, foreign-architecture Mutter
libraries, held packages, solver changes outside the four-package set, and any
newer Mutter revision. A mixed set containing only the exact official/public/
diagnostic versions, or an exact known version with a mismatched library, is
restricted to explicit official recovery; it can never be used for install.
If GNOME cannot start, recovery remains available from TTY or SSH:

```bash
sudo /usr/libexec/uu-remote-for-linux/uu-remote-mutter-fix-root rollback
```

If UU itself has already been removed, or the helper reports
`interrupted-known-partial`, the four fixed files retained under
`/var/lib/uu-remote-for-linux/mutter-fix/rollback/` can be used with
`apt-get --allow-downgrades --reinstall --no-remove install` only after
confirming that every package version belongs to the exact official/public/
diagnostic allowlist. A `.17` or any unknown/newer version must instead be
completed from the Ubuntu repositories and must never be forced back to `.16`.
The complete literal emergency command is kept in both top-level READMEs so it
also works from a TTY; it deliberately does not depend on shell glob expansion
through the root-only directory. Reinstalling the matching UU package is
another safe way to restore the helper because UU's maintainer scripts never
install or roll back Mutter.

The committed release inputs live under:

```text
third_party/mutter/ubuntu-24.04-amd64/uuremote3/
third_party/mutter/source/46.2-1ubuntu0.24.04.16-uuremote3/
```

`packaging/mutter/verify-bundle.sh` validates both trees, extracts the source
package, checks its changelog and patch series, and independently inspects every
nested `.deb`. `packaging/build-deb.sh` runs that verifier before staging the
main package. Do not substitute local incremental diagnostic packages for the
clean source build.

## Runtime acceptance

### Real APT transaction release gate

The mock manager tests and `apt-get -s` prove fail-closed plan validation, but
they do not prove that real APT reaches `dpkg`'s configure phase. Before a UU
release is published, test the staged system `.deb` in a snapshot-capable,
disposable Ubuntu 24.04 Noble amd64 GNOME environment. Do not perform this gate
on a developer's live desktop.

The release is blocked unless all of these real transactions pass:

1. Install from the exact official `.16` baseline to the public
   `+0uuremote3` version.
2. Normalize the exact diagnostic `+uuremote3` version to the public
   `+0uuremote3` version.
3. Roll the public version back to the exact official `.16` baseline.

For every path, retain the manager log and the matching `/var/log/dpkg.log`
slice. The latter must contain both `startup archives unpack` and
`startup packages configure`. After the command exits, all four package records
must be `install ok installed` at the expected identical version, the library
hash must match the profile, `dpkg --audit` must be empty, and
`apt-get -s check` must succeed. The manager must report either the expected
pending-logout state or the corresponding active/eligible state. An unpack-only
result is a failed acceptance, even when all four version strings and the
library hash already match.

Recovery failure injection, if performed, belongs only in the same disposable
environment. It must demonstrate that a failure before the verified
`Pre-Install-Pkgs` marker causes no recovery write, while a failure after that
marker restores and configures the exact official four-package set.

### Capture and IME acceptance

After installing the diagnostic-equivalent `+uuremote3` library and logging
back into GNOME Wayland, 36/36 real IME key states were matched across input,
the XWayland glyph surface, Mutter's raw MemFd, and UU's shared frame. End to
end latency was p50 58.755 ms, p95 76.558 ms, and max 93.155 ms; all six Space
commits were below 94 ms. The previously failing Mutter segment fell from
p95 707.513 ms / max 1275.488 ms to p95 34.213 ms / max 48.335 ms. A 75-second,
1,858-publication audit recorded zero stale-frame ABA/ABAB transitions, parity
violations, or same-sequence mutations.

Run the local GLib priority regression without changing the installed system:

```bash
./packaging/mutter/test-capture-priority.sh
```

It verifies that the old priority 151 timeout is starved for 50 ms by a
continuously ready priority 150 redraw-like source, while the priority 0
candidate timeout is dispatched both against that redraw-like source and
against a continuously ready priority 0 peer.
