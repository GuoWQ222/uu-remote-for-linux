# Universal DEB release

`make release-assets` produces `uu-remote-for-linux_VERSION_amd64.deb` and
its build checksum manifest. The GitHub Release needs only one user download:
the DEB. Its SHA-256 is included in the release notes.

The same runtime supports X11 and GNOME Wayland. It also embeds an opt-in
Mutter 46 keyboard-state repair, four original rollback packages and complete
corresponding source. All system packages remain inert during outer-DEB
installation; the first interactive launch offers activation only on the
matching, unmodified Ubuntu 24.04 amd64 / GNOME 46 / Mutter build.

The helper validates package hashes and metadata, simulates an offline APT
plan, refuses unrelated package changes or removals, saves rollback records,
and runs the four-package transaction after administrator authorization.
It does not invoke APT from maintainer scripts or restart the desktop.
See [the repair guide](../../contrib/mutter-46-lock-state/README.md).

CI validates the embedded payload and source and tests repair failure,
rollback, mismatched systems and prompt behavior. The standard package has
no unconditional custom-Mutter dependency and can still install on X11 or
other desktop builds without activating the optional repair.
