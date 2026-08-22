#!/usr/bin/env bash
set -euo pipefail

project_root=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd -P)
readonly project_root
readonly template="$project_root/packaging/install-with-mutter-fix.sh.in"
tmp_dir=$(mktemp -d)
if [[ ${UU_REMOTE_KEEP_TEST_TMP:-0} != 1 ]]; then
    trap 'rm -rf -- "$tmp_dir"' EXIT
fi

fail() {
    printf '串行安装器测试失败：%s\n' "$*" >&2
    exit 1
}

make_fixture() {
    local name=$1 expected_hash=${2:-actual}
    local root="$tmp_dir/$name" bin="$tmp_dir/$name/bin"
    local package="$root/uu-remote-for-linux_1.1.33_amd64.deb"
    local hash

    mkdir -p "$bin"
    printf 'fixture package %s\n' "$name" >"$package"
    hash=$(sha256sum "$package")
    hash=${hash%% *}
    [[ $expected_hash == actual ]] || hash=$expected_hash
    sed \
        -e 's/@VERSION@/1.1.33/g' \
        -e "s/@PACKAGE_SHA256@/$hash/g" \
        -e "s|/etc/os-release|$root/os-release|g" \
        -e "s|/var/tmp/uu-remote-serial-install\.|$root/root-stage.|g" \
        -e "s|/usr/bin/uu-remote-mutter-fix|$bin/uu-remote-mutter-fix|g" \
        -e "s|/usr/bin/apt-get|$bin/apt-get|g" \
        -e "s|/usr/bin/dpkg-deb|$bin/dpkg-deb|g" \
        -e "s|/usr/bin/dpkg-query|$bin/dpkg-query|g" \
        -e "s|/usr/bin/dpkg|$bin/dpkg|g" \
        -e "s|/usr/bin/grep|$bin/grep|g" \
        -e "s|/usr/bin/install|$bin/install|g" \
        -e "s|/usr/bin/mktemp|$bin/mktemp|g" \
        -e "s|/usr/bin/readlink|$bin/readlink|g" \
        -e "s|/usr/bin/rmdir|$bin/rmdir|g" \
        -e "s|/usr/bin/rm|$bin/rm|g" \
        -e "s|/usr/bin/sha256sum|$bin/sha256sum|g" \
        -e "s|/usr/bin/sudo|$bin/sudo|g" \
        "$template" >"$root/install.sh"
    chmod 0755 "$root/install.sh"
    printf 'ID=ubuntu\nVERSION_ID="24.04"\nVERSION_CODENAME=noble\n' \
        >"$root/os-release"

    ln -s /usr/bin/grep "$bin/grep"
    ln -s /usr/bin/readlink "$bin/readlink"
    ln -s /usr/bin/rm "$bin/rm"
    ln -s /usr/bin/rmdir "$bin/rmdir"
    ln -s /usr/bin/sha256sum "$bin/sha256sum"

    # The production command runs GNU install through sudo with root ownership.
    # This isolated fixture has no real privilege transition, so emulate only
    # the copy/mode result while deliberately ignoring -o/-g.
    cat >"$bin/install" <<'EOF'
#!/usr/bin/env bash
set -euo pipefail
while (($#)); do
    case $1 in
        -o|-g|-m) shift 2 ;;
        --) shift; break ;;
        *) break ;;
    esac
done
[[ $# == 2 ]] || exit 64
/usr/bin/cp -- "$1" "$2"
/usr/bin/chmod 0600 "$2"
EOF

    cat >"$bin/dpkg-deb" <<EOF
#!/usr/bin/env bash
set -euo pipefail
case \${3:-} in
    Package) printf '%s\\n' uu-remote-for-linux ;;
    Version) printf '%s\\n' 1.1.33 ;;
    Architecture) printf '%s\\n' amd64 ;;
    *) exit 64 ;;
esac
EOF
    cat >"$bin/dpkg" <<EOF
#!/usr/bin/env bash
set -euo pipefail
case \${1:-} in
    --print-architecture) printf '%s\\n' amd64 ;;
    --audit) [[ ! -e '$root/audit-dirty' ]] || printf '%s\\n' pending ;;
    --verify) : ;;
    *) exit 64 ;;
esac
EOF
    cat >"$bin/mktemp" <<EOF
#!/usr/bin/env bash
set -euo pipefail
mkdir -p '$root/root-stage.fixture'
printf '%s\\n' '$root/root-stage.fixture'
EOF
    cat >"$bin/apt-get" <<EOF
#!/usr/bin/env bash
set -euo pipefail
printf 'apt:%s\\n' "\$*" >>'$root/trace'
if [[ \${1:-} == -s && \${2:-} == check ]]; then
    exit 0
fi
if [[ \${1:-} == -s ]]; then
    if [[ -e '$root/plan-remove' ]]; then
        printf 'Remv unrelated [1.0]\\n'
    else
        printf 'Inst uu-remote-for-linux (1.1.33 local)\\n'
        printf 'Conf uu-remote-for-linux (1.1.33 local)\\n'
    fi
    exit 0
fi
if [[ -e '$root/apt-real-fail' ]]; then
    exit 42
fi
printf '%s\\n' installed >'$root/package-state'
EOF
    cat >"$bin/sudo" <<EOF
#!/usr/bin/env bash
set -euo pipefail
printf 'sudo:%s\\n' "\$*" >>'$root/trace'
exec "\$@"
EOF
    cat >"$bin/dpkg-query" <<EOF
