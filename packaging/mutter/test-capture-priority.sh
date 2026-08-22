#!/bin/sh
set -eu

script_dir=$(CDPATH='' cd -- "$(dirname -- "$0")" && pwd)
test_dir=$(mktemp -d)
trap 'rm -rf "$test_dir"' EXIT HUP INT TERM

pkg_config_flags=$(pkg-config --cflags --libs glib-2.0)
# The compiler must receive pkg-config's whitespace-separated flags as words.
# shellcheck disable=SC2086
cc -std=c11 -Wall -Wextra -Werror \
  "$script_dir/test-capture-priority.c" \
  -o "$test_dir/test-capture-priority" \
  $pkg_config_flags

"$test_dir/test-capture-priority"
