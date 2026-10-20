#!/usr/bin/env bash
set -euo pipefail
project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
output="$project_root/lib/uu-remote-for-linux/uu-remote-eis-pointer"
temporary=$(mktemp)
trap 'rm -f -- "$temporary"' EXIT
pkg-config --exists 'libei-1.0 >= 1.1'
# shellcheck disable=SC2046
"${CC:-cc}" -std=c11 -O2 -Wall -Wextra -Werror \
    -Wl,--as-needed -Wl,-z,relro,-z,now \
    $(pkg-config --cflags libei-1.0) \
    "$project_root/src/uu-remote-eis-pointer.c" \
    -o "$temporary" $(pkg-config --libs libei-1.0) -lm
strip --strip-unneeded --remove-section=.comment "$temporary"
install -Dm0755 "$temporary" "$output"
sha256sum "$output"