#!/usr/bin/env bash
set -euo pipefail
[[ \$(cat '$root/package-state' 2>/dev/null || true) == installed ]] || exit 1
printf 'install ok installed\\t1.1.33\\tamd64\\n'
EOF
    cat >"$bin/uu-remote-mutter-fix" <<EOF
#!/usr/bin/env bash
set -euo pipefail
printf 'manager:%s\\n' "\$*" >>'$root/trace'
case \${1:-} in
    install)
        [[ \$(cat '$root/package-state' 2>/dev/null || true) == installed ]] || exit 70
        [[ ! -e '$root/manager-fail' ]] || exit 43
        printf '%s\\n' installed-pending-logout >'$root/mutter-state'
        ;;
    status)
        [[ \${2:-} == --field && \${3:-} == state ]] || exit 64
        cat '$root/mutter-state'
        ;;
    *) exit 64 ;;
esac
EOF
    chmod 0755 "$bin/install" "$bin/dpkg-deb" "$bin/dpkg" "$bin/apt-get" \
        "$bin/mktemp" "$bin/sudo" "$bin/dpkg-query" \
        "$bin/uu-remote-mutter-fix"
}

run_installer() {
    local root=$1 session_type=${2:-wayland} desktop=${3:-ubuntu:GNOME}
    XDG_SESSION_TYPE=$session_type XDG_CURRENT_DESKTOP=$desktop \
        "$root/install.sh" >"$root/output" 2>&1
}

make_fixture success
run_installer "$tmp_dir/success"
grep -q '^apt:-s check$' "$tmp_dir/success/trace" ||
    fail '成功路径未先检查 APT 健康状态'
grep -q '^apt:-s --no-remove --reinstall install ' "$tmp_dir/success/trace" ||
    fail '成功路径未模拟主包安装'
grep -q '^sudo:.*apt-get -y --no-remove --reinstall install ' "$tmp_dir/success/trace" ||
    fail '成功路径未通过 sudo 启动真实主包事务'
grep -q '^manager:install --yes$' "$tmp_dir/success/trace" ||
    fail '成功路径未在主包后调用静默 Mutter 管理器'
grep -q '^manager:status --field state$' "$tmp_dir/success/trace" ||
    fail '成功路径未读取最终 Mutter 状态'
sudo_line=$(sed -n '/^sudo:.*apt-get/=' "$tmp_dir/success/trace")
manager_line=$(sed -n '/^manager:install/=' "$tmp_dir/success/trace")
((sudo_line < manager_line)) || fail 'Mutter 管理器在主 APT 完成前启动'
grep -q '请保存工作并注销' "$tmp_dir/success/output" ||
    fail '成功路径未提示注销'

make_fixture bad-hash "$(printf '0%.0s' {1..64})"
if run_installer "$tmp_dir/bad-hash"; then
    fail '错误哈希被接受'
fi
[[ ! -e $tmp_dir/bad-hash/trace ]] ||
    fail '错误哈希在拒绝前调用了 APT 或管理器'

make_fixture plan-remove
touch "$tmp_dir/plan-remove/plan-remove"
if run_installer "$tmp_dir/plan-remove"; then
    fail '包含删除的软件包计划被接受'
fi
! grep -q '^sudo:' "$tmp_dir/plan-remove/trace" ||
    fail '模拟拒绝后仍启动了真实 APT'

make_fixture apt-fail
touch "$tmp_dir/apt-fail/apt-real-fail"
if run_installer "$tmp_dir/apt-fail"; then
    fail '主 APT 失败被当作成功'
fi
! grep -q '^manager:' "$tmp_dir/apt-fail/trace" ||
    fail '主 APT 失败后仍启动了 Mutter 管理器'

make_fixture manager-fail
touch "$tmp_dir/manager-fail/manager-fail"
if run_installer "$tmp_dir/manager-fail"; then
    fail 'Mutter 失败被当作整体成功'
fi
[[ $(<"$tmp_dir/manager-fail/package-state") == installed ]] ||
    fail 'Mutter 失败破坏了已完成的主包事务'
! grep -q '^manager:status' "$tmp_dir/manager-fail/trace" ||
    fail 'Mutter 失败后错误读取完成状态'

make_fixture x11-success
run_installer "$tmp_dir/x11-success" x11
grep -q '^sudo:.*apt-get -y --no-remove --reinstall install ' \
    "$tmp_dir/x11-success/trace" ||
    fail 'X11 路径未完成 UU 主包事务'
! grep -q '^manager:' "$tmp_dir/x11-success/trace" ||
    fail 'X11 路径错误启动了 Mutter 管理器'
grep -q '当前为 X11' "$tmp_dir/x11-success/output" ||
    fail 'X11 路径未明确说明跳过 Mutter'

make_fixture unsupported-session
if run_installer "$tmp_dir/unsupported-session" tty; then
    fail '无图形会话被统一安装器接受'
fi
[[ ! -e $tmp_dir/unsupported-session/trace ]] ||
    fail '会话门禁失败后仍调用了 APT'

make_fixture unsupported-wayland
if run_installer "$tmp_dir/unsupported-wayland" wayland KDE; then
    fail '非 GNOME Wayland 被 Mutter 安装器接受'
fi
[[ ! -e $tmp_dir/unsupported-wayland/trace ]] ||
    fail 'Wayland 桌面门禁失败后仍调用了 APT'

printf '安全串行安装器隔离测试通过。\n'
