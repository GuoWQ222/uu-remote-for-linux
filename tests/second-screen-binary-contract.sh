#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
readonly project_root
readonly data_home="${XDG_DATA_HOME:-$HOME/.local/share}"
readonly prefix="${UU_REMOTE_WINE_PREFIX:-$data_home/uu-remote-for-linux/wineprefix}"
readonly install_dir="$prefix/drive_c/Program Files/Netease/GameViewer"
readonly controller="$install_dir/bin/GameViewer.exe"
readonly server="$install_dir/bin/GameViewerServer.exe"

if [[ ! -r $controller || ! -r $server ]]; then
    printf 'SKIP：当前前缀没有可供核验的 UU 主控/服务端二进制。\n'
    exit 0
fi

/usr/bin/python3 - \
    "$controller" \
    "$server" \
    "$project_root/src/uu-remote-input-hook.c" <<'PY'
import struct
import sys
from pathlib import Path


def text_section(path: Path) -> bytes:
    data = path.read_bytes()
    if data[:2] != b"MZ":
        raise AssertionError(f"not PE: {path}")
    pe = struct.unpack_from("<I", data, 0x3C)[0]
    if data[pe : pe + 4] != b"PE\0\0":
        raise AssertionError(f"invalid PE signature: {path}")
    section_count = struct.unpack_from("<H", data, pe + 6)[0]
    optional_size = struct.unpack_from("<H", data, pe + 20)[0]
    table = pe + 24 + optional_size
    for index in range(section_count):
        offset = table + index * 40
        name = data[offset : offset + 8].rstrip(b"\0")
        if name == b".text":
            raw_size, raw_offset = struct.unpack_from("<II", data, offset + 16)
            return data[raw_offset : raw_offset + raw_size]
    raise AssertionError(f"missing .text: {path}")


controller_path = Path(sys.argv[1])
controller_data = controller_path.read_bytes()
controller_text = text_section(controller_path)
for semantic_name in (
    b"slotSecondScreenBtnClicked",
    b"SecondScreenEntryAction",
    b"applySecondScreenEntryAction",
):
    assert semantic_name in controller_data, semantic_name
controller_prefix = bytes.fromhex(
    "48 89 5c 24 08 57 48 83 ec 20 48 8b f9 48 8b da "
    "48 8b 49 50 e8"
)
controller_suffix = bytes.fromhex(
    "4c 8b c3 8b d0 48 8b cf 48 8b 5c 24 30 "
    "48 83 c4 20 5f e9"
)
controller_patched_suffix = bytearray(controller_suffix)
controller_patched_suffix[3:5] = b"\x33\xd2"
controller_span = len(controller_prefix) + 4 + len(controller_suffix) + 4
controller_candidates = 0
for index in range(len(controller_text) - controller_span + 1):
    suffix_start = index + len(controller_prefix) + 4
    suffix = controller_text[
        suffix_start : suffix_start + len(controller_suffix)
    ]
    if (
        controller_text[index : index + len(controller_prefix)]
        == controller_prefix
        and suffix in {controller_suffix, bytes(controller_patched_suffix)}
    ):
        controller_candidates += 1
assert controller_candidates == 1, controller_candidates

server_text = text_section(Path(sys.argv[2]))
server_data = Path(sys.argv[2]).read_bytes()
assert b"DeviceIoControl\0" in server_data
assert b"EnumDisplayDevicesW\0" in server_data
assert "GameViewer Virtual Display Adapter".encode("utf-16le") in server_data
prefix = bytes.fromhex("33 ff 8b df 89 5c 24 40 8d 4f 02 e8")
suffix = bytes.fromhex(
    "84 c0 74 4b 88 5c 24 28 c7 44 24 20 58 01 00 00"
)
prologue = bytes.fromhex(
    "48 89 5c 24 08 48 89 6c 24 10 48 89 74 24 18 "
    "48 89 7c 24 20 41 56 48 81 ec 70 03 00 00"
)
function_delta = 0x2F
span = len(prefix) + 4 + len(suffix)
server_candidates = 0
for index in range(function_delta, len(server_text) - span + 1):
    if (
        server_text[index : index + len(prefix)] == prefix
        and server_text[index + len(prefix) + 4 : index + span] == suffix
        and server_text[index - function_delta : index - function_delta + len(prologue)] == prologue
    ):
        server_candidates += 1
assert server_candidates == 1, server_candidates

for ioctl in (0x0030C004, 0x0030C008, 0x0030C010):
    encoded = struct.pack("<I", ioctl)
    assert encoded in server_text, hex(ioctl)

source = Path(sys.argv[3]).read_text(encoding="utf-8")


def function_body(name: str) -> str:
    marker = f"{name}("
    start = source.index(marker)
    opening = source.index("{", start)
    depth = 0
    for index in range(opening, len(source)):
        if source[index] == "{":
            depth += 1
        elif source[index] == "}":
            depth -= 1
            if depth == 0:
                return source[opening : index + 1]
    raise AssertionError(f"unterminated function: {name}")


assert "#define UUSS_BRIDGE_TIMEOUT_MS 8000u" in source
assert "#define UUSS_PREPARE_TIMEOUT_MS 20000u" in source
assert "#define HOOK_VERSION 64u" in source
controller_hook = function_body("hooked_controller_second_screen_entry")
assert controller_hook.index("UUSS_PREPARE") < controller_hook.index(
    "second_screen_apply_action(controller"
)
device_hook = function_body("hooked_second_screen_device_io_control")
assert "UUSS_PREPARE" not in device_hook
identity_hook = function_body(
    "hooked_second_screen_enum_display_devices_w"
)
assert "second_screen_adapter_has_virtual_child" in identity_hook
assert "GameViewer Virtual Display Adapter" in identity_hook
assert '"EnumDisplayDevicesW"' in source
display_config_hook = function_body(
    "hooked_second_screen_set_display_config"
)
assert "original_set_display_config" in display_config_hook
assert "set_display_config_last_result" in display_config_hook
assert '"SetDisplayConfig"' in source
change_settings_hook = function_body(
    "hooked_second_screen_change_display_settings_ex_w"
)
assert "original_change_display_settings_ex_w" in change_settings_hook
assert "second_screen_native_topology_active" in change_settings_hook
assert "CDS_UPDATEREGISTRY | CDS_NORESET" in change_settings_hook
assert "change_display_settings_suppressed" in change_settings_hook
assert '"ChangeDisplaySettingsExW"' in source
set_info_hook = function_body(
    "hooked_second_screen_display_config_set_device_info"
)
assert "original_display_config_set_device_info" in set_info_hook
assert '"DisplayConfigSetDeviceInfo"' in source
gate_patch = function_body("patch_controller_second_screen_gate")
assert "patch_executable_entry" in gate_patch
assert "patch_executable_byte_fields" not in gate_patch

print("当前 UU 二进制无线副屏入口与虚拟驱动调用语义均唯一匹配。")
PY
